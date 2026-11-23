/* HTTP 正向代理核心：绝对 URI 请求直连转发 + CONNECT 隧道。
 * 每个连接一个线程，连接内部用 select 做双向字节泵，空闲时零开销。 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "proxy.h"

#include <process.h>

#define MAX_HEAD (32 * 1024)      /* 请求头上限 */
#define OUT_HEAD (MAX_HEAD + 128) /* 重写后请求头的缓冲上限 */
#define IO_BUF (16 * 1024)        /* 双向泵缓冲 */
#define HEAD_TIMEOUT_SEC 30       /* 等待完整请求头的超时 */
#define IDLE_TIMEOUT_SEC 300      /* 连接整体空闲超时 */
#define CONNECT_TIMEOUT_MS 10000  /* 连接上游的超时 */
#define SND_TIMEOUT_SEC 60        /* 发送超时 */

/* ---------------- 日志 ---------------- */

static void (*g_log)(const char *) = NULL;

void proxy_set_log(void (*fn)(const char *)) { g_log = fn; }

static void logf_(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_log(buf);
}

/* ---------------- 服务器状态 ---------------- */

static struct {
    nc_socket listen_fd;
    volatile int running;
    int port;
    HANDLE accept_thread;
} g_srv;

int proxy_port(void) { return g_srv.running ? g_srv.port : -1; }

/* ---------------- 小工具 ---------------- */

static int ci_equal(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return 1;
}

static int send_all(nc_socket s, const char *buf, size_t len) {
    while (len > 0) {
        size_t chunk = len > 32768 ? 32768 : len;
        int n = send(s, buf, (int)chunk, 0);
        if (n <= 0) return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static void send_simple(nc_socket s, int code, const char *reason) {
    char b[160];
    int n = snprintf(b, sizeof b,
                     "HTTP/1.1 %d %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                     code, reason);
    if (n > 0) send_all(s, b, (size_t)n);
}

static int is_loopback_host(const char *h) {
    return !strcmp(h, "127.0.0.1") || !strcmp(h, "localhost") ||
           !strcmp(h, "::1") || !strcmp(h, "[::1]");
}

/* 连接 host:port：getaddrinfo + 非阻塞 connect + select 限时 */
nc_socket nc_connect_host(const char *host, const char *port) {
    struct addrinfo hints, *res = NULL, *ai;
    nc_socket fd = NC_INVALID;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
        return NC_INVALID;

    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == NC_INVALID) continue;
        nc_set_nonblock(fd, 1);
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            nc_set_nonblock(fd, 0);
            break;
        }
        if (!nc_connect_pending()) {
            nc_close(fd);
            fd = NC_INVALID;
            continue;
        }
        {
            fd_set wf;
            struct timeval tv;
            FD_ZERO(&wf);
            FD_SET(fd, &wf);
            tv.tv_sec = CONNECT_TIMEOUT_MS / 1000;
            tv.tv_usec = 0;
            if (select((int)fd + 1, NULL, &wf, NULL, &tv) > 0) {
                int err = 1;
                socklen_t elen = sizeof err;
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) == 0 &&
                    err == 0) {
                    nc_set_nonblock(fd, 0);
                    break;
                }
            }
        }
        nc_close(fd);
        fd = NC_INVALID;
    }
    freeaddrinfo(res);
    return fd;
}

/* ---------------- 请求头读取与解析 ---------------- */

/* 读到 \r\n\r\n；返回 1=成功 0=对端提前关闭 -1=错误/超时/超长 */
static int recv_head(nc_socket s, char *buf, size_t cap,
                     size_t *head_len, size_t *total) {
    size_t len = 0;
    *head_len = *total = 0;
    for (;;) {
        size_t pos;
        for (pos = 0; pos + 4 <= len; pos++) {
            if (buf[pos] == '\r' && buf[pos + 1] == '\n' &&
                buf[pos + 2] == '\r' && buf[pos + 3] == '\n') {
                *head_len = pos + 4;
                *total = len;
                return 1;
            }
        }
        if (len >= cap) return -1;
        {
            int n = recv(s, buf + len, (int)(cap - len), 0);
            if (n == 0) return 0;
            if (n < 0) return -1;
            len += (size_t)n;
        }
    }
}

