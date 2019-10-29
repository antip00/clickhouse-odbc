#define POCO_NO_UNWINDOWS

#include "driver/platform/platform.h"
#include "driver/platform/win/resource.h"
#include "driver/utils/utils.h"
#include "driver/config/config.h"

#if defined(_win_)

#    include <algorithm>
#    include <strsafe.h>

#    include <odbcinst.h>
#    pragma comment(lib, "odbc32.lib")
#    pragma comment(lib, "legacy_stdio_definitions.lib")

#    ifndef INTFUNC
#        define INTFUNC __stdcall
#    endif /* INTFUNC */

/// Saved module handle.
extern HINSTANCE module_instance;

namespace {

#    define MAXPGPATH 1024
/// Max keyword length
#    define MAXKEYLEN (32 + 1)
/// Max data source name length
#    define MAXDSNAME (32 + 1)

#    define ABBR_PROTOCOL "A1"
#    define ABBR_READONLY "A0"

/* NOTE:  All these are used by the dialog procedures */
struct SetupDialogData {
    HWND hwnd_parent;    /// Parent window handle
    LPCTSTR driver_name; /// Driver description
    ConnInfo ci;
    TCHAR dsn[MAXDSNAME]; /// Original data source name
    bool is_new_dsn;      /// New data source flag
    bool is_default;      /// Default data source flag
};

BOOL copyAttributes(ConnInfo * ci, LPCTSTR attribute, LPCTSTR value) {
    MYTCHAR ini_name[MEDIUM_REGISTRY_LEN] = {};

#define COPY_ATTR_IF(NAME, INI_NAME)             \
	{                                            \
		stringToTCHAR(INI_NAME, ini_name);       \
	    if (stricmp(attribute, ini_name) == 0) { \
			strcpy(ci->NAME, value);             \
		    return TRUE;                         \
		}                                        \
	}

	COPY_ATTR_IF(drivername, INI_DRIVER);
    COPY_ATTR_IF(dsn,        INI_DSN);
    COPY_ATTR_IF(desc,       INI_DESC);
    COPY_ATTR_IF(url,        INI_URL);
    COPY_ATTR_IF(server,     INI_SERVER);
    COPY_ATTR_IF(port,       INI_PORT);
    COPY_ATTR_IF(username,   INI_USERNAME);
    COPY_ATTR_IF(username,   INI_UID);
    COPY_ATTR_IF(password,   INI_PASSWORD);
    COPY_ATTR_IF(password,   INI_PWD);
    COPY_ATTR_IF(timeout,    INI_TIMEOUT);
    COPY_ATTR_IF(sslmode,    INI_SSLMODE);
    COPY_ATTR_IF(database,   INI_DATABASE);
    COPY_ATTR_IF(onlyread,   INI_READONLY);
    COPY_ATTR_IF(onlyread,   ABBR_READONLY);

#undef COPY_ATTR_IF

	return FALSE;
}

static void parseAttributes(LPCTSTR lpszAttributes, SetupDialogData * lpsetupdlg) {
    LPCTSTR lpsz;
    LPCTSTR lpszStart;
    TCHAR aszKey[MAXKEYLEN];
    int cbKey;
    TCHAR value[MAXPGPATH];

    for (lpsz = lpszAttributes; *lpsz; lpsz++) {
        /*
        * Extract key name (e.g., DSN), it must be terminated by an
        * equals
        */
        lpszStart = lpsz;
        for (;; lpsz++) {
            if (!*lpsz)
                return; /* No key was found */
            else if (*lpsz == '=')
                break; /* Valid key found */
        }
        /* Determine the key's index in the key table (-1 if not found) */
        cbKey = lpsz - lpszStart;
        if (cbKey < sizeof(aszKey)) {
            memcpy(aszKey, lpszStart, cbKey * sizeof(TCHAR));
            aszKey[cbKey] = '\0';
        }

        /* Locate end of key value */
        lpszStart = ++lpsz;
        for (; *lpsz; lpsz++)
            ;

        /* lpsetupdlg->aAttr[iElement].fSupplied = TRUE; */
        memcpy(value, lpszStart, std::min<size_t>(lpsz - lpszStart + 1, MAXPGPATH) * sizeof(TCHAR));

        /* Copy the appropriate value to the conninfo  */
        copyAttributes(&lpsetupdlg->ci, aszKey, value);

        //if (!copyAttributes(&lpsetupdlg->ci, aszKey, value))
        //    copyCommonAttributes(&lpsetupdlg->ci, aszKey, value);
    }
}

static bool setDSNAttributes(HWND hwndParent, SetupDialogData * lpsetupdlg, DWORD * errcode) {
    /// Pointer to data source name
    LPCTSTR lpszDSN = lpsetupdlg->ci.dsn;

    if (errcode)
        *errcode = 0;

    /* Validate arguments */
    if (lpsetupdlg->is_new_dsn && !*lpsetupdlg->ci.dsn)
        return FALSE;
    if (!SQLValidDSN(lpszDSN))
        return FALSE;

    /* Write the data source name */
    if (!SQLWriteDSNToIni(lpszDSN, lpsetupdlg->driver_name)) {
        RETCODE ret = SQL_ERROR;
        DWORD err = SQL_ERROR;
        TCHAR szMsg[SQL_MAX_MESSAGE_LENGTH];

        ret = SQLInstallerError(1, &err, szMsg, sizeof(szMsg), NULL);
        if (hwndParent) {
            if (SQL_SUCCESS != ret)
                MessageBox(hwndParent, szMsg, TEXT("Bad DSN configuration"), MB_ICONEXCLAMATION | MB_OK);
        }
        if (errcode)
            *errcode = err;
        return FALSE;
    }

    /* Update ODBC.INI */
    //writeDriverCommoninfo(ODBC_INI, lpsetupdlg->ci.dsn, &(lpsetupdlg->ci.drivers));
    writeDSNinfo(&lpsetupdlg->ci);

    /* If the data source name has changed, remove the old name */
    if (lstrcmpi(lpsetupdlg->dsn, lpsetupdlg->ci.dsn))
        SQLRemoveDSNFromIni(lpsetupdlg->dsn);

    return TRUE;
}

} // namespace

