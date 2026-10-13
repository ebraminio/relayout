@echo off
setlocal

if not defined VSCMD_VER call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1

cl /std:c++17 /O1 /GS- /Zl /Gy /EHsc- /GR- relayout.cc /Ferelayout.exe /link ^
    /NODEFAULTLIB /SUBSYSTEM:WINDOWS /ENTRY:start ^
    /OPT:REF /OPT:ICF /FIXED /MERGE:.rdata=.text ^
    kernel32.lib user32.lib shell32.lib ole32.lib oleaut32.lib ^
    uiautomationcore.lib

endlocal
