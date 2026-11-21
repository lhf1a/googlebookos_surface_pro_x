@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "PS1_PATH=%SCRIPT_DIR%Install-GooglebookDualBoot.ps1"

if not exist "%PS1_PATH%" (
    echo [ERROR] Missing "%PS1_PATH%".
    pause
    exit /b 1
)

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting Administrator privileges...
    if "%~1"=="" (
        powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    ) else (
        set "_SPX_PS1=%PS1_PATH%"
        set "_SPX_ARGS=%*"
        powershell -NoProfile -ExecutionPolicy Bypass -Command "$p=$env:_SPX_PS1; $a=$env:_SPX_ARGS; Start-Process -FilePath 'powershell.exe' -ArgumentList ('-NoProfile -ExecutionPolicy Bypass -File \"' + $p + '\" ' + $a) -Verb RunAs -Wait"
    )
    exit /b %errorlevel%
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1_PATH%" %*
set "EXIT_CODE=%errorlevel%"
if "%~1"=="" (
    if %EXIT_CODE% neq 0 (
        echo.
        echo Script exited with code %EXIT_CODE%.
        pause
    )
)
exit /b %EXIT_CODE%
