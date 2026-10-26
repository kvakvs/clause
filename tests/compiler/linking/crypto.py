"""The crypto runtime archive (plan 21 step LIB79): programs link it only when they compile the library crypto module
or ask with --with-crypto; --with-crypto=PATH and CLAUSE_OPENSSL_ROOT select another OpenSSL (an installation, or a
source tree built once into the build cache with --source); refused uses."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

tool, root, directory, suffix = sys.argv[1:5]
source_build = "--source" in sys.argv[5:]
work = Path(directory)
shutil.rmtree(work, ignore_errors=True)
work.mkdir(parents=True)
HASH = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"

(work / "plain.erl").write_text('-module(plain).\n-export([main/1]).\nmain(_) -> io:format("plain: ok~n").\n',
                                encoding="utf-8")
# A dynamic call names crypto only at run time, so the batch has no crypto module unless --with-crypto adds it.
(work / "dynamic.erl").write_text(
    "-module(dynamic).\n-export([main/1]).\n"
    "main(_) ->\n"
    "    Module = list_to_atom(\"cry\" ++ \"pto\"),\n"
    "    case catch erlang:apply(Module, hash, [sha256, <<\"abc\">>]) of\n"
    "        Digest when is_binary(Digest) -> io:format(\"~s~n\", [binary:encode_hex(Digest, lowercase)]);\n"
    "        {'EXIT', {Reason, _}} -> io:format(\"~p~n\", [Reason])\n"
    "    end,\n"
    "    io:format(\"dynamic: ok~n\").\n", encoding="utf-8")
(work / "static.erl").write_text(
    '-module(static).\n-export([main/1]).\n'
    'main(_) -> io:format("~s~nstatic: ok~n", [binary:encode_hex(crypto:hash(sha256, "abc"), lowercase)]).\n',
    encoding="utf-8")


def clau(arguments, status=0, environment=None):
    """Run clau in the work directory; returns its stderr after checking the exit status."""
    result = subprocess.run([tool, *arguments], cwd=work, capture_output=True, text=True, timeout=1800,
                            env=environment)
    assert result.returncode == status, (arguments, result)
    return result.stderr


def output(name):
    """Run a linked executable; returns its stdout."""
    result = subprocess.run([str(work / (name + suffix))], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, (name, result)
    return result.stdout.replace("\r\n", "\n")


def with_root(path):
    """The environment with CLAUSE_OPENSSL_ROOT set."""
    return dict(os.environ, CLAUSE_OPENSSL_ROOT=str(path))


bogus = work / "no-openssl"
# Programs without crypto never look at an OpenSSL: a bad CLAUSE_OPENSSL_ROOT does not matter to them.
clau(["plain.erl", "-o", "plain"], environment=with_root(bogus))
assert output("plain") == "plain: ok\n"
clau(["static.erl", "-o", "static"])
assert output("static") == f"{HASH}\nstatic: ok\n"
assert "neither an OpenSSL source tree" in clau(["static.erl", "-o", "bad"], 1, with_root(bogus))
print("crypto: linked only into programs that call crypto")

clau(["dynamic.erl", "-o", "without"])
assert output("without") == "undef\ndynamic: ok\n"
clau(["dynamic.erl", "--with-crypto", "-o", "with"])
assert output("with") == f"{HASH}\ndynamic: ok\n"
print("crypto: --with-crypto links it for dynamic callers")

# The recorded libcrypto of a source-built OpenSSL sits in <prefix>/lib: link that installation by its prefix.
environment = subprocess.run([tool, "--print-env", "dynamic.erl", "--with-crypto", "-o", "x"], cwd=work,
                             capture_output=True, text=True, timeout=60, check=True).stdout
assert "with_crypto = true" in environment, environment
runtime = Path(next(line.split("=", 1)[1].strip().strip('"') for line in environment.splitlines()
                    if line.startswith("runtime_library")))
link = next(runtime.parent.glob("*clause_runtime_crypto.link"), None)
libcrypto = Path(link.read_text(encoding="utf-8").splitlines()[0]) if link else None
prefix = libcrypto.parent.parent if libcrypto else None
if prefix and (prefix / "include" / "openssl" / "opensslv.h").exists():
    clau(["dynamic.erl", f"--with-crypto={prefix}", "-o", "installed"])
    assert output("installed") == f"{HASH}\ndynamic: ok\n"
    clau(["static.erl", "-o", "rooted"], environment=with_root(prefix))
    assert output("rooted") == f"{HASH}\nstatic: ok\n"
    print("crypto: --with-crypto=PREFIX and CLAUSE_OPENSSL_ROOT link an installation")
assert "neither an OpenSSL source tree" in clau(["dynamic.erl", f"--with-crypto={bogus}", "-o", "bad"], 1)
assert "--with-crypto requires --output" in clau(["dynamic.erl", "--with-crypto"], 2)
assert "--with-crypto specified more than once" in clau(["dynamic.erl", "--with-crypto", "--with-crypto", "-o", "x"], 2)
assert "expected an OpenSSL path" in clau(["dynamic.erl", "--with-crypto=", "-o", "x"], 2)
print("crypto: refused uses")

# A source tree is configured and built once into the build cache, then reused (minutes: --source only).
if source_build:
    tree = Path(root) / "thirdparty" / "openssl-4.1.0"
    build = ["dynamic.erl", f"--with-crypto={tree}", "--build-dir", str(work / "build"), "-o", "sourced"]
    clau(build)
    assert output("sourced") == f"{HASH}\ndynamic: ok\n"
    built = list((work / "build" / "cache" / "openssl").glob("*/install/.complete"))
    assert len(built) == 1, built
    clau(build)
    assert list((work / "build" / "cache" / "openssl").glob("*/install/.complete")) == built
    print("crypto: --with-crypto=SOURCE builds the tree once")
print("crypto: ok")
