@echo off
rem MDesktop v2 build script - needs Dev-Cpp bundled MinGW64 (TDM-GCC)
rem Output: 32-bit static single-file MDesktop.exe (Win7 compatible, no runtime deps)
set PATH=C:\MinGW64\bin;%PATH%
if not exist C:\MinGW64\bin\windres.exe (
  echo Creating no-space junction for MinGW64...
  powershell -NoProfile -Command "New-Item -ItemType Junction -Path 'C:\MinGW64' -Target 'C:\Program Files (x86)\Dev-Cpp\MinGW64' | Out-Null"
)
windres --use-temp-file --target=pe-i386 app.rc -O coff -o app_res.o
g++ -m32 -municode -std=gnu++11 -O2 -Wall -DUNICODE -D_UNICODE -mwindows src\main.cpp src\desktops.cpp src\taskview.cpp src\anim.cpp app_res.o -o MDesktop.exe -ldwmapi -lshell32 -lgdi32 -luser32 -lmsimg32 -loleaut32 -lole32 -luuid -ladvapi32 -static-libgcc -static-libstdc++ -s
if %errorlevel%==0 (echo Build OK: MDesktop.exe) else (echo Build FAILED)
