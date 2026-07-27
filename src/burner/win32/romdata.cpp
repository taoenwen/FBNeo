#include "burner.h"

// d_cps1.cpp
#define CPS1_68K_PROGRAM_BYTESWAP			1
#define CPS1_68K_PROGRAM_NO_BYTESWAP		2
#define CPS1_Z80_PROGRAM					3
#define CPS1_TILES							4
#define CPS1_OKIM6295_SAMPLES				5
#define CPS1_QSOUND_SAMPLES					6
#define CPS1_PIC							7
#define CPS1_EXTRA_TILES_SF2EBBL_400000		8
#define CPS1_EXTRA_TILES_400000				9
#define CPS1_EXTRA_TILES_SF2KORYU_400000	10
#define CPS1_EXTRA_TILES_SF2B_400000		11
#define CPS1_EXTRA_TILES_SF2MKOT_400000		12

// d_cps2.cpp
#define CPS2_PRG_68K						1
#define CPS2_PRG_68K_SIMM					2
#define CPS2_PRG_68K_XOR_TABLE				3
#define CPS2_GFX							5
#define CPS2_GFX_SIMM						6
#define CPS2_GFX_SPLIT4						7
#define CPS2_GFX_SPLIT8						8
#define CPS2_GFX_19XXJ						9
#define CPS2_PRG_Z80						10
#define CPS2_QSND							12
#define CPS2_QSND_SIMM						13
#define CPS2_QSND_SIMM_BYTESWAP				14
#define CPS2_ENCRYPTION_KEY					15

// gal.h
#define GAL_ROM_Z80_PROG1				1
#define GAL_ROM_Z80_PROG2				2
#define GAL_ROM_Z80_PROG3				3
#define GAL_ROM_TILES_SHARED			4
#define GAL_ROM_TILES_CHARS				5
#define GAL_ROM_TILES_SPRITES			6
#define GAL_ROM_PROM					7
#define GAL_ROM_S2650_PROG1				8

// megadrive.h
#define SEGA_MD_ROM_LOAD_NORMAL								0x10
#define SEGA_MD_ROM_LOAD16_WORD_SWAP						0x20
#define SEGA_MD_ROM_LOAD16_BYTE								0x30
#define SEGA_MD_ROM_LOAD16_WORD_SWAP_CONTINUE_040000_100000	0x40
#define SEGA_MD_ROM_LOAD_NORMAL_CONTINUE_020000_080000		0x50
#define SEGA_MD_ROM_OFFS_000000								0x01
#define SEGA_MD_ROM_OFFS_000001								0x02
#define SEGA_MD_ROM_OFFS_020000								0x03
#define SEGA_MD_ROM_OFFS_080000								0x04
#define SEGA_MD_ROM_OFFS_100000								0x05
#define SEGA_MD_ROM_OFFS_100001								0x06
#define SEGA_MD_ROM_OFFS_200000								0x07
#define SEGA_MD_ROM_OFFS_300000								0x08
#define SEGA_MD_ROM_RELOAD_200000_200000					0x09
#define SEGA_MD_ROM_RELOAD_100000_300000					0x0a
#define SEGA_MD_ARCADE_SUNMIXING							(0x4000)

// sys16.h
#define SYS16_ROM_PROG_FLAT									25
#define SYS16_ROM_PROG										1
#define SYS16_ROM_TILES										2
#define SYS16_ROM_SPRITES									3
#define SYS16_ROM_Z80PROG									4
#define SYS16_ROM_KEY										5
#define SYS16_ROM_7751PROG									6
#define SYS16_ROM_7751DATA									7
#define SYS16_ROM_UPD7759DATA								8
#define SYS16_ROM_PROG2										9
#define SYS16_ROM_ROAD										10
#define SYS16_ROM_PCMDATA									11
#define SYS16_ROM_Z80PROG2									12
#define SYS16_ROM_Z80PROG3									13
#define SYS16_ROM_Z80PROG4									14
#define SYS16_ROM_PCM2DATA									15
#define SYS16_ROM_PROM 										16
#define SYS16_ROM_PROG3										17
#define SYS16_ROM_SPRITES2									18
#define SYS16_ROM_RF5C68DATA								19
#define SYS16_ROM_I8751										20
#define SYS16_ROM_MSM6295									21
#define SYS16_ROM_TILES_20000								22

// taito.h
#define TAITO_68KROM1										1
#define TAITO_68KROM1_BYTESWAP								2
#define TAITO_68KROM1_BYTESWAP_JUMPING						3
#define TAITO_68KROM1_BYTESWAP32							4
#define TAITO_68KROM2										5
#define TAITO_68KROM2_BYTESWAP								6
#define TAITO_68KROM3										7
#define TAITO_68KROM3_BYTESWAP								8
#define TAITO_Z80ROM1										9
#define TAITO_Z80ROM2										10
#define TAITO_CHARS											11
#define TAITO_CHARS_BYTESWAP								12
#define TAITO_CHARSB										13
#define TAITO_CHARSB_BYTESWAP								14
#define TAITO_SPRITESA										15
#define TAITO_SPRITESA_BYTESWAP								16
#define TAITO_SPRITESA_BYTESWAP32							17
#define TAITO_SPRITESA_TOPSPEED								18
#define TAITO_SPRITESB										19
#define TAITO_SPRITESB_BYTESWAP								20
#define TAITO_SPRITESB_BYTESWAP32							21
#define TAITO_ROAD											22
#define TAITO_SPRITEMAP										23
#define TAITO_YM2610A										24
#define TAITO_YM2610B										25
#define TAITO_MSM5205										26
#define TAITO_MSM5205_BYTESWAP								27
#define TAITO_CHARS_PIVOT									28
#define TAITO_MSM6295										29
#define TAITO_ES5505										30
#define TAITO_ES5505_BYTESWAP								31
#define TAITO_DEFAULT_EEPROM								32
#define TAITO_CHARS_BYTESWAP32								33
#define TAITO_CCHIP_BIOS									34
#define TAITO_CCHIP_EEPROM									35

#ifndef ERANGE
  #define ERANGE 34
#endif

struct RDMacroMap {
	const TCHAR* pszDRMacro;
	const UINT32 nRDMacro;
};

struct BurnRomInfo* pDataRomDesc = NULL;

TCHAR szRomdataName[MAX_PATH] = _T("");
bool  bRDListScanSub          = false;

static struct BurnRomInfo* pDRD = NULL;
static struct RomDataInfo RDI = { 0 };

RomDataInfo*  pRDI = &RDI;

