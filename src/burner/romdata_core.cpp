// =============================================================================
//  FBNeo  -  RomData core (platform-independent)
// =============================================================================

#include "burnint.h"
#include "romdata_core.h"
#include "drv/capcom/cps.h"
#include "drv/galaxian/gal.h"
#include "drv/megadrive/megadrive.h"
#include "drv/sega/sys16.h"
#include "drv/taito/taito.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef BUILD_WIN32
#include <windows.h>
#endif
#ifdef _MSC_VER
#include "dirent.h"
#else
#include <dirent.h>
#endif

#ifdef _UNICODE
#define RD_DIR		_WDIR
#define RD_dirent	_wdirent
#define rd_opendir	_wopendir
#define rd_readdir	_wreaddir
#define rd_closedir	_wclosedir
static void rd_dname(const struct _wdirent* de, char* out, int outSize)
{
	int i;
	for (i = 0; i < outSize - 1 && de->d_name[i]; i++)
		out[i] = (char)de->d_name[i];
	out[i] = 0;
}
#define _T_NARROW_FMT	_T("%hs")		// narrow string in wide printf (MSVC only)
#else
#define RD_DIR		DIR
#define RD_dirent	dirent
#define rd_opendir	opendir
#define rd_readdir	readdir
#define rd_closedir	closedir
#define rd_dname(de, out, outSize)  snprintf(out, outSize, "%s", (de)->d_name)
#define _T_NARROW_FMT	_T("%s")
#endif

#define SHORT_MAX	128
#define DATE_MAX	32

// From burn.cpp (driver list plumbing established by the pDriverEx foundation).
extern "C" UINT32 LinkExtlDrivers(struct BurnDriver* drv, UINT32* pallCount);
extern "C" struct BurnDriver* BurnGetDriver(const char* szName);

//  Lightweight RomData driver record.
struct RomDataDrv {
	struct BurnDriver	drv;			// embedded engine driver (appended to pDriverEx)
	struct BurnRomInfo*	pRomInfo;		// the ONE ROM table (final form, owns szName)
	UINT32				nRomInfoCount;
	char*				pszShortName;
	char*				pszDrvName;
	char*				pszDate;
	char*				pszFullNameA;
	wchar_t*			pszFullNameW;
	char*				pszExtName;
};

static struct RomDataDrv** pRDDrv      = NULL;	// array of RomData records
static UINT32              nRDDrvCount = 0;

