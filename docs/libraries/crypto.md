# Crypto: OpenSSL integration, build configuration and FIPS

Clause implements the Erlang/OTP `crypto` module using OpenSSL's libcrypto (plan 21, step LIB79).
This document specifies OpenSSL version requirements, dependency resolution, source builds,
executable linking and FIPS mode configuration. See [builtins](../builtins.md#crypto-module) for
the builtin interface, [library](library.md) for the library module, and
[differences](../differences.md#crypto) for observable differences from Erlang/OTP. The `public_key`
module uses `crypto` for [key and signature operations](public_key.md#keys-and-signatures).

## OpenSSL versions

- **Pinned version: 4.1.0.** Clause downloads and builds this version when no alternative OpenSSL
  installation or source tree is selected.
- **Minimum version: 3.1.2.** The FIPS provider for this release is covered by FIPS 140-3 certificate
  #4985. The crypto runtime archive supports libcrypto versions 3.1.2 and later: its builtins use
  APIs available in 3.1.2 and handle version-dependent behavior at run time. Newer headers can
  alias existing API names to newer functions; for example, OpenSSL 4.1 maps `EVP_MD_CTX_get_size`
  to `EVP_MD_CTX_get_size_ex`, introduced in 3.4. The archive avoids these aliases to preserve
  compatibility. The full test suite links the crypto golden fixtures against libcrypto built
  from OpenSSL 3.1.2 source.
- **Algorithm availability: detected at run time.** The first `crypto` call attempts to load the
  `fips` provider, followed by `default`, `base` and `legacy`, matching OTP's provider initialization.
  It then fetches each digest, cipher and MAC listed in OTP's algorithm tables.
  `crypto:supports/0,1`, `hash_info/1`, `info/0` and `info_lib/0` report the capabilities of the
  linked OpenSSL. OpenSSL 3.1.2 does not provide ML-KEM, ML-DSA or SLH-DSA, so programs linked
  against that version omit these algorithms from the reported capabilities.

OpenSSL is licensed under Apache-2.0. OpenSSL source files are not committed to the Clause repository.

## OpenSSL dependency resolution

CMake resolves libcrypto for the crypto runtime archive through `cmake/OpenSSLDependencies.cmake`,
in the following order:

1. **Explicit path:** `-DCLAUSE_OPENSSL_ROOT=<path>` selects an installation prefix containing
   `include/openssl/opensslv.h`, or a source tree containing `Configure` and `VERSION.dat`.
   Source trees use the build procedure described below.
2. **System installation:** when `CLAUSE_OPENSSL_SYSTEM` is enabled, `find_package(OpenSSL)`
   searches for OpenSSL 3.1.2 or later and prefers static libcrypto. This option defaults to `ON`
   except on Windows.
3. **Pinned source release:** Clause downloads and extracts `openssl-4.1.0.tar.gz` into
   `thirdparty/openssl-4.1.0` after verifying its SHA-256 digest:
   `50c17baf54cb008f6ce42d50deb4b28050a1aad711636cabd4b241c7e90cc2fd`.
   The installation is cached at `thirdparty/openssl-4.1.0-<arch>-<crt>/install` and rebuilt when
   the compiler or build options change. `-DCLAUSE_DOWNLOAD_OPENSSL=OFF` disables the download.

`-DCLAUSE_RUNTIME_CRYPTO=OFF` excludes the crypto runtime archive from the build. When tests are
enabled, configuration also downloads `openssl-3.1.2.tar.gz` into `thirdparty/openssl-3.1.2` for
the full test suite. Its SHA-256 digest is
`a0ce69b8b97ea6a35b96875235aa453b966ba3cba8af2de23657d8b6767d6539`.
`-DCLAUSE_TEST_OPENSSL_MINIMUM=OFF` disables this download and the associated tests.

## Building OpenSSL from source

Source builds produce static libcrypto with the legacy provider included. The crypto builtins
load this provider during initialization, matching OTP. The build procedure uses these commands,
with options filtered for the selected OpenSSL version:

```text
Configure <target> no-shared no-module no-tests no-docs no-makedepend enable-ec_explicit_curves
          [enable-static-vcruntime] [--debug] --prefix=<install> --openssldir=<install>/ssl --libdir=lib
make build_libs && make install_dev
```

- **Compiler and C runtime:** `CC` selects CMake's C compiler. `enable-static-vcruntime` selects
  `/MT` for a static MSVC runtime build; `--debug` selects the Debug runtime variant.
- **Windows build tools:** configuration requires native Perl and NASM. Clause downloads
  Strawberry Perl 5.42.3.1 portable and NASM 3.02 into `thirdparty/tools/`. Their SHA-256 digests
  are `6a081a811781c30aca51dbc036afd93092af91e3297901f02c17043795a10690` and
  `161d0bfaff53c2f9e9f3e69fd0672323ebabafd1268976a5cec11be92a19aee7`, respectively.
  The build uses serial `nmake` and requires a Visual Studio developer environment. An initial
  build takes approximately four minutes.
- **Linux and macOS build tools:** builds use the system `perl` and `make -j`, with parallelism
  set to two thirds of the logical cores.
- **Version-specific options:** `enable-X` and `no-X` options are retained only when `X` appears
  in a `my @disablables... = (...)` list in the source tree's `Configure` script. OpenSSL 3.1.2
  does not recognize `no-docs`, `enable-ec_explicit_curves` or `enable-static-vcruntime`.
  Explicit curves are always enabled in that version, and its static builds already use `/MT`.
- **Cross-compilation:** `CLAUSE_OPENSSL_ROOT` must select an OpenSSL installation for the target.

### Build constraints and workarounds

- **Native Perl on Windows:** Git for Windows supplies an MSYS Perl that passes Unix paths to
  `Configure`. These paths are incompatible with `nmake`; Windows builds require native
  Strawberry Perl.
- **Out-of-tree builds with `no-apps`:** configuration fails because `apps/include` is missing.
  Clause leaves applications enabled and builds only the `build_libs` target.
- **Out-of-tree `install_fips` on Windows:** the generated makefile references
  `"apps\\openssl.exe"`, which `nmake` does not match to its `apps\openssl.exe` rule. Build
  `build_sw` and invoke `openssl fipsinstall` directly.
- **Static OpenSSL 3.x installation with clang-cl:** Windows `install_dev` attempts to copy
  `ossl_static.pdb`. Clang-cl uses embedded debug information (`/Z7`) and does not generate this
  file, so Clause supplies an empty placeholder. OpenSSL 4.0 removed this copy operation.
- **FIPS provider modules:** `no-module` disables FIPS provider builds. A FIPS provider requires
  a separate module; static libcrypto can load that module at run time.
- **Static libcrypto dependencies on Linux:** Ubuntu's static `libcrypto.a` requires `-lz -lzstd`,
  as specified by `Libs.private` in `libcrypto.pc`. Clause records the additional libraries
  required by the selected installation.

## Linking programs

The crypto builtins are compiled into `clause_runtime_crypto`, a separate archive from
`clause_runtime`. Programs that do not use `crypto` do not link this archive or libcrypto.
Linking libcrypto with its providers increases executable size by approximately 4.5 MB.

- **Archive selection:** `clau` links the archive when the compilation batch includes the library
  `crypto` module, as occurs for a static call to `crypto`. `--with-crypto` enables linking for
  programs that access `crypto` exclusively through dynamic calls, such as `beam_lib` debug
  information decryption. The startup object references the archive's extension entry to force
  archive inclusion; startup then registers its builtins. Programs without crypto support do
  not resolve or load OpenSSL.
- **Link and build metadata:** `clause_runtime_crypto.link`, stored beside the archive, lists
  libcrypto on its first line and the required system libraries on subsequent lines.
- **The ssl archive:** programs that call the [ssl engine](ssl.md#engine) also link
  `clause_runtime_ssl` and libssl from the same OpenSSL, before the crypto archive and libcrypto.
  `clause_runtime_ssl.link` names libssl, and `--with-crypto=PATH` selects libssl beside the
  selected libcrypto.
  `clause_runtime_crypto.openssl` records the source build recipe: version, minimum version,
  target, options, compiler, and Perl and NASM directories.
- **Alternative OpenSSL selection:** `--with-crypto=PATH` takes precedence over the
  `CLAUSE_OPENSSL_ROOT` environment variable. Either can select an OpenSSL 3.1.2 or later
  installation or source tree. For an installation, `clau` links its libcrypto. For a source
  tree, `clau` uses the recorded recipe to configure and build it on first use, caching the
  result under `<build-dir>/cache/openssl/<hash>`. On Windows, this build requires a Visual
  Studio developer shell for `nmake`.

## FIPS

### Runtime FIPS mode selection

The first `crypto` call attempts to load OpenSSL's `fips` provider. OpenSSL uses `OPENSSL_MODULES`
to locate provider modules and `OPENSSL_CONF` to locate a configuration that includes the
`fipsmodule.cnf` generated by `openssl fipsinstall` and activates the provider.

- `crypto:enable_fips_mode(true)` enables OpenSSL's default `fips=yes` property. It returns `true`
  on success and `false` when no provider can be loaded. `crypto:enable_fips_mode(false)` disables
  the property.
- `crypto:info_fips/0` returns `enabled` or `not_enabled` when a FIPS provider is loaded, and
  `not_supported` otherwise.
- `crypto:info/0` includes `fips_provider_buildinfo` when a FIPS provider is loaded.
- In FIPS mode, `crypto:supports/1` excludes algorithms unavailable from the FIPS provider and
  OTP's non-FIPS public key algorithms and curves: `eddsa`, `eddh`, `srp` and the Edwards curves.
  Calls to excluded algorithms raise OTP-compatible `notsup` errors with the applicable message:
  `"Bad digest type in FIPS"`, `"Digest type not supported in FIPS"`,
  `"MAC algorithm forbidden in FIPS"`, `"Digest algorithm for HMAC forbidden in FIPS"`,
  `"Cipher algorithm not supported in FIPS"` or `"Forbidden in FIPS"`.

Each algorithm table entry retains a separate fetch result obtained with `fips=yes`. FIPS mode
uses this result to select the FIPS provider's implementation. Public key operations fetch
algorithms using OpenSSL's default properties, which select the FIPS provider's implementations
when FIPS mode is enabled. In this mode, `eddsa` raises `"Unsupported algorithm in FIPS mode"`
and SRP raises `notsup`, matching OTP.

Clause does not read OTP's `fips_mode` application variable: it does not implement `on_load`, and
programs do not use `sys.config`. Programs must call `crypto:enable_fips_mode/1` or use the
mandatory FIPS mode build option described below.

### Mandatory FIPS mode: `clau --fips=PATH`

`--fips=PATH` builds an executable that requires FIPS mode and a user-supplied FIPS provider.
`PATH` must identify either a directory containing the provider module and `fipsmodule.cnf`, or
an OpenSSL installation. Each file is searched for directly in `PATH`, then in the listed
subdirectories:

| File | Search subdirectories, in order |
| --- | --- |
| `fips.dll` (Windows), `fips.so` (Linux), `fips.dylib` (macOS) | `lib/ossl-modules`, `lib64/ossl-modules`, `lib/<triplet>/ossl-modules` |
| `fipsmodule.cnf` | `ssl/`, `etc/ssl/` |

- **Build validation:** the build fails if either file is missing or if the configuration lacks
  `module-mac` in a `[fips_sect]` section. Generate the configuration with `openssl fipsinstall`.
- **Deployment files:** both files are copied into the executable's directory under the names
  listed above and must be deployed with the executable. The option implies `--with-crypto`.
  Standard OpenSSL selection rules apply; the selected libcrypto must support the supplied
  provider.
- **Startup initialization:** the startup object's FIPS entry,
  `CLAUSE_crypto_fips_extension_v1`, reads `fipsmodule.cnf` from the executable's directory and
  loads the adjacent provider module. The provider configuration contains the file's settings
  plus `module` and `activate`, supported by libcrypto 3.0 and later. Initialization loads only
  the `fips` and `base` providers, does not read the system OpenSSL configuration, and enables
  `fips=yes`.
- **Startup failure:** if provider loading or a provider self-test fails, for example because
  the module was modified or `module-mac` is incorrect, the program exits with status 70 and
  writes `crypto: cannot load the FIPS provider from <directory>: <OpenSSL's error>` to stderr.
  On Windows, module loading temporarily suppresses the modal "Bad Image" dialog.
- **Mode enforcement:** `crypto:enable_fips_mode(false)` returns `false`, and
  `crypto:info_fips/0` remains `enabled`.

### FIPS provider requirements

Clause does not build FIPS providers for deployment. A provider used for validated operation
must come from a validated release, such as OpenSSL 3.1.2, and be built according to its security
policy, or be supplied by an operating system vendor. OpenSSL permits a validated provider to
be used with other OpenSSL releases; a provider built from a non-validated release does not
qualify as a validated provider. Run the provider's `openssl fipsinstall` on the target machine
when required by its security policy.

The OpenSSL 3.1.2 FIPS provider loads into the pinned OpenSSL 4.1 libcrypto. The full test suite
builds this provider exclusively for testing and executes programs with both mandatory and
runtime-selectable FIPS modes.

## Tests

| Test | Suite | Coverage |
| --- | --- | --- |
| `executables_crypto_digests`, `executables_crypto_ciphers` | fast | OTP 29.1.1 golden fixtures: vectors, states, errors, detected algorithm lists and `info_fips` without a provider |
| `linking_crypto` | fast | Conditional archive linking, `--with-crypto[=PATH]`, `CLAUSE_OPENSSL_ROOT` and invalid input rejection |
| `linking_fips` | fast | Invalid `--fips` argument rejection, build failures for missing or malformed providers, deployment file copying and startup failures |
| `linking_crypto_source` | full | Initial build and cache reuse for the OpenSSL 4.1.0 source tree selected by `--with-crypto=SOURCE` |
| `linking_crypto_minimum` | full | Crypto fixture output matches the golden output when linked against libcrypto built from OpenSSL 3.1.2 source |
| `linking_fips_real` | full | OpenSSL 3.1.2 FIPS provider built with `enable-fips` for testing: mandatory FIPS mode enforcement, forbidden algorithms, startup failure after module modification, and runtime mode selection through `OPENSSL_MODULES` and `OPENSSL_CONF` |