extern "C" {

void INTFUNC CenterDialog(HWND hdlg) {
    HWND hwndFrame;
    RECT rcDlg;
    RECT rcScr;
    RECT rcFrame;
    int cx;
    int cy;

    hwndFrame = GetParent(hdlg);

    GetWindowRect(hdlg, &rcDlg);
    cx = rcDlg.right - rcDlg.left;
    cy = rcDlg.bottom - rcDlg.top;

    GetClientRect(hwndFrame, &rcFrame);
    ClientToScreen(hwndFrame, (LPPOINT)(&rcFrame.left));
    ClientToScreen(hwndFrame, (LPPOINT)(&rcFrame.right));
    rcDlg.top = rcFrame.top + (((rcFrame.bottom - rcFrame.top) - cy) >> 1);
    rcDlg.left = rcFrame.left + (((rcFrame.right - rcFrame.left) - cx) >> 1);
    rcDlg.bottom = rcDlg.top + cy;
    rcDlg.right = rcDlg.left + cx;

    GetWindowRect(GetDesktopWindow(), &rcScr);
    if (rcDlg.bottom > rcScr.bottom) {
        rcDlg.bottom = rcScr.bottom;
        rcDlg.top = rcDlg.bottom - cy;
    }
    if (rcDlg.right > rcScr.right) {
        rcDlg.right = rcScr.right;
        rcDlg.left = rcDlg.right - cx;
    }

    if (rcDlg.left < 0)
        rcDlg.left = 0;
    if (rcDlg.top < 0)
        rcDlg.top = 0;

    MoveWindow(hdlg, rcDlg.left, rcDlg.top, cx, cy, TRUE);
    return;
}

INT_PTR CALLBACK ConfigDlgProc(HWND hdlg, UINT wMsg, WPARAM wParam, LPARAM lParam) {
    SetupDialogData * lpsetupdlg;
    ConnInfo * ci;
    //char strbuf[64];

    switch (wMsg) {
        case WM_INITDIALOG: {
            lpsetupdlg = (SetupDialogData *)lParam;
            ci = &lpsetupdlg->ci;
            SetWindowLongPtr(hdlg, DWLP_USER, lParam);

            CenterDialog(hdlg); /* Center dialog */

            getDSNinfo(ci, false);

            SetDlgItemText(hdlg, IDC_DSN_NAME, ci->dsn);
            SetDlgItemText(hdlg, IDC_DESCRIPTION, ci->desc);
            SetDlgItemText(hdlg, IDC_URL, ci->url);
            SetDlgItemText(hdlg, IDC_SERVER_HOST, ci->server);
            SetDlgItemText(hdlg, IDC_SERVER_PORT, ci->port);
            SetDlgItemText(hdlg, IDC_DATABASE, ci->database);
            SetDlgItemText(hdlg, IDC_USER, ci->username);
            SetDlgItemText(hdlg, IDC_PASSWORD, ci->password);
            SetDlgItemText(hdlg, IDC_TIMEOUT, ci->timeout);
            SetDlgItemText(hdlg, IDC_SSLMODE, ci->sslmode);

            return TRUE; /* Focus was not set */
        }

        case WM_COMMAND:
            switch (const DWORD cmd = LOWORD(wParam)) {
                case IDOK:
                    lpsetupdlg = (SetupDialogData *)GetWindowLongPtr(hdlg, DWLP_USER);
                    ci = &lpsetupdlg->ci;

                    GetDlgItemText(hdlg, IDC_DSN_NAME, ci->dsn, sizeof(ci->dsn));
                    GetDlgItemText(hdlg, IDC_DESCRIPTION, ci->desc, sizeof(ci->desc));
                    GetDlgItemText(hdlg, IDC_URL, ci->url, sizeof(ci->url));
                    GetDlgItemText(hdlg, IDC_SERVER_HOST, ci->server, sizeof(ci->server));
                    GetDlgItemText(hdlg, IDC_SERVER_PORT, ci->port, sizeof(ci->port));
                    GetDlgItemText(hdlg, IDC_DATABASE, ci->database, sizeof(ci->database));
                    GetDlgItemText(hdlg, IDC_USER, ci->username, sizeof(ci->username));
                    GetDlgItemText(hdlg, IDC_PASSWORD, ci->password, sizeof(ci->password));
                    GetDlgItemText(hdlg, IDC_TIMEOUT, ci->timeout, sizeof(ci->timeout));
                    GetDlgItemText(hdlg, IDC_SSLMODE, ci->sslmode, sizeof(ci->sslmode));

                    /* Return to caller */
                case IDCANCEL:
                    EndDialog(hdlg, cmd);
                    return TRUE;
            }
            break;
    }

    /* Message not processed */
    return FALSE;
}

BOOL CALLBACK FUNCTION_MAYBE_W(ConfigDSN)(HWND hwnd, WORD fRequest, LPCTSTR lpszDriver, LPCTSTR lpszAttributes) {
    BOOL fSuccess = FALSE;
    GLOBALHANDLE hglbAttr;
    SetupDialogData * lpsetupdlg;

    /* Allocate attribute array */
    hglbAttr = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(SetupDialogData));
    if (!hglbAttr)
        return FALSE;
    lpsetupdlg = (SetupDialogData *)GlobalLock(hglbAttr);
    /* Parse attribute string */
    if (lpszAttributes)
        parseAttributes(lpszAttributes, lpsetupdlg);

    /* Save original data source name */
    if (lpsetupdlg->ci.dsn[0])
        lstrcpy(lpsetupdlg->dsn, lpsetupdlg->ci.dsn);
    else
        lpsetupdlg->dsn[0] = '\0';

    /* Remove data source */
    if (ODBC_REMOVE_DSN == fRequest) {
        /* Fail if no data source name was supplied */
        if (!lpsetupdlg->ci.dsn[0])
            fSuccess = FALSE;

        /* Otherwise remove data source from ODBC.INI */
        else
            fSuccess = SQLRemoveDSNFromIni(lpsetupdlg->ci.dsn);
    }
    /* Add or Configure data source */
    else {
        /* Save passed variables for global access (e.g., dialog access) */
        lpsetupdlg->hwnd_parent = hwnd;
        lpsetupdlg->driver_name = lpszDriver;
        lpsetupdlg->is_new_dsn = (ODBC_ADD_DSN == fRequest);
        {
            MYTCHAR def_dsn[MEDIUM_REGISTRY_LEN] = {};
            stringToTCHAR(INI_DSN_DEFAULT, def_dsn);
            lpsetupdlg->is_default = !lstrcmpi(lpsetupdlg->ci.dsn, def_dsn);
        }

		/*
        * Display the appropriate dialog (if parent window handle
        * supplied)
        */
        if (hwnd) {
            /* Display dialog(s) */
            auto ret = DialogBoxParam(module_instance, MAKEINTRESOURCE(IDD_DIALOG1), hwnd, ConfigDlgProc, (LPARAM)lpsetupdlg);
            if (ret == IDOK) {
                fSuccess = setDSNAttributes(hwnd, lpsetupdlg, NULL);
            } else if (ret != IDCANCEL) {
                auto err = GetLastError();
                LPVOID lpMsgBuf;
                LPVOID lpDisplayBuf;

                FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL,
                    err,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    (LPTSTR)&lpMsgBuf,
                    0,
                    NULL);

                lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, (lstrlen((LPCTSTR)lpMsgBuf) + 40) * sizeof(TCHAR));
                StringCchPrintf(
                    (LPTSTR)lpDisplayBuf, LocalSize(lpDisplayBuf) / sizeof(TCHAR), TEXT("failed with error %d: %s"), err, lpMsgBuf);
                MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

                LocalFree(lpMsgBuf);
                LocalFree(lpDisplayBuf);
            }
        } else if (lpsetupdlg->ci.dsn[0])
            fSuccess = setDSNAttributes(hwnd, lpsetupdlg, NULL);
        else
            fSuccess = TRUE;
    }

    GlobalUnlock(hglbAttr);
    GlobalFree(hglbAttr);

    return fSuccess;
}

BOOL CALLBACK FUNCTION_MAYBE_W(ConfigDriver)(
	HWND hwnd,
	WORD fRequest,
	LPCTSTR lpszDriver,
	LPCTSTR lpszArgs,
	LPTSTR lpszMsg,
	WORD cbMsgMax,
	WORD * pcbMsgOut
) {
    MessageBox(hwnd, TEXT("ConfigDriver"), TEXT("Debug"), MB_OK);
    return TRUE;
}

} // extern

#endif