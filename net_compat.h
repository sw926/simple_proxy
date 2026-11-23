#ifndef NET_COMPAT_H
#define NET_COMPAT_H

/* 本应用仅支持 Windows。 */

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

/* 连接 host:port（阻塞接口、内部限时），失败返回 NC_INVALID */
nc_socket nc_connect_host(const char *host, const char *port);

#endif /* NET_COMPAT_H */
