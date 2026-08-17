@echo off
REM ============================================================
REM  DSH Desktop - Windows build (Release, no console window)
REM  Run from any shell; requires VS2022 + Qt 6.11 msvc2022_64
REM ============================================================
setlocal

set QT=C:\Qt\6.11.1\msvc2022_64
set CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe
set NINJA=C:\Qt\Tools\Ninja\ninja.exe
set SRC=C:\dsh-desktop-qt
set BUILD=C:\dsh-desktop-qt\build-release

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (echo vcvarsall FAILED & exit /b 1)

"%CMAKE%" -S "%SRC%" -B "%BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="%QT%"
if errorlevel 1 (echo CMake configure FAILED & exit /b 1)

"%CMAKE%" --build "%BUILD%" --config Release -j
if errorlevel 1 (echo Build FAILED & exit /b 1)

echo.
echo === windeployqt (stage deploy directory) ===
set DEPLOY=C:\dsh-desktop-qt\deploy
REM IMPORTANT: never wipe the whole deploy dir - it contains the bundled
REM runtime (node.exe + 360MB dsh dependency tree with win32 natives).
REM Only refresh the exe + Qt DLLs, keep runtime\ / tsplugins\ / plugins\.
if not exist "%DEPLOY%" mkdir "%DEPLOY%"
copy /y "%BUILD%\DSHDesktop.exe" "%DEPLOY%\" >nul
"%QT%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "%DEPLOY%\DSHDesktop.exe"
if errorlevel 1 (echo windeployqt FAILED & exit /b 1)

echo.
echo === stage plugins / runtime dirs ===
if exist "%BUILD%\plugins\demo_plugin.dll" (
  mkdir "%DEPLOY%\plugins" 2>nul
  copy /y "%BUILD%\plugins\demo_plugin.dll" "%DEPLOY%\plugins\" >nul
)
mkdir "%DEPLOY%\tsplugins" 2>nul
mkdir "%DEPLOY%\runtime\node" 2>nul
mkdir "%DEPLOY%\runtime\dsh" 2>nul
mkdir "%DEPLOY%\runtime\tools" 2>nul

echo.
echo BUILD+DEPLOY OK: %DEPLOY%
dir "%DEPLOY%"
endlocal
