/* 入口：Windows 走托盘外壳；其他平台提供无界面入口（CI 测试用）。 */

#ifdef _WIN32

#include "tray_win.h"

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)hPrev;
    (void)cmdLine;
    (void)nShow;
    return tray_run(hInst);
}

#else /* 无界面测试模式：./simple_proxy [port] */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "proxy.h"

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static void log_stderr(const char *line) {
    fprintf(stderr, "[proxy] %s\n", line);
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : PROXY_DEFAULT_PORT;
    char err[160];

    if (port < 1 || port > 65535) {
        fprintf(stderr, "用法: %s [port 1-65535]\n", argv[0]);
        return 1;
    }
    if (nc_init() != 0)
        return 1;
    proxy_set_log(log_stderr);
    if (proxy_start(port, err, sizeof err) != 0) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    fprintf(stderr, "simple_proxy 监听 http://127.0.0.1:%d（Ctrl-C 退出）\n", port);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    while (!g_stop)
        pause();

    proxy_stop();
    nc_cleanup();
    return 0;
}

#endif
