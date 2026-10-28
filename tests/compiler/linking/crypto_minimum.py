"""The oldest OpenSSL the crypto runtime links with (plan 21 step LIB79F): the crypto goldens, linked with a libcrypto
that clau builds once from the OpenSSL 3.1.2 source (--with-crypto=SOURCE), print exactly what their goldens hold. So
does the ssl engine's (plan 21 step LIB81B) with that release's libssl, whose servers verify client certificates
after the handshake instead of pausing it."""
import json
from pathlib import Path
import shutil
import subprocess
import sys

tool, root, directory, suffix, source = sys.argv[1:6]
work = Path(directory)
shutil.rmtree(work, ignore_errors=True)
work.mkdir(parents=True)
cases = Path(root) / "tests" / "fixtures" / "executables"

for case in ["crypto_digests", "crypto_ciphers", "ssl_engine"]:
    golden = json.loads((cases / case / "golden.json").read_text(encoding="utf-8"))
    build = [tool, str(cases / case / f"{case}.erl"), f"--with-crypto={source}", "--build-dir", str(work / "build"),
             "-o", case]
    built = subprocess.run(build, cwd=work, capture_output=True, text=True, timeout=3000)
    assert built.returncode == 0, built
    for run in golden["runs"]:
        result = subprocess.run([str(work / (case + suffix)), *run["args"]], cwd=work, capture_output=True,
                                text=True, timeout=300)
        assert result.returncode == run["exit_status"], (case, result)
        stdout = result.stdout.replace("\r\n", "\n")
        assert stdout == run["stdout"], (case, stdout)
    print(f"crypto_minimum: {case} matches its golden with OpenSSL {Path(source).name}")
print("crypto_minimum: ok")
