"""Parse transforms in the compile pipeline (added for parse transforms).

pipeline.py otp CLAU ERL FIXTURES WORK: on the host Erlang/OTP, programs using ms_transform and owned transforms
(from -compile attributes and --parse-transform, found through --transform-path) link and print what OTP prints;
transform warnings and errors and an undefined transform are reported.
pipeline.py none CLAU - FIXTURES WORK: with --erl none a module naming a transform fails with a message.
"""
import pathlib
import re
import subprocess
import sys

TRANSFORMS = ["pipeline/pt_tag.erl", "loader/pt_warning.erl", "loader/pt_error.erl"]


def run(command, cwd, env=None):
    """Run a command, capturing text output."""
    return subprocess.run([str(part) for part in command], cwd=cwd, capture_output=True, text=True,
                          encoding="utf-8", timeout=300, check=False, env=env)


def text(result):
    return result.stdout.replace("\r\n", "\n"), result.stderr.replace("\r\n", "\n")


def without_otp(tool, fixtures, work):
    """--erl none forbids running host code: a module naming a transform fails, naming it."""
    result = run([tool, "--erl", "none", fixtures / "pipeline/tagged_user.erl"], work)
    expected = "tagged_user.erl: parse transform 'pt_tag' needs Erlang/OTP 29 on the host; pass --erl"
    if result.returncode != 1 or expected not in result.stderr:
        return [f"no OTP: exit {result.returncode}\n{result.stderr}"]
    return []


def missing_path(tool, fixtures, work):
    """A transform path that does not exist is reported before any host code runs."""
    result = run([tool, "--erl", "none", "--transform-path", work / "missing", fixtures / "pipeline/tagged_user.erl"],
                 work)
    if result.returncode != 1 or "parse transform path is not a directory" not in result.stderr:
        return [f"missing path: exit {result.returncode}\n{result.stderr}"]
    return []


def compile_transforms(erl, fixtures, ebin):
    """Precompile the owned transforms with the host OTP into one code path directory."""
    ebin.mkdir(parents=True, exist_ok=True)
    erlc = pathlib.Path(erl).with_name("erlc" + pathlib.Path(erl).suffix)
    result = run([erlc, "-o", ebin] + [fixtures / name for name in TRANSFORMS], ebin)
    return [] if result.returncode == 0 else [f"erlc failed\n{result.stdout}{result.stderr}"]


def program(tool, erl, fixtures, work, source, extra=()):
    """Link a program through its transforms, run it and compare its output with what OTP prints."""
    directory = fixtures / "pipeline"
    executable = work / (pathlib.Path(source).stem + ".exe")
    built = run([tool, "--erl", erl, "--transform-path", work / "ebin", *extra, "-o", executable, source], directory)
    if built.returncode != 0:
        return [f"{source}: compile failed\n{built.stderr}"]
    result = run([executable], work)
    expected = (directory / (pathlib.Path(source).stem + ".stdout")).read_text(encoding="utf-8")
    stdout, _ = text(result)
    return [] if result.returncode == 0 and stdout == expected else [f"{source}: exit {result.returncode}\n{stdout}"]


def reported(tool, erl, fixtures, work):
    """Transform warnings keep the module, errors and undefined transforms fail it; --print-abstr shows results."""
    directory = fixtures / "pipeline"
    base = [tool, "--erl", erl, "--transform-path", work / "ebin"]
    failures = []
    cases = [("warned.erl", 0, "warning: subject.erl:1:2: pt_warning says hello"),
             ("refused.erl", 1, "error: subject.erl:4:1: pt_error refuses"),
             ("undefined.erl", 1, "error: undefined.erl: undefined parse transform 'no_such_transform'")]
    for source, status, message in cases:
        result = run(base + [source], directory)
        if result.returncode != status or message not in text(result)[1]:
            failures.append(f"{source}: exit {result.returncode}\n{result.stderr}")
    printed = run(base + ["--print-abstr", "tagged_user.erl"], directory)
    if printed.returncode != 0 or "{function,8,tagged,0," not in printed.stdout:
        failures.append(f"--print-abstr: exit {printed.returncode}\n{printed.stdout}{printed.stderr}")
    # Stage times report the transform of a module separately from its parse. Added for --print-stage-times.
    timed = run(base + ["--print-stage-times", "tagged_user.erl"], directory)
    if timed.returncode != 0 or not re.search(r"^\[time\] transform tagged_user\.erl: [0-9.]+ ms$", text(timed)[1],
                                              re.MULTILINE):
        failures.append(f"--print-stage-times: exit {timed.returncode}\n{timed.stderr}")
    # A .beam on a transform path that does not load is named.
    (work / "ebin/pt_corrupt.beam").write_bytes(b"not a beam file")
    corrupt = run(base + ["--parse-transform", "pt_corrupt", "cli_user.erl"], directory)
    if corrupt.returncode != 1 or "cannot load parse transform 'pt_corrupt' from" not in corrupt.stderr:
        failures.append(f"corrupt beam: exit {corrupt.returncode}\n{corrupt.stderr}")
    return failures


def main():
    mode, tool, erl, fixtures, work = sys.argv[1:]
    tool, fixtures, work = (pathlib.Path(item).resolve() for item in (tool, fixtures, work))
    work.mkdir(parents=True, exist_ok=True)
    if mode == "none":
        failures = without_otp(tool, fixtures, work) + missing_path(tool, fixtures, work)
    elif erl == "-":
        print("SKIP: no host Erlang/OTP")
        return
    else:
        failures = compile_transforms(erl, fixtures, work / "ebin")
        failures += program(tool, erl, fixtures, work, "ms.erl")
        failures += program(tool, erl, fixtures, work, "tagged_user.erl")
        failures += program(tool, erl, fixtures, work, "cli_user.erl", ["--parse-transform", "pt_tag"])
        failures += program(tool, erl, fixtures, work, "warned.erl")
        failures += reported(tool, erl, fixtures, work)
    if failures:
        print("\n".join(failures), file=sys.stderr)
        sys.exit(1)
    print("transforms_no_otp: ok" if mode == "none" else "transforms_pipeline: ok")


if __name__ == "__main__":
    main()
