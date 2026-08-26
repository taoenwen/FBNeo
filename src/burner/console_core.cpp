// Cross-platform direct console ROM loading core.
// Detect -> header identification (not extension) -> hidden runtime driver
// cloned from the per-system shell driver. No win32-only dependencies.
#include "burner.h"
#include "burnint.h"
#include "console_core.h"
#include "zlib.h"
#include <wchar.h>

// ---------------------------------------------------------------------------
// System table

struct ConsoleRomSystem {
	const char* szName;			// Display name
	const char* szShellDriver;	// Hidden shell driver to clone
	const char* szBiosDriver;	// BIOS set driver exposed at nBiosSlot (NULL = none)
	const char* szBiosZip;		// Archive name holding the BIOS set (as GetZipName would resolve it)
	const char* szBiosFile;		// Bare BIOS file name (searched as-is in the ROM dirs / file dir)
	const char* szShortPrefix;	// Runtime driver short-name prefix
	UINT32 nBiosSlot;
	UINT32 nMaxSize;
	bool (*RomHeaderValid)(const UINT8* p, size_t nHdrLen, UINT32 nRomLen);
};

static bool ConsoleRomGbaHeader(const UINT8* p, size_t nHdrLen, UINT32 nRomLen)
{
	if (nRomLen <= 0xbd || nHdrLen <= 0xbd) {
		return false;
	}

	UINT32 nSum = 0;
	for (INT32 i = 0xa0; i <= 0xbd; i++) {
		nSum += p[i];
	}
	if (((nSum + 0x19) & 0xff) == 0) {				// GBATEK header checksum: sum(A0..BD) == -0x19
		return true;
	}

	if (p[3] != 0xea) {								// Fallback: ARM branch at the reset vector
		return false;
	}
	for (INT32 i = 0xa0; i <= 0xaf; i++) {			// ... with printable title/game code
		if (p[i] != 0 && (p[i] < 0x20 || p[i] > 0x7e)) {
			return false;
		}
	}
	return true;
}

static const struct ConsoleRomSystem Systems[] = {
	{ "GBA", "gba_aio", "gba_gba", "gba", "gba_bios.bin", "gba_custom_", 0x80, 64 * 1024 * 1024, ConsoleRomGbaHeader },
};

static const struct ConsoleRomSystem* ConsoleRomFindSystem(const char* szName)
{
	for (const struct ConsoleRomSystem* p = Systems; p != Systems + ARRAY_SIZE(Systems); p++) {
		if (strcmp(p->szName, szName) == 0) {
			return p;
		}
	}
	return NULL;
}

// ---------------------------------------------------------------------------
// Helpers

static char* ConsoleRomBaseName(const char* szPath)
{
	const char* p = szPath + strlen(szPath);
	while (p > szPath && p[-1] != '\\' && p[-1] != '/') {
		p--;
	}
	return (char*)p;
}

static void ConsoleRomStripExt(char* szName)
{
	char* p = strrchr(szName, '.');
	if (p != NULL) {
		*p = 0;
	}
}

static UINT32 ConsoleRomPathHash(const char* szPath)
{
	UINT32 h = 2166136261u;
	for (const char* p = szPath; *p; p++) {
		UINT8 c = (UINT8)*p;
		if (c >= 'A' && c <= 'Z') {
			c += 0x20;
		}
		h ^= c;
		h *= 16777619u;
	}
	return h;
}

// ---------------------------------------------------------------------------
// Runtime driver record

static const struct ConsoleRomSystem* pSys = NULL;
static struct BurnDriver* pDrv = NULL;
static INT32 nDrvIdx = -1;

static char ShortName[64];
static char FullName[MAX_PATH + 32];
static wchar_t FullNameW[MAX_PATH + 34];
static char ZipName[MAX_PATH];
static char RomName[MAX_PATH];
static char BiosZipName[64];	// Driver-side boardrom name (e.g. "gba_gba")
static char BiosZip[64];	// Resolved archive name holding the BIOS set (e.g. "gba")
static char BiosFile[32];

static struct BurnRomInfo RomDesc[1];

