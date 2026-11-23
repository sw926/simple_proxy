# pytest — Windows 托盘 HTTP 正向代理

纯 C + Win32 API 编写的轻量 HTTP 正向代理（仅支持 Windows），单文件 exe：

- **极小占用**：exe 约 100KB，空闲内存约 1~2MB，无任何运行时依赖
- **系统托盘**常驻后台，无控制台窗口，单实例保护
- 固定监听 `127.0.0.1`（即 `http://localhost`），**端口可通过配置文件修改**
- 标准 HTTP 正向代理：绝对 URI 请求直连目标转发，HTTPS 走 CONNECT 隧道
- 支持开机自启（写入当前用户注册表，不需要管理员权限）

## 下载

- **Release**（推荐）：推送 `v*` 标签后由 CI 自动发布
- **最新构建**：GitHub → Actions → 最新一次 `Build` → Artifacts 下载 `pytest-windows-x64`

> 首次运行如遇 SmartScreen 提示，点「更多信息」→「仍要运行」。

## 使用

1. 双击 `pytest.exe`，系统托盘出现蓝色双向箭头图标。
2. 在需要代理的程序或系统中，把 HTTP 代理设置为：

   ```
   http://localhost:8080
   ```

3. 需要代理的 HTTPS 网站同样走这个地址（代理自动使用 CONNECT 隧道）。

## 修改端口

1. 右键托盘图标 → **编辑配置**（自动用记事本打开 exe 同目录的 `config.ini`）
2. 修改 `port` 值并保存：

   ```ini
   [proxy]
   port=8080
   ```

3. 右键托盘图标 → **重新加载配置**，立即生效（无需重启程序）。

托盘菜单：

| 菜单项 | 说明 |
|---|---|
| 运行中 127.0.0.1:8080 | 当前状态（含监听端口） |
| 编辑配置 | 记事本打开 `config.ini` |
| 重新加载配置 | 重读配置并重启监听 |
| 开机自启动 | 写入/删除 `HKCU\...\Run` 注册表项 |
| 退出 | 结束程序 |

## 说明与限制

- 只监听 `127.0.0.1`，局域网/外网无法访问，无需防火墙放行
- 代理转发时使用 `Connection: close` 语义，逐跳头（`Proxy-Connection` 等）会被剔除
- 连接空闲 5 分钟自动断开；错误日志写在 exe 同目录 `pytest.log`（超 1MB 自动截断）
- 不解析、不缓存、不修改任何业务数据

## 从源码构建

CI（GitHub Actions）推送到 `main` 自动构建并运行代理功能测试，打 `v*` 标签自动发 Release。

本地构建（需 MinGW-w64，如 MSYS2 MINGW64）：

```sh
# 主程序（托盘，GUI）
gcc -O2 -s -static -mwindows -o pytest.exe \
    main.c proxy.c tray_win.c config_win.c \
    -lws2_32 -luser32 -lshell32 -ladvapi32

# 测试入口（控制台，供 CI/手动验证代理核心）
gcc -O2 -s -o test_proxy.exe tests/test_main.c proxy.c -lws2_32
```
