/* CI 测试入口：控制台版代理（仅 Windows），供自动化测试驱动。
 * 用法: test_proxy.exe [port] [off|blacklist|whitelist] [列表文件] [访问日志文件]
 * 运行 120 秒后自动退出。 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "proxy.h"
#include "filter.h"

static FILE *g_acc;

static void access_writer(const char *line) {
    if (g_acc) {
        fputs(line, g_acc);
        fputc('\n', g_acc);
        fflush(g_acc);
    }
}

static void log_stderr(const char *line) {
    fprintf(stderr, "[proxy] %s\n", line);
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : PROXY_DEFAULT_PORT;
    const char *mode_s = argc > 2 ? argv[2] : "off";
    const char *list = argc > 3 ? argv[3] : NULL;
    const char *acc = argc > 4 ? argv[4] : NULL;
    char err[160];

    if (port < 1 || port > 65535) {
        fprintf(stderr, "usage: test_proxy.exe [port] [mode] [listfile] [accesslog]\n");
        return 1;
    }
    if (nc_init() != 0) return 1;

    filter_init();
    if (acc) {
        g_acc = fopen(acc, "w");
        proxy_set_access_log(access_writer);
    }
    if (list && strcmp(mode_s, "off") != 0) {
        wchar_t wpath[1024];
        int n;
        filter_set_mode(strcmp(mode_s, "blacklist") == 0 ? FILTER_BLACKLIST
                        : strcmp(mode_s, "whitelist") == 0 ? FILTER_WHITELIST
                                                           : FILTER_OFF);
        filter_clear();
        MultiByteToWideChar(CP_UTF8, 0, list, -1, wpath, 1024);
        n = filter_load_file(wpath);
        fprintf(stderr, "filter mode=%s loaded=%d\n", mode_s, n);
        if (n >= 0) proxy_set_filter(filter_blocked);
    }

    proxy_set_log(log_stderr);
    if (proxy_start(port, err, sizeof err) != 0) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    fprintf(stderr, "test_proxy listening on 127.0.0.1:%d\n", port);
    fflush(stderr);

    Sleep(120000);

    proxy_stop();
    nc_cleanup();
    return 0;
}