static INT32 ConsoleRomGetZipName(char** pszName, UINT32 i)
{
	if (pszName == NULL) {
		return 1;
	}
	if (i == 0 && ZipName[0] != 0) {
		*pszName = ZipName;
		return 0;
	}
	if (i == 1 && BiosZip[0] != 0) {				// BIOS set archive, searched like a normal parent set
		*pszName = BiosZip;
		return 0;
	}
	if (i == 2 && BiosFile[0] != 0) {				// Bare BIOS file beside the ROM / in the ROM dirs
		*pszName = BiosFile;
		return 0;
	}
	*pszName = NULL;
	return 1;
}

static INT32 ConsoleRomGetRomInfo(struct BurnRomInfo* pri, UINT32 i)
{
	if (pSys == NULL) {
		return 1;
	}

	if (i == 0) {
		if (RomDesc[0].nLen == 0) {
			return 1;
		}
		if (pri) {
			*pri = RomDesc[0];
		}
		return 0;
	}

	if (pSys->szBiosDriver != NULL && i >= pSys->nBiosSlot) {
		const struct BurnDriver* pBios = BurnGetDriver(pSys->szBiosDriver);
		if (pBios == NULL || pBios->GetRomInfo == NULL) {
			return 1;
		}
		return pBios->GetRomInfo(pri, i - pSys->nBiosSlot);
	}

	static const struct BurnRomInfo Empty = { "", 0, 0, 0 };	// Filler slots: keeps the BIOS slot reachable, mirrors STDROMPICKEXT
	if (pri) {
		*pri = Empty;
	}
	return 0;
}

static INT32 ConsoleRomGetRomName(char** pszName, UINT32 i, INT32 nAka)
{
	if (pSys == NULL || pszName == NULL) {
		return 1;
	}

	if (i == 0) {
		if (nAka || RomName[0] == 0) {
			return 1;
		}
		*pszName = RomName;
		return 0;
	}

	if (pSys->szBiosDriver != NULL && i >= pSys->nBiosSlot) {
		const struct BurnDriver* pBios = BurnGetDriver(pSys->szBiosDriver);
		if (pBios == NULL || pBios->GetRomName == NULL) {
			return 1;
		}
		return pBios->GetRomName(pszName, i - pSys->nBiosSlot, nAka);
	}

	if (nAka) {
		return 1;
	}
	*pszName = (char*)"";
	return 0;
}

// ---------------------------------------------------------------------------
// Detection

