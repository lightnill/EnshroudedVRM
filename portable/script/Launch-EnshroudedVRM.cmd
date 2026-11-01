@echo off
setlocal DisableDelayedExpansion
rem Copyright (c) 2026 lightnill. MIT; see LICENSE.
rem Use Windows' installed PowerShell. Do not change execution policy.
set "EVRM_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "EVRM_POWERSHELL=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%EVRM_POWERSHELL%" -NoLogo -NoProfile -STA -File "%~dp0Launch-EnshroudedVRM.ps1" %*
set "EVRM_EXIT=%ERRORLEVEL%"
if not "%EVRM_EXIT%"=="0" if "%~1"=="" pause
exit /b %EVRM_EXIT%
