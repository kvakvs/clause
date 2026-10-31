"""Manifest runtime options (plan 30 step DIST4a3b): the project option runtime_options is built into the target's
executable as its default runtime options, CLAUSE_FLAGS and then the command line override them, a word that is not
a runtime option stops the program, and --print-env reports the option."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

tool, directory, suffix = sys.argv[1:]
work = Path(directory)
shutil.rmtree(work, ignore_errors=True)
work.mkdir(parents=True)
module = work / "app.erl"
module.write_text('-module(app).\n-export([main/1]).\nmain(Args) ->\n'
                  '    io:format("~p ~p ~p ~p~n", [Args, init:get_argument(setcookie), init:get_argument(hidden),\n'
                  '        application:get_env(kernel, net_ticktime)]).\n', encoding="utf-8")
manifest = work / "project.toml"
executable = work / ("app" + suffix)


def clau(arguments, status=0):
    """Run clau in the work directory; returns its output after checking the exit status."""
    result = subprocess.run([tool, *arguments], cwd=work, capture_output=True, text=True, timeout=600)
    assert result.returncode == status, (arguments, result)
    return result.stdout + result.stderr


def build(options):
    """Write the manifest with these runtime_options and link its target."""
    manifest.write_text(f'schema_version = 1\n\n[[targets]]\nname = "app"\nsources = ["app.erl"]\n'
                        f'source_dirs = []\noutput = "app"\n\n[targets.options]\nruntime_options = '
                        f'{json.dumps(options)}\n', encoding="utf-8")
    clau(["--project", str(manifest)])


def run(arguments, flags=None, status=0):
    """Run the executable with these arguments and CLAUSE_FLAGS; returns its stdout and stderr."""
    environment = {name: value for name, value in os.environ.items() if name.upper() != "CLAUSE_FLAGS"}
    if flags is not None:
        environment["CLAUSE_FLAGS"] = flags
    result = subprocess.run([str(executable), *arguments], cwd=work, capture_output=True, text=True, timeout=60,
                            env=environment)
    assert result.returncode == status, (arguments, flags, result)
    return result.stdout.replace("\r\n", "\n"), result.stderr.replace("\r\n", "\n")


build(["--setcookie", "built_in", "--hidden", "--env", "kernel", "net_ticktime", "25"])
environment_report = clau(["--print-env", "--project", str(manifest)])
assert "runtime_options = [" in environment_report and '"built_in"' in environment_report, environment_report
stdout, _ = run(["plain"])
assert stdout == '["plain"] {ok,[["built_in"]]} {ok,[[]]} {ok,25}\n', stdout
stdout, _ = run([], "--setcookie from_flags --env kernel net_ticktime 30")
assert stdout == '[] {ok,[["from_flags"]]} {ok,[[]]} {ok,30}\n', stdout
stdout, _ = run(["--setcookie", "from_line", "x"], "--setcookie from_flags")
assert stdout == '["x"] {ok,[["from_line"]]} {ok,[[]]} {ok,25}\n', stdout
print("runtime_options: built-in defaults, overridden by CLAUSE_FLAGS and the command line")

build(["--bogus"])
_, stderr = run([], status=70)
assert stderr == "clau: runtime failure: runtime_options: not a runtime option: --bogus\n", stderr
manifest.write_text(manifest.read_text(encoding="utf-8").replace('["--bogus"]', '"--hidden"'), encoding="utf-8")
assert "runtime_options" in clau(["--project", str(manifest)], 1)
print("runtime_options: built-in defaults, overrides and refused words: ok")