/* 解析 authority（host[:port]，支持 [IPv6] 形式） */
static int split_authority(const char *a, size_t alen,
                           char *host, size_t hsz, int *port, int def_port) {
    const char *colon = NULL;
    if (alen == 0 || alen >= hsz) return 0;

    if (a[0] == '[') {
        const char *rb = memchr(a, ']', alen);
        if (!rb) return 0;
        colon = (rb + 1 < a + alen && rb[1] == ':') ? rb + 1 : NULL;
    } else {
        size_t i;
        for (i = 0; i < alen; i++)
            if (a[i] == ':') colon = a + i; /* 取最后一个冒号 */
    }

    if (colon) {
        size_t hlen = (size_t)(colon - a);
        char portbuf[8];
        size_t plen = alen - hlen - 1;
        long v;
        if (hlen == 0 || hlen >= hsz || plen == 0 || plen >= sizeof portbuf) return 0;
        memcpy(portbuf, colon + 1, plen);
        portbuf[plen] = 0;
        v = strtol(portbuf, NULL, 10);
        if (v < 1 || v > 65535) return 0;
        memcpy(host, a, hlen);
        host[hlen] = 0;
        *port = (int)v;
    } else {
        memcpy(host, a, alen);
        host[alen] = 0;
        *port = def_port;
    }
    return 1;
}

/* http://host[:port]/rest → host / port / origin 路径 */
static int parse_absolute(const char *t, char *host, size_t hsz,
                          int *port, const char **origin) {
    const char *p, *slash;
    size_t alen;
    if (!ci_equal(t, "http://", 7)) return 0;
    p = t + 7;
    slash = strchr(p, '/');
    alen = slash ? (size_t)(slash - p) : strlen(p);
    if (!split_authority(p, alen, host, hsz, port, 80)) return 0;
    *origin = slash ? slash : "/";
    return 1;
}

/* 从请求头里取 Host 值（origin-form 请求的兜底目标） */
static int get_host_header(const char *in, size_t head_len,
                           char *host, size_t hsz, int *port) {
    size_t p1 = 0, pos, end;
    while (p1 + 1 < head_len && !(in[p1] == '\r' && in[p1 + 1] == '\n')) p1++;
    pos = p1 + 2;
    end = head_len - 2;
    while (pos < end) {
        const char *line = in + pos;
        const char *le = memchr(line, '\n', end - pos);
        size_t llen = le ? (size_t)(le - line) : end - pos;
        if (llen) llen--; /* 去掉行尾 \r */
        if (llen > 5 && ci_equal(line, "host:", 5)) {
            char value[600];
            size_t vs = 5, ve = llen, vlen;
            const char *pb;
            while (vs < llen && (line[vs] == ' ' || line[vs] == '\t')) vs++;
            while (ve > vs && (line[ve - 1] == ' ' || line[ve - 1] == '\t' ||
                               line[ve - 1] == '\r')) ve--;
            vlen = ve - vs;
            if (vlen == 0 || vlen >= sizeof value) return 0;
            memcpy(value, line + vs, vlen);
            value[vlen] = 0;
            pb = strchr(value, '/');
            return split_authority(value, pb ? (size_t)(pb - value) : vlen,
                                   host, hsz, port, 80);
        }
        if (!le) break;
        pos = (size_t)(le - in) + 1;
    }
    return 0;
}

/* 逐跳头：代理不透传 */
static int header_skipped(const char *line, size_t llen) {
    static const char *const names[] = {
        "connection", "proxy-connection", "keep-alive",
        "proxy-authorization", "proxy-authenticate",
    };
    const char *colon = memchr(line, ':', llen);
    size_t nlen, i;
    if (!colon) return 1; /* 畸形行（无冒号）直接丢弃 */
    nlen = (size_t)(colon - line);
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strlen(names[i]) == nlen && ci_equal(line, names[i], nlen)) return 1;
    return 0;
}

/* 重写请求头：绝对 URI → origin-form、剔除逐跳头、补 Connection: close。
 * 返回新长度；0 表示失败。 */
static size_t rebuild_head(const char *in, size_t head_len, const char *origin,
                           char *out, size_t outcap) {
    const char *sp1 = strchr(in, ' ');
    const char *sp2 = sp1 ? strchr(sp1 + 1, ' ') : NULL;
    const char *p1 = memchr(in, '\r', head_len);
    size_t o, pos, end, olen, mlen, vlen;
    int prev_skipped = 0;

    if (!sp1 || !sp2 || !p1 || sp2 > p1) return 0;
    mlen = (size_t)(sp1 - in);          /* method */
    vlen = (size_t)(p1 - sp2 - 1);      /* version */
    if (vlen == 0) return 0;
    olen = strlen(origin);

    o = 0;
    if (o + mlen + 1 + olen + 1 + vlen + 2 >= outcap) return 0;
    memcpy(out + o, in, mlen); o += mlen;
    out[o++] = ' ';
    memcpy(out + o, origin, olen); o += olen;
    out[o++] = ' ';
    memcpy(out + o, sp2 + 1, vlen); o += vlen;
    out[o++] = '\r'; out[o++] = '\n';

    /* 头部行：非逐跳头原样保留；折叠行跟随前一行 */
    pos = (size_t)(p1 - in) + 2;
    end = head_len - 2;
    while (pos < end) {
        const char *line = in + pos;
        const char *le = memchr(line, '\n', end - pos);
        size_t raw = le ? (size_t)(le - line) : end - pos;
        size_t llen = raw ? raw - 1 : 0;
        int skip = header_skipped(line, llen);
        if (llen && (line[0] == ' ' || line[0] == '\t') && prev_skipped) skip = 1;
        if (!skip) {
            if (o + llen + 2 >= outcap) return 0;
            memcpy(out + o, line, llen); o += llen;
            out[o++] = '\r'; out[o++] = '\n';
        }
        prev_skipped = skip;
        if (!le) break;
        pos = (size_t)(le - in) + 1;
    }

    if (o + 21 >= outcap) return 0;
    memcpy(out + o, "Connection: close\r\n\r\n", 21);
    return o + 21;
}

