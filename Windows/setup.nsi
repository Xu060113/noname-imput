Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
Name "语句拼音"
OutFile "../dist/Yuju-Setup-Windows-x64.exe"
InstallDir "$PROGRAMFILES64\YujuInput"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "此安装包适用于 Windows x64。"
    Abort
  ${EndIf}
FunctionEnd
Section
  SetRegView 64
  SetShellVarContext all
  SetOutPath "$INSTDIR"
  File "${PAYLOAD}/YujuIME.dll"
  File "${PAYLOAD}/YujuSettings.exe"
  File "${PAYLOAD}/rime.dll"
  SetOutPath "$INSTDIR\Rime\build"
  File "${PAYLOAD}/Rime/build/*"
  SetOutPath "$INSTDIR\Licenses"
  File "../Resources/Licenses/*"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YujuInput" "DisplayName" "语句拼音"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YujuInput" "DisplayVersion" "1.0"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YujuInput" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  ${DisableX64FSRedirection}
  ExecWait '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\YujuIME.dll"' $0
  ${EnableX64FSRedirection}
  StrCmp $0 "0" registered
    MessageBox MB_ICONSTOP "输入法注册失败（$0）。可在已安装的应用中卸载或重新运行安装程序。"
    SetErrorLevel 1
    Abort
  registered:
  CreateDirectory "$SMPROGRAMS\语句拼音"
  CreateShortCut "$SMPROGRAMS\语句拼音\DeepSeek 设置.lnk" "$INSTDIR\YujuSettings.exe"
  CreateShortCut "$SMPROGRAMS\语句拼音\卸载语句拼音.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd
Section "Uninstall"
  SetShellVarContext all
  SetRegView 64
  ${DisableX64FSRedirection}
  ExecWait '"$SYSDIR\regsvr32.exe" /s /u "$INSTDIR\YujuIME.dll"' $0
  ${EnableX64FSRedirection}
  StrCmp $0 "0" +3
    MessageBox MB_ICONSTOP "注销失败（$0），请关闭使用输入法的应用后重试。"
    Abort
  Delete "$SMPROGRAMS\语句拼音\DeepSeek 设置.lnk"
  Delete "$SMPROGRAMS\语句拼音\卸载语句拼音.lnk"
  RMDir "$SMPROGRAMS\语句拼音"
  Delete /REBOOTOK "$INSTDIR\YujuIME.dll"
  Delete /REBOOTOK "$INSTDIR\rime.dll"
  Delete /REBOOTOK "$INSTDIR\YujuSettings.exe"
  Delete /REBOOTOK "$INSTDIR\Rime\build\yuju.*.bin"
  Delete "$INSTDIR\Rime\build\yuju_pinyin.schema.yaml"
  Delete "$INSTDIR\Rime\build\default.yaml"
  RMDir "$INSTDIR\Rime\build"
  RMDir "$INSTDIR\Rime"
  Delete "$INSTDIR\Licenses\*-LICENSE"
  Delete "$INSTDIR\Licenses\pinyin-simp-AUTHORS"
  RMDir "$INSTDIR\Licenses"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /REBOOTOK "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YujuInput"
SectionEnd
