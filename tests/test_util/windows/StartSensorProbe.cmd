@echo off
setlocal
set "SENSOR_PROBE_EXE=%~dp0SensorProbe.exe"
if not exist "%SENSOR_PROBE_EXE%" (
    echo SensorProbe.exe is missing. Copy the complete portable folder.
    pause
    exit /b 1
)
powershell.exe -NoProfile -Command "try { $process = Start-Process -FilePath $env:SENSOR_PROBE_EXE -Verb RunAs -Wait -PassThru -ErrorAction Stop; exit $process.ExitCode } catch { Write-Host $_.Exception.Message; exit 1 }"
set "SENSOR_PROBE_EXIT=%ERRORLEVEL%"
echo.
if not "%SENSOR_PROBE_EXIT%"=="0" echo SensorProbe exit code: %SENSOR_PROBE_EXIT%
echo Reports: "%~dp0..\logs"
pause
exit /b %SENSOR_PROBE_EXIT%
