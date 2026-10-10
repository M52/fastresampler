@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0.."
set "OUT=%~1"
if not defined OUT set "OUT=build\wavpack"
set "WP=3rdparty\wavpack"
if not exist "%WP%\src\pack.c" (
    echo Error: WavPack not found in %WP%. Run git submodule update --init --recursive first.
    exit /b 1
)
if not exist "%OUT%" mkdir "%OUT%"
set CFLAGS=/nologo /W2 /O2 /MT /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS /DENABLE_THREADS /DENABLE_DSD /DOPT_ASM_X64 /I "%WP%\include"
set "SOURCES="
set "OBJECTS="
for %%s in (common_utils decorr_utils entropy_utils extra1 extra2 open_utils open_filename open_legacy open_raw pack pack_dns pack_floats pack_utils read_words tags tag_utils unpack unpack_floats unpack_seek unpack_utils write_words pack_dsd unpack_dsd) do (
    set SOURCES=!SOURCES! "%WP%\src\%%s.c"
    set OBJECTS=!OBJECTS! "%OUT%\%%s.obj"
)
echo Building WavPack...
cl.exe %CFLAGS% /c /Fo"%OUT%\\" %SOURCES%
if errorlevel 1 exit /b 1
for %%s in (pack_x64 unpack_x64) do (
    ml64.exe /nologo /c /Fo"%OUT%\%%s.obj" "%WP%\src\%%s.asm"
    if errorlevel 1 exit /b 1
    set OBJECTS=!OBJECTS! "%OUT%\%%s.obj"
)
> "%OUT%\wavpack.def" echo EXPORTS
for /f "usebackq tokens=2 delims='" %%s in ("%WP%\src\wavpack.exports") do >> "%OUT%\wavpack.def" echo %%s
link.exe /nologo /DLL /OUT:"%OUT%\wavpackdll.dll" /IMPLIB:"%OUT%\wavpackdll.lib" /DEF:"%OUT%\wavpack.def" /INCREMENTAL:NO %OBJECTS%
if errorlevel 1 exit /b 1
exit /b 0
