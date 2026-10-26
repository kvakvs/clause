# Crypto: OpenSSL, building and FIPS

Clause's `crypto` (plan 21 step LIB79) answers OTP's `crypto` module over OpenSSL's libcrypto. This
note covers which OpenSSL is used and how it is found, how Clause and `clau` build it from source,
how programs link it, and FIPS mode. The builtins themselves are described in
[builtins](../builtins.md#crypto-module), the library module in [library](library.md), and the
observable differences from OTP in [differences](../differences.md#crypto). `public_key`'s keys and
signatures call it ([public_key](public_key.md#keys-and-signatures)).

## OpenSSL versions

- **Pinned: 4.1.0.** The release Clause downloads and builds when no other OpenSSL is given.
- **Minimum: 3.1.2.** The latest release whose FIPS provider is validated (FIPS 140-3 certificate
  #4985). Any libcrypto from 3.1.2 on links with the crypto archive: the builtins use only API
  present in 3.1.2 and decide version differences at run time, so one archive serves every such
  libcrypto. Newer headers can still map an old name to a newer function (4.1's
  `EVP_MD_CTX_get_size` is 3.4's `EVP_MD_CTX_get_size_ex`), so the archive avoids such names; the
  full test suite links the crypto goldens with a libcrypto built from the 3.1.2 source.
- **Algorithms are probed, not assumed.** The first `crypto` call loads the providers (`fips` when
  loadable, then `default`, `base` and `legacy`, as OTP does) and fetches every digest, cipher and
  MAC of OTP's tables; `crypto:supports/0,1`, `hash_info/1`, `info/0` and `info_lib/0` report what
  the linked OpenSSL has. A program linked with 3.1.2 simply lists fewer algorithms (no ML-KEM,
  ML-DSA or SLH-DSA).

OpenSSL is Apache-2.0 licensed; no OpenSSL file is committed.

## Where Clause's OpenSSL comes from

CMake (`cmake/OpenSSLDependencies.cmake`) resolves libcrypto for the crypto archive in this order:

1. `-DCLAUSE_OPENSSL_ROOT=<path>`: an installed prefix (`include/openssl/opensslv.h`) or a source
   tree (`Configure`, `VERSION.dat`), which is built as in 3.
2. An installed OpenSSL ≥ 3.1.2 (`CLAUSE_OPENSSL_SYSTEM`, ON by default except on Windows):
   `find_package(OpenSSL)`, static libcrypto preferred.
3. The pinned release, downloaded into `thirdparty/openssl-4.1.0` and verified
   (`openssl-4.1.0.tar.gz`, SHA-256
   `50c17baf54cb008f6ce42d50deb4b28050a1aad711636cabd4b241c7e90cc2fd`), configured and built once
   into `thirdparty/openssl-4.1.0-<arch>-<crt>/install` (rebuilt when the compiler or options
   change); `-DCLAUSE_DOWNLOAD_OPENSSL=OFF` disables the download.

`-DCLAUSE_RUNTIME_CRYPTO=OFF` leaves the crypto archive out altogether. With tests enabled,
configure also downloads the 3.1.2 source (`openssl-3.1.2.tar.gz`, SHA-256
`a0ce69b8b97ea6a35b96875235aa453b966ba3cba8af2de23657d8b6767d6539`) into `thirdparty/openssl-3.1.2`
for the full suite's tests; `-DCLAUSE_TEST_OPENSSL_MINIMUM=OFF` skips it and those tests.

## Building OpenSSL from source

A source build makes a static libcrypto with the legacy provider built in (which the builtins load
as OTP does):

```
Configure <target> no-shared no-module no-tests no-docs no-makedepend enable-ec_explicit_curves
          [enable-static-vcruntime] [--debug] --prefix=<install> --openssldir=<install>/ssl --libdir=lib
make build_libs && make install_dev
```

- **Compiler and runtime.** CMake's C compiler builds it (`CC`); `enable-static-vcruntime` selects
  `/MT` when the build uses the static MSVC runtime, `--debug` the Debug one.
- **Windows tools.** Configure needs a native Perl and NASM: pinned Strawberry Perl 5.42.3.1
  portable (SHA-256 `6a081a811781c30aca51dbc036afd93092af91e3297901f02c17043795a10690`) and NASM
  3.02 (SHA-256 `161d0bfaff53c2f9e9f3e69fd0672323ebabafd1268976a5cec11be92a19aee7`) are downloaded
  into `thirdparty/tools/`. `nmake` then builds serially (about 4 minutes once), so a Visual Studio
  developer environment is needed. Linux and macOS use their `perl` and `make -j` (two thirds of the
  cores).
- **Older source trees get only the options they know.** enable-X and no-X are kept when X is in one
  of the tree's `my @disablables... = (...)` lists in `Configure`: 3.1.2 knows neither `no-docs` nor
  `enable-ec_explicit_curves` (explicit curves are always in) nor `enable-static-vcruntime` (its
  static builds already use `/MT`).
- **Cross builds** take the target's installed OpenSSL through `CLAUSE_OPENSSL_ROOT`.

### Pitfalls met on the way

- **Git's MSYS Perl breaks Configure.** It hands Configure Unix paths, which `nmake` cannot use:
  Windows builds need the native Strawberry Perl, never the `perl` that Git for Windows puts on
  `PATH`.
- **Out-of-tree builds with `no-apps` fail** (`apps/include` is missing), so Clause keeps the apps
  enabled and builds the `build_libs` target instead of everything.
- **Out-of-tree `install_fips` fails on Windows.** The makefile lists the program as
  `"apps\\openssl.exe"`, which `nmake` does not match with its own `apps\openssl.exe` rule. Build
  `build_sw` and run `openssl fipsinstall` yourself instead.
- **3.x installs a PDB that clang-cl never writes.** The Windows `install_dev` of a static 3.x build
  copies `ossl_static.pdb`, which clang-cl's embedded debug information (`/Z7`) does not produce;
  Clause gives it an empty one (4.0 dropped the copy).
- **`no-module` disables FIPS.** Configure turns `fips` off with `no-module`: a FIPS provider is
  always a separate module, never part of a static libcrypto (which can still load one).
- **Static libcrypto on Linux needs compression libraries.** Ubuntu's static `libcrypto.a` wants
  `-lz -lzstd` (`libcrypto.pc`'s `Libs.private`); the libraries an install needs are recorded with
  it.

## Linking programs

The builtins live in their own archive, `clause_runtime_crypto` beside `clause_runtime`, so programs
that never call `crypto` stay as small as before; libcrypto with its providers adds about 4.5 MB to
those that do.

- **When it is linked.** `clau` links the archive when the batch compiled the library `crypto`
  module (a static call to `crypto`), or with `--with-crypto` for programs that reach `crypto` only
  through dynamic calls (`beam_lib`'s decryption of debug information is one). The startup object
  then lists the archive's extension entry, which pulls the archive in, and startup registers its
  builtins. Programs without `crypto` never look at an OpenSSL.
- **What is linked.** `clause_runtime_crypto.link` beside the archive names libcrypto (first line)
  and its system libraries; `clause_runtime_crypto.openssl` records how a source tree is built
  (version, minimum, target, options, compiler, Perl and NASM directories).
- **Another OpenSSL.** `--with-crypto=PATH`, else the `CLAUSE_OPENSSL_ROOT` environment variable,
  links a different OpenSSL ≥ 3.1.2: an installation's libcrypto, or a source tree that `clau`
  configures and builds with the recorded recipe into `<build-dir>/cache/openssl/<hash>` on first
  use (in a Visual Studio developer shell on Windows, for `nmake`).

## FIPS

### Switchable mode (as OTP)

The first `crypto` call also tries to load OpenSSL's `fips` provider, which OpenSSL finds through
its own `OPENSSL_MODULES` (the module directory) and `OPENSSL_CONF` (a configuration that includes
the `fipsmodule.cnf` that `openssl fipsinstall` wrote, and activates the provider). Then:

- `crypto:enable_fips_mode(true)` turns on OpenSSL's default `fips=yes` property (`true` when it
  could, `false` without a loadable provider); `crypto:enable_fips_mode(false)` turns it off.
- `crypto:info_fips/0` answers `enabled` or `not_enabled`, and `not_supported` when no FIPS provider
  loaded.
- `crypto:info/0` adds `fips_provider_buildinfo` when a FIPS provider is loaded.
- In FIPS mode, the algorithms the provider lacks are left out of `crypto:supports/1` (and OTP's
  non-FIPS public keys and curves: `eddsa`, `eddh`, `srp`, the Edwards curves) and raise OTP's
  `notsup` errors: `"Bad digest type in FIPS"`,`"Digest type not supported in FIPS"`,`"MAC
  algorithm forbidden in FIPS"`, `"Digest algorithm for HMAC forbidden in FIPS"`, `"Cipher algorithm
  not supported in FIPS"`, `"Forbidden in FIPS"`. Every table entry keeps a second fetch with
  `fips=yes`, used while FIPS mode is on, so FIPS mode computes with the provider's own
  implementations.

Public key operations fetch their algorithms through OpenSSL's default properties, so in FIPS mode
they run on the provider's implementations; `eddsa` raises `"Unsupported algorithm in FIPS mode"`
and SRP `notsup`, as OTP's.

OTP's `fips_mode` application variable is not read (Clause has no `on_load` and programs have no
`sys.config`): programs call `crypto:enable_fips_mode/1`, or are built pinned.

### Pinned programs: `clau --fips=PATH`

`--fips=PATH` builds a program that always runs in FIPS mode, with a user-supplied FIPS provider.
`PATH` is either a directory holding the provider module and its `fipsmodule.cnf`, or an OpenSSL
installation:

| File | Looked for in `PATH`, then |
| --- | --- |
| `fips.dll` (Windows), `fips.so` (Linux), `fips.dylib` (macOS) | `lib/ossl-modules`, `lib64/ossl-modules`, `lib/<triplet>/ossl-modules` |
| `fipsmodule.cnf` | `ssl/`, `etc/ssl/` |

- **The build fails** when the module or the settings file is missing, or the settings have no
  `module-mac` in a `[fips_sect]` section (run `openssl fipsinstall`).
- **Both files are copied beside the executable**, under the names above; deploy them with it. The
  option implies `--with-crypto`; the libcrypto is chosen as usual and must accept the provider.
- **Before the program runs**, the startup object's FIPS entry (`CLAUSE_crypto_fips_extension_v1`)
  reads `fipsmodule.cnf` beside the executable, loads the module beside it through an OpenSSL
  provider configuration (the file's settings plus `module` and `activate`, which every libcrypto
  from 3.0 takes), loads only OpenSSL's `base` provider besides it (never `default` or `legacy`; the
  system's OpenSSL configuration is not read) and turns on `fips=yes`.
- **When the provider does not load** or fails its self-test (a changed module, a wrong
  `module-mac`), the program stops with exit status 70 and
  `crypto: cannot load the FIPS provider from <directory>: <OpenSSL's error>` on stderr. Windows
  shows no "Bad Image" dialog for a broken module: the error mode is quiet while it loads.
- **The mode cannot be left.** `crypto:enable_fips_mode(false)` returns `false`, and
  `crypto:info_fips/0` stays `enabled`.

### Which FIPS provider

Clause never builds a FIPS provider for deployment. OpenSSL allows other releases to use a validated
FIPS provider but forbids using a self-built one from a non-validated release: take the provider of
a validated release (3.1.2 as of this writing, built as its security policy prescribes, or one
shipped by an operating system vendor) and run its `openssl fipsinstall` on the target machine where
the policy requires it. The 3.1.2 provider loads into the pinned 4.1 libcrypto: the full suite
builds it (for the test only) and runs pinned and switchable programs with it.

## Tests

| Test | Suite | What it checks |
| --- | --- | --- |
| `executables_crypto_digests`, `executables_crypto_ciphers` | fast | Goldens from OTP 29.1.1: vectors, states, errors, probed lists, `info_fips` without a provider |
| `linking_crypto` | fast | The archive is linked only when used; `--with-crypto[=PATH]`, `CLAUSE_OPENSSL_ROOT`, refusals |
| `linking_fips` | fast | `--fips` refusals, missing or malformed providers fail the build, copying, start failures |
| `linking_crypto_source` | full | `--with-crypto=SOURCE` builds the 4.1.0 tree once into the build cache |
| `linking_crypto_minimum` | full | The crypto goldens linked with a libcrypto built from the 3.1.2 source print their goldens |
| `linking_fips_real` | full | 3.1.2's FIPS provider (built with `enable-fips` for the test) in a pinned program (stays in FIPS mode, forbidden algorithms, tampered module stops it) and a switchable one (`OPENSSL_MODULES`/`OPENSSL_CONF`) |
