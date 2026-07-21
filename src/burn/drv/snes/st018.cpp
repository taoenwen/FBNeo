// =============================================================================
//  ST018 (Seta ARMv3 / ARM7TDMI) coprocessor for FBNeo
// =============================================================================
// Ported from ares (ares/sfc/coprocessor/armdsp), cross-checked with
// bsnes-mercury (sfc/chip/armdsp).  Both upstreams are ISC-licensed; see
// license.txt.  FBNeo flat-C port / integration: (this file)
//
// Design
// ------
// The ARM core is FBNeo's own ARM7TDMI (src/cpu/arm7), the same core the PGM
// ASIC27A coprocessor and the GBA driver use.  The ST018 is an ARMv3 part that
// only ever runs in ARM mode (no THUMB); FBNeo's ARM7TDMI powers up in ARM mode
// and the firmware never switches, so no special-casing is needed.
//
// On-die memory is mapped straight into the ARM address space with
// Arm7MapMemory at the ST018 bus offsets.  FBNeo's ARM core masks addresses to
// 31 bits (MAX_MEMORY 0x80000000), so the top-bit ST018 regions fold down
// without colliding:
//     programROM  0x00000000            -> pages 0x00000..
//     dataROM     0xa0000000 & 0x7fff.. -> 0x20000000
//     programRAM  0xe0000000 & 0x7fff.. -> 0x60000000
//     bridge MMIO 0x40000000            -> served by read/write callbacks
// The bridge window is the host<->ARM mailbox: a byte each way plus a status
// register and a 24-bit down-counter, exactly as ares models it.
//
// Timing follows the GSU/SA-1 pattern used elsewhere in this driver: a
// cycle-budget catch-up (snes_st018_run) driven by cart_run() under
// Cart::heavySync, instead of ares' scheduler thread.  ares' 65536-cycle reset
// hold is folded into the bridge.ready gate.
// =============================================================================

#include "st018.h"
#include "snes.h"
#include "cart.h"
#include "burnint.h"
#include "arm7_intf.h"
#include <string.h>

// -----------------------------------------------------------------------------
//  ST018 bus offsets (ARM side)
// -----------------------------------------------------------------------------
#define ST018_PROM_BASE   0x00000000u   // 128KB on-die program ROM
#define ST018_DROM_BASE   0xa0000000u   // 32KB  on-die data ROM
#define ST018_PRAM_BASE   0xe0000000u   // 16KB  program RAM
#define ST018_MMIO_BASE   0x40000000u   // bridge mailbox window

#define ST018_PROM_SIZE   0x20000       // 128KB
#define ST018_DROM_SIZE   0x08000       // 32KB
#define ST018_PRAM_SIZE   0x04000       // 16KB

// ares reset-sequence hold: the ARM is held for this many of its own clocks
// after a bridge reset before it begins executing.
#define ST018_BOOT_HOLD   65536

// -----------------------------------------------------------------------------
//  State
// -----------------------------------------------------------------------------
static Snes*  s_snes       = NULL;

static UINT8* s_prom       = NULL;	// 128KB (points into firmware image)
static UINT8* s_drom       = NULL;	// 32KB  (points into firmware image)
static UINT8  s_pram[ST018_PRAM_SIZE];

static INT32  s_haveArm    = 0;		// ARM core successfully initialised
static INT32  s_running    = 0;		// ARM currently clocked (post-boot-hold)

// Host<->ARM bridge (ares ARMDSP::Bridge)
static UINT8  s_c2a_ready;			// CPU -> ARM byte pending
static UINT8  s_c2a_data;
static UINT8  s_a2c_ready;			// ARM -> CPU byte pending
static UINT8  s_a2c_data;
static UINT32 s_timer;				// 24-bit down-counter, ticked each ARM step
static UINT32 s_timerlatch;
static UINT8  s_reset;				// bridge reset line held by the S-CPU
static UINT8  s_ready;				// ARM has passed its boot-hold
static UINT8  s_signal;				// ARM -> CPU signal flag