/* ---------------- 双向字节泵 ---------------- */

static void pump(nc_socket a, nc_socket b) {
    char buf[IO_BUF];
    int a_alive = 1, b_alive = 1;
    time_t last_act = time(NULL);

    for (;;) {
        fd_set rf;
        struct timeval tv;
        nc_socket maxfd = 0;
        int n;
        time_t now;

        FD_ZERO(&rf);
        if (a_alive) { FD_SET(a, &rf); if (a > maxfd) maxfd = a; }
        if (b_alive) { FD_SET(b, &rf); if (b > maxfd) maxfd = b; }
        if (!a_alive && !b_alive) break;

        tv.tv_sec = 5; tv.tv_usec = 0;
        n = select((int)maxfd + 1, &rf, NULL, NULL, &tv);
        if (n < 0) break;
        now = time(NULL);
        if (n == 0) {
            if (now - last_act >= IDLE_TIMEOUT_SEC) break;
            continue;
        }
        last_act = now;

        if (a_alive && FD_ISSET(a, &rf)) {
            int k = recv(a, buf, (int)sizeof buf, 0);
            if (k <= 0) {
                a_alive = 0;
                if (k < 0) break;  /* 出错：两边直接断开 */
                nc_shut_wr(b);     /* 对端关闭：半关闭传播 */
            } else if (send_all(b, buf, (size_t)k) != 0) {
                break;
            }
        }
        if (b_alive && FD_ISSET(b, &rf)) {
            int k = recv(b, buf, (int)sizeof buf, 0);
            if (k <= 0) {
                b_alive = 0;
                if (k < 0) break;
                nc_shut_wr(a);
            } else if (send_all(a, buf, (size_t)k) != 0) {
                break;
            }
        }
    }
}

/* ---------------- 连接处理 ---------------- */

static void client_run(nc_socket c) {
    char *in = malloc(MAX_HEAD + 1);
    char *out = malloc(OUT_HEAD);
    char host[512], portstr[8];
    char method[16], target[9216], version[16];
    size_t head_len = 0, total = 0, olen = 0;
    nc_socket up = NC_INVALID;
    int r, port;
    const char *origin = NULL;

    if (!in || !out) goto done;

    nc_set_timeo(c, HEAD_TIMEOUT_SEC, SND_TIMEOUT_SEC);
    r = recv_head(c, in, MAX_HEAD, &head_len, &total);
    if (r != 1) goto done; /* 超时/断开/超长 */
    in[total] = 0;

    if (sscanf(in, "%15s %9215s %15s", method, target, version) != 3) {
        send_simple(c, 400, "Bad Request");
        goto done;
    }

    if (strcmp(method, "CONNECT") == 0) {
        /* --- CONNECT 隧道（HTTPS 等） --- */
        if (!split_authority(target, strlen(target), host, sizeof host, &port, 443)) {
            send_simple(c, 400, "Bad Target");
            goto done;
        }
        if (is_loopback_host(host) && port == g_srv.port) {
            send_simple(c, 400, "Proxy Loop");
            goto done;
        }
        snprintf(portstr, sizeof portstr, "%d", port);
        up = nc_connect_host(host, portstr);
        if (up == NC_INVALID) {
            logf_("CONNECT %s:%d 连接失败(%d)", host, port, nc_last_error());
            send_simple(c, 502, "Bad Gateway");
            goto done;
        }
        if (send_all(c, "HTTP/1.1 200 Connection Established\r\n\r\n",
                     sizeof("HTTP/1.1 200 Connection Established\r\n\r\n") - 1) != 0)
            goto done;
        /* recv_head 可能多读了隧道初始数据，先转发 */
        if (total > head_len &&
            send_all(up, in + head_len, total - head_len) != 0)
            goto done;
        nc_set_timeo(c, IDLE_TIMEOUT_SEC, SND_TIMEOUT_SEC);
        nc_set_timeo(up, IDLE_TIMEOUT_SEC, SND_TIMEOUT_SEC);
        pump(c, up);
        goto done;
    }

    /* --- 普通 HTTP 请求，直连目标 --- */
    if (parse_absolute(target, host, sizeof host, &port, &origin)) {
        ; /* 标准代理请求 */
    } else if (target[0] == '/') {
        /* origin-form + Host 兜底（部分客户端的写法） */
        if (!get_host_header(in, head_len, host, sizeof host, &port)) {
            send_simple(c, 400, "Bad Request");
            goto done;
        }
        origin = target;
    } else if (ci_equal(target, "https://", 8)) {
        send_simple(c, 400, "Use CONNECT");
        goto done;
    } else {
        send_simple(c, 400, "Bad Request");
        goto done;
    }

    if (is_loopback_host(host) && port == g_srv.port) {
        send_simple(c, 400, "Proxy Loop");
        goto done;
    }

    snprintf(portstr, sizeof portstr, "%d", port);
    olen = rebuild_head(in, head_len, origin, out, OUT_HEAD);
    if (olen == 0) {
        send_simple(c, 400, "Bad Request");
        goto done;
    }
    up = nc_connect_host(host, portstr);
    if (up == NC_INVALID) {
        logf_("%s %s:%d 连接失败(%d)", method, host, port, nc_last_error());
        send_simple(c, 502, "Bad Gateway");
        goto done;
    }
    if (send_all(up, out, olen) != 0) goto done;
    /* 请求体前缀（recv_head 多读的部分） */
    if (total > head_len && send_all(up, in + head_len, total - head_len) != 0)
        goto done;
    nc_set_timeo(c, IDLE_TIMEOUT_SEC, SND_TIMEOUT_SEC);
    nc_set_timeo(up, IDLE_TIMEOUT_SEC, SND_TIMEOUT_SEC);
    pump(c, up);

done:
    if (up != NC_INVALID) nc_close(up);
    nc_close(c);
    free(in);
    free(out);
}

