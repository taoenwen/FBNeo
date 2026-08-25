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
	if (nRomLen <= 0xBD || nHdrLen <= 0xBD) {
		return false;
	}

	UINT32 nSum = 0;
	for (INT32 i = 0xA0; i <= 0xBD; i++) {
		nSum += p[i];
	}
	if (((nSum + 0x19) & 0xFF) == 0) {				// GBATEK header checksum: sum(A0..BD) == -0x19
		return true;
	}

	if (p[3] != 0xEA) {								// Fallback: ARM branch at the reset vector
		return false;
	}
	for (INT32 i = 0xA0; i <= 0xAF; i++) {			// ... with printable title/game code
		if (p[i] != 0 && (p[i] < 0x20 || p[i] > 0x7E)) {
			return false;
		}
	}
	return true;
}

static const struct ConsoleRomSystem g_Systems[] = {
	{ "GBA", "gba_aio", "gba_gba", "gba", "gba_bios.bin", "gba_custom_", 0x80, 64 * 1024 * 1024, ConsoleRomGbaHeader },
};

static const struct ConsoleRomSystem* ConsoleRomFindSystem(const char* szName)
{
	for (const struct ConsoleRomSystem* p = g_Systems; p != g_Systems + ARRAY_SIZE(g_Systems); p++) {
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

static const struct ConsoleRomSystem* g_pSys = NULL;
static struct BurnDriver* g_pDrv = NULL;
static INT32 g_nDrvIdx = -1;

static char g_ShortName[64];
static char g_FullName[MAX_PATH + 32];
static wchar_t g_FullNameW[MAX_PATH + 34];
static char g_ZipName[MAX_PATH];
static char g_RomName[MAX_PATH];
static char g_BiosZipName[64];		// Driver-side boardrom name (e.g. "gba_gba")
static char g_BiosZip[64];			// Resolved archive name holding the BIOS set (e.g. "gba")
static char g_BiosFile[32];			// Bare BIOS file name (e.g. "gba_bios.bin")

static struct BurnRomInfo g_RomDesc[1];

static INT32 ConsoleRomGetZipName(char** pszName, UINT32 i)
{
	if (pszName == NULL) {
		return 1;
	}
	if (i == 0 && g_ZipName[0] != 0) {
		*pszName = g_ZipName;
		return 0;
	}
	if (i == 1 && g_BiosZip[0] != 0) {				// BIOS set archive, searched like a normal parent set
		*pszName = g_BiosZip;
		return 0;
	}
	if (i == 2 && g_BiosFile[0] != 0) {				// Bare BIOS file beside the ROM / in the ROM dirs
		*pszName = g_BiosFile;
		return 0;
	}
	*pszName = NULL;
	return 1;
}

static INT32 ConsoleRomGetRomInfo(struct BurnRomInfo* pri, UINT32 i)
{
	if (g_pSys == NULL) {
		return 1;
	}

	if (i == 0) {
		if (g_RomDesc[0].nLen == 0) {
			return 1;
		}
		if (pri) {
			*pri = g_RomDesc[0];
		}
		return 0;
	}

	if (g_pSys->szBiosDriver != NULL && i >= g_pSys->nBiosSlot) {
		const struct BurnDriver* pBios = BurnGetDriver(g_pSys->szBiosDriver);
		if (pBios == NULL || pBios->GetRomInfo == NULL) {
			return 1;
		}
		return pBios->GetRomInfo(pri, i - g_pSys->nBiosSlot);
	}

	static const struct BurnRomInfo Empty = { "", 0, 0, 0 };	// Filler slots: keeps the BIOS slot reachable, mirrors STDROMPICKEXT
	if (pri) {
		*pri = Empty;
	}
	return 0;
}

static INT32 ConsoleRomGetRomName(char** pszName, UINT32 i, INT32 nAka)
{
	if (g_pSys == NULL || pszName == NULL) {
		return 1;
	}

	if (i == 0) {
		if (nAka || g_RomName[0] == 0) {
			return 1;
		}
		*pszName = g_RomName;
		return 0;
	}

	if (g_pSys->szBiosDriver != NULL && i >= g_pSys->nBiosSlot) {
		const struct BurnDriver* pBios = BurnGetDriver(g_pSys->szBiosDriver);
		if (pBios == NULL || pBios->GetRomName == NULL) {
			return 1;
		}
		return pBios->GetRomName(pszName, i - g_pSys->nBiosSlot, nAka);
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
			for (const struct ConsoleRomSystem* pSys = g_Systems; pSys != g_Systems + ARRAY_SIZE(g_Systems); pSys++) {
				if (pList[i].nLen <= 0xBD || pList[i].nLen > pSys->nMaxSize) {
					continue;
				}
				memset(header, 0, sizeof(header));
				if (ZipLoadFile(header, sizeof(header), NULL, i)) {
					continue;
				}
				if (pSys->RomHeaderValid(header, sizeof(header), pList[i].nLen)) {
					nFound = i;
					pFoundSys = pSys;
					break;
				}
			}
		}

		if (nFound >= 0) {
			pInfo->szSystem = pFoundSys->szName;
			pInfo->nLen = pList[nFound].nLen;
			pInfo->nCrc = pList[nFound].nCrc;					// zip/7z listings already carry the CRC32
			strcpy(pInfo->szRomName, ConsoleRomBaseName(pList[nFound].szName));
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

	for (const struct ConsoleRomSystem* pSys = g_Systems; pSys != g_Systems + ARRAY_SIZE(g_Systems); pSys++) {
		if ((UINT32)lSize <= pSys->nMaxSize && pSys->RomHeaderValid(header, nGot, (UINT32)lSize)) {
			pInfo->szSystem = pSys->szName;
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
	return g_FullName;
}

void ConsoleRomSetFullNameW(const wchar_t* szW)
{
	const size_t nCount = sizeof(g_FullNameW) / sizeof(g_FullNameW[0]);

	if (szW == NULL || szW[0] == 0) {
		g_FullNameW[0] = 0;
		g_FullNameW[1] = 0;
		if (g_pDrv != NULL) {
			g_pDrv->szFullNameW = NULL;
		}
		return;
	}

	wcsncpy(g_FullNameW, szW, nCount - 2);
	g_FullNameW[nCount - 2] = 0;
	g_FullNameW[nCount - 1] = 0;					// Double-NUL: DRV_NEXTNAME enumeration terminates cleanly

	if (g_pDrv != NULL) {
		g_pDrv->szFullNameW = g_FullNameW;
	}
}

INT32 ConsoleRomCreateDriver(const char* szPath, const struct ConsoleRomInfo* pInfo)
{
	if (szPath == NULL || pInfo == NULL || pInfo->nLen == 0) {
		return -1;
	}

	g_pSys = ConsoleRomFindSystem(pInfo->szSystem);
	if (g_pSys == NULL) {
		return -1;
	}

	// Update per-load state (pointers stay valid across reloads: same static buffers)
	sprintf(g_ShortName, "%s%08x", g_pSys->szShortPrefix, ConsoleRomPathHash(szPath));
	strcpy(g_ZipName, pInfo->szZipName);
	strcpy(g_RomName, pInfo->szRomName);
	strcpy(g_FullName, ConsoleRomBaseName(szPath));
	ConsoleRomStripExt(g_FullName);					// Fallback title: file name without the extension
	if (pInfo->nCrc != 0) {
		ConsoleRomLookupTitle(g_FullName, (INT32)sizeof(g_FullName), pInfo);	// Prefer the database title
	}
	g_RomDesc[0].szName = g_RomName;
	g_RomDesc[0].nLen   = pInfo->nLen;
	g_RomDesc[0].nCrc   = pInfo->nCrc;
	g_RomDesc[0].nType  = BRF_PRG;
	g_BiosZipName[0] = 0;
	g_BiosZip[0] = 0;
	g_BiosFile[0] = 0;
	if (g_pSys->szBiosDriver != NULL) {
		strcpy(g_BiosZipName, g_pSys->szBiosDriver);
	}
	if (g_pSys->szBiosZip != NULL) {
		strcpy(g_BiosZip, g_pSys->szBiosZip);
	}
	if (g_pSys->szBiosFile != NULL) {
		strcpy(g_BiosFile, g_pSys->szBiosFile);
	}

	if (g_nDrvIdx >= 0) {							// Reuse the single session slot
		return g_nDrvIdx;
	}

	const struct BurnDriver* pBase = BurnGetDriver(g_pSys->szShellDriver);
	if (pBase == NULL) {
		return -1;
	}

	g_pDrv = (struct BurnDriver*)calloc(1, sizeof(struct BurnDriver));
	if (g_pDrv == NULL) {
		return -1;
	}
	memcpy(g_pDrv, pBase, sizeof(struct BurnDriver));

	g_pDrv->szShortName = g_ShortName;
#ifdef _UNICODE
	g_pDrv->szFullNameA = NULL;						// Unicode builds: only the W title is exposed
#else
	g_pDrv->szFullNameA = g_FullName;				// ANSI builds: keep the A title (W is unused)
#endif
	g_pDrv->szFullNameW = NULL;
	g_pDrv->szBoardROM  = (g_pSys->szBiosDriver != NULL) ? g_BiosZipName : NULL;
	g_pDrv->szParent    = NULL;
	g_pDrv->GetZipName  = ConsoleRomGetZipName;
	g_pDrv->GetRomInfo  = ConsoleRomGetRomInfo;
	g_pDrv->GetRomName  = ConsoleRomGetRomName;
	g_pDrv->Flags       = BDF_GAME_WORKING | BDF_CUSTOMROM;

	if (~0U == LinkExtlDrivers(g_pDrv, &nBurnDrvCount)) {
		free(g_pDrv);
		g_pDrv = NULL;
		return -1;
	}
	g_nDrvIdx = (INT32)(nBurnDrvCount - 1);

	return g_nDrvIdx;
}

void ConsoleRomExit(void)
{
	if (g_pDrv != NULL) {
		free(g_pDrv);
		g_pDrv = NULL;
	}
	g_nDrvIdx = -1;
}
