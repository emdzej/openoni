@echo off
rem OpenOni @VERSION@ on gasm-run @GASM_VERSION@. Double-click to play; OpenOni.cmd --help for the options.
rem The work is done by openoni.ps1 (game data picker, saved location, gasm-run command line).
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0openoni.ps1" %*
set rc=%errorlevel%
rem Keep the window open on errors when started by double-click (OPENONI_NO_PAUSE=1 skips this).
if not "%rc%"=="0" if not "%OPENONI_NO_PAUSE%"=="1" pause
exit /b %rc%
