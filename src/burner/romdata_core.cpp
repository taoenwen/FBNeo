// =============================================================================
//  FBNeo  -  RomData core (platform-independent)
// =============================================================================

#include "burnint.h"
#include "romdata_core.h"
#include "m68000_intf.h"	// SekMapMemory (Neo Geo ExtraRom mapping)
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

// TCHAR-aware dir ops (no _t-prefix macros exist for opendir/readdir/closedir,
// so we provide our own -- same pattern as msu1_backend.cpp).
#ifdef _UNICODE
#define RD_DIR       _WDIR
#define RD_dirent    _wdirent
#define rd_opendir   _wopendir
#define rd_readdir   _wreaddir
#define rd_closedir  _wclosedir
static void rd_dname(const struct _wdirent* de, char* out, int outSize)
{
	int i;
	for (i = 0; i < outSize - 1 && de->d_name[i]; i++)
		out[i] = (char)de->d_name[i];
	out[i] = 0;
}
#define _T_NARROW_FMT  _T("%hs")   // narrow string in wide printf (MSVC only)
#else
#define RD_DIR       DIR
#define RD_dirent    dirent
#define rd_opendir   opendir
#define rd_readdir   readdir
#define rd_closedir  closedir
#define rd_dname(de, out, outSize)  snprintf(out, outSize, "%s", (de)->d_name)
#define _T_NARROW_FMT  _T("%s")
#endif

// From burn.cpp (driver list plumbing established by the pDriverEx foundation).
extern "C" UINT32 LinkExtlDrivers(struct BurnDriver* drv, UINT32* pallCount);
extern "C" struct BurnDriver* BurnGetDriver(const char* szName);

//  Lightweight RomData driver record.
struct RomDataDrv {
	struct BurnDriver   drv;			// embedded engine driver (appended to pDriverEx)
	struct BurnRomInfo* pRomInfo;		// the ONE ROM table (final form, owns szName)
	UINT32              nRomInfoCount;
	char*               pszZipName;		// overridden short name (romset filename)
	char*               pszParent;		// overridden parent short name
	char*               pszFullNameA;	// overridden full name (ANSI)
	wchar_t*            pszFullNameW;	// overridden full name (wide, UNICODE builds)
	char*               pszExtraRom;	// optional Neo Geo ExtraRom name
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

static inline bool rd_is_empty(const char* s) { return (!s || '\0' == *s); }

//  Encoding detection + normalisation to UTF-8/char
enum RDEncoding {
	RD_ENC_ANSI = 0,	// unknown 8-bit / ASCII-compatible; passed through verbatim
	RD_ENC_UTF8,		// UTF-8 without BOM (ASCII is a subset)
	RD_ENC_UTF8_BOM,
	RD_ENC_UTF16_LE,
	RD_ENC_UTF16_BE,
};

static bool rd_is_valid_utf8(const unsigned char* buf, size_t len)
{
	size_t p = 0;
	while (p < len) {
		unsigned char c = buf[p];
		if (c < 0x80) { p++; continue; }			// ASCII

		int nExtra;
		if      (c >= 0xC2 && c <= 0xDF) nExtra = 1;	// 2-byte
		else if (c >= 0xE0 && c <= 0xEF) nExtra = 2;	// 3-byte
		else if (c >= 0xF0 && c <= 0xF4) nExtra = 3;	// 4-byte
		else return false;							// invalid lead byte -> not UTF-8

		// Each of the nExtra continuation bytes must exist and be 10xxxxxx.
		for (int i = 1; i <= nExtra; i++) {
			if (p + (size_t)i >= len) return false;	// truncated sequence at EOF
			if (0x80 != (buf[p + i] & 0xC0)) return false;
		}
		p += nExtra + 1;
	}
	return true;
}

static unsigned char* rd_read_all(FILE* fp, size_t* pLen)
{
	*pLen = 0;
	if (fseek(fp, 0, SEEK_END) != 0) return NULL;
	long sz = ftell(fp);
	if (sz < 0) return NULL;
	rewind(fp);
	unsigned char* buf = (unsigned char*)malloc((size_t)sz + 1);
	if (!buf) return NULL;
	size_t got = fread(buf, 1, (size_t)sz, fp);
	buf[got] = 0;
	*pLen = got;
	return buf;
}

static RDEncoding rd_detect_encoding(const unsigned char* buf, size_t len)
{
	// BOM first (Notepad++ determineEncodingFromBOM order).
	if (len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) return RD_ENC_UTF8_BOM;
	if (len >= 2 && buf[0] == 0xFF && buf[1] == 0xFE)                    return RD_ENC_UTF16_LE;
	if (len >= 2 && buf[0] == 0xFE && buf[1] == 0xFF)                    return RD_ENC_UTF16_BE;
	if (len == 0) return RD_ENC_ANSI;

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
	unsigned char* raw = rd_read_all(fp, &len);
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
		memmove(raw, raw + 3, len - 3 + 1);			// drop BOM
		return (char*)raw;
	}

