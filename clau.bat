@echo off
setlocal DisableDelayedExpansion
if not defined TOOL_BUILD_DIR set "TOOL_BUILD_DIR=build/tool"
if not defined BUILD_TYPE set "BUILD_TYPE=Release"
rem Build from the repository, keeping build diagnostics off the compiler's stdout.
call "%~dp0make-build.bat" 1>&2
if errorlevel 1 exit /b %errorlevel%

rem Resolve repository-relative build paths, then restore the caller's working directory.
pushd "%~dp0" || exit /b 1
for %%I in ("%TOOL_BUILD_DIR%") do set "binary_dir=%%~fI\bin"
popd
set "executable=%binary_dir%\%BUILD_TYPE%\clau.exe"
if exist "%executable%" goto run
set "executable=%binary_dir%\clau.exe"
if exist "%executable%" goto run
echo No clau.exe found in "%binary_dir%" for %BUILD_TYPE%. 1>&2
exit /b 1

:run
rem Forward the original arguments without CALL's second expansion.
"%executable%" %*
exit /b %errorlevel%
