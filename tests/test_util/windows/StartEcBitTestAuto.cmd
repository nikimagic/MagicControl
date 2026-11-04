@echo off
setlocal
set "EC_BIT_TEST_EXE=%~dp0EcBitTest.exe"
if not exist "%EC_BIT_TEST_EXE%" (
    echo EcBitTest.exe is missing.
    pause
    exit /b 1
)
powershell.exe -NoProfile -Command "try { Start-Process -FilePath $env:EC_BIT_TEST_EXE -ArgumentList @('--manual-context','--auto','--step-seconds','40') -WindowStyle Normal -ErrorAction Stop } catch { Write-Host $_.Exception.Message; exit 1 }"
exit /b %ERRORLEVEL%
