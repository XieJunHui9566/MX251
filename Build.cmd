@echo off
"%~dp0host\powershell\pwsh.exe" -NoProfile -File "%~dp0Build.ps1" %*
exit /b %ERRORLEVEL%
