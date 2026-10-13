@echo off
rem Write the unoptimized (Debug) assembly of one C++ source to <module>.debug.asm in the current directory.
rem Usage: decompile-debug.bat <module> [--function TEXT] [other tools\asm.py options]
rem <module> is the source name without path or extension (attributes, or ast/attributes when ambiguous).
setlocal
if not defined PYTHON set "PYTHON=python"
"%PYTHON%" "%~dp0tools\asm.py" --config Debug %*
exit /b %errorlevel%