// ST018 runs its ARM at the S-CPU master clock (ares uses 21.477MHz NTSC).
static UINT32 s_armFreq;
// Catch-up bookkeeping: ARM clocks owed relative to the S-CPU cycle counter.
static UINT64 s_clockBase;			// snes->cycles at last catch-up
static UINT64 s_clockFp;			// fixed-point accumulator (armFreq * delta)

// -----------------------------------------------------------------------------
//  Bridge status byte (ares Bridge::status)
// -----------------------------------------------------------------------------
static UINT8 st018_status()
{
	UINT8 data = 0;
	if (s_a2c_ready) data |= 0x01;	// bit0: ARM -> CPU byte ready
	if (s_signal)    data |= 0x04;	// bit2: ARM signalled
	if (s_c2a_ready) data |= 0x08;	// bit3: CPU -> ARM byte ready
	if (s_ready)     data |= 0x80;	// bit7: ARM booted / ready
	return data;
}

// -----------------------------------------------------------------------------
//  ARM-side bridge MMIO  (0x40000000 window)  -  served via Arm7 callbacks
// -----------------------------------------------------------------------------
//  ares get()/set() decode address & 0xe000003f in the 0x40000000 region.
//  Word/byte width does not matter for the mailbox (data is a single byte); the
//  ST018 firmware uses 32-bit loads/stores, so we hook the long handlers and
//  mirror the byte onto the low 8 bits.

static UINT32 st018_arm_read_long(UINT32 address)
{
	switch (address & 0xe000003fu) {
		case 0x40000010u:																				// read CPU -> ARM mailbox
			if (s_c2a_ready) {
				s_c2a_ready = 0;
				return s_c2a_data;
			}
			return 0;
		case 0x40000020u:																				// read bridge status
			return st018_status();
	}
	return 0;
}

static void st018_arm_write_long(UINT32 address, UINT32 word)
{
	word &= 0xff;
	switch (address & 0xe000003fu) {
		case 0x40000000u:																				// write ARM -> CPU mailbox
			s_a2c_ready = 1;
			s_a2c_data  = (UINT8)word;
			break;
		case 0x40000010u:																				// raise signal to CPU
			s_signal    = 1;
			break;
		case 0x40000020u: s_timerlatch = (s_timerlatch & 0xffff00u) | ((word & 0xff) <<  0); break;
		case 0x40000024u: s_timerlatch = (s_timerlatch & 0xff00ffu) | ((word & 0xff) <<  8); break;
		case 0x40000028u: s_timerlatch = (s_timerlatch & 0x00ffffu) | ((word & 0xff) << 16); break;
		case 0x4000002cu: s_timer      = s_timerlatch;                                       break;		// arm the down-counter
	}
}

// ST018 firmware is expected to use 32-bit bus cycles for the mailbox, but hook
// the narrow widths too so a byte/word access still lands coherently.
static UINT8 st018_arm_read_byte(UINT32 address)
{
	return (UINT8)st018_arm_read_long(address);
}
static UINT16 st018_arm_read_word(UINT32 address)
{
	return (UINT16)st018_arm_read_long(address);
}
static void st018_arm_write_byte(UINT32 address, UINT8 data)
{
	st018_arm_write_long(address, data);
}
static void st018_arm_write_word(UINT32 address, UINT16 data)
{
	st018_arm_write_long(address, data);
}

// -----------------------------------------------------------------------------
//  S-CPU-side bus bridge  ($3800-$3807, mirrored; a0 ignored -> & 0xff06)
// -----------------------------------------------------------------------------
UINT8 snes_st018_read(UINT32 address, UINT8 openbus)
{
	// keep the ARM caught up so the mailbox reflects its latest writes
	snes_st018_run();

	UINT8 data = 0x00;
	UINT32 reg = address & 0xff06u;

	if (reg == 0x3800) {			// read ARM -> CPU mailbox
		if (s_a2c_ready) {
			s_a2c_ready = 0;
			data = s_a2c_data;
		}
	} else if (reg == 0x3802) {		// acknowledge / clear signal
		s_signal = 0;
	} else if (reg == 0x3804) {		// bridge status
		data = st018_status();
	}

	(void)openbus;
	return data;
}

