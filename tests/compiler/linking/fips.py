"""crypto's FIPS mode (plan 21 step LIB79F): clau --fips=PATH pins a program to a user-supplied FIPS provider, which
is found and copied beside the executable or the build fails, and whose loading failure stops the program before it
runs. With --real SOURCE, the FIPS provider of the OpenSSL source tree SOURCE (3.1.2, the latest validated one) is
built with enable-fips (test-only, minutes) and loaded into the pinned libcrypto, for a pinned program (FIPS mode
stays on, forbidden algorithms raise OTP's errors) and a switchable one (OPENSSL_MODULES/OPENSSL_CONF as OTP finds a
provider; enable_fips_mode/1 switches)."""
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

tool, root, directory, suffix = sys.argv[1:5]
real = sys.argv[sys.argv.index("--real") + 1] if "--real" in sys.argv[5:] else None
work = Path(directory)
shutil.rmtree(work, ignore_errors=True)
work.mkdir(parents=True, exist_ok=True)
MODULE = "fips.dll" if os.name == "nt" else ("fips.dylib" if sys.platform == "darwin" else "fips.so")
CONFIG = "[fips_sect]\nactivate = 1\nmodule-mac = 00:11:22\n"

# The probe prints what FIPS mode makes of each call: the mode, switching it, and allowed and forbidden algorithms.
(work / "probe.erl").write_text("""-module(probe).
-export([main/1]).

caught(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:{Class, {_File, _Line}, Description} -> {Class, Description};
        Class:Reason -> {Class, Reason}
    end.

show(Label, Value) -> io:format("~s ~p~n", [Label, Value]).

checks() ->
    show(mode, crypto:info_fips()),
    show(sha256, binary:encode_hex(crypto:hash(sha256, <<"abc">>), lowercase)),
    show(md5, caught(fun() -> crypto:hash(md5, <<"abc">>) end)),
    show(md5_init, caught(fun() -> crypto:hash_init(md5) end)),
    show(listed, [lists:member(md5, crypto:supports(hashs)), lists:member(chacha20_poly1305, crypto:supports(ciphers)),
                  lists:member(poly1305, crypto:supports(macs)), lists:member(aes_256_gcm, crypto:supports(ciphers))]),
    show(poly1305, caught(fun() -> crypto:mac(poly1305, binary:copy(<<1>>, 32), <<"x">>) end)),
    show(hmac_md5, caught(fun() -> crypto:mac(hmac, md5, <<"k">>, <<"x">>) end)),
    show(chacha20, caught(fun() -> crypto:crypto_one_time(chacha20, binary:copy(<<1>>, 32), binary:copy(<<2>>, 16), <<"x">>, true) end)),
    show(chacha20_poly1305, caught(fun() -> crypto:crypto_one_time_aead(chacha20_poly1305, binary:copy(<<1>>, 32), binary:copy(<<2>>, 12), <<"x">>, <<>>, true) end)),
    {Text, Tag} = crypto:crypto_one_time_aead(aes_256_gcm, binary:copy(<<1>>, 32), binary:copy(<<2>>, 12), <<"x">>, <<>>, true),
    show(gcm, byte_size(<<Text/binary, Tag/binary>>)).

main(["pinned"]) ->
    checks(),
    show(disable, crypto:enable_fips_mode(false)),
    show(still, crypto:info_fips()),
    show(buildinfo, is_list(maps:get(fips_provider_buildinfo, crypto:info()))),
    io:format("probe: ok~n");
main(["switch"]) ->
    show(mode, crypto:info_fips()),
    show(md5_before, caught(fun() -> byte_size(crypto:hash(md5, <<"abc">>)) end)),
    show(enable, crypto:enable_fips_mode(true)),
    checks(),
    show(disable, crypto:enable_fips_mode(false)),
    show(again, crypto:info_fips()),
    show(md5_after, caught(fun() -> byte_size(crypto:hash(md5, <<"abc">>)) end)),
    io:format("probe: ok~n").
""", encoding="utf-8")


def clau(arguments, status=0, environment=None):
    """Run clau in the work directory; returns its stderr after checking the exit status."""
    result = subprocess.run([tool, *arguments], cwd=work, capture_output=True, text=True, timeout=1800,
                            env=environment)
    assert result.returncode == status, (arguments, result)
    return result.stderr


