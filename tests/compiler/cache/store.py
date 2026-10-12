"""Cache store (plan step 62D5): threads, damage and prune in one process, then several processes at once.

store.py STORE_TEST WORK: runs the focused cache_store program's modes, then starts four writer processes on one
database together and checks that a fresh process finds every entry each of them wrote.
"""
import pathlib
import shutil
import subprocess
import sys

PROCESSES = 4
COUNT = 60


def run(command):
    """Run one cache_store mode; fail with its output unless it succeeds."""
    result = subprocess.run([str(part) for part in command], capture_output=True, text=True, timeout=300,
                            check=False)
    assert result.returncode == 0, (command, result.stdout, result.stderr)
    return result.stdout


def main():
    tool, work = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve()
    for mode in ("threads", "damage", "prune"):
        output = run([tool, mode, work / mode])
        assert output.endswith("cache_store: ok\n"), output
        print(output.splitlines()[0])

    shared = work / "processes"
    shutil.rmtree(shared, ignore_errors=True)
    writers = [subprocess.Popen([str(tool), "write", str(shared), str(i), str(COUNT)], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True) for i in range(PROCESSES)]
    for writer in writers:
        out, err = writer.communicate(timeout=300)
        assert writer.returncode == 0, (out, err)
    output = run([tool, "check", shared, PROCESSES, COUNT])
    assert output.endswith("cache_store: ok\n"), output
    print(output.splitlines()[0])
    print("store: ok")


if __name__ == "__main__":
    main()
