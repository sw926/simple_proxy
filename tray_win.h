#ifndef TRAY_WIN_H
#define TRAY_WIN_H

#include "net_compat.h" /* 确保 winsock2 先于 windows.h 引入 */
#include <windows.h>
#include <shellapi.h>

int tray_run(HINSTANCE hInst);

#endif /* TRAY_WIN_H */
