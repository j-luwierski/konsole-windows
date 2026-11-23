@echo off
setlocal
set "PWSH=%~dp0..\powershell\pwsh.exe"
if not exist "%PWSH%" (
    echo Bundled PowerShell was not found at "%PWSH%".
    echo Extract the complete Konsole portable package before starting the remote host.
    exit /b 2
)
rem Use a process-local policy so unsigned portable scripts work without changing
rem the user's machine policy. Group Policy restrictions still take precedence.
"%PWSH%" -NoLogo -NoExit -ExecutionPolicy Bypass -File "%~dp0Start-RemoteHost.ps1" %*
exit /b %ERRORLEVEL%
