@echo off
setlocal
cd /d "%~dp0..\.."
wscript.exe "%~dp0start_dds_cache_watcher.vbs"
