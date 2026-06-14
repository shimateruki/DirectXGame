@echo off
setlocal
cd /d "%~dp0.."
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\shader_texture_baker.ps1 -Force
if errorlevel 1 (
    echo.
    echo Shader texture bake failed.
    pause
)