#define X(a) { _T(#a), a }
static const RDMacroMap RDMacroTable[] = {
	X(BRF_PRG),
	X(BRF_GRA),
	X(BRF_SND),
	X(BRF_ESS),
	X(BRF_BIOS),
	X(BRF_SELECT),
	X(BRF_OPT),
	X(BRF_NODUMP),
	X(CPS1_68K_PROGRAM_BYTESWAP),
	X(CPS1_68K_PROGRAM_NO_BYTESWAP),
	X(CPS1_Z80_PROGRAM),
	X(CPS1_TILES),
	X(CPS1_OKIM6295_SAMPLES),
	X(CPS1_QSOUND_SAMPLES),
	X(CPS1_PIC),
	X(CPS1_EXTRA_TILES_SF2EBBL_400000),
	X(CPS1_EXTRA_TILES_400000),
	X(CPS1_EXTRA_TILES_SF2KORYU_400000),
	X(CPS1_EXTRA_TILES_SF2B_400000),
	X(CPS1_EXTRA_TILES_SF2MKOT_400000),
	X(CPS2_PRG_68K),
	X(CPS2_PRG_68K_SIMM),
	X(CPS2_PRG_68K_XOR_TABLE),
	X(CPS2_GFX),
	X(CPS2_GFX_SIMM),
	X(CPS2_GFX_SPLIT4),
	X(CPS2_GFX_SPLIT8),
	X(CPS2_GFX_19XXJ),
	X(CPS2_PRG_Z80),
	X(CPS2_QSND),
	X(CPS2_QSND_SIMM),
	X(CPS2_QSND_SIMM_BYTESWAP),
	X(CPS2_ENCRYPTION_KEY),
	X(GAL_ROM_Z80_PROG1),
	X(GAL_ROM_Z80_PROG2),
	X(GAL_ROM_Z80_PROG3),
	X(GAL_ROM_TILES_SHARED),
	X(GAL_ROM_TILES_CHARS),
	X(GAL_ROM_TILES_SPRITES),
	X(GAL_ROM_PROM),
	X(GAL_ROM_S2650_PROG1),
	X(SEGA_MD_ROM_LOAD_NORMAL),
	X(SEGA_MD_ROM_LOAD16_WORD_SWAP),
	X(SEGA_MD_ROM_LOAD16_BYTE),
	X(SEGA_MD_ROM_LOAD16_WORD_SWAP_CONTINUE_040000_100000),
	X(SEGA_MD_ROM_LOAD_NORMAL_CONTINUE_020000_080000),
	X(SEGA_MD_ROM_OFFS_000000),
	X(SEGA_MD_ROM_OFFS_000001),
	X(SEGA_MD_ROM_OFFS_020000),
	X(SEGA_MD_ROM_OFFS_080000),
	X(SEGA_MD_ROM_OFFS_100000),
	X(SEGA_MD_ROM_OFFS_100001),
	X(SEGA_MD_ROM_OFFS_200000),
	X(SEGA_MD_ROM_OFFS_300000),
	X(SEGA_MD_ROM_RELOAD_200000_200000),
	X(SEGA_MD_ROM_RELOAD_100000_300000),
	X(SEGA_MD_ARCADE_SUNMIXING),
	X(SYS16_ROM_PROG_FLAT),
	X(SYS16_ROM_PROG),
	X(SYS16_ROM_TILES),
	X(SYS16_ROM_SPRITES),
	X(SYS16_ROM_Z80PROG),
	X(SYS16_ROM_KEY),
	X(SYS16_ROM_7751PROG),
	X(SYS16_ROM_7751DATA),
	X(SYS16_ROM_UPD7759DATA),
	X(SYS16_ROM_PROG2),
	X(SYS16_ROM_ROAD),
	X(SYS16_ROM_PCMDATA),
	X(SYS16_ROM_Z80PROG2),
	X(SYS16_ROM_Z80PROG3),
	X(SYS16_ROM_Z80PROG4),
	X(SYS16_ROM_PCM2DATA),
	X(SYS16_ROM_PROM),
	X(SYS16_ROM_PROG3),
	X(SYS16_ROM_SPRITES2),
	X(SYS16_ROM_RF5C68DATA),
	X(SYS16_ROM_I8751),
	X(SYS16_ROM_MSM6295),
	X(SYS16_ROM_TILES_20000),
	X(TAITO_68KROM1),
	X(TAITO_68KROM1_BYTESWAP),
	X(TAITO_68KROM1_BYTESWAP_JUMPING),
	X(TAITO_68KROM1_BYTESWAP32),
	X(TAITO_68KROM2),
	X(TAITO_68KROM2_BYTESWAP),
	X(TAITO_68KROM3),
	X(TAITO_68KROM3_BYTESWAP),
	X(TAITO_Z80ROM1),
	X(TAITO_Z80ROM2),
	X(TAITO_CHARS),
	X(TAITO_CHARS_BYTESWAP),
	X(TAITO_CHARSB),
	X(TAITO_CHARSB_BYTESWAP),
	X(TAITO_SPRITESA),
	X(TAITO_SPRITESA_BYTESWAP),
	X(TAITO_SPRITESA_BYTESWAP32),
	X(TAITO_SPRITESA_TOPSPEED),
	X(TAITO_SPRITESB),
	X(TAITO_SPRITESB_BYTESWAP),
	X(TAITO_SPRITESB_BYTESWAP32),
	X(TAITO_ROAD),
	X(TAITO_SPRITEMAP),
	X(TAITO_YM2610A),
	X(TAITO_YM2610B),
	X(TAITO_MSM5205),
	X(TAITO_MSM5205_BYTESWAP),
	X(TAITO_CHARS_PIVOT),
	X(TAITO_MSM6295),
	X(TAITO_ES5505),
	X(TAITO_ES5505_BYTESWAP),
	X(TAITO_DEFAULT_EEPROM),
	X(TAITO_CHARS_BYTESWAP32),
	X(TAITO_CCHIP_BIOS),
	X(TAITO_CCHIP_EEPROM)
};
#undef X

// gal.h
#undef GAL_ROM_Z80_PROG1
#undef GAL_ROM_Z80_PROG2
#undef GAL_ROM_Z80_PROG3
#undef GAL_ROM_TILES_SHARED
#undef GAL_ROM_TILES_CHARS
#undef GAL_ROM_TILES_SPRITES
#undef GAL_ROM_PROM
#undef GAL_ROM_S2650_PROG1

// megadrive.h
#undef SEGA_MD_ROM_LOAD_NORMAL
#undef SEGA_MD_ROM_LOAD16_WORD_SWAP
#undef SEGA_MD_ROM_LOAD16_BYTE
#undef SEGA_MD_ROM_LOAD16_WORD_SWAP_CONTINUE_040000_100000
#undef SEGA_MD_ROM_LOAD_NORMAL_CONTINUE_020000_080000
#undef SEGA_MD_ROM_OFFS_000000
#undef SEGA_MD_ROM_OFFS_000001
#undef SEGA_MD_ROM_OFFS_020000
#undef SEGA_MD_ROM_OFFS_080000
#undef SEGA_MD_ROM_OFFS_100000
#undef SEGA_MD_ROM_OFFS_100001
#undef SEGA_MD_ROM_OFFS_200000
#undef SEGA_MD_ROM_OFFS_300000
#undef SEGA_MD_ROM_RELOAD_200000_200000
#undef SEGA_MD_ROM_RELOAD_100000_300000
#undef SEGA_MD_ARCADE_SUNMIXING

// sys16.h
#undef SYS16_ROM_PROG_FLAT
#undef SYS16_ROM_PROG
#undef SYS16_ROM_TILES
#undef SYS16_ROM_SPRITES
#undef SYS16_ROM_Z80PROG
#undef SYS16_ROM_KEY
#undef SYS16_ROM_7751PROG
#undef SYS16_ROM_7751DATA
#undef SYS16_ROM_UPD7759DATA
#undef SYS16_ROM_PROG2
#undef SYS16_ROM_ROAD
#undef SYS16_ROM_PCMDATA
#undef SYS16_ROM_Z80PROG2
#undef SYS16_ROM_Z80PROG3
#undef SYS16_ROM_Z80PROG4
#undef SYS16_ROM_PCM2DATA
#undef SYS16_ROM_PROM
#undef SYS16_ROM_PROG3
#undef SYS16_ROM_SPRITES2
#undef SYS16_ROM_RF5C68DATA
#undef SYS16_ROM_I8751
#undef SYS16_ROM_MSM6295
#undef SYS16_ROM_TILES_20000

