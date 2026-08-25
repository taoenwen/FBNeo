// Win32 frontend for direct console ROM loading: file dialog + quick-load plumbing.
// Detection and the runtime driver live in the platform-independent console_core.cpp.
#include "burner.h"
#include "console_core.h"

#ifdef _UNICODE
// Database title in Unicode (may be non-English); NULL = not found
static const TCHAR* ConsoleRunLookupTitle(const struct ConsoleRomInfo* pInfo)
{
	const UINT32 nOldActive = nBurnDrvActive;
	const TCHAR* pszFound = NULL;

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

		const TCHAR* pszName = BurnDrvGetText(DRV_FULLNAME);
		if (pszName != NULL && pszName[0] != 0) {
			pszFound = pszName;							// Copy out immediately: static buffer of BurnDrvGetText
			break;
		}
	}

	nBurnDrvActive = nOldActive;
	return pszFound;
}

// Mirror the ANSI title into the driver's Unicode title (double-NUL handled by the core)
static void ConsoleRunSetFullNameW(const struct ConsoleRomInfo* pInfo)
{
	wchar_t szWide[MAX_PATH + 34];
	const size_t nCount = sizeof(szWide) / sizeof(szWide[0]);
	const TCHAR* pszTitle = ConsoleRunLookupTitle(pInfo);

	if (pszTitle != NULL) {
		_tcsncpy(szWide, pszTitle, nCount - 2);
	} else {
		ANSIToTCHAR(ConsoleRomGetFullName(), szWide, (int)(nCount - 2));
	}
	szWide[nCount - 2] = 0;

	ConsoleRomSetFullNameW(szWide);
}
#endif

INT32 ConsoleRomOpenFile(const TCHAR* pszPath)
{
	if (pszPath == NULL) {
		return -1;
	}

	char szAnsiPath[MAX_PATH];
	strcpy(szAnsiPath, TCHARToANSI(pszPath, NULL, 0));

	struct ConsoleRomInfo info;
	INT32 nDetect = ConsoleRomDetect(szAnsiPath, &info);
	if (nDetect != CONSOLE_ROM_OK) {
		FBAPopupAddText(PUF_TEXT_DEFAULT, _T("Console ROM file:\n\n"));
		if (nDetect == CONSOLE_ROM_ERR_NOT_FOUND) {
			FBAPopupAddText(PUF_TEXT_DEFAULT, _T("File not found: %s"), pszPath);
		} else {
			FBAPopupAddText(PUF_TEXT_DEFAULT, _T("No supported ROM found (checked file header): %s"), pszPath);
		}
		FBAPopupDisplay(PUF_TYPE_ERROR);
		return -1;
	}

	INT32 nDrvIdx = ConsoleRomCreateDriver(szAnsiPath, &info);
	if (nDrvIdx < 0) {
		FBAPopupAddText(PUF_TEXT_DEFAULT, _T("Console ROM file:\n\n"));
		FBAPopupAddText(PUF_TEXT_DEFAULT, _T("Failed to create driver for: %s"), pszPath);
		FBAPopupDisplay(PUF_TYPE_ERROR);
		return -1;
	}

#ifdef _UNICODE
	ConsoleRunSetFullNameW(&info);					// Unicode title (database name may be non-English)
#endif

	// Keep the availability array in step with the extended driver list
	if (gameAv != NULL) {
		char* pNew = (char*)realloc(gameAv, nBurnDrvCount);
		if (pNew != NULL) {
			gameAv = pNew;
			gameAv[nBurnDrvCount - 1] = 0;
		}
	}

	TCHAR szDir[MAX_PATH];
	_tcscpy(szDir, pszPath);
	{
		TCHAR* p = _tcsrchr(szDir, _T('\\'));
		TCHAR* q = _tcsrchr(szDir, _T('/'));
		if (q != NULL && (p == NULL || q > p)) {
			p = q;
		}
		if (p == NULL) {
			_tcscpy(szDir, _T(".\\"));
		} else {
			p[1] = 0;
		}
	}

	_tcscpy(szAppQuickPath, szDir);
	nQuickOpen = 5;

	QuitGame();									// Exit whatever is running (saves its RAM) before switching

	nDialogSelect = nOldDlgSelected = nBurnDrvActive = nDrvIdx;
	bLoading = 1;
	SplashDestroy(1);
	StopReplay();
	DrvInit(nDrvIdx, bSramLoad);
	MenuEnableItems();
	bAltPause = 0;
	AudSoundPlay();
	bLoading = 0;

	// The file was consumed by BzipOpen during DrvInit; clear the quick path
	nQuickOpen = 0;
	memset(szAppQuickPath, 0, sizeof(szAppQuickPath));

	return nDrvIdx;
}

void ConsoleRomOpenDialog()
{
	TCHAR szFilter[128] = _T("Console ROM files (*.gba;*.zip;*.7z)\0*.gba;*.zip;*.7z\0All files (*.*)\0*.*\0\0");
	TCHAR szSelect[MAX_PATH] = { 0 };
	OPENFILENAME ofn;

	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize     = sizeof(OPENFILENAME);
	ofn.hwndOwner       = hScrnWnd;
	ofn.lpstrFilter     = szFilter;
	ofn.lpstrFile       = szSelect;
	ofn.nMaxFile        = MAX_PATH;
	ofn.lpstrInitialDir = _T(".");
	ofn.Flags           = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

	if (GetOpenFileName(&ofn)) {
		ConsoleRomOpenFile(szSelect);
	}
}
