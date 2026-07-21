// =============================================================================
//  FBNeo SNES  -  MSU-1 media coprocessor  -  public interface
// =============================================================================
//  Experimental MSU-1 core for the FBNeo LakeSnes SNES driver.
//
//  The emulation logic (the $2000-$2007 MMIO map, the data-port seek/stream
//  unit and the 44100Hz PCM audio state machine) is a clean-room C
//  re-implementation derived from the open-source MSU-1 modules of:
//        ares/sfc/coprocessor/msu1/*          (primary)
//        bsnes-mercury/sfc/chip/msu1/*        (cross-check)
//  Both are distributed under the ISC licence; see license.txt for the upstream
//  copyright and licence text.  No proprietary or reverse-engineered material
//  is used - everything here is reconstructed from public, open-source code.
//
//  This is NOT a line-for-line transliteration.  ares' C++ object model
//  (MSU1 : Thread, nall VFS::File streams, BitField registers, libco
//  scheduling) has been fully dismantled and re-expressed against FBNeo
//  conventions:
//    * the whole chip collapses into one flat C translation unit - no classes,
//      templates or member functions;
//    * ares' per-sample Thread step at 44.1kHz becomes a per-frame additive
//      mix (snes_msu1_mixSamples) that advances the PCM stream at 44100Hz and
//      resamples to FBNeo's output rate;
//    * the external data-ROM and PCM-track streams bind through a small
//      pluggable MsuFile backend (see below) instead of nall's VFS, so the
//      media source (in-memory ROM entry, host fopen, ...) is decided by the
//      mount layer, not the chip.
//
//  FBNeo mount / bridge glue (c) 2026 (see license.txt).
// =============================================================================

#ifndef MSU1_H
#define MSU1_H

#include "burnint.h"
#include "statehandler.h"

// -----------------------------------------------------------------------------
//  Pluggable media backend
// -----------------------------------------------------------------------------
//  MSU-1 needs two external byte streams that are far too large to live inside
//  the SNES cart image: a data ROM (arbitrary binary, random-access) and a set
//  of PCM audio tracks (one file per track, 44100Hz signed-16 stereo).  ares
//  streams them from nall's VFS; FBNeo has no equivalent, and how the media is
//  provided (an extra BurnRomInfo entry loaded to memory, a host fopen next to
//  the ROM, ...) is a mount-layer policy decision, not chip behaviour.
//
//  The chip therefore talks to the outside world only through this small
//  interface: a cursor-based file object plus two open callbacks.  Every hook
//  may be NULL / report "closed", in which case the chip degrades exactly like
//  real hardware with no media present (data reads return 0x00, audio flags an
//  error) - this is the state the core runs in until a backend is wired up.
//
//  Cursor semantics match nall::file: read() returns the byte at the cursor and
//  post-increments it; seek() sets the absolute byte offset; end() is nonzero
//  once the cursor has reached size(); isOpen() is nonzero while a backing
//  store is attached.  read() past the end must return 0x00 and leave end() set.
typedef struct MsuFile {
	void*  ctx;                                  // opaque backend handle (memory cursor, FILE*, ...)
	UINT8  (*read)(void* ctx);                   // read one byte at cursor, advance cursor
	void   (*seek)(void* ctx, UINT32 offset);    // set absolute cursor position
	INT32  (*end)(void* ctx);                    // nonzero once cursor is at/past EOF
	UINT32 (*size)(void* ctx);                   // total size in bytes (0 if closed/unknown)
	INT32  (*isOpen)(void* ctx);                 // nonzero while a backing store is attached
} MsuFile;

// Open callbacks supplied by the mount layer.  Each fills *out with a ready
// MsuFile (cursor at 0) and returns nonzero on success; on failure it returns 0
// and leaves *out describing a closed file.  audioOpen selects by track number.
typedef INT32 (*MsuDataOpen)(MsuFile* out);
typedef INT32 (*MsuAudioOpen)(UINT16 track, MsuFile* out);

// Install the media backend.  Pass NULL for either callback to run with no
// media (the default until a backend exists).  Call before snes_msu1_reset().
void snes_msu1_setBackend(MsuDataOpen dataOpen, MsuAudioOpen audioOpen);

// -----------------------------------------------------------------------------
//  Lifecycle  (called from cart.cpp)
// -----------------------------------------------------------------------------
void snes_msu1_init();
void snes_msu1_reset();
void snes_msu1_exit();
void snes_msu1_handleState(StateHandler* sh);

// -----------------------------------------------------------------------------
//  S-CPU bus bridge  (called from the MSU-1 cart read/write path)
// -----------------------------------------------------------------------------
//  MSU-1 decodes the eight I/O ports $2000-$2007 in banks $00-$3f / $80-$bf.
//  address is the full 24-bit bus address; only the low 3 bits select a port.
UINT8 snes_msu1_read(UINT32 address, UINT8 openbus);
void  snes_msu1_write(UINT32 address, UINT8 data);

// -----------------------------------------------------------------------------
//  Audio
// -----------------------------------------------------------------------------
//  Advance the PCM stream and additively mix it into an already-filled stereo
//  output buffer.  'out' holds 'samples' interleaved L/R INT16 frames produced
//  by the S-DSP; the MSU-1 track (native 44100Hz) is resampled to 'outRate' and
//  summed in with saturation.  dspMuted mirrors the S-DSP mute line: while set,
//  MSU output is silenced but the stream still advances (matches ares).
void snes_msu1_mixSamples(INT16* out, INT32 samples, INT32 outRate, INT32 dspMuted);

#endif
