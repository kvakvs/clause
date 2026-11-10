"""Application resources (plan 21 step LIB24): a project's .app.src beside its sources, with the modules beside it
when it lists none, and an [targets.options.applications] directory's ebin/*.app are compiled into the executable,
so application:load/1 finds them by name; a broken resource is reported at its own file and line. A vsn rebar3
computes at build time ({cmd, Cmd}) is kept with a warning, or replaced by the project option application_vsn (plan 21
step MIM9f)."""
from pathlib import Path
import shutil
import subprocess
import sys

tool, directory = sys.argv[1:]
work = Path(directory)
shutil.rmtree(work, ignore_errors=True)


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
                      '        application:load(other), application:load(gen), application:get_key(gen, vsn)]]).\n')
write("src/helper.erl", "-module(helper).\n")
write("src/mine.app.src", '%% A comment before the term.\n{application, mine, [{vsn, "0.1"}, {modules, []}, '
                          '{env, [{k, v}]}]}.\n')
write("deps/dep/ebin/dep.app", '{application, dep, [{vsn, "2.0"}, {modules, [dep_mod]}]}.\n')
write("deps/gen/ebin/gen.app", '{application, gen, [{vsn, {cmd, "echo 9"}}, {modules, []}]}.\n')
write("project.toml", 'schema_version = 1\n\n[[targets]]\nname = "app"\nsources = ["src/*.erl"]\n'
                      'entry = "main:main"\noutput = "build/app"\n\n'
                      '[targets.options.applications]\ndep = "deps/dep"\ngen = "deps/gen"\n')


def run_app(vsn):
    """Build the project and check the program's output, gen's vsn being `vsn`; returns clau's stderr."""
    built = clau("--project", "project.toml")
    assert built.returncode == 0, built
    ran = subprocess.run([str(work / "build" / "app")], cwd=work, capture_output=True, text=True, timeout=60)
    expected = ('[ok,\n {ok,[helper,main]},\n {ok,v},\n ok,\n {ok,"2.0"},\n'
                ' {error,{"no such file or directory","other.app"}},\n ok,\n {ok,' + vsn + '}]\n')
    assert ran.returncode == 0 and ran.stdout.replace("\r\n", "\n") == expected, ran
    return built.stderr


warning = "the vsn of application gen is not a string"
assert warning in run_app('{cmd,"echo 9"}')
manifest = work / "project.toml"
manifest.write_bytes(manifest.read_bytes() + b'\n[targets.options.application_vsn]\ngen = "9.1"\n')
assert warning not in run_app('"9.1"')

write("src/broken.app.src", '{application, broken, [{vsn, "1"}\n')
failed = clau("--project", "project.toml")
assert failed.returncode != 0 and "broken.app.src:" in failed.stderr, failed
print("applications: ok")
