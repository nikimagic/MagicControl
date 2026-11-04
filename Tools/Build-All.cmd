@echo off
REM One version for Windows and Linux; retries an unfinished build automatically.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-All.ps1" %*
exit /b %errorlevel%