// taito.h
#undef TAITO_68KROM1
#undef TAITO_68KROM1_BYTESWAP
#undef TAITO_68KROM1_BYTESWAP_JUMPING
#undef TAITO_68KROM1_BYTESWAP32
#undef TAITO_68KROM2
#undef TAITO_68KROM2_BYTESWAP
#undef TAITO_68KROM3
#undef TAITO_68KROM3_BYTESWAP
#undef TAITO_Z80ROM1
#undef TAITO_Z80ROM2
#undef TAITO_CHARS
#undef TAITO_CHARS_BYTESWAP
#undef TAITO_CHARSB
#undef TAITO_CHARSB_BYTESWAP
#undef TAITO_SPRITESA
#undef TAITO_SPRITESA_BYTESWAP
#undef TAITO_SPRITESA_BYTESWAP32
#undef TAITO_SPRITESA_TOPSPEED
#undef TAITO_SPRITESB
#undef TAITO_SPRITESB_BYTESWAP
#undef TAITO_SPRITESB_BYTESWAP32
#undef TAITO_ROAD
#undef TAITO_SPRITEMAP
#undef TAITO_YM2610A
#undef TAITO_YM2610B
#undef TAITO_MSM5205
#undef TAITO_MSM5205_BYTESWAP
#undef TAITO_CHARS_PIVOT
#undef TAITO_MSM6295
#undef TAITO_ES5505
#undef TAITO_ES5505_BYTESWAP
#undef TAITO_DEFAULT_EEPROM
#undef TAITO_CHARS_BYTESWAP32
#undef TAITO_CCHIP_BIOS
#undef TAITO_CCHIP_EEPROM

static UINT32 RDGetRomsType(const TCHAR* pszDrvName, const UINT32 nBaseType, const INT32 nMinIdx, const INT32 nMaxIdx)
{
	char* pRomName;
	struct BurnRomInfo ri = { 0 };
	const UINT32 nOldDrvSel = nBurnDrvActive;	// Backup

	nBurnDrvActive = BurnDrvGetIndex(TCHARToANSI(pszDrvName, NULL, 0));

	for (INT32 i = 0; !BurnDrvGetRomName(&pRomName, i, 0); i++) {
		BurnDrvGetRomInfo(&ri, i);

		if ((ri.nType & nBaseType) && ((ri.nType & 0x0f) >= nMinIdx) && ((ri.nType & 0x0f) <= nMaxIdx)) {
			nBurnDrvActive = nOldDrvSel;		// Restore
			return ri.nType;
		}
	}

	nBurnDrvActive = nOldDrvSel;				// Restore
	return nBaseType;
}

// Consoles will automatically get the Type of the ROMs
static UINT32 RDGetConsoleRomsType(const TCHAR* pszDrvName)
{
	char* pRomName = NULL;
	struct BurnRomInfo ri = { 0 };
	const UINT32 nOldDrvSel = nBurnDrvActive;	// Backup

	nBurnDrvActive = BurnDrvGetIndex(TCHARToANSI(pszDrvName, NULL, 0));

	for (INT32 i = 0; !BurnDrvGetRomName(&pRomName, i, 0); i++) {
		BurnDrvGetRomInfo(&ri, i);

		if (ri.nType & BRF_PRG) {
			nBurnDrvActive = nOldDrvSel;		// Restore
			return ri.nType;
		}
	}

	nBurnDrvActive = nOldDrvSel;				// Restore
	return BRF_PRG;
}

// FBNeo style is preferred, universal solutions are not recommended
static UINT32 RDGetUniversalRomsType(const TCHAR* pszDrvName, const TCHAR* pszMask)
{
	UINT32 nBaseType = 0;

	if (0 == _tcsicmp(pszMask, _T("Program")) || 0 == _tcsicmp(pszMask, _T("PRG1"))) {
		nBaseType = BRF_PRG;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Graphics")) || 0 == _tcsicmp(pszMask, _T("GRA1"))) {
		nBaseType = BRF_GRA;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Z80")) || 0 == _tcsicmp(pszMask, _T("SNDA"))) {
		nBaseType = BRF_PRG;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Samples")) || 0 == _tcsicmp(pszMask, _T("SND1"))) {
		nBaseType = BRF_SND;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("BoardPLD")) || 0 == _tcsicmp(pszMask, _T("OPT1"))) {
		nBaseType = BRF_OPT;
	}

	char* pRomName = NULL;
	struct BurnRomInfo ri = { 0 };
	const UINT32 nOldDrvSel = nBurnDrvActive;	// Backup

	nBurnDrvActive = BurnDrvGetIndex(TCHARToANSI(pszDrvName, NULL, 0));

	for (INT32 i = 0; !BurnDrvGetRomName(&pRomName, i, 0); i++) {
		BurnDrvGetRomInfo(&ri, i);

		if (ri.nType & nBaseType) {
			nBurnDrvActive = nOldDrvSel;		// Restore
			return ri.nType;
		}
	}

	nBurnDrvActive = nOldDrvSel;				// Restore
	return nBaseType;
}

static UINT32 RDGetCps1RomsType(const TCHAR* pszDrvName, const TCHAR* pszMask, const UINT32 nPrgLen)
{
	INT32 nMinIdx = 0, nMaxIdx = 0;
	UINT32 nBaseType = 0;

	if (0 == _tcsicmp(pszMask, _T("Program")) || 0 == _tcsicmp(pszMask, _T("PRG1"))) {
		nMinIdx = CPS1_68K_PROGRAM_BYTESWAP, nMaxIdx = CPS1_68K_PROGRAM_NO_BYTESWAP;
		nBaseType = BRF_PRG;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("ProgramS")) || 0 == _tcsicmp(pszMask, _T("PRGS"))) {
		return (BRF_ESS | BRF_PRG | CPS1_68K_PROGRAM_BYTESWAP);
	}
	else
	if (0 == _tcsicmp(pszMask, _T("ProgramN")) || 0 == _tcsicmp(pszMask, _T("PRGN"))) {
		return (BRF_ESS | BRF_PRG | CPS1_68K_PROGRAM_NO_BYTESWAP);
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Z80")) || 0 == _tcsicmp(pszMask, _T("SNDA"))) {
		nMinIdx = nMaxIdx = CPS1_Z80_PROGRAM;
		nBaseType = BRF_PRG;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Graphics")) || 0 == _tcsicmp(pszMask, _T("GRA1"))) {
		nMinIdx = nMaxIdx = CPS1_TILES;
		nBaseType = BRF_GRA;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Samples")) || 0 == _tcsicmp(pszMask, _T("SND1"))) {
		nMinIdx = CPS1_OKIM6295_SAMPLES, nMaxIdx = CPS1_QSOUND_SAMPLES;
		nBaseType = BRF_SND;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Pic")) || 0 == _tcsicmp(pszMask, _T("SNDB"))) {
		nMinIdx = nMaxIdx = CPS1_PIC;
		nBaseType = BRF_PRG;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Tiles")) || 0 == _tcsicmp(pszMask, _T("Extras")) || 0 == _tcsicmp(pszMask, _T("GRA2"))) {
		nMinIdx = CPS1_EXTRA_TILES_SF2EBBL_400000, nMaxIdx = CPS1_EXTRA_TILES_SF2MKOT_400000;
		nBaseType = BRF_GRA;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("BoardPLD")) || 0 == _tcsicmp(pszMask, _T("OPT1"))) {
		return BRF_OPT;
	}

	char* pRomName = NULL;
	struct BurnRomInfo ri = { 0 };
	const UINT32 nOldDrvSel = nBurnDrvActive;	// Backup

	nBurnDrvActive = BurnDrvGetIndex(TCHARToANSI(pszDrvName, NULL, 0));

	for (INT32 i = 0; !BurnDrvGetRomName(&pRomName, i, 0); i++) {
		BurnDrvGetRomInfo(&ri, i);

		if ((ri.nType & nBaseType) && ((ri.nType & 0x0f) >= nMinIdx) && ((ri.nType & 0x0f) <= nMaxIdx)) {
			if (nPrgLen > 0 && (CPS1_68K_PROGRAM_BYTESWAP == nMinIdx || CPS1_68K_PROGRAM_NO_BYTESWAP == nMaxIdx)) {
				if (nPrgLen >= 0x40000 && (CPS1_68K_PROGRAM_BYTESWAP == (ri.nType & 0x0f))) {
					ri.nType = (ri.nType & ~CPS1_68K_PROGRAM_BYTESWAP) | CPS1_68K_PROGRAM_NO_BYTESWAP;
				}
				else
				if (nPrgLen < 0x40000 && (CPS1_68K_PROGRAM_NO_BYTESWAP == (ri.nType & 0x0f))) {
					ri.nType = (ri.nType & ~CPS1_68K_PROGRAM_NO_BYTESWAP) | CPS1_68K_PROGRAM_BYTESWAP;
				}
			}
			nBurnDrvActive = nOldDrvSel;		// Restore
			return ri.nType;
		}
	}

	nBurnDrvActive = nOldDrvSel;				// Restore
	return 0;
}

