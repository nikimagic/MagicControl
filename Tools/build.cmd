@echo off
REM ---------------------------------------------------------------------------
REM MagicControl - Windows build helper.
REM
REM Uses the default CMake generator for Windows (Visual Studio / MSBuild), which
REM is already installed with Visual Studio. Ninja is deliberately not required.
REM
REM Usage:
REM   Tools\build.cmd                 build Release
REM   Tools\build.cmd Debug           build Debug
REM   Tools\build.cmd Release clean   wipe the build directory first
REM   Tools\build.cmd Release test    also run the unit tests
REM ---------------------------------------------------------------------------
setlocal EnableDelayedExpansion

set "ROOT=%~dp0.."
pushd "%ROOT%"

REM --- Compiler message language and console code page ------------------------
REM MSBuild writes its messages in the language Visual Studio was installed with,
REM using the OEM code page. Mixing that with a UTF-8 console produces mojibake,
REM and a half-broken message is worse than no message.
REM
REM VSLANG=1033 selects English messages, which match the English build flags and
REM are readable in any terminal. Set MAGICCONTROL_VSLANG to a different locale
REM identifier to get another language, for example 1049 for Russian, in which
REM case the console code page must match it.
if not defined MAGICCONTROL_VSLANG set "MAGICCONTROL_VSLANG=1033"
set "VSLANG=%MAGICCONTROL_VSLANG%"

if "%MAGICCONTROL_VSLANG%"=="1033" (
    REM English messages are plain ASCII, so UTF-8 is safe for the console.
    chcp 65001 >nul
) else (
    REM Localised messages use the OEM code page of this system.
    chcp 866 >nul
)

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "ACTION=%~2"
REM Normal Release builds use one version and refresh both portable copies.
if /i "%CONFIG%"=="Release" if "%ACTION%"=="" (
    call "%~dp0Build-All.cmd"
    set "BUILD_RESULT=!errorlevel!"
    popd
    exit /b !BUILD_RESULT!
)
set "BUILD_DIR=%ROOT%\build\windows"

REM --- Locate Visual Studio ---------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found. Visual Studio is required.
    popd
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if "%VSPATH%"=="" (
    echo [ERROR] No Visual Studio installation with the C++ toolset was found.
    popd
    exit /b 1
)

set "CMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
    echo [ERROR] cmake.exe not found inside Visual Studio.
    popd
    exit /b 1
)

if /i "%ACTION%"=="clean" (
    echo [INFO] Removing %BUILD_DIR%
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

REM --- Configure --------------------------------------------------------------
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [INFO] Configuring ...
    "%CMAKE%" -S "%ROOT%" -B "%BUILD_DIR%" -A x64
    if errorlevel 1 (
        echo [ERROR] CMake configure failed.
        popd
        exit /b 1
    )
) else (
    echo [INFO] Reusing configuration in %BUILD_DIR%
)

REM --- Build ------------------------------------------------------------------
REM TrackFileAccess=false: MSBuild's file tracker runs the compiler through a
REM named pipe, which some restricted environments refuse to open. The build then
REM dies with TRK0002 before the compiler starts. The tracker only accelerates
REM incremental builds, so turning it off costs a little rebuild time and nothing
REM else.
echo [INFO] Building %CONFIG% ...
"%CMAKE%" --build "%BUILD_DIR%" --config %CONFIG% --parallel -- /p:TrackFileAccess=false /v:minimal /nologo
if errorlevel 1 (
    echo [ERROR] Build failed.
    popd
    exit /b 1
)

REM --- Signing ----------------------------------------------------------------
REM Disabled by default. Enable explicitly at configure time with
REM -DMAGICCONTROL_SIGN=ON for a private Windows release.

REM --- Optional tests ---------------------------------------------------------
if /i "%ACTION%"=="test" (
    echo.
    echo [INFO] Running unit tests ...
    "%BUILD_DIR%\bin\%CONFIG%\magiccontrol_tests.exe"
    if errorlevel 1 (
        echo [ERROR] Unit tests failed.
        popd
        exit /b 1
    )
)

echo.
echo [OK] Output: %BUILD_DIR%\bin\%CONFIG%
popd
endlocal
exit /b 0
