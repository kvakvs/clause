@echo off
rem Write the optimized (Release) assembly of one C++ source to <module>.release.asm in the current directory.
rem Usage: decompile-release.bat <module> [--function TEXT] [other tools\asm.py options]
rem <module> is the source name without path or extension (attributes, or ast/attributes when ambiguous).
setlocal
if not defined PYTHON set "PYTHON=python"
"%PYTHON%" "%~dp0tools\asm.py" --config Release %*
exit /b %errorlevel%