// d_cps1.cpp
#undef CPS1_68K_PROGRAM_BYTESWAP
#undef CPS1_68K_PROGRAM_NO_BYTESWAP
#undef CPS1_Z80_PROGRAM
#undef CPS1_TILES
#undef CPS1_OKIM6295_SAMPLES
#undef CPS1_QSOUND_SAMPLES
#undef CPS1_PIC
#undef CPS1_EXTRA_TILES_SF2EBBL_400000
#undef CPS1_EXTRA_TILES_400000
#undef CPS1_EXTRA_TILES_SF2KORYU_400000
#undef CPS1_EXTRA_TILES_SF2B_400000
#undef CPS1_EXTRA_TILES_SF2MKOT_400000

static UINT32 RDGetCps2RomsType(const TCHAR* pszDrvName, const TCHAR* pszMask)
{
	INT32 nMinIdx = 0, nMaxIdx = 0;
	UINT32 nBaseType = 0;

	if (0 == _tcsicmp(pszMask, _T("Program")) || 0 == _tcsicmp(pszMask, _T("PRG1"))) {
		nMinIdx = CPS2_PRG_68K, nMaxIdx = CPS2_PRG_68K_XOR_TABLE;
		nBaseType = BRF_PRG;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Graphics")) || 0 == _tcsicmp(pszMask, _T("GRA1"))) {
		nMinIdx = CPS2_GFX, nMaxIdx = CPS2_GFX_19XXJ;
		nBaseType = BRF_GRA;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Z80")) || 0 == _tcsicmp(pszMask, _T("SNDA"))) {
		nMinIdx = nMaxIdx = CPS2_PRG_Z80;
		nBaseType = BRF_PRG;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Samples")) || 0 == _tcsicmp(pszMask, _T("SND1"))) {
		nMinIdx = CPS2_QSND, nMaxIdx = CPS2_QSND_SIMM_BYTESWAP;
		nBaseType = BRF_SND;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Decryption")) || 0 == _tcsicmp(pszMask, _T("KEY1"))) {
		return CPS2_ENCRYPTION_KEY;
	}

	return RDGetRomsType(pszDrvName, nBaseType, nMinIdx, nMaxIdx);
}

// d_cps2.cpp
#undef CPS2_PRG_68K
#undef CPS2_PRG_68K_SIMM
#undef CPS2_PRG_68K_XOR_TABLE
#undef CPS2_GFX
#undef CPS2_GFX_SIMM
#undef CPS2_GFX_SPLIT4
#undef CPS2_GFX_SPLIT8
#undef CPS2_GFX_19XXJ
#undef CPS2_PRG_Z80
#undef CPS2_QSND
#undef CPS2_QSND_SIMM
#undef CPS2_QSND_SIMM_BYTESWAP
#undef CPS2_ENCRYPTION_KEY

static UINT32 RDGetCps3RomsType(const TCHAR* pszMask)
{
	if (0 == _tcsicmp(pszMask, _T("Bios"))) {
		return (BRF_ESS | BRF_BIOS);
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Program")) || 0 == _tcsicmp(pszMask, _T("PRG1"))) {
		return (BRF_ESS | BRF_PRG);
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Graphics")) || 0 == _tcsicmp(pszMask, _T("GRA1"))) {
		return BRF_GRA;
	}
	return 0;
}

static INT32 RDGetPgmRomsType(const TCHAR* pszDrvName, const TCHAR* pszMask)
{
	INT32 nMinIdx = 0, nMaxIdx = 0;
	UINT32 nBaseType = 0;

	if (0 == _tcsicmp(pszMask, _T("Program")) || 0 == _tcsicmp(pszMask, _T("PRG1"))) {
		nMinIdx = nMaxIdx = 1;
		nBaseType = BRF_PRG;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Tile")) || 0 == _tcsicmp(pszMask, _T("GRA1"))) {
		nMinIdx = nMaxIdx = 2;
		nBaseType = BRF_GRA;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("SpriteData")) || 0 == _tcsicmp(pszMask, _T("GRA2"))) {
		nMinIdx = nMaxIdx = 3;
		nBaseType = BRF_GRA;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("SpriteMasks")) || 0 == _tcsicmp(pszMask, _T("GRA3"))) {
		nMinIdx = nMaxIdx = 4;
		nBaseType = BRF_GRA;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Samples")) || 0 == _tcsicmp(pszMask, _T("SND1"))) {
		nMinIdx = nMaxIdx = 5;
		nBaseType = BRF_SND;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("InternalARM7")) || 0 == _tcsicmp(pszMask, _T("PRG2"))) {
		nMinIdx = nMaxIdx = 7;
		nBaseType = BRF_PRG;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Ramdump"))) {
		//What's the point of existing when the rom length is 0 and the Nodump marker won't load?
		return (7 | BRF_PRG | BRF_ESS | BRF_NODUMP);
	}
	else
	if (0 == _tcsicmp(pszMask, _T("ExternalARM7")) || 0 == _tcsicmp(pszMask, _T("PRG3"))) {
		nMinIdx = nMaxIdx = 8;
		nBaseType = BRF_PRG;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("ProtectionRom")) || 0 == _tcsicmp(pszMask, _T("PRG4"))) {
		nMinIdx = nMaxIdx = 9;
		nBaseType = BRF_PRG;
	}

	return RDGetRomsType(pszDrvName, nBaseType, nMinIdx, nMaxIdx);
}

static UINT32 RDGetNeoGeoRomsType(const TCHAR* pszDrvName, const TCHAR* pszMask)
{
	INT32 nMinIdx = 0, nMaxIdx = 0;
	UINT32 nBaseType = 0;

	if (0 == _tcsicmp(pszMask, _T("Program")) || 0 == _tcsicmp(pszMask, _T("PRG1"))) {
		nMinIdx = nMaxIdx = 1;
		nBaseType = BRF_PRG;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Text")) || 0 == _tcsicmp(pszMask, _T("GRA1"))) {
		nMinIdx = nMaxIdx = 2;
		nBaseType = BRF_GRA;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Graphics")) || 0 == _tcsicmp(pszMask, _T("GRA2"))) {
		nMinIdx = nMaxIdx = 3;
		nBaseType = BRF_GRA;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Z80")) || 0 == _tcsicmp(pszMask, _T("SNDA"))) {
		nMinIdx = nMaxIdx = 4;
		nBaseType = BRF_PRG;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("Samples")) || 0 == _tcsicmp(pszMask, _T("SND1"))) {
		nMinIdx = nMaxIdx = 5;
		nBaseType = BRF_SND;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("SamplesAES")) || 0 == _tcsicmp(pszMask, _T("SND2"))) {
		// See nam1975RomDesc
		nMinIdx = nMaxIdx = 6;
		nBaseType = BRF_SND;
	}
	else
	if (0 == _tcsicmp(pszMask, _T("BoardPLD")) || 0 == _tcsicmp(pszMask, _T("OPT1"))) {
		return 0 | BRF_OPT;
	}

	return RDGetRomsType(pszDrvName, nBaseType, nMinIdx, nMaxIdx);
}

static INT32 GetDrvHardwareMask(const TCHAR* pszDrvName)
{
	const UINT32 nOldDrvSel = nBurnDrvActive;	// Backup
	nBurnDrvActive = BurnDrvGetIndex(TCHARToANSI(pszDrvName, NULL, 0));

	const INT32 nHardwareCode = BurnDrvGetHardwareCode();
	nBurnDrvActive = nOldDrvSel;				// Restore

	return (nHardwareCode & HARDWARE_PUBLIC_MASK);
}

