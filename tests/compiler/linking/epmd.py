"""clause-epmd beside programs (plan 30 step DIST3c): the project option epmd_executable selects what
--epmd-executable selects, and both need an executable; bundled_epmd = false selects what --no-bundled-epmd selects
(DIST3d). Linking clause-epmd, a program without the bundled epmd and their behaviour against OTP's epmd are the
executables golden epmd_executable."""
import json
from pathlib import Path
import shutil
import subprocess
import sys

tool, directory = sys.argv[1:]
work = Path(directory)
shutil.rmtree(work, ignore_errors=True)
work.mkdir(parents=True)
module = work / "app.erl"
module.write_text("-module(app).\n-export([main/1]).\nmain(_) -> ok.\n", encoding="utf-8")


def clau(arguments, status=0):
    """Run clau in the work directory; returns its output after checking the exit status."""
    result = subprocess.run([tool, *arguments], cwd=work, capture_output=True, text=True, timeout=300)
    assert result.returncode == status, (arguments, result)
    return result.stdout + result.stderr


default = clau(["--print-env", "-o", "app", str(module)])
assert "epmd_executable = false" in default and "bundled_epmd = true" in default
assert "bundled_epmd = false" in clau(["--print-env", "--no-bundled-epmd", "-o", "app", str(module)])
assert "epmd_executable = true" in clau(["--print-env", "--epmd-executable", "-o", "app", str(module)])
manifest = work / "project.toml"
manifest.write_text(f'schema_version = 1\n\n[[targets]]\nname = "app"\nsources = [{json.dumps(module.name)}]\n'
                    'source_dirs = []\noutput = "app"\n\n[targets.options]\nepmd_executable = true\n',
                    encoding="utf-8")
assert "epmd_executable = true" in clau(["--print-env", "--project", str(manifest)])
manifest.write_text(manifest.read_text(encoding="utf-8") + "bundled_epmd = false\n", encoding="utf-8")
assert "bundled_epmd = false" in clau(["--print-env", "--project", str(manifest)])
manifest.write_text(manifest.read_text(encoding="utf-8").replace("= true", '= "yes"'), encoding="utf-8")
assert "expected a boolean" in clau(["--project", str(manifest)], 1)
assert "--epmd-executable requires --output" in clau(["--epmd-executable", str(module)], 2)
print("epmd: the project options epmd_executable and bundled_epmd, and refused uses: ok")
