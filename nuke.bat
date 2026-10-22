@echo off
rem Run Nuke from the IED checkout without it being on PATH: nuke build, nuke help, ...
rem Its own folder is read in a subroutine: at the top level, a batch file found on PATH under a quoted name gets the
rem current folder for %~dp0.
setlocal
call :folder
call "%NUKE_HERE%bin\nuke.cmd" %*
exit /b %errorlevel%

:folder
set "NUKE_HERE=%~dp0"
exit /b 0
