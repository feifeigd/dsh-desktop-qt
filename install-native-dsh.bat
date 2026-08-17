@echo off
REM ============================================================
REM  Install the dsh harness INTO deploy\runtime\dsh using the
REM  bundled Windows node.exe + npm, so native modules (koffi,
REM  sharp, node-pty) resolve to win32-x64 builds.
REM  Run on the WINDOWS side (after build-win.bat + runtime merge).
REM ============================================================
setlocal

set DEPLOY=C:\dsh-desktop-qt\deploy
set NODE=%DEPLOY%\runtime\node\node.exe
set NPM=%DEPLOY%\runtime\node\node_modules\npm\bin\npm-cli.js
set DSH_DIR=%DEPLOY%\runtime\dsh
set DSH_VER=0.1.0-rc.6
set REGISTRY=https://registry.npmmirror.com

if not exist "%NODE%" (echo node.exe missing: %NODE% & exit /b 1)
if not exist "%NPM%"  (echo npm-cli.js missing: %NPM% & exit /b 1)

echo ==^> Removing linux-only dep tree (built on WSL) ...
if exist "%DSH_DIR%\node_modules" rmdir /s /q "%DSH_DIR%\node_modules"
if exist "%DSH_DIR%\package-lock.json" del /q "%DSH_DIR%\package-lock.json"

echo ==^> Fresh npm install of @deepseek-ai/dsh@%DSH_VER% (win32-x64 natives) ...
pushd "%DSH_DIR%"
"%NODE%" "%NPM%" install @deepseek-ai/dsh@%DSH_VER% --registry=%REGISTRY% --no-audit --no-fund --loglevel=warn
set RC=%ERRORLEVEL%
popd
if not %RC%==0 (echo npm install FAILED & exit /b %RC%)

echo ==^> Verify win32 natives:
if exist "%DSH_DIR%\node_modules\@koromix\koffi-win32-x64" (echo   koffi-win32-x64 OK) else (echo   WARN koffi-win32-x64 MISSING)
if exist "%DSH_DIR%\node_modules\@img\sharp-win32-x64" (echo   sharp-win32-x64 OK) else (echo   WARN sharp-win32-x64 MISSING)
if exist "%DSH_DIR%\node_modules\node-pty\prebuilds\win32-x64" (echo   node-pty win32-x64 OK) else (echo   WARN node-pty win32-x64 MISSING)

echo ==^> Done.
endlocal