void snes_st018_write(UINT32 address, UINT8 data)
{
	snes_st018_run();

	UINT32 reg = address & 0xff06u;

	if (reg == 0x3802) {			// write CPU -> ARM mailbox
		s_c2a_ready = 1;
		s_c2a_data  = data;
	} else if (reg == 0x3804) {		// bridge reset line (bit0)
		data &= 1;
		if (!s_reset && data) {
			// rising edge: (re)start the ARM
			snes_st018_reset();
		}
		s_reset = data;
	}
}

// -----------------------------------------------------------------------------
//  Catch-up run  (GSU-style: advance ARM clocks up to the S-CPU cycle counter)
// -----------------------------------------------------------------------------
void snes_st018_run()
{
	if (!s_haveArm) return;

	// While the S-CPU holds the reset line, the ARM is frozen (ares boot()).
	if (s_reset) { s_clockBase = s_snes->cycles; return; }

	UINT64 now   = s_snes->cycles;
	UINT64 delta = (now >= s_clockBase) ? (now - s_clockBase) : 0;
	s_clockBase  = now;

	// S-CPU cycle counter runs at the same master clock we clock the ARM with,
	// so one S-CPU cycle == one ARM clock owed.
	s_clockFp += delta;
	if (s_clockFp == 0) return;

	Arm7Open(0);

	// First run after a reset absorbs the boot-hold delay before executing.
	// cart_run() fires every couple of master cycles (Cart::heavySync), so each
	// call only adds a tiny delta.  The boot hold must ACCUMULATE across calls
	// until 65536 ARM clocks have elapsed - do NOT clear the accumulator here.
	// (The old code zeroed s_clockFp every call, so it never reached the
	// threshold: the ARM never started and the host hung at "TRANSMIT WAIT".)
	if (!s_running) {
		if (s_clockFp >= ST018_BOOT_HOLD) {
			s_clockFp -= ST018_BOOT_HOLD;
			s_ready   = 1;
			s_running = 1;
		} else {
			Arm7Close();   // still within the boot hold; keep accumulating
			return;
		}
	}

	UINT32 owed = (s_clockFp > 0x3fffffffu) ? 0x3fffffffu : (UINT32)s_clockFp;
	if (owed > 0) {
		// Tick the bridge down-counter across the executed window (ares ticks it
		// one per ARM step; per-window is equivalent for the timer's observable
		// "reached zero" semantics used by the firmware).
		if (s_timer) {
			s_timer = (s_timer > owed) ? (s_timer - owed) : 0;
		}
		Arm7Run((INT32)owed);
	}
	s_clockFp = 0;

	Arm7Close();
}

