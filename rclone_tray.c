#include <windows.h>
#include <shellapi.h>

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_OPEN     1001
#define ID_TRAY_UNMOUNT  1002
#define ID_TRAY_REMOUNT  1003
#define ID_TRAY_EXIT     1004

#define IDI_MAIN_ICON    101   // 在 rc 里用同一个 ID

// ============ 用户配置 ============
// rclone.exe 路径
static const wchar_t *RCLONE_PATH  = L"C:\\Program Files\\rclone\\rclone.exe";
// remote 名
static const wchar_t *REMOTE_NAME  = L"openlist:";
// 挂载盘符
static const wchar_t *MOUNT_POINT  = L"Z:";   // 只写 Z: 即可
// ============ 用户配置结束 ========

static NOTIFYICONDATAW g_nid;
static PROCESS_INFORMATION g_pi = {0};

void StopRcloneMount(void);

void StartRcloneMount(void)
{
    // 如已有我们自己启动的 rclone，在这里先停掉
    if (g_pi.hProcess != NULL) {
        TerminateProcess(g_pi.hProcess, 0);
        CloseHandle(g_pi.hProcess);
        CloseHandle(g_pi.hThread);
        ZeroMemory(&g_pi, sizeof(g_pi));
    }

    // 组合挂载命令行
    wchar_t cmd[1024];
    wsprintfW(cmd,
              L"\"%s\" mount %s %s "
              L"--vfs-cache-mode full "
              L"--vfs-cache-max-size 10G "
              L"--vfs-cache-max-age 24h "
              L"--dir-cache-time 24h "
              L"--poll-interval 0 "
              L"--network-mode",
              RCLONE_PATH, REMOTE_NAME, MOUNT_POINT);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    if (!CreateProcessW(
            NULL,
            cmd,
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &si,
            &g_pi)) {
        ZeroMemory(&g_pi, sizeof(g_pi));
    }
}

// 尽可能把 Z: 盘挂载断掉，但不退出托盘程序
void StopRcloneMount(void)
{
    // 1) 尝试停止 WinFsp 启动的 rclone 服务：net stop rclone
    {
        wchar_t cmd1[] = L"cmd.exe /c net stop rclone";
        STARTUPINFOW si1;
        PROCESS_INFORMATION pi1;
        ZeroMemory(&si1, sizeof(si1));
        ZeroMemory(&pi1, sizeof(pi1));
        si1.cb = sizeof(si1);

        if (CreateProcessW(
                NULL,
                cmd1,
                NULL,
                NULL,
                FALSE,
                CREATE_NO_WINDOW,
                NULL,
                NULL,
                &si1,
                &pi1)) {
            CloseHandle(pi1.hThread);
            WaitForSingleObject(pi1.hProcess, 10000);
            CloseHandle(pi1.hProcess);
        }
    }

    // 2) 再把当前我们自己启动的 rclone 进程关掉
    if (g_pi.hProcess != NULL) {
        TerminateProcess(g_pi.hProcess, 0);
        CloseHandle(g_pi.hProcess);
        CloseHandle(g_pi.hThread);
        ZeroMemory(&g_pi, sizeof(g_pi));
    }

    // 3) 兜底：taskkill /IM rclone.exe /F，防止还有残留
    {
        wchar_t cmd2[] = L"cmd.exe /c taskkill /IM rclone.exe /F >NUL 2>&1";
        STARTUPINFOW si2;
        PROCESS_INFORMATION pi2;
        ZeroMemory(&si2, sizeof(si2));
        ZeroMemory(&pi2, sizeof(pi2));
        si2.cb = sizeof(si2);

        if (CreateProcessW(
                NULL,
                cmd2,
                NULL,
                NULL,
                FALSE,
                CREATE_NO_WINDOW,
                NULL,
                NULL,
                &si2,
                &pi2)) {
            CloseHandle(pi2.hThread);
            WaitForSingleObject(pi2.hProcess, 10000);
            CloseHandle(pi2.hProcess);
        }
    }
}

void OpenMountPoint(void)
{
    wchar_t path[16];
    wsprintfW(path, L"%s\\", MOUNT_POINT);
    ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);

            HMENU hMenu = CreatePopupMenu();
            if (hMenu) {
                InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_OPEN,    L"Open Z:");
                InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_UNMOUNT, L"Unmount");
                InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_REMOUNT, L"Remount");
                InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT,    L"Exit");

                SetForegroundWindow(hWnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                               pt.x, pt.y, 0, hWnd, NULL);
                DestroyMenu(hMenu);
            }
        } else if (lParam == WM_LBUTTONDBLCLK) {
            OpenMountPoint();
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_OPEN:
            OpenMountPoint();
            break;

        case ID_TRAY_UNMOUNT:
            // 只断开挂载，托盘程序继续运行
            StopRcloneMount();
            break;

        case ID_TRAY_REMOUNT:
            StopRcloneMount();
            StartRcloneMount();
            break;

        case ID_TRAY_EXIT:
            // 退出前先断开挂载
            StopRcloneMount();
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            break;
        }
        break;

    case WM_DESTROY:
        // 异常退出时也顺手清图标 + 断开挂载
        StopRcloneMount();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    // 注册隐藏窗口类
    WNDCLASSEXW wcex;
    ZeroMemory(&wcex, sizeof(wcex));
    wcex.cbSize        = sizeof(wcex);
    wcex.lpfnWndProc   = WndProc;
    wcex.hInstance     = hInstance;
    wcex.lpszClassName = L"RcloneTrayClass";
    wcex.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wcex.hIconSm       = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));

    if (!RegisterClassExW(&wcex)) {
        return 1;
    }

    HWND hWnd = CreateWindowExW(
        0,
        wcex.lpszClassName,
        L"RcloneTray",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
        NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        return 1;
    }

    ShowWindow(hWnd, SW_HIDE);
    UpdateWindow(hWnd);

    // 添加托盘图标
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hWnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    lstrcpyW(g_nid.szTip, L"rclone Z: tray");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // 启动挂载
    StartRcloneMount();

    // 消息循环
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
