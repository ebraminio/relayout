@echo off
setlocal

if not defined VSCMD_VER (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "%VSWHERE%" (
        echo vswhere.exe not found. Install Visual Studio with the C++ workload.
        exit /b 1
    )
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * ^
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
        -property installationPath`) do set "VSPATH=%%i"
    if not defined VSPATH (
        echo No Visual Studio installation with C++ tools found.
        exit /b 1
    )
    call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
)

rc /nologo relayout.rc || exit /b 1

cl /std:c++17 /O1 /GS- /Zl /Gy /EHsc- /GR- relayout.cc relayout.res /Ferelayout.exe /link ^
    /NODEFAULTLIB /SUBSYSTEM:WINDOWS /ENTRY:start ^
    /OPT:REF /OPT:ICF /FIXED /MERGE:.rdata=.text ^
    kernel32.lib user32.lib shell32.lib ole32.lib oleaut32.lib ^
    uiautomationcore.lib
if errorlevel 1 exit /b 1

endlocal
