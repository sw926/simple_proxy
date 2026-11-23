#ifndef PROXY_H
#define PROXY_H

#include "net_compat.h"

#define PROXY_DEFAULT_PORT 8080

/* 设置日志回调（会被工作线程调用，需线程安全；NULL 表示丢弃日志） */
void proxy_set_log(void (*fn)(const char *line));

/* 在 127.0.0.1:port 启动监听与工作线程。
 * 成功返回 0；失败返回 -1 并把原因写入 errbuf。 */
int proxy_start(int port, char *errbuf, int errbuf_len);

/* 停止监听（已建立的连接会继续服务到自然结束） */
void proxy_stop(void);

/* 当前监听端口；未运行返回 -1 */
int proxy_port(void);

#endif /* PROXY_H */
