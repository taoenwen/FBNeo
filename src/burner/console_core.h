// Cross-platform direct console ROM loading core.
// Detects ROMs by file header (not extension), builds a hidden runtime driver
// cloned from a per-system shell driver, and owns its lifetime.
// Extend the system table in console_core.cpp for other consoles (NES/SNES/...).
#pragma once

#ifndef MAX_PATH
#define MAX_PATH	260
#endif

struct ConsoleRomInfo {
	const char* szSystem;		// System name from the table (points into static storage)
	char szZipName[MAX_PATH];	// Name used to locate the file during BzipOpen (bare: filename w/ ext, archive: basename w/o ext)
	char szRomName[MAX_PATH];	// Basename of the ROM entry (archive member / bare file)
	UINT32 nLen;				// ROM size in bytes
	UINT32 nCrc;				// CRC32 of the ROM (0 = unknown)
};

#define CONSOLE_ROM_OK				0
#define CONSOLE_ROM_ERR_NOT_FOUND	-1
#define CONSOLE_ROM_ERR_INVALID		-2

// Identify the first supported ROM in a file or archive (by header, not extension).
INT32 ConsoleRomDetect(const char* szPath, struct ConsoleRomInfo* pInfo);

// Build or reuse the hidden runtime driver for the detected ROM; returns the engine driver index.
INT32 ConsoleRomCreateDriver(const char* szPath, const struct ConsoleRomInfo* pInfo);

// Current ANSI title of the runtime driver (database title, or the extensionless file name).
const char* ConsoleRomGetFullName(void);

// Update the runtime driver's Unicode title (may be non-English). Double-NUL terminated so
// DRV_NEXTNAME enumeration terminates cleanly.
void ConsoleRomSetFullNameW(const wchar_t* szW);

// Release the runtime driver record (called once at app exit).
void ConsoleRomExit(void);
