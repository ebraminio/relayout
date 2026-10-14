@echo off
setlocal

set "LLVM=C:\Program Files\LLVM\bin"

"%LLVM%\llvm-rc" relayout.rc || exit /b 1

"%LLVM%\clang" relayout.cc relayout.res -o Relayout.exe ^
    -Weverything -Wall -Wextra -Wpedantic -Werror -Weffc++ -std=c++23 ^
    -Wno-c++98-compat-pedantic ^
    -Wno-unsafe-buffer-usage ^
    -Wno-reserved-macro-identifier ^
    -Wno-language-extension-token ^
    -Wno-lifetime-safety-intra-tu-suggestions ^
    -fno-exceptions -fno-rtti -fsafe-buffer-usage-suggestions -flto -Oz ^
    -nostdlib -nodefaultlibs -nostartfiles -fuse-ld=lld-link -m32 ^
    -Wl,/entry:start -Wl,/subsystem:windows -Wl,/fixed -Wl,/merge:.rdata=.text ^
    -lkernel32 -luser32 -lshell32 -lole32 -loleaut32
if errorlevel 1 exit /b 1

endlocal
