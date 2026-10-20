@echo off
rem IED setup: installs everything this repository needs that is not committed - a private Python, the LLVM
rem toolchain the C4 compiler builds with, git submodules and the web client's libraries. Safe to re-run.
rem   setup.bat [--toolchain] [--with extras] [--add-to-path ^| --no-path] [--only NAME,...] [--force] [--list]
setlocal
set "ROOT=%~dp0"
set "PYDIR=%ROOT%vendor\python"
set "PYVER=3.12.10"
set "PYSHA=4ACBED6DD1C744B0376E3B1CF57CE906F9DC9E95E68824584C8099A63025A3C3"
set "PYZIP=%ROOT%vendor\_downloads\python-%PYVER%-embed-amd64.zip"
if exist "%PYDIR%\python.exe" goto run

echo [setup] installing Python %PYVER% (embeddable) into vendor\python
if not exist "%ROOT%vendor\_downloads" mkdir "%ROOT%vendor\_downloads"
curl.exe -fsSL --retry 3 -o "%PYZIP%" "https://www.python.org/ftp/python/%PYVER%/python-%PYVER%-embed-amd64.zip"
if errorlevel 1 goto failed
powershell -NoProfile -Command "if ((Get-FileHash -Algorithm SHA256 '%PYZIP%').Hash -ne '%PYSHA%') { exit 1 }"
if errorlevel 1 (
	echo [setup] ERROR: the Python download does not match its pinned SHA256; not installing it
	del "%PYZIP%"
	exit /b 1
)
powershell -NoProfile -Command "Expand-Archive -Force '%PYZIP%' '%PYDIR%'"
if errorlevel 1 goto failed
rem The embeddable runtime pins sys.path in python312._pth; keep that, but turn on the site module.
(echo python312.zip& echo .& echo import site)> "%PYDIR%\python312._pth"
del "%PYZIP%"

:run
"%PYDIR%\python.exe" "%ROOT%tools\setup\setup.py" %*
exit /b %errorlevel%

:failed
echo [setup] ERROR: could not install Python %PYVER% (see above)
exit /b 1
