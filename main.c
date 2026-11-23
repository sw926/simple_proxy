/* 入口：托盘外壳（GUI，无控制台）。 */

#include "tray_win.h"

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)hPrev;
    (void)cmdLine;
    (void)nShow;
    return tray_run(hInst);
}
