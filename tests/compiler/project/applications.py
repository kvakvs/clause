"""Application resources (plan 21 step LIB24): a project's .app.src beside its sources, with the modules beside it
when it lists none, and an [targets.options.applications] directory's ebin/*.app are compiled into the executable,
so application:load/1 finds them by name; a broken resource is reported at its own file and line. A vsn rebar3
computes at build time ({cmd, Cmd}) is kept with a warning, or replaced by the project option application_vsn (plan 21
step MIM9f). One link shows both: gen's vsn is overridden, computed's is not (plan 11 step VAL10)."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

tool, directory = sys.argv[1:]
work = Path(directory)
shutil.rmtree(work, ignore_errors=True)
# The fast gate keeps the build cache beside the work directory, so an unchanged clau and library replay the compile
# and only link (as the golden runner does, plan 11 step VAL8); the full gate compiles cold.
KEPT_CACHE = ["--build-dir", str(work.with_name(work.name + "-cache"))] if os.environ.get(
    "CLAUSE_TEST_MODE") == "fast" else []


def write(relative, text):
    """Create one fixture file below the work directory."""
    path = work / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode())


def clau(*arguments):
    """Run clau in the work directory; returns the completed process."""
    return subprocess.run([tool, *arguments], cwd=work, capture_output=True, text=True, timeout=300)


write("src/main.erl", '-module(main).\n-export([main/1]).\n'
                      'main(_) ->\n    io:format("~p~n", [[application:load(mine), application:get_key(mine, modules),\n'
                      '        application:get_env(mine, k), application:load(dep), application:get_key(dep, vsn),\n'
                      '        application:load(other), application:load(gen), application:get_key(gen, vsn),\n'
                      '        application:load(computed), application:get_key(computed, vsn)]]).\n')
write("src/helper.erl", "-module(helper).\n")
write("src/mine.app.src", '%% A comment before the term.\n{application, mine, [{vsn, "0.1"}, {modules, []}, '
                          '{env, [{k, v}]}]}.\n')
write("deps/dep/ebin/dep.app", '{application, dep, [{vsn, "2.0"}, {modules, [dep_mod]}]}.\n')
write("deps/gen/ebin/gen.app", '{application, gen, [{vsn, {cmd, "echo 9"}}, {modules, []}]}.\n')
write("deps/computed/ebin/computed.app", '{application, computed, [{vsn, {cmd, "echo 8"}}, {modules, []}]}.\n')
write("project.toml", 'schema_version = 1\n\n[[targets]]\nname = "app"\nsources = ["src/*.erl"]\n'
                      'entry = "main:main"\noutput = "build/app"\n\n'
                      '[targets.options.applications]\ndep = "deps/dep"\ngen = "deps/gen"\ncomputed = "deps/computed"\n'
                      '\n[targets.options.application_vsn]\ngen = "9.1"\n')

# The computed vsn without an override is kept with a warning; gen's override replaces it without one.
built = clau("--project", "project.toml", *KEPT_CACHE)
assert built.returncode == 0, built
assert "the vsn of application computed is not a string" in built.stderr, built.stderr
assert "the vsn of application gen " not in built.stderr, built.stderr
ran = subprocess.run([str(work / "build" / "app")], cwd=work, capture_output=True, text=True, timeout=60)
expected = ('[ok,\n {ok,[helper,main]},\n {ok,v},\n ok,\n {ok,"2.0"},\n'
            ' {error,{"no such file or directory","other.app"}},\n ok,\n {ok,"9.1"},\n ok,\n'
            ' {ok,{cmd,"echo 8"}}]\n')
assert ran.returncode == 0 and ran.stdout.replace("\r\n", "\n") == expected, ran

write("src/broken.app.src", '{application, broken, [{vsn, "1"}\n')
failed = clau("--project", "project.toml")
assert failed.returncode != 0 and "broken.app.src:" in failed.stderr, failed
print("project_applications: ok")
