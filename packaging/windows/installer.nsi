; Windows installer for Path of Building (Native), built with NSIS.
;
; Like the official PoB setup it installs per user (no administrator rights),
; adds Start menu and optional desktop shortcuts, and registers an uninstaller
; under Settings > Apps. It never touches the user's builds in
; Documents\Path of Building.
;
; usage: makensis /DVERSION=0.1.1 /DSRC=<package dir> /DOUT=<setup.exe> installer.nsi
;   SRC is the folder packaging/windows/package.ps1 builds (exe + PathOfBuilding\).

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user

!ifndef VERSION
  !error "Pass /DVERSION=x.y.z"
!endif
!ifndef SRC
  !error "Pass /DSRC=<package folder>"
!endif
!ifndef OUT
  !define OUT "PathOfBuilding-Setup-win64.exe"
!endif

!define APP_NAME "Path of Building (Native)"
!define APP_DIR "Path of Building Native"
!define APP_EXE "PathOfBuilding.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\PathOfBuildingNative"
!define DATA_DIR "$APPDATA\PathOfBuilding-Native"

Name "${APP_NAME}"
OutFile "${OUT}"
InstallDir "$LOCALAPPDATA\Programs\${APP_DIR}"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
BrandingText "${APP_NAME} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APP_NAME} setup"
VIAddVersionKey "LegalCopyright" "Native host (c) 2026 Alexander Vikman; Path of Building (c) PoB Community; MIT licence"

!include "MUI2.nsh"

!define MUI_ICON "PathOfBuilding.ico"
!define MUI_UNICON "PathOfBuilding.ico"
!define MUI_ABORTWARNING

!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP_NAME}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRC}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_COMPONENTS
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "${APP_NAME}" SecMain
  SectionIn RO

  ; An earlier install's program files are replaced as a whole.
  RMDir /r "$INSTDIR\PathOfBuilding"

  SetOutPath "$INSTDIR"
  File "${SRC}\${APP_EXE}"
  File "${SRC}\LICENSE"
  File "${SRC}\LICENSE-THIRD-PARTY.md"
  File /r "${SRC}\PathOfBuilding"

  CreateDirectory "$SMPROGRAMS"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Alexander Vikman"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/alexvikman/pob-optimized"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

Section "un.Program files and shortcuts" SecUnMain
  SectionIn RO
  Delete "$SMPROGRAMS\${APP_NAME}.lnk"
  Delete "$DESKTOP\${APP_NAME}.lnk"
  RMDir /r "$INSTDIR\PathOfBuilding"
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\LICENSE-THIRD-PARTY.md"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
SectionEnd

; Optional, unticked by default: the installed copy of PoB, its updates, the log
; and host-settings.ini. Builds and Settings.xml in Documents are never removed.
Section /o "un.Remove downloaded PoB files and settings (${DATA_DIR})" SecUnData
  RMDir /r "${DATA_DIR}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "The program and the bundled Path of Building files."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "A shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

!insertmacro MUI_UNFUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecUnMain} "The program, its shortcuts and the uninstaller."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecUnData} "PoB files downloaded by updates, the log and host-settings.ini. Your builds in Documents are kept either way."
!insertmacro MUI_UNFUNCTION_DESCRIPTION_END
