@echo off
setlocal
rem RemoteSigned applies only to this PowerShell process; it does not change the saved policy.
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File "%~dp0PortfolioPipeline.ps1" -NoPause
set "result=%ERRORLEVEL%"
echo.
echo Exit code: %result%  ^(0 = success, 1 = failure^)
pause
exit /b %result%
