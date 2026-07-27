#include "burner.h"

struct BurnRomInfo* pDataRomDesc = NULL;

TCHAR szRomdataName[MAX_PATH] = _T("");

static struct RomDataInfo RDI = { 0 };
RomDataInfo* pRDI = &RDI;

char* RomdataGetDrvName()
{
	return NULL;
}

void RomDataInit()
{
}

void RomDataSetFullName()
{
}

void RomDataExit()
{
}

INT32 RomdataGetDrvIndex(const TCHAR* pszDrvName)
{
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
