@echo off
setlocal
title ReSkate Hall of Meat - install
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Manage.ps1" -Action Install
set "RESULT=%ERRORLEVEL%"
echo.
if not defined RESKATE_HOM_NO_PAUSE pause
exit /b %RESULT%
