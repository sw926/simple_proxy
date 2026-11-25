/* 配置文件（exe 同目录 config.ini）与开机自启注册表读写。 */

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "config_win.h"

static const wchar_t *RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t *RUN_NAME = L"pytest";

void config_get_path(wchar_t *path, DWORD cap, const wchar_t *name) {
    wchar_t *base;
    GetModuleFileNameW(NULL, path, cap);
    base = wcsrchr(path, L'\\');
    if (base) lstrcpyW(base + 1, name);
    else lstrcpyW(path, name);
}

static void create_template(const wchar_t *path) {
    static const char tpl[] =
        "; pytest 配置文件\r\n"
        "; port        = HTTP 代理监听端口 (1-65535)\r\n"
        "; log         = 是否记录访问日志: 0/1（写入 access.log）\r\n"
        "; filter      = 域名过滤: off / blacklist(黑名单) / whitelist(白名单)\r\n"
        "; filter_file = 域名列表文件（相对 exe 目录），每行一个域名，\r\n"
        ";               自动匹配其子域名，# 开头为注释\r\n"
        "; 修改保存后，右键托盘图标 - 重新加载配置 即可生效\r\n"
        "[proxy]\r\n"
        "port=8080\r\n"
        "log=0\r\n"
        "filter=off\r\n"
        "filter_file=domains.txt\r\n";
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    if (f == INVALID_HANDLE_VALUE) return;
    WriteFile(f, tpl, (DWORD)(sizeof tpl - 1), &written, NULL);
    CloseHandle(f);
}

/* ASCII 大小写不敏感比较（避免依赖 CRT 的 wcsicmp 变体） */
static int wcseqi(const wchar_t *a, const wchar_t *b) {
    while (*a && *b) {
        wchar_t ca = *a, cb = *b;
        if (ca >= L'A' && ca <= L'Z') ca += 32;
        if (cb >= L'A' && cb <= L'Z') cb += 32;
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

void config_load(app_config *cfg) {
    wchar_t path[MAX_PATH], raw[MAX_PATH], mode[32];

    config_get_path(path, MAX_PATH, L"config.ini");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        create_template(path);

    cfg->port = (int)GetPrivateProfileIntW(L"proxy", L"port", CONFIG_DEFAULT_PORT, path);
    if (cfg->port < 1 || cfg->port > 65535) cfg->port = CONFIG_DEFAULT_PORT;

    cfg->log_enabled = GetPrivateProfileIntW(L"proxy", L"log", 0, path) != 0;

    GetPrivateProfileStringW(L"proxy", L"filter", L"off", mode, 32, path);
    if (wcseqi(mode, L"blacklist"))       cfg->filter_mode = FILTER_BLACKLIST;
    else if (wcseqi(mode, L"whitelist"))  cfg->filter_mode = FILTER_WHITELIST;
    else                                  cfg->filter_mode = FILTER_OFF;

    GetPrivateProfileStringW(L"proxy", L"filter_file", L"domains.txt",
                             raw, MAX_PATH, path);
    if (raw[0])
        config_get_path(cfg->filter_file, MAX_PATH, raw);
    else
        cfg->filter_file[0] = 0;
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
