"""Build caches (plan step 62D2): parse transform results kept in <build_dir>/cache.

cache.py CLAU ERL WORK: a project whose module a.erl runs a project-source transform (with a warning) and b.erl a
precompiled one. A warm build reuses both results ("(cached)" stage times, no host erl run) with the same
diagnostics as the cold build. Editing a header, a define, a transform source and a .beam on transform_paths
rebuilds exactly the modules whose inputs changed; damaged, truncated and old-format entries and a deleted build
directory are rebuilt silently; --no-cache neither reads nor writes.
"""
import pathlib
import re
import shutil
import subprocess
import sys

PT_SRC = """-module(pt_src).
-export([parse_transform/2, format_error/1]).

%% Keep the forms; warn once per module.
parse_transform(Forms, _Options) ->
    [File | _] = [F || {attribute, _, file, {F, _}} <- Forms],
    {warning, Forms, [{File, [{1, ?MODULE, checked}]}]}.

format_error(checked) -> "checked by pt_src".
"""

PT_BEAM = """-module(pt_beam).
-export([parse_transform/2{extra_export}]).

%% Keep the forms.
parse_transform(Forms, _Options) -> Forms.
{extra}"""


def write(path, text):
    """Create one file with its directories."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode())


def run(command, cwd):
    """Run a command, capturing text output."""
    return subprocess.run([str(part) for part in command], cwd=cwd, capture_output=True, text=True,
                          encoding="utf-8", timeout=600, check=False)


def build_beam(erl, root, extra):
    """Compile pt_beam.erl (with an extra function when `extra`) into deps/ebin with the host OTP."""
    source = root / "deps/src/pt_beam.erl"
    write(source, PT_BEAM.format(extra_export=", spare/0" if extra else "", extra="spare() -> ok.\n" if extra else ""))
    (root / "deps/ebin").mkdir(parents=True, exist_ok=True)
    erlc = pathlib.Path(erl).with_name("erlc" + pathlib.Path(erl).suffix)
    built = run([erlc, "-o", root / "deps/ebin", source], root)
    assert built.returncode == 0, built


class Build:
    """One clau run: its exit status, diagnostics without traces, cache outcomes and cached transform times."""

    def __init__(self, tool, erl, root, *options):
        result = run([tool, "--erl", erl, "--verbose", "--print-stage-times", *options, "--project", "project.toml"],
                     root)
        self.status = result.returncode
        lines = result.stderr.splitlines()
        self.diagnostics = [line for line in lines if not re.match(r"\[(cache|time|index|pp|parse|comp)\]", line)]
        self.cache = {}
        for line in lines:
            if match := re.match(r"\[cache\] (hit|miss) (\w+) .*/([^/:]+?)(: (.*))?$", line):
                self.cache[(match[2], match[3])] = match[1] if match[1] == "hit" else match[5]
        self.cached = {m[1] for m in (re.match(r"\[time\] transform .*/([^/]+): .* \(cached\)$", line)
                                      for line in lines) if m}

    def outcome(self, module):
        """'hit', or the miss reason, of a module's transform result."""
        return self.cache.get(("transform", module))


