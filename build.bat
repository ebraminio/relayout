@echo off
setlocal

set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
call "%VCVARS%" >nul || exit /b 1

rc /nologo relayout.rc || exit /b 1

cl /std:c++latest /O1 /GS- /Zl /Gy /EHsc- /GR- relayout.cc relayout.res /FeRelayout.exe /link ^
    /NODEFAULTLIB /SUBSYSTEM:WINDOWS /ENTRY:start ^
    /OPT:REF /OPT:ICF /FIXED /MERGE:.rdata=.text ^
    kernel32.lib user32.lib shell32.lib ole32.lib oleaut32.lib
if errorlevel 1 exit /b 1

endlocal
