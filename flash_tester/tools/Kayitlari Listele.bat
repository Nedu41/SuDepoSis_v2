@echo off
chcp 65001 >nul
cd /d "%~dp0"
"%USERPROFILE%\.platformio\penv\Scripts\python.exe" kaydet.py --liste
echo.
pause
