@echo off
setlocal
cd /d "%~dp0.."
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\dds_cache_builder.ps1
if errorlevel 1 (
    echo.
    echo DDS cache build failed.
    pause
)
