@echo off
setlocal EnableDelayedExpansion

echo ================================================
echo    Forge Recoil C++ Build
echo ================================================
echo.

set "VCVARS="
for %%p in (
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
) do (
    if exist %%p (
        set "VCVARS=%%~p"
        goto :found_vc
    )
)

echo [!] Could not find Visual Studio vcvars64.bat
echo     Install Visual Studio 2019 or 2022 with C++ tools
pause
exit /b 1

:found_vc
echo [+] Found: %VCVARS%
call "%VCVARS%" >nul 2>&1

if not exist "build" mkdir build

set SRC_GUI=gui\menu_globals.cpp gui\menu_tabs.cpp gui\menu_render.cpp
set SRC_IMGUI=ImGui\imgui.cpp ImGui\imgui_draw.cpp ImGui\imgui_tables.cpp ImGui\imgui_widgets.cpp ImGui\imgui_impl_dx11.cpp ImGui\imgui_impl_win32.cpp
set SRC_MAIN=main.cpp
set SRC_SHARED=src\game_data.cpp src\recoil_legit.cpp src\recoil_blatant.cpp src\settings_manager.cpp src\security.cpp

set INCLUDES=/I"ImGui" /I"src" /I"gui"
set DEFINES_BASE=/D WIN32 /D _WINDOWS /D NDEBUG /D _CRT_SECURE_NO_WARNINGS /D WIN32_LEAN_AND_MEAN
set LIBS=d3d11.lib dxgi.lib dwmapi.lib user32.lib gdi32.lib shell32.lib advapi32.lib setupapi.lib
set CFLAGS_BASE=/nologo /std:c++17 /EHsc /O2 /MT %INCLUDES% /W3 /wd4996



echo.
echo ================================================
echo    Building MakcuRecoil (Hardware Version)
echo ================================================
echo.

set SRC_MAKCU=src\makcu_mouse.cpp src\makcu_sdk.cpp src\serialport.cpp
set ALL_SRC_MAKCU=%SRC_SHARED% %SRC_MAKCU% %SRC_GUI% %SRC_IMGUI% %SRC_MAIN%
set DEFINES_MAKCU=%DEFINES_BASE%
set CFLAGS_MAKCU=%CFLAGS_BASE% %DEFINES_MAKCU%
set LFLAGS_MAKCU=/nologo /SUBSYSTEM:WINDOWS /OUT:build\MakcuRecoil.exe /MANIFEST:NO

echo [*] Compiling MakcuRecoil...
cl %CFLAGS_MAKCU% %ALL_SRC_MAKCU% /link %LFLAGS_MAKCU% %LIBS%

if %ERRORLEVEL% EQU 0 (
    echo [*] Embedding manifest...
    mt.exe -nologo -manifest forgerecoil.manifest -outputresource:build\MakcuRecoil.exe;#1
    echo.
    echo [+] MakcuRecoil built: build\MakcuRecoil.exe
) else (
    echo.
    echo [!] MakcuRecoil BUILD FAILED
)

del /Q *.obj >nul 2>&1

echo.
echo ================================================
echo    Build Complete
echo ================================================
echo.

if exist build\MakcuRecoil.exe echo    [OK] build\MakcuRecoil.exe
echo.
pause

