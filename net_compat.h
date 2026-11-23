#ifndef NET_COMPAT_H
#define NET_COMPAT_H

/* Winsock 与 POSIX socket 的最小兼容层，让代理核心代码可以在
 * Windows 与 macOS/Linux 上编译运行（后者用于 CI 里做无界面测试）。 */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

typedef SOCKET nc_socket;
#define NC_INVALID INVALID_SOCKET

static inline int nc_init(void) {
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0 ? 0 : -1;
}
static inline void nc_cleanup(void) { WSACleanup(); }
static inline void nc_close(nc_socket s) { closesocket(s); }
static inline void nc_shut_wr(nc_socket s) { shutdown(s, SD_SEND); }
static inline int nc_last_error(void) { return (int)WSAGetLastError(); }
static inline int nc_eagain(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
static inline int nc_connect_pending(void) {
    return WSAGetLastError() == WSAEWOULDBLOCK;
}
static inline int nc_set_nonblock(nc_socket s, int on) {
    u_long mode = on ? 1 : 0;
    return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
}
static inline void nc_set_timeo(nc_socket s, int rcv_sec, int snd_sec) {
    DWORD r = (DWORD)rcv_sec * 1000, w = (DWORD)snd_sec * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&r, (int)sizeof r);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&w, (int)sizeof w);
}

#else /* POSIX */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

typedef int nc_socket;
#define NC_INVALID (-1)

static inline int nc_init(void) { return 0; }
static inline void nc_cleanup(void) {}
static inline void nc_close(nc_socket s) { close(s); }
static inline void nc_shut_wr(nc_socket s) { shutdown(s, SHUT_WR); }
static inline int nc_last_error(void) { return errno; }
static inline int nc_eagain(void) { return errno == EAGAIN || errno == EWOULDBLOCK; }
static inline int nc_connect_pending(void) {
    return errno == EINPROGRESS || errno == EWOULDBLOCK;
}
static inline int nc_set_nonblock(nc_socket s, int on) {
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(s, F_SETFL, on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK)) == 0 ? 0 : -1;
}
static inline void nc_set_timeo(nc_socket s, int rcv_sec, int snd_sec) {
    struct timeval r, w;
    r.tv_sec = rcv_sec; r.tv_usec = 0;
    w.tv_sec = snd_sec; w.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &r, sizeof r);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &w, sizeof w);
}

#endif

/* 连接 host:port（阻塞接口、内部限时），失败返回 NC_INVALID */
nc_socket nc_connect_host(const char *host, const char *port);

#endif /* NET_COMPAT_H */
