"""The erlang library module (plan step 41B) and builtin programs (41E), as one of three checks.

  inventory ROOT                 erlang.terms agrees with the catalog; erlang.erl and the auto-import table are current
  freshness ROOT ERL WORK        erlang.terms matches what the host OTP (ERL, or "-" for none) observes
  program ROOT CLAU WORK SUFFIX FIXTURE  tests/fixtures/builtins/FIXTURE/main.erl links and prints "FIXTURE: ok"
"""

import filecmp
import pathlib
import shutil
import subprocess
import sys


def run(command, **options):
    """Run a command, failing with its output when it fails."""
    result = subprocess.run(command, capture_output=True, text=True, timeout=600, **options)
    if result.returncode != 0:
        raise AssertionError(f"{command}: {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def inventory(root):
    run([sys.executable, str(root / "tests/compiler/builtins/generate_erlang.py"), str(root), "--check"])
    print("builtins_erlang_inventory: ok")


def freshness(root, erl, work):
    if erl == "-":
        print("SKIP: no host Erlang/OTP")
        return
    escript = pathlib.Path(erl).with_name("escript.exe" if erl.lower().endswith(".exe") else "escript")
    work.mkdir(parents=True, exist_ok=True)
    observed = work / "erlang.terms"
    run([str(escript), str(root / "tests/compiler/builtins/observe_erlang.escript"), str(root), str(observed)])
    if not filecmp.cmp(observed, root / "library/stdlib/erlang.terms", shallow=False):
        raise AssertionError("library/stdlib/erlang.terms differs from the host OTP; rerun observe_erlang.escript")
    print("builtins_erlang_freshness: ok")


def program(root, clau, work, suffix, fixture):
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    source = root / "tests/fixtures/builtins" / fixture / "main.erl"
    run([clau, "-o", str(work / fixture), str(source)])
    output = run([str(work / (fixture + suffix))]).replace("\r\n", "\n")
    if output != f"{fixture}: ok\n":
        raise AssertionError(f"unexpected output: {output!r}")
    print(f"builtins_{fixture}: ok")


def main(arguments):
    action, root = arguments[0], pathlib.Path(arguments[1])
    if action == "inventory":
        inventory(root)
    elif action == "freshness":
        freshness(root, arguments[2], pathlib.Path(arguments[3]))
    else:
        program(root, arguments[2], pathlib.Path(arguments[3]), arguments[4], arguments[5])


if __name__ == "__main__":
    main(sys.argv[1:])
