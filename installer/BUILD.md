# 构建与打包手册

DSH Desktop 一键安装包构建流程（三阶段）。

## 前置条件

| 组件 | 位置 |
|---|---|
| VS2022 Community（x64 工具链） | `C:\Program Files\Microsoft Visual Studio\2022\Community` |
| Qt 6.11.1 msvc2022_64 | `C:\Qt\6.11.1\msvc2022_64` |
| NSIS 3.x | `C:\Program Files (x86)\NSIS` |
| Node.js（仅构建机需要，用于组装 dsh 依赖树） | WSL 或 Windows |

## 阶段 1：组装 dsh 依赖树（WSL，一次性）

```bash
mkdir -p ~/dsh-runtime-src
cd ~/dsh-runtime-src
npm init -y
npm install @deepseek-ai/dsh@0.1.0-rc.6 --registry=https://registry.npmmirror.com
# 产物：~/dsh-runtime-src/node_modules（含 win32-x64 prebuilds，跨平台可用）
```

## 阶段 2：准备 Windows runtime + 编译 Qt 壳

```bash
# 2a) 准备 runtime（下载 win-x64 node.exe + npm + dsh 树 + tools + tsplugins）
bash installer/prepare-runtime.sh   # 输出 ~/deploy-wsl-runtime（369M）

# 2b) 拷贝源码到 C 盘（9P 文件系统编译慢，且 MSVC 不认 WSL 路径）
cp -r src plugins CMakeLists.txt /mnt/c/dsh-desktop-qt/

# 2c) Windows 侧编译（Release + windeployqt → C:\dsh-desktop-qt\deploy）
cmd.exe /c "cd /d C:\dsh-desktop-qt && build-win.bat"
```

## 阶段 3：合并 runtime + Windows 侧重装原生模块 + NSIS 打包

```bash
# 3a) 快速合并：WSL 本地 tar → Windows tar 解压（9P 直接 cp 太慢）
cd ~/deploy-wsl-runtime && tar cf /tmp/runtime.tar runtime tsplugins
cp /tmp/runtime.tar /mnt/c/dsh-desktop-qt/runtime.tar
cd /mnt/c/dsh-desktop-qt/deploy && cmd.exe /c "tar -xf ..\runtime.tar"

# 3b) 重要！WSL 装的 dsh 依赖树只有 linux 原生模块（koffi/sharp）。
#     必须在 Windows 侧用内置 node+npm 重装，npm 按平台自动选 win32-x64：
cd /mnt/c/dsh-desktop-qt && cmd.exe /c "install-native-dsh.bat"

# 3c) 同步 NSIS 脚本 + 打包（注意：先 cd /mnt/c 避免 UNC 问题）
cp installer/dsh-desktop.nsi /mnt/c/dsh-desktop-qt/installer/
cd /mnt/c/dsh-desktop-qt/installer
"/mnt/c/Program Files (x86)/NSIS/makensis.exe" /NOCD /V2 C:\dsh-desktop-qt\installer\dsh-desktop.nsi
# 产物：C:\dsh-desktop-qt\dist\DSHDesktop-Setup-0.1.0.exe（约 120-150M，LZMA 压缩 713M 需 15-25 分钟）
```

## 安装包内容（安装后）

```
$LOCALAPPDATA\Programs\DSH Desktop\
├── DSHDesktop.exe                 Qt 壳（GUI 子系统，无控制台）
├── Qt6*.dll / platforms/ / resources/ ...   windeployqt 产物
├── vc_redist.x64.exe              VC++ 运行库（静默安装）
├── plugins\demo_plugin.dll        原生 Qt 插件 demo
├── tsplugins\dsh-desktop-demo\    TS 插件 demo（dsh bundle）
└── runtime\
    ├── node\node.exe + npm        便携 Node.js（客户免装）
    ├── dsh\node_modules\...       DeepSeek Harness 本体
    └── tools\provision.js / update.js

运行时数据：%APPDATA%\DSH\DSHDesktop（profiles/sessions，与安装目录隔离）
```

## 踩坑记录

1. **9P 拷贝慢**：WSL 直拷 360M 到 /mnt/c 要 10+ 分钟；tar 打包 + Windows tar 解压秒级完成
2. **UNC 路径**：cmd.exe 从 WSL 目录启动报错 → 先 `cd /mnt/c` 再执行
3. **makensis 参数**：`//NOCD` 会被 WSL 转义成路径 → 用 `/NOCD` + `cd /mnt/c`
4. **NSIS 编码**：.nsi 必须纯 ASCII（UTF-8 中文注释/em-dash 报 "Bad text encoding"）
5. **Qt6 图形栈**：6.11 用 D3D（dxcompiler/dxil），没有 libEGL/libGLESv2，File /r 递归最稳
6. **pnpm worker 崩溃**：受限环境 pnpm 多线程 worker 会挂 → 更新器改用 Node 自带 npm
7. **MSVC most-vexing-parse**：`QNetworkRequest req(QUrl(...))` 被当函数声明 → 拆两步
8. **WSL 装的 dsh 依赖树缺 win32 原生模块**：koffi/sharp 用 optionalDependencies
   按平台分发二进制，WSL 装只有 linux 版 → 必须 Windows 侧用内置 npm 重装
   （install-native-dsh.bat）；node-pty 用 prebuilds 跨平台没问题
9. **dsh 需要 node --expose-internals**：HMR 服务要求，HarnessProcess 启动
   参数已加；不加则 `--expose-internals is required for HMR service`
10. **WSL 探测 Windows 进程端口会失败**：WSL2 网络隔离，验证 Windows 侧
    服务要用 PowerShell（Get-NetTCPConnection / Invoke-WebRequest）