def run(name, argument, environment=None):
    """Run a linked executable; returns the completed process."""
    return subprocess.run([str(work / (name + suffix)), argument], capture_output=True, text=True, timeout=120,
                          env=environment)


def provider(name, module=True, config=CONFIG):
    """A provider directory with a module file (fake bytes unless copied) and an optional settings file."""
    path = work / name
    path.mkdir()
    if module:
        (path / MODULE).write_bytes(b"not a provider module")
    if config is not None:
        (path / "fipsmodule.cnf").write_text(config, encoding="utf-8")
    return path


# Refused options.
assert "--fips expects a FIPS provider path" in clau(["probe.erl", "--fips", "-o", "x"], 2)
assert "expected a FIPS provider path after --fips=" in clau(["probe.erl", "--fips=", "-o", "x"], 2)
assert "--fips specified more than once" in clau(["probe.erl", "--fips=a", "--fips=b", "-o", "x"], 2)
assert "--fips requires --output" in clau(["probe.erl", "--fips=a"], 2)
print("fips: refused options")

# A missing or malformed provider fails the build, and nothing is written.
assert f"no FIPS provider module {MODULE}" in clau(["probe.erl", f"--fips={provider('empty', False, None)}", "-o", "a"], 1)
assert "no fipsmodule.cnf" in clau(["probe.erl", f"--fips={provider('no_config', config=None)}", "-o", "b"], 1)
bad = provider("bad_config", config="[fips_sect]\nactivate = 1\n")
assert "has no module-mac in a [fips_sect] section" in clau(["probe.erl", f"--fips={bad}", "-o", "c"], 1)
assert not any((work / (name + suffix)).exists() for name in "abc")
print("fips: missing or malformed providers fail the build")

# A provider in an OpenSSL installation's layout is found and copied beside the executable; a module that does not
# load stops the program before it runs.
installed = work / "installed"
(installed / "lib" / "ossl-modules").mkdir(parents=True)
(installed / "ssl").mkdir()
(installed / "lib" / "ossl-modules" / MODULE).write_bytes(b"not a provider module")
(installed / "ssl" / "fipsmodule.cnf").write_text(CONFIG, encoding="utf-8")
environment = subprocess.run([tool, "--print-env", "probe.erl", f"--fips={installed}", "-o", "fake"], cwd=work,
                             capture_output=True, text=True, timeout=60, check=True).stdout
assert "fips = " in environment and "installed" in environment, environment
clau(["probe.erl", f"--fips={installed}", "-o", "fake"])
assert (work / MODULE).read_bytes() == b"not a provider module"
assert (work / "fipsmodule.cnf").read_text(encoding="utf-8") == CONFIG
started = run("fake", "pinned")
assert started.returncode != 0 and "probe: ok" not in started.stdout, started
assert "crypto: cannot load the FIPS provider from" in started.stderr, started
(work / "fipsmodule.cnf").unlink()
started = run("fake", "pinned")
assert started.returncode != 0 and "crypto: cannot read" in started.stderr, started
print("fips: providers are copied beside the program, which stops when they do not load")


def recipe():
    """The OpenSSL source build recipe the crypto archive recorded (clause_runtime_crypto.openssl)."""
    printed = subprocess.run([tool, "--print-env", "probe.erl", "--with-crypto", "-o", "x"], cwd=work,
                             capture_output=True, text=True, timeout=60, check=True).stdout
    runtime = Path(next(line.split("=", 1)[1].strip().strip('"') for line in printed.splitlines()
                        if line.startswith("runtime_library")))
    text = next(runtime.parent.glob("*clause_runtime_crypto.openssl")).read_text(encoding="utf-8")
    return dict(line.split("=", 1) for line in text.splitlines() if "=" in line)


def known_options(source, options):
    """The Configure options the source tree knows: enable-X and no-X when X is in its @disablables lists."""
    configure = (Path(source) / "Configure").read_text(encoding="utf-8", errors="replace")
    lists = re.findall(r"my @disablables\w* = (?:qw)?\(([^;]*)\);", configure)
    features = set(re.findall(r"[A-Za-z0-9_\-]+", " ".join(re.sub(r"#[^\n]*", "", text) for text in lists)))
    return [option for option in options
            if not option.startswith(("enable-", "no-")) or option.split("-", 1)[1] in features]


