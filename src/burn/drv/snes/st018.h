// =============================================================================
//  FBNeo SNES  -  ST018 (Seta ARMv3 / ARM7TDMI) coprocessor  -  public interface
// =============================================================================
//  Experimental ST018 coprocessor for the FBNeo LakeSnes SNES driver.
//
//  The ST018 is a Seta custom chip built around an ARMv3 (ARM60-class) core with
//  on-die 128KB program ROM + 32KB data ROM and 16KB program RAM, used by the
//  shogi title "Hayazashi Nidan Morita Shogi 2" (早指し二段 森田将棋2).  The
//  glue logic (register map, host<->ARM bridge FIFO, boot/reset timing) is a
//  clean-room C re-implementation derived from the open-source ST018 modules of:
//        ares/sfc/coprocessor/armdsp/*        (primary)
//        bsnes-mercury/sfc/chip/armdsp/*      (cross-check)
//  Both are ISC-licensed; see license.txt.  No proprietary or reverse-engineered
//  material is used.
//
//  Unlike the SPC7110 / GSU ports, the ARM core itself is NOT re-transliterated
//  from ares: FBNeo already ships a mature, widely-validated ARM7TDMI core
//  (src/cpu/arm7, as used by the PGM ASIC27A coprocessor and the GBA driver).
//  The ST018's ARMv3 differs only in forcing ARM mode (no THUMB), which is a
//  runtime state, so this port drives FBNeo's ARM7TDMI directly and only ports
//  the Seta-specific shell:
//    * on-die ROM/RAM mapped through Arm7MapMemory at the ST018 bus offsets
//      (programROM @0x00000000, dataROM @0xa0000000, programRAM @0xe0000000);
//    * the bridge MMIO window (@0x40000000 on the ARM side, $3800-$3807 on the
//      S-CPU side) served through Arm7Set*Handler callbacks;
//    * a cycle-budget catch-up run() driven by cart_run() under Cart::heavySync,
//      matching how GSU/SA-1 interleave with the S-CPU here.
//
//  FBNeo mount / bridge glue (c) 2026 (see license.txt).
// =============================================================================

#ifndef ST018_H
#define ST018_H

#include "burnint.h"
#include "statehandler.h"

typedef struct Snes Snes;

// -----------------------------------------------------------------------------
//  Lifecycle  (called from cart.cpp)
// -----------------------------------------------------------------------------
//  mem   : Snes* context (for palTiming / cycle clock)
//  bios  : 160KB ST018 firmware image = 128KB program ROM followed by 32KB data
//          ROM (st018.rom); biosSize must be >= 0x28000.
void snes_st018_init(void* mem, UINT8* bios, INT32 biosSize);
void snes_st018_reset();
void snes_st018_exit();
void snes_st018_run();                 // catch-up: advance ARM to the S-CPU clock
void snes_st018_handleState(StateHandler* sh);

// -----------------------------------------------------------------------------
//  S-CPU bus bridge  ($3800-$3807 in banks $00-$3f / $80-$bf)
// -----------------------------------------------------------------------------
UINT8 snes_st018_read(UINT32 address, UINT8 openbus);
void  snes_st018_write(UINT32 address, UINT8 data);

#endif