def main():
    tool, erl, work = sys.argv[1:]
    if erl == "-":
        print("SKIP: no host Erlang/OTP")
        return
    tool, work = pathlib.Path(tool).resolve(), pathlib.Path(work).resolve()
    shutil.rmtree(work, ignore_errors=True)
    root = work / "project"
    write(root / "src/a.erl", '-module(a).\n-compile({parse_transform, pt_src}).\n-include("a.hrl").\n'
                              '-export([f/0]).\nf() -> ?A + ?LEVEL.\n')
    write(root / "src/a.hrl", "-define(A, 1).\n")
    write(root / "src/b.erl", "-module(b).\n-compile({parse_transform, pt_beam}).\n-export([g/0]).\ng() -> ?LEVEL.\n")
    write(root / "src/c.erl", "-module(c).\n-export([h/0]).\nh() -> ok.\n")
    write(root / "transforms/pt_src.erl", PT_SRC)
    build_beam(erl, root, False)
    manifest = ('schema_version = 1\n\n[[targets]]\nname = "app"\nsources = ["src/*.erl"]\n\n[targets.options]\n'
                'source_search_paths = ["transforms"]\ntransform_paths = ["deps/ebin"]\ndefines = ["LEVEL={level}"]\n')
    write(root / "project.toml", manifest.format(level=1))

    cold = Build(tool, erl, root)
    assert cold.status == 0 and cold.outcome("a.erl") == "no entry" and cold.outcome("b.erl") == "no entry", vars(cold)
    assert any("checked by pt_src" in line for line in cold.diagnostics), cold.diagnostics
    warm = Build(tool, erl, root)
    assert warm.status == 0 and warm.diagnostics == cold.diagnostics, (cold.diagnostics, warm.diagnostics)
    sticky = {name for kind, name in warm.cache if kind == "sticky"}
    assert warm.cached == {"a.erl", "b.erl"} and sticky, warm.cache
    assert all(value == "hit" for value in warm.cache.values()), warm.cache
    print("cache: a warm build reuses transform results with the same diagnostics")

    # Each edit misses exactly the modules whose inputs changed.
    def edited(change, expected):
        change()
        build = Build(tool, erl, root)
        outcome = {module: build.outcome(module) for module in ("a.erl", "b.erl")}
        assert build.status == 0 and outcome == expected, (change.__doc__, outcome)
        again = Build(tool, erl, root)
        assert again.cached == {"a.erl", "b.erl"}, (change.__doc__, again.cache)

    def header():
        """Edit a header only a.erl includes."""
        write(root / "src/a.hrl", "-define(A, 2).\n")

    def define():
        """Change a define every module's compile options carry."""
        write(root / "project.toml", manifest.format(level=2))

    def transform_source():
        """Edit the source of a.erl's transform."""
        write(root / "transforms/pt_src.erl", PT_SRC + "%% edited\n")

    def beam():
        """Rebuild a .beam on transform_paths: every module whose transforms run with that path."""
        build_beam(erl, root, True)

    changed = "inputs changed"
    edited(header, {"a.erl": changed, "b.erl": "hit"})
    edited(define, {"a.erl": changed, "b.erl": changed})
    edited(transform_source, {"a.erl": changed, "b.erl": "hit"})
    edited(beam, {"a.erl": changed, "b.erl": changed})
    print("cache: headers, defines, transform sources and .beam files rebuild what depends on them")

    # Damaged, truncated and old-format entries, and a deleted build directory, are rebuilt silently.
    cache = root / "build/cache"
    for pack in cache.rglob("transform-*.pack"):
        data = bytearray(pack.read_bytes())
        data[len(data) // 2] ^= 0xFF
        pack.write_bytes(bytes(data))
    damaged = Build(tool, erl, root)
    assert damaged.status == 0 and damaged.diagnostics == Build(tool, erl, root).diagnostics, damaged.diagnostics
    assert "damaged entry" in damaged.cache.values(), damaged.cache
    for index in cache.rglob("index"):
        index.write_bytes(index.read_bytes()[:10])
    write(cache / "v0/old-00000000/index", "an older format\n")
    truncated = Build(tool, erl, root)
    assert truncated.status == 0 and truncated.outcome("a.erl") == "no entry", truncated.cache
    shutil.rmtree(root / "build")
    recreated = Build(tool, erl, root)
    assert recreated.status == 0 and recreated.outcome("a.erl") == "no entry" and cache.is_dir(), recreated.cache
    print("cache: damaged, truncated, old-format and deleted entries are rebuilt")

    shutil.rmtree(root / "build")
    uncached = Build(tool, erl, root, "--no-cache")
    assert uncached.status == 0 and not uncached.cache and not uncached.cached and not cache.exists(), uncached.cache
    print("cache: ok")


if __name__ == "__main__":
    main()
