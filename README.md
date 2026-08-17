# DSH Desktop — Qt 壳 + 免 Node 内置版 DeepSeek Harness

DeepSeek Harness 桌面壳。Qt 6 壳 + 内嵌 WebEngine 加载 `dsh web` UI，
支持两类插件，一键安装包，客户**无需安装 Node.js / Python / 任何环境**。

## 目录结构

```
src/                  Qt 壳（MainWindow / HarnessProcess / PluginManager）
plugins/demo_plugin/  Qt DLL 原生插件 demo（系统信息面板 + 托盘 + /sysinfo）
tsplugins/dsh-desktop-demo/
                      dsh TypeScript 插件 demo（desktop_hello 工具，含 TS 源码）
runtime/tools/        provision.js（首次启动装配）+ update.js（免 Node 更新）
installer/            NSIS 一键安装脚本 + 打包脚本
```

## 运行原理（免 Node）

安装布局（`$INSTDIR`）：

```
DSHDesktop.exe
plugins/demo_plugin.dll             原生 Qt 插件（QPluginLoader 扫描加载）
tsplugins/dsh-desktop-demo/         dsh bundle 插件（TS 编译产物，零构建）
runtime/node/node.exe + npm         便携 Node.js（官方发行版自带 npm）
runtime/dsh/node_modules/@deepseek-ai/dsh    harness 本体（含全部依赖）
runtime/tools/provision.js / update.js
```

启动流程：

1. `provision.js`（内置 node 执行）：
   - 让 dsh 自己 auto-init `%APPDATA%/DSH/DSHDesktop/profiles/web` 官方模板
   - 把 `tsplugins/*` 拷进 profile 的 node_modules，并注册进
     `dsh.profile.bundles`（幂等，每次启动同步，应用升级自动带新插件版本）
2. `node.exe .../dsh/lib/bin.js web --port 3080`（DSH_HOME 指向 %APPDATA%，
   用户数据与安装目录隔离）
3. 壳轮询 HTTP 就绪 → WebEngine 加载 UI

原生模块（node-pty 等）用 prebuilds 跨平台方案，`win32-x64` 产物已包含，
Windows 直接运行。

## 更新 dsh（客户无需 Node）

菜单「帮助 → 检查 Harness 更新」→ 壳调 `update.js`：

- `check`：查 registry 最新版，输出 `{"current","latest","update"}`
- `apply <ver>`：用**内置 npm**（`runtime/node/node_modules/npm/bin/npm-cli.js`）
  装到 staging 目录 → 原子 swap `runtime/dsh` → 重启服务

已实测：apply 0.1.0-rc.3（587 包）→ swap 成功 → check 正确报 rc.6 可升级。

## 构建

WSL 侧（开发机）：
```bash
# 1) 组装 dsh 依赖树（npm install 一次，prebuilds 跨平台）
npm install @deepseek-ai/dsh --registry=https://registry.npmmirror.com

# 2) 准备 Windows runtime（下载 win-x64 node.exe + npm + dsh 树 + tools + tsplugins）
bash installer/prepare-runtime.sh
```

Windows 侧（真机编译，需 VS2022 + Qt 6.11 msvc2022_64）：
```bat
C:\dsh-desktop-qt\build-win.bat   REM 编译 Release + windeployqt → deploy\
```

打包：
```bash
bash installer/package.sh         REM 合并 runtime → makensis → dist\DSHDesktop-Setup-*.exe
```

## 插件开发

- 原生 Qt 插件：实现 `src/PluginInterface.h` 的 `DshPluginInterface`，
  DLL 丢进 `<exe>/plugins/`，支持面板 / 托盘 / 斜杠命令
- TS 插件：npm 包声明 `"dsh": {"bundle": {"patch": "./cordis.patch.yml"}}`，
  丢进 `<exe>/tsplugins/` 下次启动自动注册（见 tsplugins/dsh-desktop-demo）