static UINT32 RDSetRomsType(const TCHAR* pszDrvName, const TCHAR* pszMask, const UINT32 nPrgLen)
{
	INT32 nHardwareMask = GetDrvHardwareMask(pszDrvName);
	switch (nHardwareMask){
		case HARDWARE_IGS_PGM:
			return RDGetPgmRomsType(pszDrvName, pszMask);

		case HARDWARE_SNK_NEOGEO:
			return RDGetNeoGeoRomsType(pszDrvName, pszMask);

		case HARDWARE_CAPCOM_CPS1:
		case HARDWARE_CAPCOM_CPS1_QSOUND:
		case HARDWARE_CAPCOM_CPS1_GENERIC:
		case HARDWARE_CAPCOM_CPSCHANGER:
			return RDGetCps1RomsType(pszDrvName, pszMask, nPrgLen);

		case HARDWARE_CAPCOM_CPS2:
		case HARDWARE_CAPCOM_CPS2 | HARDWARE_CAPCOM_CPS2_SIMM:
			return RDGetCps2RomsType(pszDrvName, pszMask);

		case HARDWARE_CAPCOM_CPS3:
		case HARDWARE_CAPCOM_CPS3 | HARDWARE_CAPCOM_CPS3_NO_CD:
			return RDGetCps3RomsType(pszDrvName);

		case HARDWARE_SEGA_MEGADRIVE:
		case HARDWARE_PCENGINE_PCENGINE:
		case HARDWARE_PCENGINE_SGX:
		case HARDWARE_PCENGINE_TG16:
		case HARDWARE_SEGA_SG1000:
		case HARDWARE_COLECO:
		case HARDWARE_SEGA_MASTER_SYSTEM:
		case HARDWARE_SEGA_GAME_GEAR:
		case HARDWARE_MSX:
		case HARDWARE_SPECTRUM:
		case HARDWARE_NES:
		case HARDWARE_FDS:
		case HARDWARE_SNES:
		case HARDWARE_SNK_NGP:
		case HARDWARE_CHANNELF:
			return RDGetConsoleRomsType(pszDrvName);

		default:
			return RDGetUniversalRomsType(pszDrvName, pszMask);
	}
}

TCHAR* _strqtoken(TCHAR* s, const TCHAR* delims)
{
	static TCHAR* prev_str = NULL;
	TCHAR* token = NULL;

	if (!s) {
		if (!prev_str) return NULL;

		s = prev_str;
	}

	s += _tcsspn(s, delims);

	if (s[0] == _T('\0')) {
		prev_str = NULL;
		return NULL;
	}

	if (s[0] == _T('\"')) { // time to grab quoted string!
		token = ++s;
		if ((s = _tcspbrk(token, _T("\"")))) {
			*(s++) = '\0';
		}
		if (!s) {
			prev_str = NULL;
			return NULL;
		}
	} else {
		token = s;
	}

	if ((s = _tcspbrk(s, delims))) {
		*(s++) = _T('\0');
		prev_str = s;
	} else {
		// we're at the end of the road
#if defined (_UNICODE)
		prev_str = (TCHAR*)wmemchr(token, _T('\0'), MAX_PATH);
#else
		prev_str = (char*)memchr((void*)token, '\0', MAX_PATH);
#endif
	}

	return token;
}

static INT32 FileExists(const TCHAR* pszName)
{
	DWORD dwAttrib = GetFileAttributes(pszName);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}


typedef enum {
	ENCODING_ANSI,
	ENCODING_UTF8,
	ENCODING_UTF8_BOM,
	ENCODING_UTF16_LE,
	ENCODING_UTF16_BE,
	ENCODING_ERROR
} EncodingType;

static EncodingType DetectEncoding(const TCHAR* pszDatFile)
{
	FILE* fp = _tfopen(pszDatFile, _T("rb"));
	if (NULL == fp)
		return ENCODING_ERROR;

	EncodingType encType = ENCODING_UTF8;

	// Read BOM
	UINT8 cBom[3] = { 0 };
	const UINT32 nBomSize = fread(cBom, 1, 3, fp);

	if (0 == nBomSize) {	// Empty file or read error
		fclose(fp);
		return ENCODING_ERROR;
	}
	if ((nBomSize >= 3) && (0xef == cBom[0]) && (0xbb == cBom[1]) && (0xbf == cBom[2])) {
		fclose(fp);
		return ENCODING_UTF8_BOM;
	}
	if (nBomSize >= 2) {
		if ((0xff == cBom[0]) && (0xfe == cBom[1])) {
			fclose(fp);
			return ENCODING_UTF16_LE;
		}
		if ((0xfe == cBom[0]) && (0xff == cBom[1])) {
			fclose(fp);
			return ENCODING_UTF16_BE;
		}
	}

	fseek(fp, 0, SEEK_END);
	long nLen = ftell(fp);
	rewind(fp);

	UINT8* pBuf = (UINT8*)malloc(nLen);
	if (!pBuf) {
		fclose(fp);
		return ENCODING_ERROR;
	}

	UINT32 nRead = fread(pBuf, 1, nLen, fp);
	fclose(fp);

	if (nRead != (UINT32)nLen) {
		free(pBuf);
		return ENCODING_ERROR;
	}

	UINT32 p = 0;
	while (p < nRead) {
		if (pBuf[p] < 0x80) {
			p++;
			continue;
		}

		if (!((pBuf[p] >= 0xc2) && (pBuf[p] <= 0xf4))) {
			free(pBuf);
			return ENCODING_ANSI;
		}

		INT32 nBytes = 0;
		if ((pBuf[p] >= 0xc2) && (pBuf[p] <= 0xdf))
			nBytes = 1;
		if ((pBuf[p] >= 0xe0) && (pBuf[p] <= 0xef))
			nBytes = 2;
		if ((pBuf[p] >= 0xf0) && (pBuf[p] <= 0xf4))
			nBytes = 3;

		if ((p + nBytes) >= nRead) {
			free(pBuf);
			return ENCODING_ANSI;
		}

		for (int i = 1; i <= nBytes; i++) {
			if (!(0x80 == (pBuf[p + i] & 0xc0))) {
				free(pBuf);
				return ENCODING_ANSI;
			}
		}

		p += (nBytes + 1);
	}
	free(pBuf);

	return ENCODING_UTF8;
}

static TCHAR* Utf16beToUtf16le(const TCHAR* pszDatFile)
{
	FILE* fp = _tfopen(pszDatFile, _T("rb"));
	if (NULL == fp) return NULL;

	fseek(fp, 0, SEEK_END);
	long nLen = ftell(fp);
	rewind(fp);

	UINT8* pBuffer = (UINT8*)malloc(nLen);
	if (NULL == pBuffer) {
		fclose(fp);  fp = NULL;
		return NULL;
	}
	memset(pBuffer, 0, nLen);

	UINT32 nRead = fread(pBuffer, 1, nLen, fp);
	fclose(fp);

	// Ensure that even bytes are handled
	if (0 != (nRead % 2)) {
		nRead--; // Discard last byte
	}

	// Swap the order of each double byte (BE -> LE)
	for (UINT32 i = 0; i < nRead; i += 2) {
		UINT8 cTemp = pBuffer[i];
		pBuffer[i + 0] = pBuffer[i + 1];
		pBuffer[i + 1] = cTemp;
	}

	if (NULL == (fp = _tfopen(pszDatFile, _T("wb")))) {
		free(pBuffer); pBuffer = NULL;
	}

	fwrite(pBuffer, 1, nRead, fp);
	fclose(fp);    fp      = NULL;
	free(pBuffer); pBuffer = NULL;

	return (TCHAR*)pszDatFile;
}

