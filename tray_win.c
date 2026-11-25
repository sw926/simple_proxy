/* Windows 托盘外壳：Shell_NotifyIcon + 隐藏窗口 + 右键菜单。 */

#include <stdio.h>
#include <string.h>
#include "tray_win.h"
#include "proxy.h"
#include "config_win.h"
#include "filter.h"

#define WM_TRAY (WM_APP + 1)

#define IDM_STATUS    1000
#define IDM_EDIT      1001
#define IDM_RELOAD    1002
#define IDM_AUTOSTART 1003
#define IDM_QUIT      1004

static HWND   g_hwnd;
static HICON  g_icon;
static UINT   g_msg_taskbar_created;
static int    g_ok;              /* 代理是否在监听 */
static wchar_t g_status[128];

static CRITICAL_SECTION g_log_lock;

/* ---------- 日志：带时间戳追加到 exe 目录下的文件，超 1MB 截断 ---------- */

static void append_log_file(const wchar_t *fname, const char *line) {
    wchar_t path[MAX_PATH];
    FILE *f;
    SYSTEMTIME st;

    config_get_path(path, MAX_PATH, fname);
    EnterCriticalSection(&g_log_lock);
    f = _wfopen(path, L"a+");
    if (f) {
        fseek(f, 0, SEEK_END);
        if (ftell(f) > 1024 * 1024) {
            fclose(f);
            f = _wfopen(path, L"w");
        }
    }
    if (f) {
        GetLocalTime(&st);
        fprintf(f, "[%04u-%02u-%02u %02u:%02u:%02u] %s\n",
                (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
                line);
        fclose(f);
    }
    LeaveCriticalSection(&g_log_lock);
}

/* 错误/诊断日志 → pytest.log */
static void log_to_file(const char *line) {
    append_log_file(L"pytest.log", line);
}

/* 访问日志（config.ini: log=1 时开启）→ access.log */
static void access_log_cb(const char *line) {
    append_log_file(L"access.log", line);
}

/* ---------- 应用配置：访问日志开关 + 域名过滤 ---------- */

static void apply_config(app_config *cfg) {
    proxy_set_access_log(cfg->log_enabled ? access_log_cb : NULL);
    filter_set_mode(cfg->filter_mode);
    filter_clear();
    if (cfg->filter_mode != FILTER_OFF) {
        int n = filter_load_file(cfg->filter_file);
        if (n < 0) {
            log_to_file("域名列表文件打开失败");
        } else {
            char b[96];
            snprintf(b, sizeof b, "域名过滤开启(%s)，载入 %d 条",
                     cfg->filter_mode == FILTER_BLACKLIST ? "黑名单" : "白名单", n);
            log_to_file(b);
        }
        proxy_set_filter(filter_blocked);
    } else {
        proxy_set_filter(NULL);
    }
}

/* ---------- 托盘通知 ---------- */

static void notify(int msg, const wchar_t *title, const wchar_t *info, DWORD flags) {
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof nid);
    nid.cbSize = sizeof nid;
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = g_icon;
    lstrcpynW(nid.szTip, L"pytest", 64);
    if (info) {
        nid.uFlags |= NIF_INFO;
        lstrcpynW(nid.szInfoTitle, title ? title : L"pytest", 64);
        lstrcpynW(nid.szInfo, info, 256);
        nid.dwInfoFlags = flags;
        nid.uTimeout = 5000;
    }
    Shell_NotifyIconW(msg, &nid);
}

static void set_status(int ok, const char *err_utf8) {
    wchar_t errw[128];
    g_ok = ok;
    if (ok) {
        _snwprintf(g_status, 127, L"运行中  127.0.0.1:%d", proxy_port());
    } else if (err_utf8 && MultiByteToWideChar(CP_UTF8, 0, err_utf8, -1, errw, 128)) {
        _snwprintf(g_status, 127, L"启动失败：%s", errw);
    } else {
        lstrcpynW(g_status, L"启动失败", 128);
    }
    g_status[127] = 0;
}

static void do_reload(void) {
    char err[160];
    app_config cfg;

    proxy_stop();
    config_load(&cfg);
    apply_config(&cfg);
    if (proxy_start(cfg.port, err, sizeof err) == 0) {
        set_status(1, NULL);
        notify(NIM_MODIFY, L"pytest", L"配置已重新加载", NIIF_INFO);
    } else {
        set_status(0, err);
        notify(NIM_MODIFY, L"pytest", L"重新加载失败：端口可能被占用", NIIF_WARNING);
    }
}

static void show_menu(void) {
    HMENU m = CreatePopupMenu();
    POINT pt;
    if (!m) return;
    AppendMenuW(m, MF_STRING | MF_DISABLED | MF_GRAYED, IDM_STATUS, g_status);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_EDIT, L"编辑配置");
    AppendMenuW(m, MF_STRING, IDM_RELOAD, L"重新加载配置");
    AppendMenuW(m, MF_STRING | (config_autostart_enabled() ? MF_CHECKED : 0),
                IDM_AUTOSTART, L"开机自启动");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_QUIT, L"退出");

    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd); /* 保证菜单可在点击外部时正常关闭 */
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(m);
}

