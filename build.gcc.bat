@echo off
setlocal

REM https://github.com/niXman/mingw-builds-binaries/
set "MINGW=C:\mingw32\bin"

"%MINGW%\windres" relayout.rc relayout.res || exit /b 1

"%MINGW%\i686-w64-mingw32-gcc" relayout.cc relayout.res -o Relayout.gcc.exe ^
    -Wall -Wextra -Wpedantic -Werror -Weffc++ -fno-exceptions -Oz -s -std=c++2c ^
    -fms-extensions ^
    -lkernel32 -luser32 -lshell32 -lole32 -loleaut32 ^
    -nostdlib -nodefaultlibs -nostartfiles -Wl,-e,start -Wl,-subsystem,windows
if errorlevel 1 exit /b 1

endlocal