static bool StrToUint(const TCHAR* str, UINT32* result) {
	if (NULL == str || _T('\0') == *str || NULL == result) return false;

	errno = 0;

	TCHAR* endPtr;
	unsigned long value = _tcstoul(str, &endPtr, 16);

	if (str == endPtr)       return false;
	if (_T('\0') != *endPtr) return false;
	if (value > UINT_MAX) {
		errno = ERANGE;
		return false;
	}

	*result = (UINT32)value;
	return true;
}

TCHAR* AdaptiveEncodingReads(const TCHAR* pszFileName)
{
	EncodingType nType = DetectEncoding(pszFileName);
	TCHAR* pszReadMode = NULL;

	switch (nType) {
		case ENCODING_ANSI: {
			pszReadMode = _T("rt");
			break;
		}
		case ENCODING_UTF8:
		case ENCODING_UTF8_BOM: {
			pszReadMode = _T("rt, ccs=UTF-8");
			break;
		}
		case ENCODING_UTF16_LE: {
			pszReadMode = _T("rt, ccs=UTF-16LE");
			break;
		}
		case ENCODING_UTF16_BE: {
			if (NULL == Utf16beToUtf16le(pszFileName)) return NULL;
			pszReadMode = _T("rt, ccs=UTF-16LE");
			break;
		}
		default:
			return NULL;
	}

	static TCHAR szRet[MAX_PATH] = { 0 };

	return _tcscpy(szRet, pszReadMode);
}

#define DELIM_TOKENS_NAME	_T(" \t\r\n,%:|{}")

static INT32 LoadRomdata()
{
	const TCHAR* pszReadMode = AdaptiveEncodingReads(szRomdataName);
	if (NULL == pszReadMode) return -1;

	RDI.nDescCount = -1;					// Failed

	FILE* fp = _tfopen(szRomdataName, pszReadMode);
	if (NULL == fp) return -1;

	TCHAR szBuf[MAX_PATH] = { 0 }, szRomMask[30] = { 0 }, szDrvName[100] = { 0 };
	TCHAR* pszBuf = NULL, * pszLabel = NULL, * pszInfo = NULL;
	INT32 nDatMode = 0;						// FBNeo 0, Nebula 1

	memset(RDI.szExtraRom, 0, sizeof(RDI.szExtraRom));
	memset(RDI.szFullName, 0, sizeof(RDI.szFullName));

	while (!feof(fp)) {
		if (_fgetts(szBuf, MAX_PATH, fp) != NULL) {
			pszBuf = szBuf;

			pszLabel = _strqtoken(pszBuf, DELIM_TOKENS_NAME);
			if (NULL == pszLabel) continue;
			if ((_T('/') == pszLabel[0]) && (_T('/') == pszLabel[1])) continue;

			UINT32 nLen = _tcslen(pszLabel);
			if ((nLen > 2) && (_T('[') == pszLabel[0]) && (_T(']') == pszLabel[nLen - 1])) {
				pszLabel++, nLen -= 2;
				if (0 == _tcsicmp(_T("System"), pszLabel)) break;
				memset(szRomMask, 0, sizeof(szRomMask));
				_tcsncpy(szRomMask, pszLabel, nLen);
				szRomMask[nLen] = _T('\0');
				continue;
			}
			if (0 == _tcsicmp(_T("System"), pszLabel)) {
				nDatMode = 1;				// Nebula
				continue;
			}
			if (0 == _tcsicmp(_T("ZipName"), pszLabel) || 0 == _tcsicmp(_T("RomName"), pszLabel)) {
				pszInfo = _strqtoken(NULL, DELIM_TOKENS_NAME);
				if (NULL == pszInfo) break;	// No romset specified
				if (NULL != pDRD) {
					strcpy(RDI.szZipName, TCHARToANSI(pszInfo, NULL, 0));
				}
				continue;
			}
			if (0 == _tcsicmp(_T("DrvName"), pszLabel) || 0 == _tcsicmp(_T("Parent"), pszLabel)) {
				pszInfo = _strqtoken(NULL, DELIM_TOKENS_NAME);
				if (NULL == pszInfo) break;	// No driver specified
				if (NULL != pDRD) {
					strcpy(RDI.szDrvName, TCHARToANSI(pszInfo, NULL, 0));
				}
				memset(szDrvName, 0, sizeof(szDrvName));
				_tcscpy(szDrvName, pszInfo);
				continue;
			}
			if (0 == _tcsicmp(_T("ExtraRom"), pszLabel)) {
				pszInfo = _strqtoken(NULL, DELIM_TOKENS_NAME);
				if ((NULL != pszInfo) && (NULL != pDRD)) {
					strcpy(RDI.szExtraRom, TCHARToANSI(pszInfo, NULL, 0));
				}
				continue;
			}
			if (0 == _tcsicmp(_T("FullName"), pszLabel) || 0 == _tcsicmp(_T("Game"), pszLabel)) {
				TCHAR szMerger[260] = { 0 };
				INT32 nAdd = 0;
				while (NULL != (pszInfo = _strqtoken(NULL, DELIM_TOKENS_NAME))) {
					_stprintf(szMerger + nAdd, _T("%s "), pszInfo);
					nAdd = _tcslen(szMerger);
				}
				szMerger[nAdd - 1] = _T('\0');
				if (NULL != pDRD) {
#ifdef UNICODE
					_tcscpy(RDI.szFullName, szMerger);
#else
					wcscpy(RDI.szFullName, ANSIToTCHAR(szMerger, NULL, 0));
#endif
				}
				continue;
			}

			{
				// romname, len, crc, type
				struct BurnRomInfo ri = { 0 };
				ri.nLen  = UINT_MAX;
				ri.nCrc  = UINT_MAX;
				ri.nType = 0U;

				if (1 == nDatMode) {
					// Skips content when recognized as a Nebula style
					pszInfo = _strqtoken(NULL, DELIM_TOKENS_NAME);
				}
				pszInfo = _strqtoken(NULL, DELIM_TOKENS_NAME);
				if (NULL != pszInfo) {
					StrToUint(pszInfo, &(ri.nLen));
					if ((UINT_MAX == ri.nLen) || (0 == ri.nLen)) continue;

					pszInfo = _strqtoken(NULL, DELIM_TOKENS_NAME);
					if (NULL != pszInfo) {
						StrToUint(pszInfo, &(ri.nCrc));
						if (UINT_MAX == ri.nCrc) continue;

						while (NULL != (pszInfo = _strqtoken(NULL, DELIM_TOKENS_NAME))) {
							UINT32 nValue = UINT_MAX;

							for (UINT32 i = 0; i < (sizeof(RDMacroTable) / sizeof(RDMacroMap)); i++) {
								if (0 == _tcscmp(pszInfo, RDMacroTable[i].pszDRMacro)) {
									ri.nType |= RDMacroTable[i].nRDMacro;
									continue;
								}
							}

							StrToUint(pszInfo, &nValue);
							if ((nValue >= 0) && (nValue < UINT_MAX)) {
								ri.nType |= nValue;
							}
						}
						// FBNeo style ROMs have a higher priority for nType than Nebula style
						if (0 == ri.nType) {
							ri.nType = RDSetRomsType(szDrvName, szRomMask, ri.nLen);
						}
						if (ri.nType > 0U) {
							RDI.nDescCount++;

							if (NULL != pDRD) {
								pDRD[RDI.nDescCount].szName = (char*)malloc(512);
								memset(pDRD[RDI.nDescCount].szName, 0, sizeof(char) * 512);
								strcpy(pDRD[RDI.nDescCount].szName, TCHARToANSI(pszLabel, NULL, 0));

								pDRD[RDI.nDescCount].nLen  = ri.nLen;
								pDRD[RDI.nDescCount].nCrc  = ri.nCrc;
								pDRD[RDI.nDescCount].nType = ri.nType;
							}
						}
					}
				}
			}
		}
	}
	fclose(fp);

	if (NULL != pDRD) {
		pDataRomDesc = (struct BurnRomInfo*)malloc((RDI.nDescCount + 1) * sizeof(BurnRomInfo));
		if (NULL == pDataRomDesc) return -1;

		for (INT32 i = 0; i <= RDI.nDescCount; i++) {
			pDataRomDesc[i].szName = (char*)malloc(512);
			memset(pDataRomDesc[i].szName, 0, sizeof(char) * 512);
			strcpy(pDataRomDesc[i].szName, pDRD[i].szName);
			free(pDRD[i].szName); pDRD[i].szName = NULL;

			pDataRomDesc[i].nLen  = pDRD[i].nLen;
			pDataRomDesc[i].nCrc  = pDRD[i].nCrc;
			pDataRomDesc[i].nType = pDRD[i].nType;
		}
		free(pDRD); pDRD = NULL;
	}

	return RDI.nDescCount;
}

