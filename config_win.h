#ifndef CONFIG_WIN_H
#define CONFIG_WIN_H

#include <windows.h>
#include "filter.h"

#define CONFIG_DEFAULT_PORT 8080

typedef struct {
    int port;                        /* [proxy] port */
    int log_enabled;                 /* [proxy] log：访问日志开关 */
    int filter_mode;                 /* [proxy] filter：FILTER_* */
    wchar_t filter_file[MAX_PATH];   /* [proxy] filter_file：列表绝对路径 */
} app_config;

/* 取 exe 同目录下某文件的完整路径（name 可含相对子目录） */
void config_get_path(wchar_t *path, DWORD cap, const wchar_t *name);

/* 读取 config.ini（不存在时先生成模板） */
void config_load(app_config *cfg);

/* 开机自启（HKCU\...\CurrentVersion\Run） */
int  config_autostart_enabled(void);
void config_autostart_set(int enable);

#endif /* CONFIG_WIN_H */
