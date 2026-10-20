@ECHO OFF
rem Builds the C4 compiler and its tools with Frost (C++). From the repository root, `nuke build -p C4` runs
rem the same build and then refreshes the "dev" toolchain. Arguments go to Frost/Build.py (-b, -test, -c ...).
SETLOCAL
set "PY=%~dp0..\vendor\python\python.exe"
if not exist "%PY%" set "PY=python"
"%PY%" "%~dp0Frost\Build.py" %*
exit /b %errorlevel%
