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

## 访问日志

在 `config.ini` 中开启（默认关闭）：

```ini
[proxy]
log=1
```

开启后每个代理请求写一行到 exe 同目录 `access.log`（带时间戳，超 1MB 自动截断）：

```
[2026-11-25 10:00:00] CONNECT example.com:443
[2026-11-25 10:00:05] GET api.example.com:80
[2026-11-25 10:00:09] CONNECT ads.example.com:443 BLOCKED
```

`BLOCKED` 表示该请求被域名过滤拦截（返回 403）。

## 域名黑/白名单

在 `config.ini` 中控制模式与列表文件（修改后「重新加载配置」生效）：

```ini
[proxy]
filter=blacklist        ; off / blacklist（黑名单）/ whitelist（白名单）
filter_file=domains.txt ; 列表文件，相对 exe 目录
```

列表文件每行一个域名，`#` 开头为注释，支持 `*.example.com` 写法（等价于直接写
`example.com`）；**条目自动匹配其全部子域名**，大小写不敏感：

```text
# domains.txt —— 黑名单示例：这些域名及其子域名被拦截（返回 403）
ads.example.com
tracker.example.net
```

行为说明：

- `blacklist`：命中列表 → 拦截（403），其余放行
- `whitelist`：命中列表 → 放行，其余一律拦截（403）
- 拦截发生在 DNS 解析之前，HTTP 与 HTTPS（CONNECT）均生效
- 列表文件无法打开时记入 `pytest.log` 并视为空列表

托盘菜单：

| 菜单项 | 说明 |
|---|---|
| 运行中 127.0.0.1:8080 | 当前状态（含监听端口） |
| 编辑配置 | 记事本打开 `config.ini` |
| 查看日志 | 记事本打开 `access.log`；尚未记录时弹窗提示如何开启 |
| 查看黑/白名单 | 记事本打开 `filter_file` 指定的列表文件；不存在时自动生成注释模板 |
| 重新加载配置 | 重读配置/域名列表并重启监听 |
| 开机自启动 | 写入/删除 `HKCU\...\Run` 注册表项 |
| 退出 | 结束程序 |

## 说明与限制

- 只监听 `127.0.0.1`，局域网/外网无法访问，无需防火墙放行
- 代理转发时使用 `Connection: close` 语义，逐跳头（`Proxy-Connection` 等）会被剔除
- 连接空闲 5 分钟自动断开；诊断日志 `pytest.log` 与访问日志 `access.log` 均超 1MB 自动截断
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