// -----------------------------------------------------------------------------
//  Lifecycle
// -----------------------------------------------------------------------------
void snes_st018_init(void* mem, UINT8* bios, INT32 biosSize)
{
	s_snes = (Snes*)mem;

	if (bios == NULL || biosSize < (ST018_PROM_SIZE + ST018_DROM_SIZE)) {
		bprintf(0, _T("st018: missing/short firmware (need %x, have %x) - disabled\n"),
			ST018_PROM_SIZE + ST018_DROM_SIZE, biosSize);
		s_haveArm = 0;
		return;
	}

	// cart_reset() (and thus this init) runs on every machine reset; the ARM core
	// allocates page tables in Arm7Init and must be torn down first to avoid
	// leaking them across resets.  Also clear program RAM on a cold init only.
	if (s_haveArm) {
		Arm7Exit();
		s_haveArm = 0;
	} else {
		memset(s_pram, 0, sizeof(s_pram));
	}

	s_prom = bios;							// 0x00000..0x1ffff
	s_drom = bios + ST018_PROM_SIZE;		// 0x20000..0x27fff

	s_armFreq = s_snes->palTiming ? 21281372u : 21477272u;

	Arm7Init(0);
	Arm7Open(0);
	Arm7MapMemory(s_prom, ST018_PROM_BASE, ST018_PROM_BASE + ST018_PROM_SIZE - 1, MAP_ROM);
	// high-bit regions fold to 31-bit space (see header); map at the folded base
	Arm7MapMemory(s_drom, ST018_DROM_BASE & 0x7fffffffu,
		(ST018_DROM_BASE & 0x7fffffffu) + ST018_DROM_SIZE - 1, MAP_ROM);
	Arm7MapMemory(s_pram, ST018_PRAM_BASE & 0x7fffffffu,
		(ST018_PRAM_BASE & 0x7fffffffu) + ST018_PRAM_SIZE - 1, MAP_RAM);
	Arm7SetReadByteHandler( st018_arm_read_byte);
	Arm7SetReadWordHandler( st018_arm_read_word);
	Arm7SetReadLongHandler( st018_arm_read_long);
	Arm7SetWriteByteHandler(st018_arm_write_byte);
	Arm7SetWriteWordHandler(st018_arm_write_word);
	Arm7SetWriteLongHandler(st018_arm_write_long);
	Arm7Close();

	s_haveArm = 1;
	bprintf(0, _T("st018: init  prom %x  drom %x  pram %x  arm %u Hz\n"),
		ST018_PROM_SIZE, ST018_DROM_SIZE, ST018_PRAM_SIZE, s_armFreq);
}

void snes_st018_reset()
{
	if (!s_haveArm) return;

	// ares ARMDSP::power/reset: randomise program RAM (approximate with 0-fill;
	// the firmware initialises what it needs) and clear the bridge.
	// NOTE: on a *bridge* reset (from $3804) the RAM is not re-cleared by ares'
	// reset(); only cold power() randomises it.  We only clear on first init.
	Arm7Open(0);
	Arm7Reset();
	Arm7Close();

	s_running    = 0;
	s_ready      = 0;
	s_signal     = 0;
	s_timer      = 0;
	s_timerlatch = 0;
	s_c2a_ready  = 0;
	s_c2a_data   = 0;
	s_a2c_ready  = 0;
	s_a2c_data   = 0;

	s_clockBase  = (s_snes != NULL) ? s_snes->cycles : 0;
	s_clockFp    = 0;
}

void snes_st018_exit()
{
	if (s_haveArm) {
		Arm7Exit();
		s_haveArm = 0;
	}
	s_snes = NULL;
	s_prom = NULL;
	s_drom = NULL;
}

// -----------------------------------------------------------------------------
//  Save-state
// -----------------------------------------------------------------------------
void snes_st018_handleState(StateHandler* sh)
{
	// program RAM + bridge mailbox/timer/flags
	sh_handleByteArray(sh, s_pram, ST018_PRAM_SIZE);
	sh_handleBytes(sh,
		&s_c2a_ready, &s_c2a_data, &s_a2c_ready, &s_a2c_data,
		&s_reset, &s_ready, &s_signal,
		(UINT8*)&s_running, NULL);
	sh_handleInts(sh, &s_timer, &s_timerlatch, NULL);

	// ARM core register context: FBNeo's Arm7Scan stores it through the BurnArea
	// callback framework, which the SNES StateHandler (a flat byte stream) cannot
	// drive.  Serialize the raw register block byte-for-byte via Arm7GetContext.
	if (s_haveArm) {
		void* ctx = NULL;
		INT32 ctxSize = 0;
		Arm7Open(0);
		Arm7GetContext(&ctx, &ctxSize);
		if (ctx != NULL && ctxSize > 0) {
			sh_handleByteArray(sh, (UINT8*)ctx, ctxSize);
		}
		Arm7Close();
	}

	// Re-anchor the catch-up clock base on load.
	if (!sh->saving) {
		s_clockBase = (s_snes != NULL) ? s_snes->cycles : 0;
		s_clockFp   = 0;
	}
}
