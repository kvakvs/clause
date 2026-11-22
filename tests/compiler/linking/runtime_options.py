"""Manifest runtime options (plan 30 step DIST4a3b): the project option runtime_options is built into the target's
executable as its default runtime options, CLAUSE_FLAGS and then the command line override them, a word that is not
a runtime option stops the program, and --print-env reports the option. Runtime options that name the node, and the
option distribution, link the distribution modules (plan 30 step DIST4b1b); exclude_kernel_services and
--exclude-kernel-service leave kernel services out of kernel_sup (DIST4b1e).

Every build links a batch of its own (runtime_options and the kernel services are in the compile key), so the checks
are split by cost (plan 11 step VAL10):

  runtime_options.py CLAU WORK SUFFIX         linking_runtime_options (fast gate): one build with built-in defaults
                                              and their overrides, and the refusals that need no build
  runtime_options.py CLAU WORK SUFFIX --full  linking_runtime_options_full (full gate): the refused built-in word,
                                              distribution and kernel services builds
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

tool, directory, suffix = sys.argv[1:4]
full = "--full" in sys.argv[4:]
work = Path(directory)
shutil.rmtree(work, ignore_errors=True)
work.mkdir(parents=True)
module = work / "app.erl"
module.write_text('-module(app).\n-export([main/1]).\nmain(Args) ->\n'
                  '    io:format("~p ~p ~p ~p~n", [Args, init:get_argument(setcookie), init:get_argument(hidden),\n'
                  '        application:get_env(kernel, net_ticktime)]).\n', encoding="utf-8")
manifest = work / "project.toml"
executable = work / ("app" + suffix)
# The fast gate keeps the build cache beside the work directory, so an unchanged clau and library replay the compile
# and only link (as the golden runner does, plan 11 step VAL8); the full gate compiles cold.
KEPT_CACHE = work.with_name(work.name + "-cache") if os.environ.get("CLAUSE_TEST_MODE") == "fast" else None


def clau(arguments, status=0):
    """Run clau in the work directory; returns its output after checking the exit status."""
    result = subprocess.run([tool, *arguments], cwd=work, capture_output=True, text=True, timeout=600)
    assert result.returncode == status, (arguments, result)
    return result.stdout + result.stderr


def write_manifest(options, extra=""):
    """Write the manifest with these runtime_options (and extra option lines)."""
    manifest.write_text(f'schema_version = 1\n\n[[targets]]\nname = "app"\nsources = ["app.erl"]\n'
                        f'source_dirs = []\noutput = "app"\n\n[targets.options]\nruntime_options = '
                        f'{json.dumps(options)}\n{extra}', encoding="utf-8")


def build(options, extra=""):
    """Write the manifest with these runtime_options (and extra option lines) and link its target."""
    write_manifest(options, extra)
    clau(["--project", str(manifest), *(["--build-dir", str(KEPT_CACHE)] if KEPT_CACHE else [])])


def run(arguments, flags=None, status=0):
    """Run the executable with these arguments and CLAUSE_FLAGS; returns its stdout and stderr."""
    environment = {name: value for name, value in os.environ.items() if name.upper() != "CLAUSE_FLAGS"}
    if flags is not None:
        environment["CLAUSE_FLAGS"] = flags
    result = subprocess.run([str(executable), *arguments], cwd=work, capture_output=True, text=True, timeout=60,
                            env=environment)
    assert result.returncode == status, (arguments, flags, result)
    return result.stdout.replace("\r\n", "\n"), result.stderr.replace("\r\n", "\n")


def check_refusals():
    """Manifest and command line values clau refuses before compiling anything."""
    write_manifest("--hidden")
    assert "runtime_options" in clau(["--project", str(manifest)], 1)
    write_manifest([])
    plain = manifest.read_text(encoding="utf-8")
    manifest.write_text(plain + 'exclude_kernel_services = ["net_sup"]\n', encoding="utf-8")
    assert "net_sup cannot be excluded" in clau(["--project", str(manifest)], 1)
    manifest.write_text(plain + 'exclude_kernel_services = ["bogus"]\n', encoding="utf-8")
    assert "unknown kernel service bogus" in clau(["--project", str(manifest)], 1)
    assert "net_sup cannot be excluded" in clau(["--exclude-kernel-service", "net_sup", "-o", "x", str(module)], 2)
    print("runtime_options: refused manifest values and kernel services")


def check_defaults():
    """Built-in defaults, overridden by CLAUSE_FLAGS and the command line."""
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


def check_refused_word():
    """A built-in word that is not a runtime option stops the program."""
    build(["--bogus"])
    _, stderr = run([], status=70)
    assert stderr == "clau: runtime failure: runtime_options: not a runtime option: --bogus\n", stderr
    print("runtime_options: a refused built-in word")


def check_distribution():
    """A node that listens nowhere needs no epmd. The program never names net_kernel: the manifest links
    distribution."""
    module.write_text('-module(app).\n-export([main/1]).\nmain(_) ->\n    io:format("~p~n", [node()]).\n',
                      encoding="utf-8")
    build(["--sname", "built", "--dist-listen", "false"])
    stdout, _ = run([])
    assert stdout.startswith("built@"), stdout
    build([], "distribution = true\n")
    assert "distribution = true" in clau(["--print-env", "--project", str(manifest)])
    stdout, _ = run(["--sname", "flagged", "--dist-listen", "false"])
    assert stdout.startswith("flagged@"), stdout
    print("runtime_options: distribution")


def check_kernel_services():
    """Excluded services are not kernel_sup's children; global, called anyway, starts its server unlinked as
    before."""
    module.write_text('-module(app).\n-export([main/1]).\nmain(_) ->\n'
                      '    yes = global:register_name(app_name, self()),\n'
                      '    Ids = [Id || {Id, _, _, _} <- supervisor:which_children(kernel_sup)],\n'
                      '    io:format("~p ~p~n", [Ids, is_pid(whereis(global_name_server))]).\n', encoding="utf-8")
    build([], 'exclude_kernel_services = ["global_name_server", "rex"]\n')
    assert 'exclude_kernel_services = [\n  "global_name_server",' in clau(["--print-env", "--project", str(manifest)])
    stdout, _ = run([])
    assert stdout == "[kernel_safe_sup,global_group,net_sup] true\n", stdout
    build([])
    stdout, _ = run([])
    assert stdout == "[kernel_safe_sup,global_group,net_sup,global_name_server,rex] true\n", stdout
    print("runtime_options: kernel services")


if full:
    check_refused_word()
    check_distribution()
    check_kernel_services()
    print("linking_runtime_options_full: ok")
else:
    check_refusals()
    check_defaults()
    print("linking_runtime_options: ok")
