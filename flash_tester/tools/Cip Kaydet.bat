@echo off
chcp 65001 >nul
cd /d "%~dp0"
set /p NOTU="Cip notu (hangi karttan cikti): "
"%USERPROFILE%\.platformio\penv\Scripts\python.exe" kaydet.py COM8 "%NOTU%"
echo.
pause