static char* rd_strdup(const char* s)
{
	if (!s) return NULL;
	size_t n = strlen(s) + 1;
	char* p = (char*)malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

static inline bool rd_is_empty(const TCHAR* s) { return (!s || *s == _T('\0')); }
static inline bool rd_is_emptyA(const char* s) { return (!s || *s == '\0'); }

//  Encoding detection + normalisation to UTF-8/char
enum RDEncoding {
	RD_ENC_ANSI = 0,	// unknown 8-bit / ASCII-compatible; passed through verbatim
	RD_ENC_UTF8,		// UTF-8 without BOM (ASCII is a subset)
	RD_ENC_UTF8_BOM,
	RD_ENC_UTF16_LE,
	RD_ENC_UTF16_BE,
};

static bool rd_is_valid_utf8(const UINT8* buf, size_t len)
{
	size_t p = 0;
	while (p < len) {
		UINT8 c = buf[p];
		if (c < 0x80) { p++; continue; }				// ASCII

		int nExtra;
		if      (c >= 0xC2 && c <= 0xDF) nExtra = 1;	// 2-byte
		else if (c >= 0xE0 && c <= 0xEF) nExtra = 2;	// 3-byte
		else if (c >= 0xF0 && c <= 0xF4) nExtra = 3;	// 4-byte
		else return false;								// invalid lead byte -> not UTF-8

		// Each of the nExtra continuation bytes must exist and be 10xxxxxx.
		for (int i = 1; i <= nExtra; i++) {
			if (p + (size_t)i >= len) return false;		// truncated sequence at EOF
			if (0x80 != (buf[p + i] & 0xC0)) return false;
		}
		p += nExtra + 1;
	}
	return true;
}

static UINT8* rd_read_all(FILE* fp, size_t* pLen)
{
	*pLen = 0;
	if (fseek(fp, 0, SEEK_END) != 0) return NULL;
	long sz = ftell(fp);
	if (sz < 0) return NULL;
	rewind(fp);
	UINT8* buf = (UINT8*)malloc((size_t)sz + 1);
	if (!buf) return NULL;
	size_t got = fread(buf, 1, (size_t)sz, fp);
	buf[got] = 0;
	*pLen = got;
	return buf;
}

static RDEncoding rd_detect_encoding(const UINT8* buf, size_t len)
{
	// BOM first (Notepad++ determineEncodingFromBOM order).
	if (len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)	return RD_ENC_UTF8_BOM;
	if (len >= 2 && buf[0] == 0xFF && buf[1] == 0xFE)					return RD_ENC_UTF16_LE;
	if (len >= 2 && buf[0] == 0xFE && buf[1] == 0xFF)					return RD_ENC_UTF16_BE;
	if (len == 0)														return RD_ENC_ANSI;

	// UTF-16 without BOM: ASCII text alternates with 0x00 bytes.  Sample the
	// head and see whether even- or odd-offset zero bytes dominate.
	size_t scan = len < 4096 ? len : 4096;
	size_t zeroEven = 0, zeroOdd = 0;
	for (size_t i = 0; i < scan; i++) {
		if (buf[i] == 0x00) { if (i & 1) zeroOdd++; else zeroEven++; }
	}
	// A plain 8-bit/UTF-8 text file has essentially no embedded NULs.
	if ((zeroEven + zeroOdd) * 4 >= scan) {
		if (zeroOdd  > zeroEven * 4) return RD_ENC_UTF16_LE;	// high byte (odd)  mostly 0
		if (zeroEven > zeroOdd  * 4) return RD_ENC_UTF16_BE;	// high byte (even) mostly 0
	}

	// No BOM, no NUL pattern: UTF-8 if the byte sequence validates, else ANSI.
	return rd_is_valid_utf8(buf, len) ? RD_ENC_UTF8 : RD_ENC_ANSI;
}

// ANSI (system code page, e.g. GBK) -> malloc'd UTF-8.  Win32 only.
#ifdef BUILD_WIN32
static char* rd_ansi_to_utf8(const char* s, size_t len)
{
	if (!s) return NULL;
	INT32 wn = MultiByteToWideChar(CP_ACP, 0, s, (INT32)len, NULL, 0);
	if (wn <= 0) return NULL;
	wchar_t* w = (wchar_t*)malloc((size_t)(wn + 1) * sizeof(wchar_t));
	if (!w) return NULL;
	MultiByteToWideChar(CP_ACP, 0, s, (INT32)len, w, wn);
	w[wn] = 0;
	INT32 un = WideCharToMultiByte(CP_UTF8, 0, w, wn, NULL, 0, NULL, NULL);
	if (un <= 0) { free(w); return NULL; }
	char* u = (char*)malloc((size_t)un + 1);
	if (!u) { free(w); return NULL; }
	WideCharToMultiByte(CP_UTF8, 0, w, wn, u, un, NULL, NULL);
	u[un] = 0;
	free(w);
	return u;
}
#endif

static char* rd_load_text(const TCHAR* szPath)
{
	FILE* fp = _tfopen(szPath, _T("rb"));
	if (!fp) return NULL;

	size_t len = 0;
	UINT8* raw = rd_read_all(fp, &len);
	fclose(fp);
	if (!raw) return NULL;

	RDEncoding enc = rd_detect_encoding(raw, len);

	if (enc == RD_ENC_UTF8) {
		return (char*)raw;
	}
	if (enc == RD_ENC_ANSI) {
#ifdef BUILD_WIN32
		char* u = rd_ansi_to_utf8((const char*)raw, len);	// GBK/... -> UTF-8
		if (u) { free(raw); return u; }
#endif
		return (char*)raw;
	}
	if (enc == RD_ENC_UTF8_BOM) {
		memmove(raw, raw + 3, len - 3 + 1);					// drop BOM
		return (char*)raw;
	}

	// UTF-16 LE/BE -> UTF-8.  A .dat only needs BMP; encode up to 3-byte UTF-8.
	size_t start = 2;										// skip BOM
	size_t units = (len - start) / 2;
	// worst case 3 bytes per unit + NUL
	char* out = (char*)malloc(units * 3 + 1);
	if (!out) { free(raw); return NULL; }
	size_t o = 0;
	for (size_t i = 0; i < units; i++) {
		unsigned int cp;
		UINT8 b0 = raw[start + i * 2 + 0];
		UINT8 b1 = raw[start + i * 2 + 1];
		cp = (enc == RD_ENC_UTF16_LE) ? (unsigned int)(b0 | (b1 << 8))
									  : (unsigned int)(b1 | (b0 << 8));
		if (cp < 0x80) {
			out[o++] = (char)cp;
		} else if (cp < 0x800) {
			out[o++] = (char)(0xC0 | (cp >> 6));
			out[o++] = (char)(0x80 | (cp & 0x3F));
		} else {
			out[o++] = (char)(0xE0 | (cp >> 12));
			out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			out[o++] = (char)(0x80 | (cp & 0x3F));
		}
	}
	out[o] = 0;
	free(raw);
	return out;
}

static const char* RD_DELIMS = " \t\r\n,%:|{}";

static char* rd_qtoken(char* s, char** ppSaved)
{
	char* p = s ? s : *ppSaved;
	if (!p) return NULL;

	p += strspn(p, RD_DELIMS);						// skip leading delimiters
	if (*p == '\0') { *ppSaved = NULL; return NULL; }

	char* token = p;
	if (*p == '"') {								// quoted field
		token = ++p;
		char* q = strchr(p, '"');
		if (q) { *q = '\0'; p = q + 1; }
		else   { p = strchr(p, '\0'); }
	} else {
		p = strpbrk(p, RD_DELIMS);
	}

	if (p && *p != '\0') { *p = '\0'; *ppSaved = p + 1; }
	else                 { *ppSaved = NULL; }
	return token;
}

// Parse a hex string to UINT32 with strict validation.
static bool rd_hex(const char* s, UINT32* out)
{
	if (rd_is_emptyA(s) || !out) return false;
	char* end = NULL;
	unsigned long v = strtoul(s, &end, 16);
	if (s == end || *end != '\0') return false;
	*out = (UINT32)v;
	return true;
}

//  Pure parse layer:  .dat text  ->  header fields + BurnRomInfo[]
struct RomDataParsed {
	char   szShortName[SHORT_MAX];
	char   szDrvName[SHORT_MAX];
	char   szDate[DATE_MAX];
	char   szExtraRom[SHORT_MAX];
	char   szFullName[MAX_PATH];
	struct BurnRomInfo* pRomInfo;					// malloc'd table (owns each szName)
	UINT32 nRomInfoCount;
};

static void rd_parsed_free(struct RomDataParsed* pp)
{
	if (!pp) return;
	if (pp->pRomInfo) {
		for (UINT32 i = 0; i < pp->nRomInfoCount; i++)
			free_s((void**)&pp->pRomInfo[i].szName);
		free_s((void**)&pp->pRomInfo);
	}
	pp->nRomInfoCount = 0;
}

// Copy the base driver's ROM entry at the current index into the parsed table.
static INT32 rd_copy_base_entry(struct RomDataParsed* pp)
{
	INT32 nBase = BurnDrvGetIndex(pp->szDrvName);
	if (nBase < 0) return -1;

	struct BurnRomInfo ri;
	char* pszName = NULL;
	const UINT32 nOldDrvSel = nBurnDrvActive;
	nBurnDrvActive = (UINT32)nBase;

	memset(&ri, 0, sizeof(ri));
	INT32 rInfo = BurnDrvGetRomInfo(&ri, pp->nRomInfoCount);
	INT32 rName = BurnDrvGetRomName(&pszName, pp->nRomInfoCount, 0);
	nBurnDrvActive = nOldDrvSel;

	if (rInfo || rName || !pszName) return 1;		// past the base table: skip

	struct BurnRomInfo* grown = (struct BurnRomInfo*)realloc(
		pp->pRomInfo, (pp->nRomInfoCount + 1) * sizeof(struct BurnRomInfo));
	if (!grown) return -3;
	pp->pRomInfo = grown;

	struct BurnRomInfo* dst = &pp->pRomInfo[pp->nRomInfoCount];
	memset(dst, 0, sizeof(*dst));
	dst->szName = rd_strdup(pszName);
	if (!dst->szName) return -3;
	dst->nLen = ri.nLen; dst->nCrc = ri.nCrc; dst->nType = ri.nType;
	pp->nRomInfoCount++;
	return 0;
}

// ---------------------------------------------------------------------------
//  Symbolic ROM-type lookup table  (populated from the platform headers)
// ---------------------------------------------------------------------------
struct RDMacroMap { const char* pszName; UINT32 nValue; };
#define X(a) { #a, (UINT32)(a) }
static const struct RDMacroMap RDMacroTable[] = {
	X(BRF_PRG), X(BRF_GRA), X(BRF_SND), X(BRF_ESS), X(BRF_BIOS), X(BRF_SELECT), X(BRF_OPT), X(BRF_NODUMP),
	X(CPS1_68K_PROGRAM_BYTESWAP), X(CPS1_68K_PROGRAM_NO_BYTESWAP), X(CPS1_Z80_PROGRAM), X(CPS1_TILES),
	X(CPS1_OKIM6295_SAMPLES), X(CPS1_QSOUND_SAMPLES), X(CPS1_PIC),
	X(CPS1_EXTRA_TILES_SF2EBBL_400000), X(CPS1_EXTRA_TILES_400000), X(CPS1_EXTRA_TILES_SF2KORYU_400000),
	X(CPS1_EXTRA_TILES_SF2B_400000), X(CPS1_EXTRA_TILES_SF2MKOT_400000),
	X(CPS2_PRG_68K), X(CPS2_PRG_68K_SIMM), X(CPS2_PRG_68K_XOR_TABLE), X(CPS2_GFX), X(CPS2_GFX_SIMM),
	X(CPS2_GFX_SPLIT4), X(CPS2_GFX_SPLIT8), X(CPS2_GFX_19XXJ), X(CPS2_PRG_Z80), X(CPS2_QSND),
	X(CPS2_QSND_SIMM), X(CPS2_QSND_SIMM_BYTESWAP), X(CPS2_ENCRYPTION_KEY),
	X(GAL_ROM_Z80_PROG1), X(GAL_ROM_Z80_PROG2), X(GAL_ROM_Z80_PROG3), X(GAL_ROM_TILES_SHARED),
	X(GAL_ROM_TILES_CHARS), X(GAL_ROM_TILES_SPRITES), X(GAL_ROM_PROM), X(GAL_ROM_S2650_PROG1),
	X(SEGA_MD_ROM_LOAD_NORMAL), X(SEGA_MD_ROM_LOAD16_WORD_SWAP), X(SEGA_MD_ROM_LOAD16_BYTE),
	X(SEGA_MD_ROM_LOAD16_WORD_SWAP_CONTINUE_040000_100000), X(SEGA_MD_ROM_LOAD_NORMAL_CONTINUE_020000_080000),
	X(SEGA_MD_ROM_OFFS_000000), X(SEGA_MD_ROM_OFFS_000001), X(SEGA_MD_ROM_OFFS_020000), X(SEGA_MD_ROM_OFFS_080000),
	X(SEGA_MD_ROM_OFFS_100000), X(SEGA_MD_ROM_OFFS_100001), X(SEGA_MD_ROM_OFFS_200000), X(SEGA_MD_ROM_OFFS_300000),
	X(SEGA_MD_ROM_RELOAD_200000_200000), X(SEGA_MD_ROM_RELOAD_100000_300000), X(SEGA_MD_ARCADE_SUNMIXING),
	X(SYS16_ROM_PROG_FLAT), X(SYS16_ROM_PROG), X(SYS16_ROM_TILES), X(SYS16_ROM_SPRITES), X(SYS16_ROM_Z80PROG),
	X(SYS16_ROM_KEY), X(SYS16_ROM_7751PROG), X(SYS16_ROM_7751DATA), X(SYS16_ROM_UPD7759DATA), X(SYS16_ROM_PROG2),
	X(SYS16_ROM_ROAD), X(SYS16_ROM_PCMDATA), X(SYS16_ROM_Z80PROG2), X(SYS16_ROM_Z80PROG3), X(SYS16_ROM_Z80PROG4),
	X(SYS16_ROM_PCM2DATA), X(SYS16_ROM_PROM), X(SYS16_ROM_PROG3), X(SYS16_ROM_SPRITES2), X(SYS16_ROM_RF5C68DATA),
	X(SYS16_ROM_I8751), X(SYS16_ROM_MSM6295), X(SYS16_ROM_TILES_20000),
	X(TAITO_68KROM1), X(TAITO_68KROM1_BYTESWAP), X(TAITO_68KROM1_BYTESWAP_JUMPING), X(TAITO_68KROM1_BYTESWAP32),
	X(TAITO_68KROM2), X(TAITO_68KROM2_BYTESWAP), X(TAITO_68KROM3), X(TAITO_68KROM3_BYTESWAP),
	X(TAITO_Z80ROM1), X(TAITO_Z80ROM2), X(TAITO_CHARS), X(TAITO_CHARS_BYTESWAP), X(TAITO_CHARSB),
	X(TAITO_CHARSB_BYTESWAP), X(TAITO_SPRITESA), X(TAITO_SPRITESA_BYTESWAP), X(TAITO_SPRITESA_BYTESWAP32),
	X(TAITO_SPRITESA_TOPSPEED), X(TAITO_SPRITESA_DBLAXLEU), X(TAITO_SPRITESB), X(TAITO_SPRITESB_BYTESWAP), X(TAITO_SPRITESB_BYTESWAP32),
	X(TAITO_ROAD), X(TAITO_SPRITEMAP), X(TAITO_YM2610A), X(TAITO_YM2610B), X(TAITO_MSM5205),
	X(TAITO_MSM5205_BYTESWAP), X(TAITO_CHARS_PIVOT), X(TAITO_MSM6295), X(TAITO_ES5505), X(TAITO_ES5505_BYTESWAP),
	X(TAITO_DEFAULT_EEPROM), X(TAITO_CHARS_BYTESWAP32), X(TAITO_CCHIP_BIOS), X(TAITO_CCHIP_EEPROM),
};
#undef X

static bool romdata_lookup_macro(const char* pszName, UINT32* pOut)
{
	if (!pszName || !pOut) return false;
	for (UINT32 i = 0; i < (sizeof(RDMacroTable) / sizeof(RDMacroTable[0])); i++) {
		if (0 == strcmp(pszName, RDMacroTable[i].pszName)) {
			*pOut = RDMacroTable[i].nValue;
			return true;
		}
	}
	return false;
}

static INT32 rd_parse_rom_entry(char* pName, char** ppSaved,
								struct RomDataParsed* pp)
{
	if (rd_is_emptyA(pName)) return -1;

	struct BurnRomInfo ri;
	memset(&ri, 0, sizeof(ri));
	ri.nLen = ~0U; ri.nCrc = ~0U; ri.nType = 0;

	char* tok;
	tok = rd_qtoken(NULL, ppSaved);
	if (!tok || !rd_hex(tok, &ri.nLen) || ri.nLen == 0 || ri.nLen == ~0U) return -1;

	tok = rd_qtoken(NULL, ppSaved);
	if (!tok || !rd_hex(tok, &ri.nCrc) || ri.nCrc == ~0U) return -1;

	while ((tok = rd_qtoken(NULL, ppSaved)) != NULL) {
		UINT32 v;
		if (romdata_lookup_macro(tok, &v))		ri.nType |= v;	// symbolic token
		else if (rd_hex(tok, &v) && v != ~0U)	ri.nType |= v;	// numeric token
	}

	if (ri.nType == 0) return -2;								// type must be explicit ("*" inherits instead)

	struct BurnRomInfo* grown = (struct BurnRomInfo*)realloc(
		pp->pRomInfo, (pp->nRomInfoCount + 1) * sizeof(struct BurnRomInfo));
	if (!grown) return -3;
	pp->pRomInfo = grown;

	struct BurnRomInfo* dst = &pp->pRomInfo[pp->nRomInfoCount];
	memset(dst, 0, sizeof(*dst));
	dst->szName = rd_strdup(pName);
	if (!dst->szName) return -3;
	dst->nLen = ri.nLen; dst->nCrc = ri.nCrc; dst->nType = ri.nType;
	pp->nRomInfoCount++;
	return 0;
}

// Full parse.  Returns 0 on success; pp holds header + ROM table on success.
static INT32 rd_parse_text(char* text, struct RomDataParsed* pp)
{
	memset(pp, 0, sizeof(*pp));

	INT32 nIndex = -1;
	bool bHaveZip = false, bHaveDrv = false, bHaveDate = false, bHaveFull = false, bHaveExtra = false;

	char* line;
	char* nextLine = text;

	// Iterate lines (handle \n; \r already tolerated by the token delimiters).
	while ((line = nextLine) != NULL && *line) {
		char* nl = strchr(line, '\n');
		if (nl) { *nl = '\0'; nextLine = nl + 1; }
		else    { nextLine = NULL; }

		char* saved = NULL;
		char* label = rd_qtoken(line, &saved);
		if (!label) continue;
		if ('/' == label[0] && '/' == label[1]) continue;					// comment

		if (_stricmp("ShortName", label) == 0 || _stricmp("ZipName", label) == 0 || _stricmp("RomName", label) == 0) {
			if (bHaveZip) continue;
			char* v = rd_qtoken(NULL, &saved);
			if (rd_is_emptyA(v) || strlen(v) > SHORT_MAX - 1) return -1;
			if (BurnDrvGetIndex(v) >= 0) return -1;							// clashes with a built-in
			strncpy(pp->szShortName, v, sizeof(pp->szShortName) - 1);
			bHaveZip = true; continue;
		}
		if (_stricmp("DrvName", label) == 0 || _stricmp("Parent", label) == 0) {
			if (bHaveDrv) continue;
			char* v = rd_qtoken(NULL, &saved);
			if (rd_is_emptyA(v) || strlen(v) > SHORT_MAX - 1) return -1;
			nIndex = BurnDrvGetIndex(v);
			if (-1 == nIndex) return -1;									// base driver not found
			strncpy(pp->szDrvName, v, sizeof(pp->szDrvName) - 1);
			bHaveDrv = true; continue;
		}
		if (_stricmp("Date", label) == 0 || _stricmp("Release", label) == 0) {
			if (bHaveDate) continue;
			char* v = rd_qtoken(NULL, &saved);
			if (!rd_is_emptyA(v) && strlen(v) < DATE_MAX - 1)
				strncpy(pp->szDate, v, sizeof(pp->szDate) - 1);
			bHaveDate = true; continue;
		}
		if (_stricmp("ExtraRom", label) == 0) {
			if (bHaveExtra) continue;
			char* v = rd_qtoken(NULL, &saved);
			if (!rd_is_emptyA(v) && strlen(v) <= SHORT_MAX - 1) { strncpy(pp->szExtraRom, v, sizeof(pp->szExtraRom) - 1); }
			bHaveExtra = true; continue;
		}
		if (_stricmp("FullName", label) == 0 || _stricmp("Game", label) == 0) {
			if (bHaveFull) continue;
			INT32 nAdd = 0; char* v;
			while ((v = rd_qtoken(NULL, &saved)) != NULL) {
				INT32 nRem = (INT32)sizeof(pp->szFullName) - nAdd - 1;
				if (nRem <= 0) break;
				INT32 w = snprintf(pp->szFullName + nAdd, nRem, "%s ", v);
				if (w <= 0) break;
				nAdd = (INT32)strlen(pp->szFullName);
			}
			if (nAdd == 0) return -1;
			pp->szFullName[nAdd - 1] = '\0';								// strip trailing space
			bHaveFull = true; continue;
		}

		INT32 r;
		if (strcmp(label, "*") == 0)
			r = rd_copy_base_entry(pp);										// 0=copied, 1=skip, <0=error
		else
			r = rd_parse_rom_entry(label, &saved, pp);
		if (r < 0) return r;
	}

	// Required: ZipName, DrvName, FullName, at least one ROM.
	if (!bHaveZip || !bHaveDrv || !bHaveFull || pp->nRomInfoCount == 0) return -1;
	(void)nIndex;
	return 0;
}

// Map nBurnDrvActive to a RomData record, or NULL when the active driver is not
// one of ours.
static struct RomDataDrv* rd_active(void)
{
	if (nBurnDrvActive < nIntlDrvCount || nBurnDrvActive >= nBurnDrvCount) return NULL;
	UINT32 idx = nBurnDrvActive - nIntlDrvCount;
	if (idx >= nRDDrvCount || !pRDDrv[idx]) return NULL;
	return pRDDrv[idx];
}

static INT32 RomDataGetRomInfo(struct BurnRomInfo* pri, UINT32 i)
{
	struct RomDataDrv* rd = rd_active();
	if (!rd || i >= rd->nRomInfoCount) return 1;
	if (pri) {
		pri->nLen  = rd->pRomInfo[i].nLen;
		pri->nCrc  = rd->pRomInfo[i].nCrc;
		pri->nType = rd->pRomInfo[i].nType;
	}
	return 0;
}

static INT32 RomDataGetRomName(char** pszName, UINT32 i, INT32 nAka)
{
	struct RomDataDrv* rd = rd_active();
	if (!rd || i >= rd->nRomInfoCount) return 1;
	if (nAka) return 1;														// RomData has no alternate names
	if (pszName) *pszName = rd->pRomInfo[i].szName;
	return 0;
}

//  Build + link a RomData driver from parsed data.
static void rd_free_record(struct RomDataDrv* rec)
{
	if (!rec) return;
	if (rec->pRomInfo) {
		for (UINT32 i = 0; i < rec->nRomInfoCount; i++)
			free_s((void**)&rec->pRomInfo[i].szName);
		free_s((void**)&rec->pRomInfo);
	}
	free_s((void**)&rec->pszShortName);
	free_s((void**)&rec->pszDrvName);
	free_s((void**)&rec->pszDate);
	free_s((void**)&rec->pszFullNameA);
	free_s((void**)&rec->pszFullNameW);
	free_s((void**)&rec->pszExtName);
	free(rec);
}

static wchar_t* rd_utf8_to_wide(const char* s)
{
	if (!s) return NULL;
#ifdef BUILD_WIN32
	INT32 wn = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	INT32 cp = CP_UTF8;
	if (wn <= 0) {															// not valid UTF-8: system code page
		cp = CP_ACP;
		wn = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
		if (wn <= 0) return NULL;
	}
	wchar_t* w = (wchar_t*)malloc(((size_t)wn + 1) * sizeof(wchar_t));
	if (!w) return NULL;
	MultiByteToWideChar(cp, 0, s, -1, w, wn);
	w[wn] = 0;
	return w;
#else
	size_t n = strlen(s);
	wchar_t* w = (wchar_t*)malloc((n + 2) * sizeof(wchar_t));
	if (!w) return NULL;
	const UINT8* p = (const UINT8*)s;
	size_t o = 0;
	while (*p) {
		UINT32 cp;
		if (p[0] < 0x80) { cp = p[0]; p += 1; }
		else if ((p[0] & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
		else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) { cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
		else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4; }
		else { cp = p[0]; p += 1; }
		w[o++] = (wchar_t)cp;
	}
	w[o + 0] = 0;
	w[o + 1] = 0;
	return w;
#endif
}

// Returns new driver index (>= 0) on success, negative on failure.
static INT32 rd_build_and_link(struct RomDataParsed* pp)
{
	INT32 nBaseIdx = BurnDrvGetIndex(pp->szDrvName);
	if (nBaseIdx < 0) {
		bprintf(PRINT_ERROR, _T("RomData: base driver '%hs' not found\n"), pp->szDrvName);
		return -1;
	}
	struct BurnDriver* base = BurnGetDriver(pp->szDrvName);
	if (!base) {
		bprintf(PRINT_ERROR, _T("RomData: failed to look up base driver '%hs'\n"), pp->szDrvName);
		return -1;
	}

	struct RomDataDrv* rec = (struct RomDataDrv*)calloc(1, sizeof(struct RomDataDrv));
	if (!rec) {
		bprintf(PRINT_ERROR, _T("RomData: out of memory allocating record for '%hs'\n"), pp->szShortName);
		return -1;
	}

	// Shallow-copy the base driver: inherit every field / function pointer.
	memcpy(&rec->drv, base, sizeof(struct BurnDriver));

	// Take ownership of the parsed ROM table (moved, not copied).
	rec->pRomInfo      = pp->pRomInfo;
	rec->nRomInfoCount = pp->nRomInfoCount;
	pp->pRomInfo       = NULL;						// ownership transferred
	pp->nRomInfoCount  = 0;

	// Overridden identity strings.
	rec->pszShortName = rd_strdup(pp->szShortName);
	rec->pszDrvName   = rd_strdup(pp->szDrvName);
	rec->pszDate      = !rd_is_emptyA(pp->szDate) ? rd_strdup(pp->szDate) : NULL;
	rec->pszFullNameA = rd_strdup(pp->szFullName);
#ifdef _UNICODE
	rec->pszFullNameW = rd_utf8_to_wide(pp->szFullName);
#else
	rec->pszFullNameW = NULL;
#endif
	if (!rd_is_emptyA(pp->szExtraRom)) rec->pszExtName = rd_strdup(pp->szExtraRom);

	if (!rec->pszShortName || !rec->pszDrvName || !rec->pszFullNameA
#ifdef _UNICODE
		|| !rec->pszFullNameW
#endif
	) {
		bprintf(PRINT_ERROR, _T("RomData: out of memory duplicating strings for '%hs'\n"), pp->szShortName);
		rd_free_record(rec);
		return -1;
	}

	rec->drv.szShortName = rec->pszShortName;
	rec->drv.szParent    = base->szParent ? base->szParent : rec->pszDrvName;
	rec->drv.szDate      = rec->pszDate   ? rec->pszDate   : base->szDate;
	rec->drv.szFullNameA = rec->pszFullNameA;
	rec->drv.szFullNameW = rec->pszFullNameW;
	rec->drv.GetRomInfo  = RomDataGetRomInfo;
	rec->drv.GetRomName  = RomDataGetRomName;
	rec->drv.Flags      |= BDF_ROMDATA_DRIVER | BDF_CLONE;

	// Register the record, then link the driver into the engine list.
	struct RomDataDrv** growRec = (struct RomDataDrv**)realloc(
		pRDDrv, (nRDDrvCount + 1) * sizeof(struct RomDataDrv*));
	if (!growRec) {
		bprintf(PRINT_ERROR, _T("RomData: out of memory growing driver array\n"));
		rd_free_record(rec);
		return -1;
	}
	pRDDrv = growRec;

	if (~0U == LinkExtlDrivers(&rec->drv, &nBurnDrvCount)) {
		bprintf(PRINT_ERROR, _T("RomData: failed to link driver '%hs'\n"), pp->szShortName);
		rd_free_record(rec);
		return -1;
	}
	pRDDrv[nRDDrvCount] = rec;
	nRDDrvCount++;

	bprintf(PRINT_NORMAL, _T("RomData: loaded driver '%hs' (based on '%hs')\n"),
			rec->pszShortName, rec->pszDrvName);
	return (INT32)(nBurnDrvCount - 1);				// index of the just-added driver
}

// =============================================================================
//  Public API
// =============================================================================

extern "C" INT32 RomDataLoadOne(const TCHAR* szDatPath)
{
	if (rd_is_empty(szDatPath)) return -1;

	// Only accept a .dat extension (case-insensitive).
	const TCHAR* dot = _tcsrchr(szDatPath, _T('.'));
	if (!dot || (_tcsicmp(dot, _T(".dat")) != 0)) return -1;

	char* text = rd_load_text(szDatPath);
	if (!text) {
		bprintf(PRINT_ERROR, _T("RomData: failed to read/convert '%s'\n"), szDatPath);
		return -1;
	}

	struct RomDataParsed parsed;
	INT32 r = rd_parse_text(text, &parsed);
	free(text);
	if (r != 0) {
		bprintf(PRINT_ERROR, _T("RomData: failed to parse '%s' (error %d)\n"), szDatPath, r);
		rd_parsed_free(&parsed);
		return -1;
	}

	// Idempotency: skip a romset whose ShortName is already present.
	if (BurnDrvGetIndex(parsed.szShortName) >= 0) {
		bprintf(PRINT_IMPORTANT, _T("RomData: skipping '%hs' (driver already exists)\n"), parsed.szShortName);
		rd_parsed_free(&parsed);
		return -1;
	}

	INT32 idx = rd_build_and_link(&parsed);
	rd_parsed_free(&parsed);						// frees anything not moved into the record
	return idx;
}

static void rd_scan_dir(const TCHAR* szDir, bool bScanSub, int depth)
{
	if (!szDir || depth > 4) return;
	RD_DIR* dp = rd_opendir(szDir);
	if (!dp) return;

	struct RD_dirent* de;
	TCHAR path[MAX_PATH];
	while ((de = rd_readdir(dp)) != NULL) {
		char dname[256];
		rd_dname(de, dname, sizeof(dname));
		if (strcmp(dname, ".") == 0 || strcmp(dname, "..") == 0) continue;

		_sntprintf(path, MAX_PATH, _T("%s/") _T_NARROW_FMT, szDir, dname);

		RD_DIR* sub = rd_opendir(path);				// portable "is it a directory?"
		if (sub) {
			rd_closedir(sub);
			if (bScanSub) rd_scan_dir(path, bScanSub, depth + 1);
			continue;
		}

		RomDataLoadOne(path);						// .dat check (case-insensitive) is inside
	}
	rd_closedir(dp);
}

extern "C" void RomDataScan(const TCHAR* szDir, bool bScanSub)
{
	if (rd_is_empty(szDir)) return;
	rd_scan_dir(szDir, bScanSub, 0);
}

extern "C" void RomDataFree(void)
{
	if (!pRDDrv || nRDDrvCount == 0) return;

	nBurnDrvCount = nIntlDrvCount;

	for (UINT32 i = 0; i < nRDDrvCount; i++)
		rd_free_record(pRDDrv[i]);
	free_s((void**)&pRDDrv);
	nRDDrvCount = 0;
}

extern "C" bool IsRomDataDrv(void)
{
	return (nBurnDrvActive >= nIntlDrvCount && nBurnDrvActive < nBurnDrvCount && (BurnDrvGetFlags() & BDF_ROMDATA_DRIVER));
}

extern "C" char* RomDataDrvGetDrvName(void)
{
	struct RomDataDrv* rd = rd_active();
	return rd ? rd->pszDrvName : NULL;
}

extern "C" struct BurnRomInfo* RomDataDrvGetRomInfo(UINT32* pRomCount)
{
	struct RomDataDrv* rd = rd_active();
	if (!rd) return NULL;
	if (pRomCount) *pRomCount = rd->nRomInfoCount;
	return rd->pRomInfo;
}

extern "C" const char* RomDataDrvGetExtName(void)
{
	struct RomDataDrv* rd = rd_active();
	return rd ? rd->pszExtName : NULL;
}

#ifndef _UNICODE
#undef rd_dname
#endif
#undef SHORT_MAX
#undef DATE_MAX
#undef RD_DIR
#undef RD_dirent
#undef rd_opendir
#undef rd_readdir
#undef rd_closedir
#undef _T_NARROW_FMT