bool RomDataSetQuickPath(const TCHAR* pszSelDat)
{
	if ((NULL == pszSelDat) || !FileExists(pszSelDat)) {
		FBAPopupAddText(PUF_TEXT_DEFAULT, _T("RomData:\n\n"));
		FBAPopupAddText(PUF_TEXT_DEFAULT, MAKEINTRESOURCE(IDS_ERR_FILE_EXIST), pszSelDat);
		FBAPopupDisplay(PUF_TYPE_ERROR);
		return false;
	}

	const TCHAR* pszExt = _tcsrchr(pszSelDat, _T('.'));
	if (NULL == pszExt || (0 != _tcsicmp(_T(".dat"), pszExt))) {
		FBAPopupAddText(PUF_TEXT_DEFAULT, _T("RomData:\n\n"));
		FBAPopupAddText(PUF_TEXT_DEFAULT, MAKEINTRESOURCE(IDS_ERR_FILE_EXTENSION), pszSelDat, _T(".dat"));
		FBAPopupDisplay(PUF_TYPE_ERROR);
		return false;
	}

	const TCHAR* p = pszSelDat + _tcslen(pszSelDat), * dir_end = NULL;
	INT32 nCount = 0;
	while (p > pszSelDat) {
		if ((_T('/') == *p) || (_T('\\') == *p)) {
			TCHAR c = *(p - 1);
			if ((_T('/') == c) ||
				(_T('\\') == c)) {		// xxxx//ssss\\...
				FBAPopupAddText(PUF_TEXT_DEFAULT, _T("RomData:\n\n"));
				FBAPopupAddText(PUF_TEXT_DEFAULT, MAKEINTRESOURCE(IDS_ERR_FILE_EXIST), pszSelDat);
				FBAPopupDisplay(PUF_TYPE_ERROR);
				return false;
			}
			if (1 == ++nCount) {
				dir_end = p + 1;		// Intentionally add 1
			}
		}
		p--;
	}

	memset(szAppQuickPath, 0, sizeof(szAppQuickPath));
	_tcsncpy(szAppQuickPath, pszSelDat, dir_end - pszSelDat);

	return true;
}

char* RomdataGetDrvName()
{
	const TCHAR* pszReadMode = AdaptiveEncodingReads(szRomdataName);
	if (NULL == pszReadMode) return NULL;

	FILE* fp = _tfopen(szRomdataName, pszReadMode);
	if (NULL == fp) return NULL;

	TCHAR szBuf[MAX_PATH] = { 0 };
	TCHAR* pszBuf = NULL, * pszLabel = NULL, * pszInfo = NULL;

	while (!feof(fp)) {
		if (_fgetts(szBuf, MAX_PATH, fp) != NULL) {
			pszBuf = szBuf;

			pszLabel = _strqtoken(pszBuf, DELIM_TOKENS_NAME);
			if (NULL == pszLabel) continue;
			if ((_T('/') == pszLabel[0]) && (_T('/') == pszLabel[1])) continue;

			if (0 == _tcsicmp(_T("DrvName"), pszLabel) || 0 == _tcsicmp(_T("Parent"), pszLabel)) {
				pszInfo = _strqtoken(NULL, DELIM_TOKENS_NAME);
				if (NULL == pszInfo) break;	// No driver specified
				fclose(fp);
				return TCHARToANSI(pszInfo, NULL, 0);
			}
		}
	}
	fclose(fp);

	return NULL;
}

TCHAR* RomdataGetZipName(const TCHAR* pszFileName)
{
	const TCHAR* pszReadMode = AdaptiveEncodingReads(pszFileName);
	if (NULL == pszReadMode) return NULL;

	FILE* fp = _tfopen(pszFileName, pszReadMode);
	if (NULL == fp) return NULL;

	TCHAR szBuf[MAX_PATH] = { 0 };
	TCHAR* pszBuf = NULL, * pszLabel = NULL, * pszInfo = NULL;

	while (!feof(fp)) {
		if (_fgetts(szBuf, MAX_PATH, fp) != NULL) {
			pszBuf = szBuf;

			pszLabel = _strqtoken(pszBuf, DELIM_TOKENS_NAME);
			if (NULL == pszLabel) continue;
			if ((_T('/') == pszLabel[0]) && (_T('/') == pszLabel[1])) continue;

			if (0 == _tcsicmp(_T("ZipName"), pszLabel) || 0 == _tcsicmp(_T("RomName"), pszLabel)) {
				pszInfo = _strqtoken(NULL, DELIM_TOKENS_NAME);
				if (NULL == pszInfo) break;	// No romset specified
				fclose(fp);
				static TCHAR szRet[100];
				memset(szRet, 0, sizeof(szRet));
				return _tcscpy(szRet, pszInfo);
			}
		}
	}
	fclose(fp);

	return NULL;
}

TCHAR* RomdataGetDrvName(const TCHAR* pszFileName)
{
	const TCHAR* pszReadMode = AdaptiveEncodingReads(pszFileName);
	if (NULL == pszReadMode) return NULL;

	FILE* fp = _tfopen(pszFileName, pszReadMode);
	if (NULL == fp) return NULL;

	TCHAR szBuf[MAX_PATH] = { 0 };
	TCHAR* pszBuf = NULL, * pszLabel = NULL, * pszInfo = NULL;

	while (!feof(fp)) {
		if (_fgetts(szBuf, MAX_PATH, fp) != NULL) {
			pszBuf = szBuf;

			pszLabel = _strqtoken(pszBuf, DELIM_TOKENS_NAME);
			if (NULL == pszLabel) continue;
			if ((_T('/') == pszLabel[0]) && (_T('/') == pszLabel[1])) continue;

			if (0 == _tcsicmp(_T("DrvName"), pszLabel) || 0 == _tcsicmp(_T("Parent"), pszLabel)) {
				pszInfo = _strqtoken(NULL, DELIM_TOKENS_NAME);
				if (NULL == pszInfo) break;	// No romset specified
				fclose(fp);
				static TCHAR szRet[100];
				memset(szRet, 0, sizeof(szRet));
				return _tcscpy(szRet, pszInfo);
			}
		}
	}
	fclose(fp);

	return NULL;
}

// It is recommended to save and restore the state of nBurnDrvActive before and after the call
INT32 RomdataGetDrvIndex(const TCHAR* pszDrvName)
{
	// nBurnDrvActive must be saved and restored, as BurnAreaScan() depends on it
	const UINT32 nOldDrvActive = nBurnDrvActive;

	for (INT32 nDrvIndex = 0; nDrvIndex < nBurnDrvCount; nDrvIndex++) {
		nBurnDrvActive = nDrvIndex;
		if ((0 == _tcscmp(pszDrvName, BurnDrvGetText(DRV_NAME))) && (!(BurnDrvGetFlags() & BDF_BOARDROM))) {
			nBurnDrvActive = nOldDrvActive;
			return nDrvIndex;
		}
	}

	nBurnDrvActive = nOldDrvActive;
	return -1;
}

