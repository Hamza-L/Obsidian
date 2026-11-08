@echo off
rem Obsidian build script (Windows): one clang call per executable, same options as the Makefile.
rem
rem   build.bat          build build\Obsidian.exe
rem   build.bat test     build and run build\Obsidian_test.exe
rem   build.bat clean    remove build\
rem
rem Options, set in the environment (e.g. "set GFX_BACKEND=d3d11" before calling):
rem   RENDERER_VALIDATION=1   graphics API validation (default 1)
rem   ASAN=0                  AddressSanitizer (default 0)
rem   V=0                     print every command (default 0)
rem   GFX_BACKEND=vulkan      vulkan or d3d11 (default vulkan)

setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

if not defined RENDERER_VALIDATION set RENDERER_VALIDATION=1
if not defined ASAN set ASAN=0
if not defined V set V=0
if not defined GFX_BACKEND set GFX_BACKEND=vulkan

set APP=Obsidian
set BUILD_DIR=build

set VULKAN_HEADERS_VERSION=1.4.365
set GLSLANG_VERSION=16.5.0
set FREETYPE_VERSION=2.13.3
set STB_IMAGE_COMMIT=013ac3beddff3dbffafd5177e7972067cd2b5083

set CFLAGS=-std=c99 -g -DDEBUG -Wall -Wextra -Wpedantic -Werror -DRENDERER_VALIDATION=%RENDERER_VALIDATION%
set CFLAGS=%CFLAGS% -I./external -I./include -I./src
set CFLAGS=%CFLAGS% -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE -fms-runtime-lib=dll_dbg
set LDFLAGS=-g
set LDLIBS=-luser32.lib -lgdi32.lib -ldwmapi.lib -ld3d11.lib -ldxgi.lib
set LDLIBS=%LDLIBS% -Wl,-nodefaultlib:libcmt.lib -Wl,-nodefaultlib:libcmtd.lib

if "%ASAN%"=="1" (
    set CFLAGS=!CFLAGS! -fsanitize=address -fno-omit-frame-pointer
    set LDFLAGS=!LDFLAGS! -fsanitize=address
    set LDLIBS=!LDLIBS! clang_rt.asan-x86_64.lib
)

set TARGET=%~1
if "%TARGET%"=="" set TARGET=app

if "%TARGET%"=="clean" (
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    exit /b 0
)
if not "%TARGET%"=="app" if not "%TARGET%"=="test" (
    echo usage: %~nx0 [app^|test^|clean] 1>&2
    exit /b 1
)

call :require clang "Install LLVM/Clang from https://releases.llvm.org and put it on PATH."
if errorlevel 1 exit /b 1
call :require curl "curl ships with Windows 10+. Install Git for Windows or your distro's curl package."
if errorlevel 1 exit /b 1

call :deps_stb
if errorlevel 1 exit /b 1
call :deps_freetype
if errorlevel 1 exit /b 1
set CFLAGS=%CFLAGS% -Iexternal/freetype/include/freetype2
set LDLIBS=%LDLIBS% external/freetype-build/Debug/freetyped.lib

