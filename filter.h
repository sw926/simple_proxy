#ifndef FILTER_H
#define FILTER_H

/* 域名黑/白名单过滤（线程安全，可在重载时与请求处理并发）。 */

enum { FILTER_OFF = 0, FILTER_BLACKLIST = 1, FILTER_WHITELIST = 2 };

void filter_init(void);
void filter_set_mode(int mode);
int  filter_get_mode(void);

/* 清空列表（重载前调用） */
void filter_clear(void);

/* 追加载入列表文件：每行一个域名，# 开头为注释，支持 *.example.com 写法。
 * 条目自动包含其全部子域名。返回载入条数；-1 = 文件打开失败。 */
int  filter_load_file(const wchar_t *path);

/* 返回 1 = 阻断该 host。模式为 off 或未初始化时始终放行。 */
int  filter_blocked(const char *host);

#endif /* FILTER_H */
