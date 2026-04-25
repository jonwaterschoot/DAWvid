@echo off
:: Run install.ps1 with a visible console so any crash is readable.
:: The -WindowStyle Hidden on the PS side hides the console once the form is up.
powershell.exe -ExecutionPolicy Bypass -NoProfile -File "%~dp0install.ps1"
if %errorlevel% neq 0 (
    echo.
    echo Installer exited with error code %errorlevel%.
    echo Check: %temp%\dawvid_install_error.log
    pause
)