INT32 ConsoleRomDetect(const char* szPath, struct ConsoleRomInfo* pInfo)
{
	if (szPath == NULL || pInfo == NULL) {
		return CONSOLE_ROM_ERR_INVALID;
	}
	memset(pInfo, 0, sizeof(*pInfo));

	FILE* f = fopen(szPath, "rb");
	if (f == NULL) {
		return CONSOLE_ROM_ERR_NOT_FOUND;
	}

	UINT8 magic[4] = { 0, 0, 0, 0 };
	size_t nRead = fread(magic, 1, 4, f);
	fclose(f);

	bool bIsArchive = (nRead >= 2) && ((magic[0] == 'P' && magic[1] == 'K') || (magic[0] == '7' && magic[1] == 'z'));

	if (bIsArchive) {
		if (ZipOpen((char*)szPath)) {
			return CONSOLE_ROM_ERR_INVALID;
		}

		struct ZipEntry* pList = NULL;
		INT32 nListCount = 0;
		if (ZipGetList(&pList, &nListCount) || pList == NULL) {
			ZipClose();
			return CONSOLE_ROM_ERR_INVALID;
		}

		INT32 nFound = -1;
		const struct ConsoleRomSystem* pFoundSys = NULL;
		UINT8 header[0x200];
		for (INT32 i = 0; i < nListCount && nFound < 0; i++) {
			if (pList[i].szName == NULL) {
				continue;
			}
			for (const struct ConsoleRomSystem* pSystem = Systems; pSystem != Systems + ARRAY_SIZE(Systems); pSystem++) {
				if (pList[i].nLen <= 0xbd || pList[i].nLen > pSystem->nMaxSize) {
					continue;
				}
				memset(header, 0, sizeof(header));
				if (ZipLoadFile(header, sizeof(header), NULL, i)) {
					continue;
				}
				if (pSystem->RomHeaderValid(header, sizeof(header), pList[i].nLen)) {
					nFound = i;
					pFoundSys = pSystem;
					break;
				}
			}
		}

		if (nFound >= 0) {
			pInfo->szSystem = pFoundSys->szName;
			pInfo->nLen = pList[nFound].nLen;
			pInfo->nCrc = pList[nFound].nCrc;					// zip/7z listings already carry the CRC32
			// zip member names come from an external archive, so they aren't
			// bounded by MAX_PATH; truncate explicitly instead of strcpy.
			const char* szMemberName = ConsoleRomBaseName(pList[nFound].szName);
			strncpy(pInfo->szRomName, szMemberName, MAX_PATH - 1);
			pInfo->szRomName[MAX_PATH - 1] = 0;
			strcpy(pInfo->szZipName, ConsoleRomBaseName(szPath));
			ConsoleRomStripExt(pInfo->szZipName);
		}

		for (INT32 i = 0; i < nListCount; i++) {
			if (pList[i].szName) {
				free(pList[i].szName);
			}
		}
		free(pList);
		ZipClose();

		return (nFound >= 0) ? CONSOLE_ROM_OK : CONSOLE_ROM_ERR_INVALID;
	}

	// Bare file: identify by header, name stays as-is (extension irrelevant)
	f = fopen(szPath, "rb");
	if (f == NULL) {
		return CONSOLE_ROM_ERR_NOT_FOUND;
	}
	fseek(f, 0, SEEK_END);
	long lSize = ftell(f);
	fseek(f, 0, SEEK_SET);

	UINT8 header[0x200];
	memset(header, 0, sizeof(header));
	size_t nGot = fread(header, 1, sizeof(header), f);

	if (lSize <= 0) {
		fclose(f);
		return CONSOLE_ROM_ERR_INVALID;
	}

	// Bare files have no stored CRC in a listing: compute it while the file is open
	UINT32 nCrc = crc32(0, header, (uInt)nGot);
	UINT8 buf[64 * 1024];
	size_t nChunk;
	while ((nChunk = fread(buf, 1, sizeof(buf), f)) > 0) {
		nCrc = crc32(nCrc, buf, (uInt)nChunk);
	}
	fclose(f);

	for (const struct ConsoleRomSystem* pSystem = Systems; pSystem != Systems + ARRAY_SIZE(Systems); pSystem++) {
		if ((UINT32)lSize <= pSystem->nMaxSize && pSystem->RomHeaderValid(header, nGot, (UINT32)lSize)) {
			pInfo->szSystem = pSystem->szName;
			pInfo->nLen = (UINT32)lSize;
			pInfo->nCrc = nCrc;
			strcpy(pInfo->szRomName, ConsoleRomBaseName(szPath));
			strcpy(pInfo->szZipName, ConsoleRomBaseName(szPath));
			return CONSOLE_ROM_OK;
		}
	}

	return CONSOLE_ROM_ERR_INVALID;
}

// ---------------------------------------------------------------------------
// Driver build / release

// Look the ROM up in the built-in driver table (CRC+size), like a No-Intro DB lookup
static void ConsoleRomLookupTitle(char* szOut, INT32 nMax, const struct ConsoleRomInfo* pInfo)
{
	const UINT32 nOldActive = nBurnDrvActive;

	for (UINT32 i = 0; i < nBurnDrvCount; i++) {
		nBurnDrvActive = i;

		if ((BurnDrvGetFlags() & (BDF_BOARDROM | BDF_CUSTOMROM)) ||
			((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) != HARDWARE_GBA)) {
			continue;
		}

		struct BurnRomInfo ri;
		if (BurnDrvGetRomInfo(&ri, 0) || ri.nCrc != pInfo->nCrc || ri.nLen != pInfo->nLen) {
			continue;
		}

		const char* pszName = BurnDrvGetTextA(DRV_FULLNAME);
		if (pszName != NULL) {
			strncpy(szOut, pszName, nMax - 1);
			szOut[nMax - 1] = 0;
			break;
		}
	}

	nBurnDrvActive = nOldActive;
}

const char* ConsoleRomGetFullName(void)
{
	return FullName;
}

