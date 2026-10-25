"""The interactive shell linked with --shell (plan 21 step LIB74): the executable starts an Erlang shell on standard
input, its inputs' modules and the whole library callable from it; the project option shell selects the same, and
--shell needs an executable. The shell's own behaviour against OTP is the executables golden shell_session."""
import json
from pathlib import Path
import shutil
import subprocess
import sys

tool, root, directory, suffix = sys.argv[1:]
work = Path(directory)
shutil.rmtree(work, ignore_errors=True)
work.mkdir(parents=True)
module = work / "mymod.erl"
module.write_text("-module(mymod).\n-export([double/1]).\ndouble(X) -> 2 * X.\n", encoding="utf-8")

# A command on the batch's module, then on library modules no input names.
SESSION = 'mymod:double(21).\nstring:uppercase("abc").\nqueue:to_list(queue:from_list([1, 2])).\n'
EXPECTED = ("\nEshell V17.1 (press Ctrl+G to abort, type help(). for help)\n"
            '1> 42\n2> "ABC"\n3> [1,2]\n4> *** Terminating erlang (nonode@nohost)\n')


def clau(arguments, status=0):
    """Run clau in the work directory; returns its stdout after checking the exit status."""
    result = subprocess.run([tool, *arguments], cwd=work, capture_output=True, text=True, timeout=600)
    assert result.returncode == status, (arguments, result)
    return result.stdout + result.stderr


# Without --entry the shell is the program: it reads the session and halts at its end with status 0.
clau(["--shell", "-o", "shell", str(module)])
result = subprocess.run([str(work / ("shell" + suffix))], input=SESSION, capture_output=True, text=True, timeout=60)
assert result.returncode == 0, result
lines = result.stdout.replace("\r\n", "\n").split("\n", 1)
assert lines[0].startswith("Clause compiled Erlang/OTP 29 "), result.stdout
assert lines[1] == EXPECTED, result.stdout
print("shell: --shell links the shell, the inputs and the library")

# --shell links a program from the library alone; the project option shell selects it for its target.
assert "shell = true" in clau(["--print-env", "--shell", "-o", "bare"])
manifest = work / "project.toml"
manifest.write_text(f'schema_version = 1\n\n[[targets]]\nname = "app"\nsources = [{json.dumps(module.name)}]\n'
                    'source_dirs = []\noutput = "project"\n\n[targets.options]\nshell = true\n', encoding="utf-8")
assert "shell = true" in clau(["--print-env", "--project", str(manifest)])
manifest.write_text(manifest.read_text(encoding="utf-8").replace("= true", '= "yes"'), encoding="utf-8")
assert "expected a boolean" in clau(["--project", str(manifest)], 1)
assert "--shell requires --output" in clau(["--shell", str(module)], 2)
print("shell: library-only programs, the project option and refused uses")