	// UTF-16 LE/BE -> UTF-8.  A .dat only needs BMP; encode up to 3-byte UTF-8.
	size_t start = 2;								// skip BOM
	size_t units = (len - start) / 2;
	// worst case 3 bytes per unit + NUL
	char* out = (char*)malloc(units * 3 + 1);
	if (!out) { free(raw); return NULL; }
	size_t o = 0;
	for (size_t i = 0; i < units; i++) {
		unsigned int cp;
		unsigned char b0 = raw[start + i * 2 + 0];
		unsigned char b1 = raw[start + i * 2 + 1];
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
	if ('\0' == *p) { *ppSaved = NULL; return NULL; }

	char* token = p;
	if ('"' == *p) {								// quoted field
		token = ++p;
		char* q = strchr(p, '"');
		if (q) { *q = '\0'; p = q + 1; }
		else   { p = strchr(p, '\0'); }
	} else {
		p = strpbrk(p, RD_DELIMS);
	}

	if (p && '\0' != *p) { *p = '\0'; *ppSaved = p + 1; }
	else                 { *ppSaved = NULL; }
	return token;
}

// Parse a hex string to UINT32 with strict validation.
static bool rd_hex(const char* s, UINT32* out)
{
	if (rd_is_empty(s) || !out) return false;
	char* end = NULL;
	unsigned long v = strtoul(s, &end, 16);
	if (s == end || '\0' != *end) return false;
	*out = (UINT32)v;
	return true;
}

//  Pure parse layer:  .dat text  ->  header fields + BurnRomInfo[]
struct RomDataParsed {
	char   szZipName[128];
	char   szDrvName[128];
	char   szExtraRom[128];
	char   szFullName[MAX_PATH];
	INT32  nDatMode;								// 0 = FBNeo style, 1 = Nebula style
	struct BurnRomInfo* pRomInfo;					// malloc'd table (owns each szName)
	UINT32 nRomInfoCount;
};

static void rd_parsed_free(struct RomDataParsed* pp)
{
	if (!pp) return;
	if (pp->pRomInfo) {
		for (UINT32 i = 0; i < pp->nRomInfoCount; i++)
			free(pp->pRomInfo[i].szName);
		free(pp->pRomInfo);
		pp->pRomInfo = NULL;
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

static INT32 rd_parse_rom_entry(char* pName, char** ppSaved, INT32 nDatMode,
								struct RomDataParsed* pp)
{
	if (rd_is_empty(pName)) return -1;

	struct BurnRomInfo ri;
	memset(&ri, 0, sizeof(ri));
	ri.nLen = ~0U; ri.nCrc = ~0U; ri.nType = 0;

	char* tok;
	if (1 == nDatMode)
		rd_qtoken(NULL, ppSaved);					// Nebula: skip first field

	tok = rd_qtoken(NULL, ppSaved);
	if (!tok || !rd_hex(tok, &ri.nLen) || 0 == ri.nLen || ~0U == ri.nLen) return -1;

	tok = rd_qtoken(NULL, ppSaved);
	if (!tok || !rd_hex(tok, &ri.nCrc) || ~0U == ri.nCrc) return -1;

	while ((tok = rd_qtoken(NULL, ppSaved)) != NULL) {
		UINT32 v;
		if (RomDataLookupMacro(tok, &v))            ri.nType |= v;	// symbolic token
		else if (rd_hex(tok, &v) && v != ~0U)       ri.nType |= v;	// numeric token
	}

	if (0 == ri.nType) return -2;					// type must be explicit ("*" inherits instead)

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
	bool bHaveZip = false, bHaveDrv = false, bHaveFull = false;

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

		size_t nLen = strlen(label);

		// [Section] header (Nebula grouping) -> skip.
		if (nLen > 2 && '[' == label[0] && ']' == label[nLen - 1]) {
			label[nLen - 1] = '\0'; label++;
			if (0 == strcmp("System", label)) break;						// end of entries
			continue;
		}
		if (0 == strcmp("System", label)) { pp->nDatMode = 1; continue; }	// Nebula marker

		if (0 == strcmp("ShortName", label) || 0 == strcmp("ZipName", label) || 0 == strcmp("RomName", label)) {
			if (bHaveZip) continue;
			char* v = rd_qtoken(NULL, &saved);
			if (rd_is_empty(v) || strlen(v) > 99) return -1;
			if (BurnDrvGetIndex(v) >= 0) return -1;							// clashes with a built-in
			strncpy(pp->szZipName, v, sizeof(pp->szZipName) - 1);
			bHaveZip = true; continue;
		}
		if (0 == strcmp("DrvName", label) || 0 == strcmp("Parent", label)) {
			if (bHaveDrv) continue;
			char* v = rd_qtoken(NULL, &saved);
			if (rd_is_empty(v) || strlen(v) > 99) return -1;
			nIndex = BurnDrvGetIndex(v);
			if (-1 == nIndex) return -1;									// base driver not found
			strncpy(pp->szDrvName, v, sizeof(pp->szDrvName) - 1);
			bHaveDrv = true; continue;
		}
		if (0 == strcmp("Date", label) || 0 == strcmp("Release", label)) {
			continue;														// date currently unused
		}
		if (0 == strcmp("ExtraRom", label)) {
			char* v = rd_qtoken(NULL, &saved);
			if (!rd_is_empty(v) && strlen(v) <= 99) { strncpy(pp->szExtraRom, v, sizeof(pp->szExtraRom) - 1); }
			continue;
		}
		if (0 == strcmp("FullName", label) || 0 == strcmp("Game", label)) {
			if (bHaveFull) continue;
			INT32 nAdd = 0; char* v;
			while ((v = rd_qtoken(NULL, &saved)) != NULL) {
				INT32 nRem = (INT32)sizeof(pp->szFullName) - nAdd - 1;
				if (nRem <= 0) break;
				INT32 w = snprintf(pp->szFullName + nAdd, nRem, "%s ", v);
				if (w <= 0) break;
				nAdd = (INT32)strlen(pp->szFullName);
			}
			if (0 == nAdd) return -1;
			pp->szFullName[nAdd - 1] = '\0';								// strip trailing space
			bHaveFull = true; continue;
		}

		INT32 r;
		if (0 == strcmp(label, "*"))
			r = rd_copy_base_entry(pp);										// 0=copied, 1=skip, <0=error
		else
			r = rd_parse_rom_entry(label, &saved, pp->nDatMode, pp);
		if (r < 0) return r;
	}

	// Required: ZipName, DrvName, FullName, at least one ROM.
	if (!bHaveZip || !bHaveDrv || !bHaveFull || 0 == pp->nRomInfoCount) return -1;
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
	if (nAka) return 1;								// RomData has no alternate names
	if (pszName) *pszName = rd->pRomInfo[i].szName;
	return 0;
}

// =============================================================================
//  Build + link a RomData driver from parsed data.
// =============================================================================
static void rd_free_record(struct RomDataDrv* rec)
{
	if (!rec) return;
	if (rec->pRomInfo) {
		for (UINT32 i = 0; i < rec->nRomInfoCount; i++)
			free(rec->pRomInfo[i].szName);
		free(rec->pRomInfo);
	}
	free(rec->pszZipName);
	free(rec->pszParent);
	free(rec->pszFullNameA);
	free(rec->pszFullNameW);
	free(rec->pszExtraRom);
	free(rec);
}

static wchar_t* rd_utf8_to_wide(const char* s)
{
	if (!s) return NULL;
#ifdef BUILD_WIN32
	INT32 wn = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	INT32 cp = CP_UTF8;
	if (wn <= 0) {												// not valid UTF-8: system code page
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
	const unsigned char* p = (const unsigned char*)s;
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
	if (nBaseIdx < 0) return -1;
	struct BurnDriver* base = BurnGetDriver(pp->szDrvName);
	if (!base) return -1;

	struct RomDataDrv* rec = (struct RomDataDrv*)calloc(1, sizeof(struct RomDataDrv));
	if (!rec) return -1;

	// Shallow-copy the base driver: inherit every field / function pointer.
	memcpy(&rec->drv, base, sizeof(struct BurnDriver));

	// Take ownership of the parsed ROM table (moved, not copied).
	rec->pRomInfo      = pp->pRomInfo;
	rec->nRomInfoCount = pp->nRomInfoCount;
	pp->pRomInfo       = NULL;						// ownership transferred
	pp->nRomInfoCount  = 0;

	// Overridden identity strings.
	rec->pszZipName = rd_strdup(pp->szZipName);
	// Parent: if the base is a clone keep its parent, else the base itself.
	{
		const char* parent = base->szParent;
		bool baseIsClone = (base->Flags & BDF_CLONE) != 0;
		rec->pszParent = rd_strdup((parent && baseIsClone) ? parent : pp->szDrvName);
	}
	rec->pszFullNameA = rd_strdup(pp->szFullName);
	rec->pszFullNameW = rd_utf8_to_wide(pp->szFullName);
	if (!rd_is_empty(pp->szExtraRom)) rec->pszExtraRom = rd_strdup(pp->szExtraRom);

	if (!rec->pszZipName || !rec->pszParent || !rec->pszFullNameA || !rec->pszFullNameW) {
		rd_free_record(rec);
		return -1;
	}

	// Point the driver at our overridden fields + ROM accessors.
	rec->drv.szShortName = rec->pszZipName;
	rec->drv.szParent    = rec->pszParent;
	rec->drv.szFullNameA = rec->pszFullNameA;
	rec->drv.szFullNameW = rec->pszFullNameW;
	rec->drv.GetRomInfo  = RomDataGetRomInfo;		// <-- the whole point: per-driver ROM table
	rec->drv.GetRomName  = RomDataGetRomName;
	rec->drv.GetZipName  = NULL;					// fall back to generic zip-name logic
	rec->drv.Flags      |= BDF_ROMDATA_DRIVER;
	if (!(base->Flags & BDF_CLONE))
		rec->drv.Flags  |= BDF_CLONE;				// RomData sets are treated as clones

	// Register the record, then link the driver into the engine list.
	struct RomDataDrv** growRec = (struct RomDataDrv**)realloc(
		pRDDrv, (nRDDrvCount + 1) * sizeof(struct RomDataDrv*));
	if (!growRec) { rd_free_record(rec); return -1; }
	pRDDrv = growRec;

	if (~0U == LinkExtlDrivers(&rec->drv, &nBurnDrvCount)) {
		rd_free_record(rec);
		return -1;
	}
	pRDDrv[nRDDrvCount] = rec;
	nRDDrvCount++;

	return (INT32)(nBurnDrvCount - 1);				// index of the just-added driver
}

// =============================================================================
//  Public API
// =============================================================================

extern "C" INT32 RomDataLoadOne(const TCHAR* szDatPath)
{
	if (!szDatPath || _T('\0') == *szDatPath) return -1;

	// Only accept a .dat extension (case-insensitive).
	const TCHAR* dot = _tcsrchr(szDatPath, _T('.'));
	if (!dot || (0 != _tcsicmp(dot, _T(".dat")))) return -1;

	char* text = rd_load_text(szDatPath);
	if (!text) return -1;

	struct RomDataParsed parsed;
	INT32 r = rd_parse_text(text, &parsed);
	free(text);
	if (0 != r) { rd_parsed_free(&parsed); return -1; }

	// Idempotency: skip a romset whose ZipName is already present.
	if (BurnDrvGetIndex(parsed.szZipName) >= 0) { rd_parsed_free(&parsed); return -1; }

	INT32 idx = rd_build_and_link(&parsed);
	rd_parsed_free(&parsed);						// frees anything not moved into the record
	return idx;
}

static void rd_scan_dir(const TCHAR* szDir, bool bScanSub, int depth)
{
	if (!szDir || depth > 8) return;
	RD_DIR* dp = rd_opendir(szDir);
	if (!dp) return;

	struct RD_dirent* de;
	TCHAR path[MAX_PATH];
	while ((de = rd_readdir(dp)) != NULL) {
		char dname[256];
		rd_dname(de, dname, sizeof(dname));
		if (0 == strcmp(dname, ".") || 0 == strcmp(dname, "..")) continue;

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
	if (!szDir || _T('\0') == *szDir) return;
	rd_scan_dir(szDir, bScanSub, 0);
}

extern "C" void RomDataFree(void)
{
	if (!pRDDrv || 0 == nRDDrvCount) return;

	// Detach RomData drivers from the engine list before freeing them, so no
	// dangling pointer is ever left behind in pDriverEx.  Our drivers are the
	// tail [nIntlDrvCount, nBurnDrvCount); drop them by resetting the count.
	nBurnDrvCount = nIntlDrvCount;

	for (UINT32 i = 0; i < nRDDrvCount; i++)
		rd_free_record(pRDDrv[i]);
	free(pRDDrv);
	pRDDrv = NULL;
	nRDDrvCount = 0;
}

extern "C" bool IsRomDataDrv(void)
{
	return (nBurnDrvActive >= nIntlDrvCount && nBurnDrvActive < nBurnDrvCount
			&& (BurnDrvGetFlags() & BDF_ROMDATA_DRIVER));
}

extern "C" char* RomDataDrvGetDrvName(void)
{
	struct RomDataDrv* rd = rd_active();
	// The base driver's short name is the parent we cloned from.
	return rd ? rd->pszParent : NULL;
}

extern "C" struct BurnRomInfo* RomDataDrvGetRomInfo(UINT32* pRomCount)
{
	struct RomDataDrv* rd = rd_active();
	if (!rd) return NULL;
	if (pRomCount) *pRomCount = rd->nRomInfoCount;
	return rd->pRomInfo;
}

extern "C" void NeoProcessExtraRom(UINT8* rom)
{
	if (!IsRomDataDrv()) return;
	struct RomDataDrv* rd = rd_active();
	if (!rd || rd_is_empty(rd->pszExtraRom)) return;

	UINT32 romLen = 0, exromLen = 0;
	for (UINT32 i = 0; i < rd->nRomInfoCount; i++) {
		const struct BurnRomInfo* ri = &rd->pRomInfo[i];
		if (0 == strcmp(ri->szName ? ri->szName : "", rd->pszExtraRom)) exromLen = ri->nLen;
		if (1 == (ri->nType & 7)) romLen += ri->nLen;	// P-ROMs
	}
	if (0 == exromLen || romLen <= exromLen) return;

	SekMapMemory(rom + (romLen - exromLen), 0x900000, 0x900000 + (exromLen - 1), MAP_ROM);
}
