"""Build directory (plan step 62D1): Clause's temporary files live under the build directory.

build_dir.py CLAU ERL WORK: a project whose module runs a parse transform that prints the loader workspace (the
loader's plain argument) shows the workspace below <manifest-dir>/build/tmp by default, below the manifest's
build_dir, and below --build-dir, which overrides it; nothing stays in tmp after a success or a failure and nothing
goes to the system temporary directory. A positional check without --build-dir writes no file.
"""
import os
import pathlib
import shutil
import subprocess
import sys

TRANSFORM = """-module(pt_where).
-export([parse_transform/2]).

%% Print the loader's workspace; fail for a module named broken.
parse_transform(Forms, _Options) ->
    io:format("workspace ~ts~n", [hd(init:get_plain_arguments())]),
    case lists:keyfind(module, 3, Forms) of
        {attribute, _, module, broken} -> erlang:error(broken);
        _ -> Forms
    end.
"""


def write(path, text):
    """Create one file with its directories."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode())


def run(tool, arguments, cwd, temporary):
    """Run clau with the system temporary directory pointed at `temporary`."""
    environment = dict(os.environ, TMP=str(temporary), TEMP=str(temporary), TMPDIR=str(temporary))
    return subprocess.run([str(tool), *arguments], cwd=cwd, capture_output=True, text=True, encoding="utf-8",
                          timeout=600, env=environment, check=False)


def workspace(result):
    """The workspace path the transform printed."""
    for line in result.stderr.splitlines():
        if line.startswith("workspace "):
            return pathlib.Path(line[len("workspace "):]).resolve()
    raise AssertionError(f"no workspace line\n{result.stdout}{result.stderr}")


def files(directory):
    """Every file and directory below `directory`."""
    return sorted(path for path in directory.rglob("*")) if directory.exists() else []


def check(tool, erl, root, temporary, arguments, expected, status=0):
    """One build: the workspace lies below `expected`/tmp, which is left empty, and the system temp stays empty."""
    result = run(tool, ["--erl", erl, *arguments], root, temporary)
    assert result.returncode == status, (arguments, result.stdout, result.stderr)
    tmp = (root / expected / "tmp").resolve()
    assert tmp in workspace(result).parents, (arguments, workspace(result), tmp)
    assert not files(tmp), (arguments, files(tmp))
    assert not files(temporary), (arguments, files(temporary))


def main():
    tool, erl, work = sys.argv[1:]
    if erl == "-":
        print("SKIP: no host Erlang/OTP")
        return
    tool, work = pathlib.Path(tool).resolve(), pathlib.Path(work).resolve()
    shutil.rmtree(work, ignore_errors=True)
    root, temporary = work / "project", work / "systemp"
    temporary.mkdir(parents=True)
    write(root / "src/app.erl", "-module(app).\n-compile({parse_transform, pt_where}).\n-export([f/0]).\nf() -> ok.\n")
    write(root / "transforms/pt_where.erl", TRANSFORM)
    manifest = ('schema_version = 1\n\n[[targets]]\nname = "app"\nsources = ["src/*.erl"]\n{build}\n'
                '[targets.options]\nsource_search_paths = ["transforms"]\n')
    write(root / "project.toml", manifest.format(build=""))
    check(tool, erl, root, temporary, ["--project", "project.toml"], "build")
    write(root / "project.toml", manifest.format(build='build_dir = "out"\n'))
    check(tool, erl, root, temporary, ["--project", "project.toml"], "out")
    check(tool, erl, root, temporary, ["--build-dir", "cli", "--project", "project.toml"], "cli")
    # A failing transform leaves nothing either.
    write(root / "src/broken.erl", "-module(broken).\n-compile({parse_transform, pt_where}).\n")
    check(tool, erl, root, temporary, ["--project", "project.toml"], "out", status=1)
    # Positional inputs: --build-dir places the workspace; a check without it writes nothing at all.
    check(tool, erl, root, temporary,
          ["--print-ast", "--build-dir", "positional", "src/app.erl", "transforms/pt_where.erl"], "positional")
    before = files(root)
    result = run(tool, ["--parse-check", "src/app.erl"], root, temporary)
    assert result.returncode == 0 and files(root) == before and not files(temporary), result
    print("build_dir: ok")


if __name__ == "__main__":
    main()
