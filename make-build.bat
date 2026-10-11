@echo off
setlocal DisableDelayedExpansion
rem Match the Makefile build defaults; environment variables may override them.
if not defined CMAKE set "CMAKE=cmake"
rem Builds without tests use their own directory, so they never reconfigure the testing build (BUILD_DIR).
if not defined TOOL_BUILD_DIR set "TOOL_BUILD_DIR=build/tool"
rem Manual builds are optimized Release builds (clang-cl /O2: maximize speed) without tests.
if not defined BUILD_TYPE set "BUILD_TYPE=Release"
rem An omitted count requests the native build tool's default parallelism.
set "PARALLEL_JOBS="
if defined JOBS set PARALLEL_JOBS="%JOBS%"
pushd "%~dp0" || exit /b 1
rem The toolchain script checks the cache of BUILD_DIR; point it at the tool build.
set "BUILD_DIR=%TOOL_BUILD_DIR%"
call tools\windows-toolchain.cmd
if errorlevel 1 goto finish
call "%CMAKE%" -S . -B "%TOOL_BUILD_DIR%" %CLAUSE_CONFIGURE_ARGS% %CMAKE_ARGS% -DCMAKE_BUILD_TYPE="%BUILD_TYPE%" -DBUILD_TESTING=OFF -DCLAUSE_BUILD_COMPILER=ON -DCLAUSE_BUILD_RUNTIME=ON
if errorlevel 1 goto finish
rem Build the runtime archive too: clau links executables against it (-o).
set "MAKEFLAGS="
call "%CMAKE%" --build "%TOOL_BUILD_DIR%" --config "%BUILD_TYPE%" --target clau clause_runtime --parallel %PARALLEL_JOBS%
:finish
set "result=%errorlevel%"
popd
exit /b %result%
