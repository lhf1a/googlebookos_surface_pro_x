@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "PS1_PATH=%SCRIPT_DIR%Upgrade-GooglebookViaAdb.ps1"

if not exist "%PS1_PATH%" (
    echo [ERROR] Missing "%PS1_PATH%".
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1_PATH%" %*
set "EXIT_CODE=%errorlevel%"
if "%~1"=="" (
    echo.
    if %EXIT_CODE% neq 0 (
        echo Script exited with code %EXIT_CODE%.
    )
    pause
)
exit /b %EXIT_CODE%
