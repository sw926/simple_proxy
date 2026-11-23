/* CI 测试入口：控制台版代理（仅 Windows），供自动化测试驱动。
 * 用法：test_proxy.exe [port]，运行 120 秒后自动退出。 */

#include <stdio.h>
#include <stdlib.h>
#include "proxy.h"

static void log_stderr(const char *line) {
    fprintf(stderr, "[proxy] %s\n", line);
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : PROXY_DEFAULT_PORT;
    char err[160];

    if (port < 1 || port > 65535) {
        fprintf(stderr, "usage: test_proxy.exe [port 1-65535]\n");
        return 1;
    }
    if (nc_init() != 0) return 1;
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
