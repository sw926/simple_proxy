#ifndef CONFIG_WIN_H
#define CONFIG_WIN_H

#include <windows.h>

#define CONFIG_DEFAULT_PORT 8080

/* 取 exe 同目录下某文件的完整路径（如 config.ini / proxy.log） */
void config_get_path(wchar_t *path, DWORD cap, const wchar_t *name);

/* 读取 [proxy] port；config.ini 不存在时先生成模板 */
int config_load_port(void);

/* 开机自启（HKCU\...\CurrentVersion\Run） */
int  config_autostart_enabled(void);
void config_autostart_set(int enable);

#endif /* CONFIG_WIN_H */