/* ---------------- 线程封装 ---------------- */

typedef struct { nc_socket fd; } conn_arg_t;

static unsigned __stdcall client_thread(void *p) {
    conn_arg_t ca = *(conn_arg_t *)p;
    free(p);
    client_run(ca.fd);
    return 0;
}

static int spawn_client(nc_socket fd) {
    conn_arg_t *ca = malloc(sizeof *ca);
    uintptr_t h;
    if (!ca) return -1;
    ca->fd = fd;
    h = _beginthreadex(NULL, 128 * 1024, client_thread, ca, 0, NULL);
    if (!h) { free(ca); return -1; }
    CloseHandle((HANDLE)h);
    return 0;
}

static unsigned __stdcall accept_thread(void *unused) {
    (void)unused;
    for (;;) {
        struct sockaddr_storage ss;
        int alen = (int)sizeof ss;
        nc_socket c = accept(g_srv.listen_fd, (struct sockaddr *)&ss, &alen);
        if (c == NC_INVALID) break; /* 监听 socket 已关闭 */
        if (spawn_client(c) != 0) nc_close(c);
    }
    return 0;
}

/* ---------------- 监听器启停 ---------------- */

static nc_socket make_listener(int port, int *os_err) {
    struct sockaddr_in addr;
    int one = 1;
    nc_socket fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == NC_INVALID) { *os_err = nc_last_error(); return NC_INVALID; }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, (int)sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 固定只监听本机 */
    addr.sin_port = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, 16) != 0) {
        *os_err = nc_last_error();
        nc_close(fd);
        return NC_INVALID;
    }
    return fd;
}

int proxy_start(int port, char *errbuf, int errbuf_len) {
    int os_err = 0;
    nc_socket fd;

    if (g_srv.running) return 0;
    if (port < 1 || port > 65535) {
        snprintf(errbuf, errbuf_len, "端口 %d 无效（需 1-65535）", port);
        return -1;
    }
    fd = make_listener(port, &os_err);
    if (fd == NC_INVALID) {
        snprintf(errbuf, errbuf_len,
                 "监听 127.0.0.1:%d 失败（端口被占用？错误码 %d）", port, os_err);
        return -1;
    }
    g_srv.listen_fd = fd;
    g_srv.port = port;

    {
        uintptr_t h = _beginthreadex(NULL, 64 * 1024, accept_thread, NULL, 0, NULL);
        if (!h) {
            nc_close(fd);
            snprintf(errbuf, errbuf_len, "创建监听线程失败");
            return -1;
        }
        g_srv.accept_thread = (HANDLE)h;
    }
    g_srv.running = 1;
    return 0;
}

void proxy_stop(void) {
    if (!g_srv.running) return;
    g_srv.running = 0;
    nc_close(g_srv.listen_fd);
    if (g_srv.accept_thread) {
        WaitForSingleObject(g_srv.accept_thread, 3000);
        CloseHandle(g_srv.accept_thread);
        g_srv.accept_thread = NULL;
    }
    g_srv.listen_fd = NC_INVALID;
}
