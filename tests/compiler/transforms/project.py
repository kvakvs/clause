"""Transform modules from a project (added for parse transforms).

project.py CLAU ERL FIXTURES WORK: a project whose transform and its helper are project sources found through
source_search_paths, plus a dependency transform precompiled with the host OTP and found through transform_paths,
links a program that prints what OTP prints; ignored search directories and sources of the host's sticky modules
(with what they call) are left out of the transform's sources; a transform source the loader cannot compile is
reported with the
precompile hint; a precompiled .beam on a code path wins over the source. With --erl none, Clause's transform host
interprets those .beam files from their debug_info, and asks for --erl for one without it (plan 21 LIB77C).
"""
import pathlib
import shutil
import subprocess
import sys


def run(command, cwd):
    """Run a command, capturing text output."""
    return subprocess.run([str(part) for part in command], cwd=cwd, capture_output=True, text=True,
                          encoding="utf-8", timeout=600, check=False)


def prepare(erl, fixtures, work):
    """A fresh copy of the project with its dependency transform built by the host OTP, with debug_info for Clause's
    transform host."""
    root = work / "project"
    shutil.rmtree(root, ignore_errors=True)
    shutil.copytree(fixtures / "project", root)
    ebin = root / "deps/ptdep/ebin"
    ebin.mkdir(parents=True)
    erlc = pathlib.Path(erl).with_name("erlc" + pathlib.Path(erl).suffix)
    built = run([erlc, "+debug_info", "-o", ebin, root / "deps/ptdep/src/pt_dep.erl"], root)
    return root, ([] if built.returncode == 0 else [f"erlc failed\n{built.stdout}{built.stderr}"])


def program(tool, erl, root):
    """Build the project and compare the program's output with OTP's."""
    built = run([tool, "--erl", erl, "--project", "project.toml"], root)
    if built.returncode != 0:
        return [f"project: build failed\n{built.stderr}"]
    result = run([root / "app.exe"], root)
    expected = (root / "app.stdout").read_text(encoding="utf-8")
    stdout = result.stdout.replace("\r\n", "\n")
    return [] if result.returncode == 0 and stdout == expected else [f"project: exit {result.returncode}\n{stdout}"]


def skipped_sources(tool, erl, root):
    """Transform source discovery passes over an ignored search directory holding a broken pt_helper.erl, and stops
    at the host's sticky modules: a broken module called only from an erl_anno.erl source is never compiled.
    Added for transform source caching."""
    (root / "transforms/broken").mkdir()
    (root / "transforms/broken/pt_helper.erl").write_text("-module(pt_helper).\nadd_function(F, _, _) -> F\n",
                                                          encoding="utf-8")
    (root / "transforms/erl_anno.erl").write_text("-module(erl_anno).\n-export([line/1]).\n"
                                                  "line(A) -> pt_never:line(A).\n", encoding="utf-8")
    (root / "transforms/pt_never.erl").write_text("-module(pt_never).\nline(A) -> A\n", encoding="utf-8")
    manifest = root / "project.toml"
    text = manifest.read_text(encoding="utf-8").replace(
        'source_search_paths = ["transforms"]',
        'source_search_paths = ["transforms/broken", "transforms"]\nignore_dirs = ["transforms/broken"]')
    manifest.write_text(text, encoding="utf-8")
    return program(tool, erl, root)


def broken(tool, erl, root):
    """A transform source that does not compile fails the modules using it, with the precompile hint."""
    (root / "transforms/pt_rewrite.erl").write_text("-module(pt_rewrite).\nparse_transform(F, _) -> F\n",
                                                     encoding="utf-8")
    result = run([tool, "--erl", erl, "--project", "project.toml"], root)
    hint = "precompile it into a .beam with the host Erlang/OTP and pass its directory with --transform-path"
    if result.returncode != 1 or "pt_rewrite.erl" not in result.stderr or hint not in result.stderr:
        return [f"broken transform: exit {result.returncode}\n{result.stderr}"]
    return []


def precompiled(tool, erl, root, fixtures):
    """With the broken source still there, a precompiled pt_rewrite.beam on a transform path wins."""
    ebin = root / "deps/ptdep/ebin"
    erlc = pathlib.Path(erl).with_name("erlc" + pathlib.Path(erl).suffix)
    sources = [fixtures / "project/transforms/pt_rewrite.erl", fixtures / "project/transforms/pt_helper.erl"]
    built = run([erlc, "+debug_info", "-o", ebin] + sources, root)
    if built.returncode != 0:
        return [f"erlc failed\n{built.stdout}{built.stderr}"]
    return program(tool, erl, root)


def host_beams(tool, erl, root, fixtures):
    """Without erl, the precompiled transforms and the pt_helper they call are interpreted from their .beam files; a
    .beam built without debug_info needs erl. Added for transform sources (plan 21 LIB77C)."""
    failures = program(tool, "none", root)
    erlc = pathlib.Path(erl).with_name("erlc" + pathlib.Path(erl).suffix)
    built = run([erlc, "+no_debug_info", "-o", root / "deps/ptdep/ebin", root / "deps/ptdep/src/pt_dep.erl"], root)
    if built.returncode != 0:
        return failures + [f"erlc failed\n{built.stdout}{built.stderr}"]
    result = run([tool, "--erl", "none", "--project", "project.toml"], root)
    message = "parse transform 'pt_dep' in "
    if result.returncode != 1 or message not in result.stderr or "has no debug_info; pass --erl" not in result.stderr:
        failures.append(f"no debug_info: exit {result.returncode}\n{result.stderr}")
    return failures


def main():
    tool, erl, fixtures, work = sys.argv[1:]
    if erl == "-":
        print("SKIP: no host Erlang/OTP")
        return
    tool, fixtures, work = (pathlib.Path(item).resolve() for item in (tool, fixtures, work))
    work.mkdir(parents=True, exist_ok=True)
    root, failures = prepare(erl, fixtures, work)
    if not failures:
        failures = (program(tool, erl, root) + skipped_sources(tool, erl, root) + broken(tool, erl, root) +
                    precompiled(tool, erl, root, fixtures) + host_beams(tool, erl, root, fixtures))
    if failures:
        print("\n".join(failures), file=sys.stderr)
        sys.exit(1)
    print("transforms_project: ok")


if __name__ == "__main__":
    main()
