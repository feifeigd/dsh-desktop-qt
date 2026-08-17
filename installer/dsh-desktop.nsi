; ============================================================
;  DSH Desktop - one-click installer (NSIS 3.x)
;  Bundles: DSHDesktop.exe + Qt runtime + embedded Node.js +
;           dsh harness + native plugins + TS plugin demos
;  Install layout:
;    $INSTDIR\DSHDesktop.exe
;    $INSTDIR\plugins\demo_plugin.dll
;    $INSTDIR\tsplugins\dsh-desktop-demo\...
;    $INSTDIR\runtime\node\node.exe (+ npm)
;    $INSTDIR\runtime\dsh\node_modules\@deepseek-ai\dsh\...
;    $INSTDIR\runtime\tools\provision.js / update.js
; ============================================================

Unicode True
!include "MUI2.nsh"
!include "x64.nsh"

!define APP_NAME "DSH Desktop"
!define APP_EXE "DSHDesktop.exe"
!define APP_VERSION "0.1.0"
!define APP_REG_KEY "Software\DSH\DSHDesktop"

Name "${APP_NAME}"
OutFile "..\dist\DSHDesktop-Setup-${APP_VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\DSH Desktop"
InstallDirRegKey HKCU "${APP_REG_KEY}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
CRCCheck on

!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------
;  Sections
; ------------------------------------------------------------------

Section "DSH Desktop (required)" SecApp
  SectionIn RO
  SetOutPath "$INSTDIR"

  ; Everything windeployqt produced + the embedded runtime + plugins.
  ; The deploy dir layout is copied verbatim:
  ;   DSHDesktop.exe, Qt*.dll, QtWebEngineProcess.exe, platforms/,
  ;   resources/, translations/, ..., plugins/, tsplugins/, runtime/
  File /r "..\deploy\*"

  ; VC++ redistributable (install quietly if the system lacks it)
  File "..\deploy\vc_redist.x64.exe"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Registry: uninstall entry (per-user)
  WriteRegStr HKCU "${APP_REG_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${APP_REG_KEY}" "Version" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DSHDesktop" \
    "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DSHDesktop" \
    "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DSHDesktop" \
    "Publisher" "DSH"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DSHDesktop" \
    "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DSHDesktop" \
    "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DSHDesktop" \
    "NoRepair" 1

  ; Shortcuts
  CreateDirectory "$SMPROGRAMS\DSH Desktop"
  CreateShortcut "$SMPROGRAMS\DSH Desktop\DSH Desktop.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$DESKTOP\DSH Desktop.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\DSH Desktop\DSH Desktop.lnk"
  RMDir "$SMPROGRAMS\DSH Desktop"
  Delete "$DESKTOP\DSH Desktop.lnk"
  DeleteRegKey HKCU "${APP_REG_KEY}"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DSHDesktop"
SectionEnd

; ------------------------------------------------------------------
;  Functions
; ------------------------------------------------------------------

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "DSH Desktop requires 64-bit Windows."
    Abort
  ${EndIf}
FunctionEnd
