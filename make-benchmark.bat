@echo off
setlocal DisableDelayedExpansion
rem Manual benchmarks against Erlang/OTP (plan step 69A); never part of the test gate.
rem Match the Makefile benchmark defaults; environment variables may override them.
if not defined CMAKE set "CMAKE=cmake"
if not defined PYTHON set "PYTHON=python"
rem Benchmarks use their own optimized build directory, so they never reconfigure the testing build.
if not defined BENCH_BUILD_DIR set "BENCH_BUILD_DIR=build/bench"
set "BUILD_TYPE=Release"
rem An omitted count requests the native build tool's default parallelism.
set "PARALLEL_JOBS="
set "BENCH_JOBS="
if defined JOBS set PARALLEL_JOBS="%JOBS%"
if defined JOBS set BENCH_JOBS=--jobs "%JOBS%"
pushd "%~dp0" || exit /b 1
rem The toolchain script checks the cache of BUILD_DIR; point it at the benchmark build.
set "BUILD_DIR=%BENCH_BUILD_DIR%"
call tools\windows-toolchain.cmd
if errorlevel 1 goto finish
call "%CMAKE%" -S . -B "%BENCH_BUILD_DIR%" %CLAUSE_CONFIGURE_ARGS% %CMAKE_ARGS% -DCMAKE_BUILD_TYPE="%BUILD_TYPE%" -DBUILD_TESTING=OFF -DCLAUSE_BUILD_COMPILER=ON -DCLAUSE_BUILD_RUNTIME=ON
if errorlevel 1 goto finish
set "MAKEFLAGS="
call "%CMAKE%" --build "%BENCH_BUILD_DIR%" --config "%BUILD_TYPE%" --target clau clause_runtime --parallel %PARALLEL_JOBS%
if errorlevel 1 goto finish
rem The driver pins every program, OTP and Clause alike, to the performance cores (--cpus performance).
rem Remaining arguments go to the driver, for example --baseline <rev>, --repeat 9 or --cpus 0x5554.
call "%PYTHON%" benchmarks\run.py --build-dir "%BENCH_BUILD_DIR%" --config "%BUILD_TYPE%" %BENCH_JOBS% %*
:finish
set "result=%errorlevel%"
popd
exit /b %result%
