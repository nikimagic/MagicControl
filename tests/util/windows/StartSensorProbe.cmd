@echo off
setlocal
set "SENSOR_PROBE_EXE=%~dp0SensorProbe.exe"
set "SENSOR_PROBE_OUTPUT="
for %%I in ("%~dp0..\..\..") do set "MAGICCONTROL_SOURCE_ROOT=%%~fI"
if exist "%MAGICCONTROL_SOURCE_ROOT%\CMakeLists.txt" if exist "%MAGICCONTROL_SOURCE_ROOT%\src\core\CMakeLists.txt" set "SENSOR_PROBE_OUTPUT=%MAGICCONTROL_SOURCE_ROOT%\out\test_util\logs"
if not exist "%SENSOR_PROBE_EXE%" (
    echo SensorProbe.exe is missing. Copy the complete portable folder.
    pause
    exit /b 1
)
powershell.exe -NoProfile -Command "try { $start = @{ FilePath=$env:SENSOR_PROBE_EXE; Verb='RunAs'; Wait=$true; PassThru=$true; ErrorAction='Stop' }; if ($env:SENSOR_PROBE_OUTPUT) { $start.ArgumentList=@('--output',([char]34+$env:SENSOR_PROBE_OUTPUT+[char]34)) }; $process = Start-Process @start; exit $process.ExitCode } catch { Write-Host $_.Exception.Message; exit 1 }"
set "SENSOR_PROBE_EXIT=%ERRORLEVEL%"
echo.
if not "%SENSOR_PROBE_EXIT%"=="0" echo SensorProbe exit code: %SENSOR_PROBE_EXIT%
if defined SENSOR_PROBE_OUTPUT (echo Reports: "%SENSOR_PROBE_OUTPUT%") else (echo Reports: "%~dp0..\logs")
pause
exit /b %SENSOR_PROBE_EXIT%
