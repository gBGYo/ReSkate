@echo off
setlocal
title ReSkate Blood - install
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Manage.ps1" -Action Install
set "RESULT=%ERRORLEVEL%"
echo.
if not defined RESKATE_BLOOD_NO_PAUSE pause
exit /b %RESULT%