/* ---------- 窗口过程 ---------- */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_msg_taskbar_created && msg == g_msg_taskbar_created && g_icon) {
        notify(NIM_ADD, NULL, NULL, 0); /* explorer 重启后补回图标 */
        return 0;
    }
    switch (msg) {
    case WM_TRAY:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU || lp == WM_LBUTTONDBLCLK)
            show_menu();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_EDIT: {
            wchar_t path[MAX_PATH];
            config_get_path(path, MAX_PATH, L"config.ini");
            ShellExecuteW(NULL, L"open", L"notepad.exe", path, NULL, SW_SHOWNORMAL);
            break;
        }
        case IDM_RELOAD:
            do_reload();
            break;
        case IDM_AUTOSTART:
            config_autostart_set(!config_autostart_enabled());
            break;
        case IDM_QUIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    case WM_DESTROY:
        notify(NIM_DELETE, NULL, NULL, 0);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------- 图标：运行时画一个 32x32 像素图标，免资源文件 ---------- */

static HICON make_icon(HINSTANCE hInst) {
    enum { IW = 32, IH = 32 };
    static BYTE and_bits[IH * 4];       /* 全 0：显示交给 alpha 通道 */
    static BYTE xor_bits[IH * IW * 4];  /* DIB 行序自下而上 */
    static const BYTE bg[4] = { 0xEB, 0x63, 0x25, 0xFF }; /* BGRA #2563EB */
    static const BYTE fg[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    int x, y;

    memset(xor_bits, 0, sizeof xor_bits);

    /* 深蓝圆角底 */
    for (y = 0; y < IH; y++) {
        for (x = 0; x < IW; x++) {
            int dx = x < (IW - 1 - x) ? x : (IW - 1 - x);
            int dy = y < (IH - 1 - y) ? y : (IH - 1 - y);
            int inside;
            BYTE *px;
            if (dx >= 6 || dy >= 6)
                inside = 1;
            else
                inside = (6 - dx) * (6 - dx) + (6 - dy) * (6 - dy) <= 36;
            if (!inside) continue;
            px = &xor_bits[(IH - 1 - y) * (IW * 4) + x * 4];
            memcpy(px, bg, 4);
        }
    }

#define PLOT(px, py) \
    memcpy(&xor_bits[(IH - 1 - (py)) * (IW * 4) + (px) * 4], fg, 4)

    /* 白色双向箭头（向右） */
    for (x = 6; x <= 21; x++) { PLOT(x, 10); PLOT(x, 11); PLOT(x, 12); }
    for (x = 0; x < 5; x++) {
        int half = 4 - x;
        for (y = 11 - half; y <= 11 + half; y++) PLOT(22 + x, y);
    }
    /* 白色双向箭头（向左） */
    for (x = 10; x <= 25; x++) { PLOT(x, 19); PLOT(x, 20); PLOT(x, 21); }
    for (x = 0; x < 5; x++) {
        int half = 4 - x;
        for (y = 20 - half; y <= 20 + half; y++) PLOT(9 - x, y);
    }

#undef PLOT

    return CreateIcon(hInst, IW, IH, 1, 32, and_bits, xor_bits);
}

/* ---------- 主流程 ---------- */

int tray_run(HINSTANCE hInst) {
    HANDLE mutex;
    WNDCLASSW wclass;
    MSG msg;
    char err[160];
    app_config cfg;
    HICON icon;

    /* 单实例 */
    mutex = CreateMutexW(NULL, TRUE, L"Local\\pytest_SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"pytest 已在运行，请查看系统托盘。",
                    L"pytest", MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 0;
    }

    if (nc_init() != 0) {
        MessageBoxW(NULL, L"Winsock 初始化失败。", L"pytest", MB_ICONERROR);
        if (mutex) CloseHandle(mutex);
        return 1;
    }

    InitializeCriticalSection(&g_log_lock);
    proxy_set_log(log_to_file);
    filter_init();

    config_load(&cfg);
    apply_config(&cfg);
    if (proxy_start(cfg.port, err, sizeof err) == 0)
        set_status(1, NULL);
    else
        set_status(0, err);

    g_msg_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

    memset(&wclass, 0, sizeof wclass);
    wclass.lpfnWndProc = WndProc;
    wclass.hInstance = hInst;
    wclass.lpszClassName = L"pytestWnd";
    RegisterClassW(&wclass);
    g_hwnd = CreateWindowExW(0, L"pytestWnd", L"pytest",
                             WS_OVERLAPPED, 0, 0, 0, 0,
                             NULL, NULL, hInst, NULL);

    icon = make_icon(hInst);
    g_icon = icon ? icon : LoadIconW(NULL, MAKEINTRESOURCEW(IDI_APPLICATION));

    notify(NIM_ADD, NULL, NULL, 0);
    if (!g_ok)
        notify(NIM_MODIFY, L"pytest",
               L"启动失败，请右键托盘图标重试", NIIF_WARNING);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    proxy_stop();
    if (g_icon) DestroyIcon(g_icon);
    DeleteCriticalSection(&g_log_lock);
    nc_cleanup();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
