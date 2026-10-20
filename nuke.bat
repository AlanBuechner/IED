@echo off
rem Run Nuke from the IED checkout without it being on PATH: nuke build, nuke help, ...
call "%~dp0bin\nuke.cmd" %*
exit /b %errorlevel%
