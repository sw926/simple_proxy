/* 配置文件（exe 同目录 config.ini）与开机自启注册表读写。 */

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "config_win.h"

static const wchar_t *RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t *RUN_NAME = L"SimpleProxy";

void config_get_path(wchar_t *path, DWORD cap, const wchar_t *name) {
    wchar_t *base;
    GetModuleFileNameW(NULL, path, cap);
    base = wcsrchr(path, L'\\');
    if (base) lstrcpyW(base + 1, name);
    else lstrcpyW(path, name);
}

static void create_template(const wchar_t *path) {
    static const char tpl[] =
        "; SimpleProxy 配置文件\r\n"
        "; port = HTTP 代理监听端口 (1-65535)\r\n"
        "; 修改保存后，右键托盘图标 - 重新加载配置 即可生效\r\n"
        "[proxy]\r\n"
        "port=8080\r\n";
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    if (f == INVALID_HANDLE_VALUE) return;
    WriteFile(f, tpl, (DWORD)(sizeof tpl - 1), &written, NULL);
    CloseHandle(f);
}

int config_load_port(void) {
    wchar_t path[MAX_PATH];
    int port;
    config_get_path(path, MAX_PATH, L"config.ini");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        create_template(path);
    port = (int)GetPrivateProfileIntW(L"proxy", L"port", CONFIG_DEFAULT_PORT, path);
    if (port < 1 || port > 65535) port = CONFIG_DEFAULT_PORT;
    return port;
}

int config_autostart_enabled(void) {
    HKEY k;
    LONG r;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return 0;
    r = RegQueryValueExW(k, RUN_NAME, NULL, NULL, NULL, NULL);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

void config_autostart_set(int enable) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    if (enable) {
        wchar_t exe[MAX_PATH], val[MAX_PATH + 4];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        _snwprintf(val, MAX_PATH + 3, L"\"%s\"", exe);
        val[MAX_PATH + 3] = 0;
        RegSetValueExW(k, RUN_NAME, 0, REG_SZ, (const BYTE *)val,
                       (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, RUN_NAME);
    }
    RegCloseKey(k);
}
