@echo off
setlocal
set "EC_BIT_TEST_EXE=%~dp0EcBitTest.exe"
set "EC_BIT_TEST_OUTPUT="
for %%I in ("%~dp0..\..\..") do set "MAGICCONTROL_SOURCE_ROOT=%%~fI"
if exist "%MAGICCONTROL_SOURCE_ROOT%\CMakeLists.txt" if exist "%MAGICCONTROL_SOURCE_ROOT%\src\core\CMakeLists.txt" set "EC_BIT_TEST_OUTPUT=%MAGICCONTROL_SOURCE_ROOT%\out\test_util\logs\ec-bit-test"
if not exist "%EC_BIT_TEST_EXE%" (
    echo EcBitTest.exe is missing.
    pause
    exit /b 1
)
powershell.exe -NoProfile -Command "try { $arguments=@('--manual-context','--auto','--step-seconds','40'); if($env:EC_BIT_TEST_OUTPUT){$arguments+=@('--output',([char]34+$env:EC_BIT_TEST_OUTPUT+[char]34))}; Start-Process -FilePath $env:EC_BIT_TEST_EXE -ArgumentList $arguments -WindowStyle Normal -ErrorAction Stop } catch { Write-Host $_.Exception.Message; exit 1 }"
exit /b %ERRORLEVEL%
