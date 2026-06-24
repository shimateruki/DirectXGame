@echo off
setlocal
cd /d "%~dp0..\.."
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "tools\asset_audit\asset_audit.ps1"
endlocal
