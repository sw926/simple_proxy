#ifndef PROXY_H
#define PROXY_H

#include "net_compat.h"

#define PROXY_DEFAULT_PORT 8080

/* 设置日志回调（会被工作线程调用，需线程安全；NULL 表示丢弃日志） */
void proxy_set_log(void (*fn)(const char *line));

/* 访问日志回调：每个代理请求一行摘要（如 "CONNECT example.com:443 BLOCKED"）。
 * 工作线程调用，需线程安全；NULL 表示关闭。 */
void proxy_set_access_log(void (*fn)(const char *line));

/* 域名过滤回调：请求发出前以目标 host 调用，返回 1 = 拦截（回 403）。
 * 工作线程调用，需线程安全；NULL 表示不过滤。 */
void proxy_set_filter(int (*fn)(const char *host));

/* 在 127.0.0.1:port 启动监听与工作线程。
 * 成功返回 0；失败返回 -1 并把原因写入 errbuf。 */
int proxy_start(int port, char *errbuf, int errbuf_len);

/* 停止监听（已建立的连接会继续服务到自然结束） */
void proxy_stop(void);

/* 当前监听端口；未运行返回 -1 */
int proxy_port(void);

#endif /* PROXY_H */