def build_provider(source):
    """Build the FIPS provider of `source` with enable-fips and write its fipsmodule.cnf (test-only): a directory
    holding both."""
    settings = recipe()
    build, provided = work / "openssl" / "build", work / "openssl" / "provider"
    build.mkdir(parents=True)
    provided.mkdir()
    environment = dict(os.environ)
    environment["PATH"] = os.pathsep.join([*filter(None, settings.get("tools", "").split("|")), environment["PATH"]])
    if settings.get("cc"):
        environment["CC"] = settings["cc"]
    options = [option for option in known_options(source, settings["options"].split()) if option != "no-module"]
    configure = [shutil.which("perl", path=environment["PATH"]), str(Path(source) / "Configure"),
                 *filter(None, [settings.get("target")]), *options, "enable-fips"]
    make = ([shutil.which("nmake", path=environment["PATH"]), "/nologo"] if os.name == "nt"
            else ["make", f"-j{max(1, (os.cpu_count() or 3) * 2 // 3)}"])
    program = build / "apps" / ("openssl.exe" if os.name == "nt" else "openssl")
    built_module = build / "providers" / MODULE
    fipsinstall = [str(program), "fipsinstall", "-module", str(built_module), "-out", str(provided / "fipsmodule.cnf")]
    for command in (configure, [*make, "build_sw"], fipsinstall):
        result = subprocess.run(command, cwd=build, env=environment, capture_output=True, text=True, timeout=3600)
        assert result.returncode == 0, (command, result.stdout[-4000:], result.stderr[-4000:])
    shutil.copy2(built_module, provided / MODULE)
    return provided


if real:
    install = build_provider(real)
    clau(["probe.erl", f"--fips={install}", "-o", "pinned"])
    pinned = run("pinned", "pinned")
    assert pinned.returncode == 0, pinned
    expected = [
        "mode enabled",
        "md5 {notsup,\"Bad digest type in FIPS\"}",
        "md5_init {notsup,\"Digest type not supported in FIPS\"}",
        "listed [false,false,false,true]",
        "poly1305 {notsup,\"MAC algorithm forbidden in FIPS\"}",
        "hmac_md5 {notsup,\"Digest algorithm for HMAC forbidden in FIPS\"}",
        "chacha20 {notsup,\"Forbidden in FIPS\"}",
        "chacha20_poly1305 {notsup,\"Forbidden in FIPS\"}",
        "gcm 17",
        "disable false",
        "still enabled",
        "buildinfo true",
        "probe: ok",
    ]
    for line in expected:
        assert line in pinned.stdout.splitlines(), (line, pinned.stdout)
    print("fips: a pinned program stays in FIPS mode")
    module = work / MODULE
    module.write_bytes(module.read_bytes() + b"\0")
    tampered = run("pinned", "pinned")
    assert tampered.returncode != 0 and "cannot load the FIPS provider" in tampered.stderr, tampered
    print("fips: a tampered provider stops the program")

    clau(["probe.erl", "--with-crypto", "-o", "switch"])
    config = work / "openssl.cnf"
    config.write_text("openssl_conf = openssl_init\n"
                      f".include {(install / 'fipsmodule.cnf').as_posix()}\n"
                      "[openssl_init]\nproviders = provider_sect\n"
                      "[provider_sect]\nfips = fips_sect\ndefault = default_sect\n"
                      "[default_sect]\nactivate = 1\n", encoding="utf-8")
    environment = dict(os.environ, OPENSSL_CONF=str(config), OPENSSL_MODULES=str(install))
    switched = run("switch", "switch", environment)
    assert switched.returncode == 0, switched
    for line in ["mode not_enabled", "md5_before {value,16}", "enable true", "mode enabled",
                 "md5 {notsup,\"Bad digest type in FIPS\"}", "disable true", "again not_enabled",
                 "md5_after {value,16}", "probe: ok"]:
        assert line in switched.stdout.splitlines(), (line, switched.stdout)
    unprovided = run("switch", "switch")
    assert "mode not_supported" in unprovided.stdout and "enable false" in unprovided.stdout, unprovided
    print("fips: a switchable program enables and disables FIPS mode")
print("fips: ok")
