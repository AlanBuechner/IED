@echo off
rem Nuke launcher. Put this folder on PATH (setup.bat offers to) to run `nuke` from any workspace.
setlocal
set "NUKE_ROOT=%~dp0.."
set "NUKE_PY=%NUKE_ROOT%\vendor\python\python.exe"
if exist "%NUKE_PY%" goto run
set "NUKE_PY=python"
where python >nul 2>nul
if errorlevel 1 (
	echo nuke: no Python found. Run setup.bat in %NUKE_ROOT% to install the bundled one.
	exit /b 1
)
:run
"%NUKE_PY%" "%NUKE_ROOT%\Nuke\nuke" %*
exit /b %errorlevel%
