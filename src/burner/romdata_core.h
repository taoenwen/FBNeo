// =============================================================================
//  FBNeo  -  RomData core (platform-independent)
// =============================================================================

#ifndef ROMDATA_CORE_H
#define ROMDATA_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

void  RomDataScan(const TCHAR* szDir, bool bScanSub);
void  RomDataFree(void);

INT32 RomDataLoadOne(const TCHAR* szDatPath);

bool  IsRomDataDrv(void);
char* RomDataDrvGetDrvName(void);
struct BurnRomInfo* RomDataDrvGetRomInfo(UINT32* pRomCount);
const char* RomDataDrvGetExtName(void);

#ifdef __cplusplus
}
#endif

#endif // ROMDATA_CORE_H
