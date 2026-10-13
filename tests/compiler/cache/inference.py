"""Inference cache (plan step 58W): covered modules keep their inference results in the build cache.

inference.py CLAU WORK: a project whose library modules sit in an OTP-like `otp/lib/stdlib/src` (covered by the
implicit `*/stdlib`), in `vendor/pairs` (covered by the manifest's cache_inference_in_dirs) and in `extra` (covered
by --cache-inference-in-dirs) builds again and again. A warm build reuses every covered module's results with the
same --print-types output and executable behaviour; editing the user module keeps every hit; editing a library
module misses it and the modules that reach it, and the mixed result equals a build without the cache. A module
using another module's native records (qualified, or through -import_record) misses when that module changes.
WORK must not contain a directory the implicit patterns match (`stdlib`, `kernel`, `compiler`, `syntax_tools`).
"""
import pathlib
import re
import shutil
import subprocess
import sys

SEQS = """-module(seqs).
-export([map/2, sum/1, labelled/1]).
map(F, [H | T]) -> [F(H) | map(F, T)];
map(_, []) -> [].
sum(L) -> sum(L, 0).
sum([H | T], Acc) -> sum(T, Acc + H);
sum([], Acc) -> Acc.
labelled(L) -> [{X, texts:label(X)} || X <- L].
"""
TEXTS = """-module(texts).
-export([label/1]).
label(N) when is_integer(N) -> integer_to_list(N);
label(A) when is_atom(A) -> atom_to_list(A).
"""
PAIRS = """-module(pairs).
-export([swap/1, firsts/1]).
swap({A, B}) -> {B, A}.
firsts(L) -> [A || {A, _} <- L].
"""
MORE = """-module(more).
-export([twice/1]).
twice(X) -> {X, X}.
"""
APP = """-module(app).
-export([main/1]).
main(_) ->
    L = seqs:map(fun(X) -> X * FACTOR end, [1, 2, 3]),
    io:format("~p ~p~n", [seqs:sum(L), seqs:labelled(L)]),
    io:format("~p ~p~n", [pairs:firsts([pairs:swap({a, 1})]), more:twice(ok)]),
    io:format("app: ok~n").
"""
DEFS = """-module(defs).
-export_record([point]).
-record(#point{x = 1, y = 2}).
"""
QUALIFIED = """-module(recq).
-export([origin/0]).
origin() -> #defs:point{}.
"""
IMPORTED = """-module(reci).
-export([origin/0]).
-import_record(defs, [point]).
origin() -> #point{}.
"""
MANIFEST = """schema_version = 1
[[targets]]
name = "app"
sources = ["src/app.erl", "vendor/recs/*.erl"]
entry = "app"
output = "out/app"
[targets.options]
source_search_paths = ["otp/lib/stdlib/src", "vendor/pairs", "extra"]
cache_inference_in_dirs = ["vendor/*"]
"""
COVERED = {"seqs.erl", "texts.erl", "pairs.erl", "more.erl", "defs.erl", "recq.erl", "reci.erl"}


def write(path, text):
    """Create one file with its directories."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode())


class Project:
    """The project and the clau runs over it."""

    def __init__(self, tool, work):
        self.tool, self.work = tool, work
        write(work / "otp/lib/stdlib/src/seqs.erl", SEQS)
        write(work / "otp/lib/stdlib/src/texts.erl", TEXTS)
        write(work / "vendor/pairs/pairs.erl", PAIRS)
        write(work / "extra/more.erl", MORE)
        write(work / "vendor/recs/defs.erl", DEFS)
        write(work / "vendor/recs/recq.erl", QUALIFIED)
        write(work / "vendor/recs/reci.erl", IMPORTED)
        self.app(2)
        write(work / "p.toml", MANIFEST)

    def app(self, factor):
        write(self.work / "src/app.erl", APP.replace("FACTOR", str(factor)))

    def run(self, *options, cli_dirs=True):
        """One verbose clau run: its stdout, and each covered module's cache outcome (hit, or miss reason)."""
        command = [str(self.tool), "--verbose", "--print-stage-times", "--build-dir", "build", "--project", "p.toml",
                   *(["--cache-inference-in-dirs", "extra"] if cli_dirs else []), *options]
        run = subprocess.run(command, cwd=self.work, capture_output=True, text=True, timeout=600)
        assert run.returncode == 0, (command, run.stdout, run.stderr)
        outcomes = {}
        for line in run.stderr.splitlines():
            if match := re.match(r"\[cache\] (hit|miss) inference (\S+)(?:: (.*))?$", line):
                outcomes[match[2]] = match[1] if match[1] == "hit" else match[3]
        reused = re.search(r"\[time\] inference-cache [^\n]*\((\d+) of (\d+) covered modules reused\)", run.stderr)
        return run.stdout, outcomes, reused and (int(reused[1]), int(reused[2]))

    def execute(self):
        run = subprocess.run([str(self.work / "out/app")], capture_output=True, text=True, timeout=60)
        assert run.returncode == 0, (run.stdout, run.stderr)
        return run.stdout


def main():
    tool, work = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve()
    shutil.rmtree(work, ignore_errors=True)
    project = Project(tool, work)

    cold, outcomes, reused = project.run("--print-types")
    assert outcomes == dict.fromkeys(COVERED, "no entry") and reused == (0, 7), (outcomes, reused)
    warm, outcomes, reused = project.run("--print-types")
    assert outcomes == dict.fromkeys(COVERED, "hit") and reused == (7, 7) and warm == cold, (outcomes, reused)
    print("inference: a warm --print-types reuses all 7 covered modules with the same output")

    _, outcomes, _ = project.run()
    assert outcomes == dict.fromkeys(COVERED, "hit"), outcomes
    expected = '12 [{2,"2"},{4,"4"},{6,"6"}]\n[1] {ok,ok}\napp: ok\n'
    assert project.execute() == expected, project.execute()
    project.app(3)
    _, outcomes, _ = project.run()
    assert outcomes == dict.fromkeys(COVERED, "hit"), outcomes
    assert project.execute() == expected.replace('12 [{2,"2"},{4,"4"},{6,"6"}]', '18 [{3,"3"},{6,"6"},{9,"9"}]')
    print("inference: executables reuse results kept with more live functions; editing the user module keeps hits")

    write(work / "otp/lib/stdlib/src/texts.erl", TEXTS.replace("integer_to_list(N)", "{n, N}"))
    mixed, outcomes, reused = project.run("--print-types")
    changed = dict.fromkeys(COVERED, "hit") | dict.fromkeys(["seqs.erl", "texts.erl"], "inputs changed")
    assert outcomes == changed and reused == (5, 7), (outcomes, reused)
    fresh, outcomes, reused = project.run("--print-types", "--no-cache")
    assert not outcomes and reused is None and mixed == fresh and mixed != warm, outcomes
    print("inference: editing texts misses it and seqs, which reaches it; the result equals an uncached build")

    write(work / "vendor/recs/defs.erl", DEFS.replace("x = 1", "x = one"))
    _, outcomes, _ = project.run("--print-types")
    changed = dict.fromkeys(COVERED, "hit") | dict.fromkeys(["defs.erl", "recq.erl", "reci.erl"], "inputs changed")
    assert outcomes == changed, outcomes
    print("inference: editing defs misses the modules that use its native records")

    _, outcomes, _ = project.run("--print-types", cli_dirs=False)
    assert outcomes == {name: "hit" for name in COVERED - {"more.erl"}}, outcomes
    print("inference: without --cache-inference-in-dirs extra/more.erl is not covered")
    print("inference: ok")


if __name__ == "__main__":
    main()
