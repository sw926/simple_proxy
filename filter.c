/* 域名黑/白名单：列表常驻内存，匹配为精确 + 子域名（example.com 可匹配
 * a.example.com）。用临界区保证重载与并发查询安全。 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "filter.h"

#define FILTER_MAX_BYTES (1024 * 1024)

static CRITICAL_SECTION g_lock;
static int g_inited;
static int g_mode = FILTER_OFF;
static char **g_ent;
static int g_n, g_cap;

static void lower_ascii(char *s) {
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s += 32;
}

void filter_init(void) {
    if (!g_inited) {
        InitializeCriticalSection(&g_lock);
        g_inited = 1;
    }
}

void filter_set_mode(int mode) { g_mode = mode; }
int  filter_get_mode(void) { return g_mode; }

void filter_clear(void) {
    int i;
    EnterCriticalSection(&g_lock);
    for (i = 0; i < g_n; i++) free(g_ent[i]);
    free(g_ent);
    g_ent = NULL;
    g_n = g_cap = 0;
    LeaveCriticalSection(&g_lock);
}

/* 追加一条：1=成功 0=忽略 -1=内存失败。raw 区间 [raw, raw+len)。 */
static int add_entry(const char *raw, size_t len) {
    size_t a = 0, b = len, i;
    char *e;
    while (a < b && (raw[a] == ' ' || raw[a] == '\t' || raw[a] == '\r')) a++;
    while (b > a && (raw[b - 1] == ' ' || raw[b - 1] == '\t' || raw[b - 1] == '\r')) b--;
    if (b - a == 0 || b - a > 253) return 0;
    if (raw[a] == '#') return 0; /* 注释行 */
    for (i = a; i < b; i++)
        if (raw[i] == '/' || raw[i] == ' ' || raw[i] == '\t' ||
            raw[i] == '\r' || raw[i] == '\n') return 0;
    if (b - a > 2 && raw[a] == '*' && raw[a + 1] == '.') a += 2; /* *.x → x */

    if (g_n == g_cap) {
        int ncap = g_cap ? g_cap * 2 : 64;
        char **ne = realloc(g_ent, (size_t)ncap * sizeof *ne);
        if (!ne) return -1;
        g_ent = ne;
        g_cap = ncap;
    }
    e = malloc(b - a + 1);
    if (!e) return -1;
    memcpy(e, raw + a, b - a);
    e[b - a] = 0;
    lower_ascii(e);
    g_ent[g_n++] = e;
    return 1;
}

int filter_load_file(const wchar_t *path) {
    FILE *f;
    char *buf;
    size_t len, pos;
    int added = 0;

    if (!g_inited) return -1;
    f = _wfopen(path, L"rb");
    if (!f) return -1;
    buf = malloc(FILTER_MAX_BYTES + 1);
    if (!buf) { fclose(f); return -1; }
    len = fread(buf, 1, FILTER_MAX_BYTES, f);
    fclose(f);
    buf[len] = 0;

    pos = (len >= 3 && (unsigned char)buf[0] == 0xEF &&
           (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) ? 3 : 0;

    EnterCriticalSection(&g_lock);
    while (pos < len) {
        size_t nl = pos;
        int r;
        while (nl < len && buf[nl] != '\n') nl++;
        r = add_entry(buf + pos, nl - pos);
        if (r < 0) break;
        if (r > 0) added++;
        pos = nl + 1;
    }
    LeaveCriticalSection(&g_lock);
    free(buf);
    return added;
}

int filter_blocked(const char *host) {
    char h[256];
    size_t hl, i;
    int found = 0;

    if (!g_inited || g_mode == FILTER_OFF || !host || !*host) return 0;
    hl = strlen(host);
    if (hl >= sizeof h) /* 超长域名：黑名单放行、白名单拦截 */
        return g_mode == FILTER_BLACKLIST ? 0 : 1;
    memcpy(h, host, hl + 1);
    lower_ascii(h);
    if (hl && h[hl - 1] == '.') h[--hl] = 0; /* FQDN 结尾点 */

    EnterCriticalSection(&g_lock);
    for (i = 0; i < (size_t)g_n; i++) {
        size_t el = strlen(g_ent[i]);
        if (el == 0 || el > hl) continue;
        if (!strcmp(h + hl - el, g_ent[i]) && (el == hl || h[hl - el - 1] == '.')) {
            found = 1;
            break;
        }
    }
    LeaveCriticalSection(&g_lock);
    return g_mode == FILTER_BLACKLIST ? found : !found;
}
