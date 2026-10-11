"""Directory index (plan step 62E): lookups answered from one listing per directory keep the precedence of probing.
Modules found through source_search_paths take the first directory (passing over an ignored copy); headers take
the including file's directory, then the working directory, then include directories in order; include_lib reaches
application directories; on Windows a header name differing only in case still resolves. --verbose reports what the
index did; normal output never does."""
import os
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
    """Run clau in the work directory; returns (stdout, stderr) after checking it succeeded."""
    result = subprocess.run([tool, *arguments], cwd=work, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, (arguments, result)
    return result.stdout, result.stderr


write("src/main.erl", '-module(main).\n-export([main/1]).\n-include("h.hrl").\n'
                      '-include_lib("app/include/l.hrl").\nmain(_) -> {?H, ?L, other:f()}.\n')
for place, value in (("src", "including"), (".", "working"), ("inc/a", "first_include"), ("inc/b", "second_include")):
    write(f"{place}/h.hrl", f"-define(H, {value}).\n")
write("vendor/app/include/l.hrl", "-define(L, application).\n")
for place in ("ignored", "one", "two"):
    write(f"search/{place}/other.erl", f"-module(other).\n-export([f/0]).\nf() -> {place}.\n")
write("project.toml", 'schema_version = 1\n\n[[targets]]\nname = "app"\nsources = ["src/main.erl"]\n'
                      'source_dirs = []\n\n[targets.options]\n'
                      'source_search_paths = ["search/ignored", "search/one", "search/two"]\n'
                      'ignore_dirs = ["search/ignored"]\ninclude_dirs = ["inc/**"]\n\n'
                      '[targets.options.applications]\napp = "vendor/app"\n')

# Modules: the first search directory that is not ignored.
inputs, _ = clau("--print-inputs", "--project", "project.toml")
assert "search/one/other.erl (referenced at" in inputs and "search/two" not in inputs, inputs

# Headers: including directory, working directory, then include directories in order (inc/** lists inc/a first).
for removed, expected in (("", "including"), ("src/h.hrl", "working"), ("h.hrl", "first_include"),
                          ("inc/a/h.hrl", "second_include")):
    if removed:
        (work / removed).unlink()
    expanded, _ = clau("--print-pp", "--project", "project.toml")
    assert expected in expanded and "application" in expanded, (removed, expanded)
write("inc/a/h.hrl", "-define(H, first_include).\n")
# Positional -I: the last one given is searched first.
expanded, _ = clau("--print-pp", "-I", "inc/a", "-I", "inc/b", "--app-dir", "app=vendor/app", "src/main.erl")
assert "second_include" in expanded, expanded
print("index: modules and headers keep their search precedence")

# A header spelled in another case resolves where volumes ignore case.
if os.name == "nt":
    write("casing.erl", '-module(casing).\n-include("Case.hrl").\nvalue() -> ?C.\n')
    write("case.hrl", "-define(C, folded).\n")
    expanded, _ = clau("--print-pp", "casing.erl")
    assert "folded" in expanded, expanded
    print("index: a header name differing in case resolves on Windows")

# --verbose reports the index; normal output never does.
_, trace = clau("--parse-check", "--verbose", "--project", "project.toml")
assert "[index] listed=" in trace and " lookups=" in trace, trace
_, quiet = clau("--parse-check", "--project", "project.toml")
assert "[index]" not in quiet, quiet
print("index: --verbose reports what the index did")