if "%GFX_BACKEND%"=="d3d11" (
    set CFLAGS=!CFLAGS! -DGFX_BACKEND_D3D11
    set LDLIBS=!LDLIBS! -ld3dcompiler.lib -ldxguid.lib
) else if "%GFX_BACKEND%"=="vulkan" (
    call :deps_vulkan_headers
    if errorlevel 1 exit /b 1
    call :deps_glslang
    if errorlevel 1 exit /b 1
    set CFLAGS=!CFLAGS! -Iexternal/vulkan-headers/include
    set CFLAGS=!CFLAGS! -isystem external/glslang/include -isystem external/glslang/include/glslang/Include
    set LDLIBS=!LDLIBS! external/glslang/lib/glslangd.lib external/glslang/lib/glslang-default-resource-limitsd.lib
) else (
    echo Unknown GFX_BACKEND '%GFX_BACKEND%'. Use vulkan or d3d11 1>&2
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

if "%TARGET%"=="app" (
    call :build_exe src\main.c "%BUILD_DIR%\%APP%.exe"
    exit /b !errorlevel!
)

call :build_exe test\test_main.c "%BUILD_DIR%\%APP%_test.exe"
if errorlevel 1 exit /b 1
"%BUILD_DIR%\%APP%_test.exe"
exit /b %errorlevel%


rem --- %1 = root source, %2 = output executable -------------------------------
:build_exe
echo BUILD %~2
if "%V%"=="1" echo clang %CFLAGS% %1 -o %2 %LDFLAGS% %LDLIBS%
clang %CFLAGS% %1 -o %2 %LDFLAGS% %LDLIBS%
exit /b %errorlevel%

rem --- %1 = tool, %2 = install hint ------------------------------------------
:require
where %1 >nul 2>&1
if errorlevel 1 (
    echo Missing required tool '%1'. %~2 1>&2
    exit /b 1
)
exit /b 0

rem --- stb_image (single header) ----------------------------------------------
:deps_stb
if exist external\stb\stb_image.h exit /b 0
echo DEPS stb_image (%STB_IMAGE_COMMIT%)
if not exist external\stb mkdir external\stb
curl -sSfL -o external\stb\stb_image.h "https://raw.githubusercontent.com/nothings/stb/%STB_IMAGE_COMMIT%/stb_image.h" || exit /b 1
exit /b 0

rem --- FreeType, built from source with the app's CRT (/MDd) -----------------
:deps_freetype
if exist external\freetype-build\Debug\freetyped.lib exit /b 0
call :require cmake "Install CMake from https://cmake.org/download/ and add it to PATH."
if errorlevel 1 exit /b 1
echo DEPS FreeType v%FREETYPE_VERSION% (building from source)
if not exist external\deps mkdir external\deps
powershell -NoProfile -Command "Invoke-WebRequest -Uri 'https://download.savannah.gnu.org/releases/freetype/freetype-%FREETYPE_VERSION%.tar.gz' -OutFile 'external/deps/freetype.tar.gz'" || exit /b 1
tar -xzf external\deps\freetype.tar.gz -C external\deps || exit /b 1
if exist external\freetype-src rmdir /s /q external\freetype-src
move "external\deps\freetype-%FREETYPE_VERSION%" external\freetype-src >nul || exit /b 1
cmake -S external\freetype-src -B external\freetype-build -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_INSTALL_PREFIX="%CD%\external\freetype" ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebugDLL ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DFT_DISABLE_ZLIB=TRUE ^
    -DFT_DISABLE_BZIP2=TRUE ^
    -DFT_DISABLE_PNG=TRUE ^
    -DFT_DISABLE_HARFBUZZ=TRUE ^
    -DFT_DISABLE_BROTLI=TRUE || exit /b 1
cmake --build external\freetype-build --config Debug --target install --parallel || exit /b 1
exit /b 0

rem --- Vulkan-Headers (headers only) -----------------------------------------
:deps_vulkan_headers
if exist external\vulkan-headers\include\vulkan\vulkan.h exit /b 0
echo DEPS Vulkan-Headers v%VULKAN_HEADERS_VERSION%
if not exist external\deps mkdir external\deps
powershell -NoProfile -Command "Invoke-WebRequest -Uri 'https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v%VULKAN_HEADERS_VERSION%.zip' -OutFile 'external/deps/vulkan-headers.zip'" || exit /b 1
powershell -NoProfile -Command "Expand-Archive -Force 'external/deps/vulkan-headers.zip' 'external/deps'" || exit /b 1
if exist external\vulkan-headers rmdir /s /q external\vulkan-headers
move "external\deps\Vulkan-Headers-%VULKAN_HEADERS_VERSION%" external\vulkan-headers >nul || exit /b 1
exit /b 0

rem --- glslang, built from source with the app's CRT (/MDd) ------------------
:deps_glslang
if exist external\glslang\lib\glslangd.lib exit /b 0
call :require cmake "Install CMake from https://cmake.org/download/ and add it to PATH."
if errorlevel 1 exit /b 1
echo DEPS glslang %GLSLANG_VERSION% (building from source)
if not exist external\deps mkdir external\deps
powershell -NoProfile -Command "Invoke-WebRequest -Uri 'https://github.com/KhronosGroup/glslang/archive/refs/tags/%GLSLANG_VERSION%.tar.gz' -OutFile 'external/deps/glslang.tar.gz'" || exit /b 1
tar -xzf external\deps\glslang.tar.gz -C external\deps || exit /b 1
if exist external\glslang-src rmdir /s /q external\glslang-src
move "external\deps\glslang-%GLSLANG_VERSION%" external\glslang-src >nul || exit /b 1
if exist external\glslang rmdir /s /q external\glslang
cmake -S external\glslang-src -B external\glslang-build -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_INSTALL_PREFIX="%CD%\external\glslang" ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebugDLL ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DBUILD_EXTERNAL=OFF ^
    -DENABLE_OPT=OFF ^
    -DENABLE_HLSL=OFF ^
    -DGLSLANG_TESTS=OFF ^
    -DGLSLANG_ENABLE_INSTALL=ON ^
    -DENABLE_GLSLANG_BINARIES=OFF || exit /b 1
cmake --build external\glslang-build --config Debug --target install --parallel || exit /b 1
exit /b 0
