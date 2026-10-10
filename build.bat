@echo off

echo Configuring MSVC x64 build environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to configure MSVC build environment.
    exit /b %ERRORLEVEL%
)
cd /d "%~dp0"

if not exist build_number.txt (
    echo 0 > build_number.txt
)
set /p BUILD_NUM=<build_number.txt
if not defined FASTRESAMPLER_KEEP_BUILD_NUMBER set /a BUILD_NUM=%BUILD_NUM%+1
>build_number.txt echo %BUILD_NUM%
echo #define FASTRESAMPLER_BUILD_NUMBER %BUILD_NUM% > src\fastresampler_build.h

set WARN_FLAGS=/W4 /wd4100 /wd4127 /wd4324

set FORMAT_TEST=0
set OPT_FLAGS=/O2 /DNDEBUG
for %%x in (%*) do (
    if "%%x"=="format-tests" set FORMAT_TEST=1
    if "%%x"=="debug" set OPT_FLAGS=/Od /Zi /DDEBUG=1
)

if not exist build mkdir build

if not exist "3rdparty\fastsampler-shared\include\fastsampler.pb.h" (
    echo Error: Run git submodule update --init --recursive first.
    exit /b 1
)
if "%FASTSAMPLER_DIR%"=="" set FASTSAMPLER_DIR=%~dp0..\fastsampler

set WAVPACK_DIR=%FASTSAMPLER_DIR%\3rdparty\wavpack-5.9.0-dll
set WAVPACK_BUILD=%~dp0build\wavpack
call "%FASTSAMPLER_DIR%\scripts\build-wavpack.bat" "%WAVPACK_BUILD%"
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to build WavPack from "%FASTSAMPLER_DIR%".
    exit /b %ERRORLEVEL%
)
cd /d "%~dp0"

if "%VST3_SDK_DIR%"=="" set VST3_SDK_DIR=W:\vst-sdk_3.8.1\VST_SDK\vst3sdk
if not exist "%VST3_SDK_DIR%\pluginterfaces\base\funknown.h" (
    echo Error: VST3 SDK not found at "%VST3_SDK_DIR%". Set VST3_SDK_DIR.
    exit /b 1
)

set INCLUDES=/I "src" /I "3rdparty" /I "3rdparty\imgui" /I "3rdparty\imgui\backends" /I "%VST3_SDK_DIR%" /I "3rdparty\fastsampler-shared\include" /I "3rdparty\fastsampler-shared\src" /I "3rdparty\fastsampler-shared\3rdparty\nanopb" /I "%WAVPACK_DIR%"
set DEFINES=/DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DPB_FIELD_32BIT /DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD /D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
set LIBS=user32.lib gdi32.lib d3d11.lib dxgi.lib d3dcompiler.lib ole32.lib oleaut32.lib uuid.lib advapi32.lib shell32.lib dwmapi.lib "%WAVPACK_BUILD%\wavpackdll.lib"

if "%FORMAT_TEST%"=="1" goto format_tests

echo Compiling fastresampler.exe (build %BUILD_NUM%)...
cl.exe %WARN_FLAGS% %OPT_FLAGS% /MT /EHsc /std:c++17 %DEFINES% %INCLUDES% src\fastresampler_win32.cpp /Fe"build\fastresampler.exe" /Fo"build/" /link /SUBSYSTEM:WINDOWS /INCREMENTAL:NO %LIBS%

if %ERRORLEVEL% neq 0 (
    echo Error: Compilation failed.
    exit /b %ERRORLEVEL%
)

copy /y "%WAVPACK_BUILD%\wavpackdll.dll" build\ >nul
copy /y "3rdparty\lucide\lucide.ttf" build\ >nul

echo fastresampler.exe compiled successfully.

exit /b 0

:format_tests
if not exist build\format-test mkdir build\format-test
cl.exe %WARN_FLAGS% %OPT_FLAGS% /MT /EHsc /std:c++17 %DEFINES% %INCLUDES% tests\format_fixture.cpp /Fe"build\format-test\format_fixture.exe" /Fo"build\format-test/" /link /SUBSYSTEM:CONSOLE /INCREMENTAL:NO %LIBS%
if errorlevel 1 exit /b 1
copy /y "%WAVPACK_BUILD%\wavpackdll.dll" build\format-test\ >nul
if errorlevel 1 exit /b 1
build\format-test\format_fixture.exe
exit /b %ERRORLEVEL%
