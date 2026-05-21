// ==WindhawkMod==
// @id           legacy-sound-flyout
// @name         Legacy Volume Tray Button
// @description  Enables legacy sound volume flyout on Win10 taskbar
// @version      1.0.0
// @author       Lenom3242
// @github       https://github.com/lenom3242
// @compilerOptions -lcomctl32 -lpsapi -lshell32 -luser32
// @include      explorer.exe
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
This mod adds a custom legacy volume tray icon to the taskbar.
Left-clicking the icon opens the classic volume mixer.
Right-clicking the icon opens a context menu with options to launch the volume mixer or sound settings.
*/
// ==/WindhawkModReadme==

#include <windhawk_utils.h>
#include <windows.h>
#include <shellapi.h>

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_VOLUME_MIXER 2001
#define ID_TRAY_SOUND_SETTINGS 2002

static DWORD g_lastLaunchTick = 0;
static HWND g_hTrayWnd = nullptr;
static HANDLE g_hTrayThread = nullptr;
static NOTIFYICONDATAW g_nid = {0};
static UINT g_taskbarCreatedMsg = 0;

// -------------------------------------------------------
// Process check
// -------------------------------------------------------

static bool IsMainExplorerProcess() {
    // 1. Check command line arguments for separate/factory process flags
    LPWSTR cmdLine = GetCommandLineW();
    if (cmdLine) {
        if (wcsstr(cmdLine, L"/separate") || wcsstr(cmdLine, L"/factory")) {
            return false;
        }
    }
    
    // 2. If Shell window exists, check if it belongs to us
    HWND hShellWnd = GetShellWindow();
    if (hShellWnd) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hShellWnd, &pid);
        if (pid != GetCurrentProcessId()) {
            return false;
        }
    }
    
    // 3. If Tray window exists, check if it belongs to us
    HWND hTrayWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (hTrayWnd) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hTrayWnd, &pid);
        if (pid != GetCurrentProcessId()) {
            return false;
        }
    }
    
    return true;
}

// -------------------------------------------------------
// sndvol
// -------------------------------------------------------

static DWORD WINAPI LaunchSndvolThread(LPVOID lpParam) {
    POINT pt = *(POINT*)lpParam;
    delete (POINT*)lpParam;
    
    wchar_t sys32[MAX_PATH]{};
    GetSystemDirectoryW(sys32, MAX_PATH);
    
    wchar_t cmdLine[1024]{};
    DWORD encoded = (DWORD)MAKELONG(pt.x, pt.y);
    wsprintfW(cmdLine, L"\"%s\\SndVol.exe\" -t %u", sys32, encoded);
    
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_FORCEOFFFEEDBACK;
    PROCESS_INFORMATION pi{};
    
    if (CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE, 
                       0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    return 0;
}

static void LaunchClassicVolume() {
    DWORD now = GetTickCount();
    if (now - g_lastLaunchTick < 400) return;
    g_lastLaunchTick = now;
    
    POINT* pt = new POINT;
    GetCursorPos(pt);
    
    HANDLE hThread = CreateThread(nullptr, 0, LaunchSndvolThread, pt, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete pt;
    }
}

static void LaunchSoundSettings() {
    ShellExecuteW(nullptr, L"open", L"ms-settings:sound", nullptr, nullptr, SW_SHOWNORMAL);
}

// -------------------------------------------------------
// WndProc for tray icon
// -------------------------------------------------------

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TRAYICON) {
        if (LOWORD(lParam) == WM_LBUTTONUP) {
            LaunchClassicVolume();
            return 0;
        }
        else if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            
            HMENU hMenu = CreatePopupMenu();
            if (hMenu) {
                InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_VOLUME_MIXER, L"Volume Mixer");
                InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_SOUND_SETTINGS, L"Sound Settings");
                
                SetForegroundWindow(hwnd);
                
                int trackResult = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, 
                                                 pt.x, pt.y, 0, hwnd, nullptr);
                
                PostMessageW(hwnd, WM_NULL, 0, 0);
                DestroyMenu(hMenu);
                
                if (trackResult == ID_TRAY_VOLUME_MIXER) {
                    LaunchClassicVolume();
                } else if (trackResult == ID_TRAY_SOUND_SETTINGS) {
                    LaunchSoundSettings();
                }
            }
            return 0;
        }
    }
    else if (msg == g_taskbarCreatedMsg && g_taskbarCreatedMsg != 0) {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        return 0;
    }
    else if (msg == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// -------------------------------------------------------
// Thread proc
// -------------------------------------------------------

static DWORD WINAPI TrayThreadProc(LPVOID lpParam) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LegacyVolumeTrayIconWindowClass";
    
    if (!RegisterClassExW(&wc)) {
        return 0;
    }
    
    g_hTrayWnd = CreateWindowExW(0, wc.lpszClassName, L"Legacy Volume Tray Window", 
                                 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!g_hTrayWnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 0;
    }
    
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hTrayWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    
    wchar_t sys32[MAX_PATH]{};
    GetSystemDirectoryW(sys32, MAX_PATH);
    wchar_t sndvolPath[MAX_PATH]{};
    wsprintfW(sndvolPath, L"%s\\sndvol.exe", sys32);
    
    HICON hIcon = ExtractIconW(wc.hInstance, sndvolPath, 0);
    if (hIcon && hIcon != (HICON)1) {
        g_nid.hIcon = hIcon;
    } else {
        g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    
    wcscpy_s(g_nid.szTip, L"Volume");
    
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_nid.hIcon) {
        DestroyIcon(g_nid.hIcon);
    }
    
    DestroyWindow(g_hTrayWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    
    return 0;
}

// -------------------------------------------------------
// Init / Uninit
// -------------------------------------------------------

BOOL Wh_ModInit() {
    if (!IsMainExplorerProcess()) {
        Wh_Log(L"Skipping initialization for secondary explorer.exe process");
        return TRUE;
    }

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    
    g_hTrayThread = CreateThread(nullptr, 0, TrayThreadProc, nullptr, 0, nullptr);
    if (!g_hTrayThread) {
        return FALSE;
    }
    
    Wh_Log(L"Initialized");
    return TRUE;
}

void Wh_ModUninit() {
    if (g_hTrayWnd) {
        PostMessageW(g_hTrayWnd, WM_CLOSE, 0, 0);
    }
    if (g_hTrayThread) {
        WaitForSingleObject(g_hTrayThread, INFINITE);
        CloseHandle(g_hTrayThread);
        g_hTrayThread = nullptr;
    }
    Wh_Log(L"Uninitialized");
}