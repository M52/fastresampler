@echo off
rem FastResampler unity build script.
rem Exposes MSVC and compiles the per-platform TU root (fastresampler_win32.cpp)
rem directly as a single translation unit.

echo Configuring MSVC x64 build environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to configure MSVC build environment.
    exit /b %ERRORLEVEL%
)
rem vcvarsall can move the cwd; come back to wherever this script lives.
cd /d "%~dp0"

if not exist build_number.txt (
    echo 0 > build_number.txt
)
set /p BUILD_NUM=<build_number.txt
set /a BUILD_NUM=%BUILD_NUM%+1
>build_number.txt echo %BUILD_NUM%
echo #define FASTRESAMPLER_BUILD_NUMBER %BUILD_NUM% > src\fastresampler_build.h

rem First-party code compiles at /W4. 4100 is the uniform callback signature
rem that does not use every argument, 4127 is the constant conditional in
rem while(true), 4324 is padding asked for with alignas. Third-party code is
rem silenced at its include site with pragma pairs in fastresampler_win32.cpp.
set WARN_FLAGS=/W4 /wd4100 /wd4127 /wd4324

set OPT_FLAGS=/O2 /DNDEBUG
for %%x in (%*) do (
    if "%%x"=="debug" set OPT_FLAGS=/Od /Zi /DDEBUG=1
)

if not exist build mkdir build

set INCLUDES=/I "src" /I "3rdparty\imgui" /I "3rdparty\imgui\backends" /I "W:\VST_SDK\vst3sdk"
set DEFINES=/DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD /D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
set LIBS=user32.lib gdi32.lib d3d11.lib dxgi.lib d3dcompiler.lib ole32.lib oleaut32.lib uuid.lib advapi32.lib shell32.lib dwmapi.lib

echo Compiling fastresampler.exe (build %BUILD_NUM%)...
cl.exe %WARN_FLAGS% %OPT_FLAGS% /MT /EHsc /std:c++17 %DEFINES% %INCLUDES% src\fastresampler_win32.cpp /Fe"build\fastresampler.exe" /Fo"build/" /link /SUBSYSTEM:WINDOWS /INCREMENTAL:NO %LIBS%

if %ERRORLEVEL% neq 0 (
    echo Error: Compilation failed.
    exit /b %ERRORLEVEL%
)

echo fastresampler.exe compiled successfully.