TCHAR* RomdataGetFullName(const TCHAR* pszFileName)
{
	const TCHAR* pszReadMode = AdaptiveEncodingReads(pszFileName);
	if (NULL == pszReadMode) return NULL;

	FILE* fp = _tfopen(pszFileName, pszReadMode);
	if (NULL == fp) return NULL;

	TCHAR szBuf[MAX_PATH] = { 0 };
	TCHAR* pszBuf = NULL, * pszLabel = NULL, * pszInfo = NULL;

	while (!feof(fp)) {
		if (_fgetts(szBuf, MAX_PATH, fp) != NULL) {
			pszBuf = szBuf;

			pszLabel = _strqtoken(pszBuf, DELIM_TOKENS_NAME);
			if (NULL == pszLabel) continue;
			if ((_T('/') == pszLabel[0]) && (_T('/') == pszLabel[1])) continue;

			if (0 == _tcsicmp(_T("FullName"), pszLabel) || 0 == _tcsicmp(_T("Game"), pszLabel)) {
				static TCHAR szMerger[260];
				memset(szMerger, 0, sizeof(szMerger));
				INT32 nAdd = 0;
				while (NULL != (pszInfo = _strqtoken(NULL, DELIM_TOKENS_NAME))) {
					_stprintf(szMerger + nAdd, _T("%s "), pszInfo);
					nAdd = _tcslen(szMerger);
				}
				szMerger[nAdd - 1] = _T('\0');
				fclose(fp);
				return szMerger;
			}
		}
	}
	fclose(fp);

	return NULL;
}

static INT32 RomsetDuplicateName(const TCHAR* pszFileName)
{
	if (NULL != pDataRomDesc) return -2;

	TCHAR* pszZipName = RomdataGetZipName(pszFileName);
	if (NULL == pszZipName)   return -3;
/*
	return:	-1 is success
	0 ~ N	The name is duplicated
	-1		Not in the list of drivers
	-2		RomData mode
	-3		No results were found in the Dat file
*/
	return BurnDrvGetIndex(_TtoA(pszZipName));
}

// Checking in RomData mode is strictly prohibited
INT32 RomDataCheck(const TCHAR* pszDatFile)
{
	if (NULL == pszDatFile || !FileExists(pszDatFile)) {
		FBAPopupAddText(PUF_TEXT_DEFAULT, _T("RomData:\n\n"));
		FBAPopupAddText(PUF_TEXT_DEFAULT, MAKEINTRESOURCE(IDS_ERR_FILE_EXIST), pszDatFile);
		FBAPopupDisplay(PUF_TYPE_ERROR);
		return -1;
	}

	INT32 nRet = 0;
	const TCHAR* pszDrvName = RomdataGetDrvName(pszDatFile);
	if (NULL == pszDrvName) {
		FBAPopupAddText(PUF_TEXT_DEFAULT, _T("RomData: %s\n\n"), pszDatFile);
		FBAPopupAddText(PUF_TEXT_DEFAULT, MAKEINTRESOURCE(IDS_ERR_DRIVER_NOT_EXIST));
		FBAPopupDisplay(PUF_TYPE_ERROR);
		return -3;
	}

	const INT32 nDrvIdx = BurnDrvGetIndex(_TtoA(pszDrvName));
	if (-1 == nDrvIdx) {
		FBAPopupAddText(PUF_TEXT_DEFAULT, _T("RomData: %s\n\n"), pszDatFile);
		FBAPopupAddText(PUF_TEXT_DEFAULT, MAKEINTRESOURCE(IDS_ERR_DRIVER_NOT_EXIST));
		FBAPopupDisplay(PUF_TYPE_ERROR);
		return -4;
	}

	nRet = RomsetDuplicateName(pszDatFile);
	if (nRet >= 0) {
		FBAPopupAddText(PUF_TEXT_DEFAULT, _T("RomData: %s\n\n"), pszDatFile);
		FBAPopupAddText(PUF_TEXT_DEFAULT, MAKEINTRESOURCE(IDS_ERR_ROMSET_DUPLICATE));
		FBAPopupDisplay(PUF_TYPE_ERROR);
		return -5;
	}
/*
	-2 and -3 should have no chance of being detected and are reserved for now
*/
	if (-2 == nRet) {
		return -6;								// RomData mode
	}
	if (-3 == nRet) {
		return -7;								// No romset specified,
	}

/*
	Now we're going to go into RomData mode and check the integrity of the Romset
	Exit RomData mode as soon as the check is complete
*/
	TCHAR szBackup[MAX_PATH] = { 0 };
	_tcscpy(szBackup, szRomdataName);			// Backup szRomdataName
	_tcscpy(szRomdataName, pszDatFile);
	RomDataInit();								// Replace DrvName##RomDesc
	const UINT32 nOldDrvSel = nBurnDrvActive;	// Backup nBurnDrvActive
	nBurnDrvActive = nDrvIdx;					// Required nBurnDrvActive
	nRet = BzipOpen(1);
	if (1 == nRet) {							// ROMs error report
		BzipClose();
		FBAPopupAddText(PUF_TEXT_DEFAULT, _T("RomData: %s\n\n"), pszDatFile);
		BzipOpen(0);
		FBAPopupDisplay(PUF_TYPE_ERROR);
		nRet = -1;
	}
	BzipClose();
	nBurnDrvActive = nOldDrvSel;				// Restore nBurnDrvActive
	RomDataExit();								// Restore DrvName##RomDesc
	_tcscpy(szRomdataName, szBackup);			// Restore szRomdataName

	return (0 == nRet) ? nDrvIdx : nRet;
}


#undef DELIM_TOKENS_NAME

void RomDataInit()
{
	INT32 nLen = LoadRomdata();

	if ((nLen >= 0) && (NULL == pDRD)) {
		pDRD = (struct BurnRomInfo*)malloc((nLen + 1) * sizeof(BurnRomInfo));
		if (NULL == pDRD) return;

		LoadRomdata();
		RDI.nDriverId = BurnDrvGetIndex(RDI.szDrvName);

		if (RDI.nDriverId >= 0) {
			BurnDrvSetZipName(RDI.szZipName, RDI.nDriverId);
		}
	}
}

void RomDataSetFullName()
{
	// At this point, the driver's default ZipName has been replaced with the ZipName from the rom data
	RDI.nDriverId = BurnDrvGetIndex(RDI.szZipName);

	if (RDI.nDriverId >= 0) {
		wchar_t* szOldName = BurnDrvGetFullNameW(RDI.nDriverId);
		memset(RDI.szOldName, '\0', sizeof(RDI.szOldName));

		if (0 != wcscmp(szOldName, RDI.szOldName)) {
			wcscpy(RDI.szOldName, szOldName);
		}

		BurnDrvSetFullNameW(RDI.szFullName, RDI.nDriverId);
	}
}

void RomDataExit()
{
	if (NULL != pDataRomDesc) {
		for (INT32 i = 0; i < RDI.nDescCount + 1; i++) {
			free(pDataRomDesc[i].szName);
		}
		free(pDataRomDesc); pDataRomDesc = NULL;

		if (RDI.nDriverId >= 0) {
			BurnDrvSetZipName(RDI.szDrvName, RDI.nDriverId);
			if (0 != wcscmp(BurnDrvGetFullNameW(RDI.nDriverId), RDI.szOldName)) {
				BurnDrvSetFullNameW(RDI.szOldName, RDI.nDriverId);
			}
		}

		memset(&RDI,          0, sizeof(RomDataInfo));
		memset(szRomdataName, 0, sizeof(szRomdataName));

		RDI.nDescCount = -1;
	}
}


bool FindZipNameFromDats(const TCHAR* /*dirPath*/, const char* /*pszZipName*/, TCHAR* /*pszFindDat*/)
{
	return false;
}