void ConsoleRomSetFullNameW(const wchar_t* szW)
{
	const size_t nCount = sizeof(FullNameW) / sizeof(FullNameW[0]);

	if (szW == NULL || szW[0] == 0) {
		FullNameW[0] = 0;
		FullNameW[1] = 0;
		if (pDrv != NULL) {
			pDrv->szFullNameW = NULL;
		}
		return;
	}

	wcsncpy(FullNameW, szW, nCount - 2);
	FullNameW[nCount - 2] = 0;
	FullNameW[nCount - 1] = 0;					// Double-NUL: DRV_NEXTNAME enumeration terminates cleanly

	if (pDrv != NULL) {
		pDrv->szFullNameW = FullNameW;
	}
}

INT32 ConsoleRomCreateDriver(const char* szPath, const struct ConsoleRomInfo* pInfo)
{
	if (szPath == NULL || pInfo == NULL || pInfo->nLen == 0) {
		return -1;
	}

	pSys = ConsoleRomFindSystem(pInfo->szSystem);
	if (pSys == NULL) {
		return -1;
	}

	// Update per-load state (pointers stay valid across reloads: same static buffers)
	sprintf(ShortName, "%s%08x", pSys->szShortPrefix, ConsoleRomPathHash(szPath));
	strcpy(ZipName, pInfo->szZipName);
	strcpy(RomName, pInfo->szRomName);
	strcpy(FullName, ConsoleRomBaseName(szPath));
	ConsoleRomStripExt(FullName);					// Fallback title: file name without the extension
	if (pInfo->nCrc != 0) {
		ConsoleRomLookupTitle(FullName, (INT32)sizeof(FullName), pInfo);	// Prefer the database title
	}
	RomDesc[0].szName = RomName;
	RomDesc[0].nLen   = pInfo->nLen;
	RomDesc[0].nCrc   = pInfo->nCrc;
	RomDesc[0].nType  = BRF_PRG;
	BiosZipName[0] = 0;
	BiosZip[0] = 0;
	BiosFile[0] = 0;
	if (pSys->szBiosDriver != NULL) {
		strcpy(BiosZipName, pSys->szBiosDriver);
	}
	if (pSys->szBiosZip != NULL) {
		strcpy(BiosZip, pSys->szBiosZip);
	}
	if (pSys->szBiosFile != NULL) {
		strcpy(BiosFile, pSys->szBiosFile);
	}

	if (nDrvIdx >= 0) {							// Reuse the single session slot
		return nDrvIdx;
	}

	const struct BurnDriver* pBase = BurnGetDriver(pSys->szShellDriver);
	if (pBase == NULL) {
		return -1;
	}

	pDrv = (struct BurnDriver*)calloc(1, sizeof(struct BurnDriver));
	if (pDrv == NULL) {
		return -1;
	}
	memcpy(pDrv, pBase, sizeof(struct BurnDriver));

	pDrv->szShortName = ShortName;
	// Keep the ANSI title in Unicode builds too: BurnDrvGetTextA() returns the field
	// as-is, and consumers like MakeScreenShot()/DecorateGameName() strlen() it.
	pDrv->szFullNameA = FullName;				// (W still takes precedence in BurnDrvGetText)
	pDrv->szFullNameW = NULL;
	pDrv->szBoardROM  = (pSys->szBiosDriver != NULL) ? BiosZipName : NULL;
	pDrv->szParent    = NULL;
	pDrv->GetZipName  = ConsoleRomGetZipName;
	pDrv->GetRomInfo  = ConsoleRomGetRomInfo;
	pDrv->GetRomName  = ConsoleRomGetRomName;
	pDrv->Flags       = BDF_GAME_WORKING | BDF_CUSTOMROM;

	if (~0U == LinkExtlDrivers(pDrv, &nBurnDrvCount)) {
		free(pDrv);
		pDrv = NULL;
		return -1;
	}
	nDrvIdx = (INT32)(nBurnDrvCount - 1);

	return nDrvIdx;
}

void ConsoleRomExit(void)
{
	if (pDrv != NULL) {
		free(pDrv);
		pDrv = NULL;
	}
	nDrvIdx = -1;
}
