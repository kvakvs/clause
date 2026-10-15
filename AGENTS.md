# Abstract / Intent of Clause Project

This project researches and implements a new target language for LLVM (Clang is
assumed to be available) which is latest Erlang version 29. Supported platforms:
Windows (x86 targets), Linux (for x86 and ARM targets), MacOS for Apple Silicon
ARM targets.

The expected modules this project will have:
- Preprocessor of Erlang
- Parser of Erlang
- Compiler/transformer of Erlang abstract code into LLVM abstract code
- Tests?
- Interface between compiler stages to be either text or internal parsed data
  format. Intermediate stage parsers can be done later do not plan more than
  directory source locations for them.

# Extra Resources

`references/otp` is a gitignored clone of Erlang OTP source repository used to
look at tests and other implementation details to comply with.

Track the latest official `maint-29` branch, not a fixed release tag or
`master`. At the start of future OTP-dependent work, check upstream and refresh
the pin when the branch advances. Keep `references/otp-pin.cmake`, the checkout,
corpus hashes, grammar evidence and current documentation synchronized; follow
`docs/otp-reference.md`. Preserve historical validation records with their
original revisions. Never silently change the reference during configuration or
tests.

# End Goal

The source code of existing pure Erlang projects will be buildable via LLVM into
runnable executables which retain most of Erlang features (isolated memory,
process execution and task switching, message passing, garbage collection etc).
The support for dynamic code loading and code upgrades can be implemented as
separate dynamic SO/DLL modules, or dropped.

# Questions to Answer

(Replace text here in place as the answers are derived)
- Implementation language: C++23 for the preprocessor, parser, compiler tool,
  and a separately built C++ runtime. Use CMake; all project targets treat
  compiler warnings as errors.
- Project structure: compiler executable `clau` and separate runtime library
  `clause_runtime`, in one repository. See `README.md` for current usage,
  `.agents/00-finished.md` for completed work and the outstanding
  compiler/runtime checklist. Stages exchange internal owned data; public
  interchange remains deferred.
- Future directories: `compiler/`, `runtime/`, `abi/`, `cmake/`, `tests/`,
  `examples/`, and `docs/`; see `.agents/files.md` for component locations.
  Intermediate stage readers have reserved directory locations only.

Use `.agents/aimemory.md` for AI notes and memory, this file will not be read by
humans.

Delegated subagents (search, doc cleanup, review, mechanical edits) run on a
faster model such as Sonnet; Claude Code applies this through
`CLAUDE_CODE_SUBAGENT_MODEL` in `.claude/settings.json`. Override per task only
when the work needs deeper reasoning.

## Artifacts Produced

Completed foundations, frontend/project/compiler implementation, runtime
skeleton and test migration are archived in `.agents/00-finished.md`.
- Maintain a compact architecture overview in `.agents/arch.md` update it after
  major changes. Compact the contents sometimes.
- Maintain a compact file list and overview of which large group of modules does
  what, and which exact file implements what in `.agents/files.md`, keep this
  file updated, and compact its contents sometimes.

## When Coding

- Keep APIs project-internal C++23. C-compatible headers, linkage wrappers and
  external interoperability are deferred until there is a concrete need.
- Never hardcode mangled C++ symbols (`?name@@...`, `_Z...`). Declare each
  runtime service called from generated code once in
  `compiler/src/codegen/runtime_symbols.hpp` as a `mangling::Function<...>`
  alias mirroring its `abi/include` declaration, and emit it via
  `services::symbol<services::X>(triple)`. Extend
  `compiler/include/clause/compiler/mangling.hpp` when a new signature shape is
  needed, and add the expected spellings, checked against Clang for every target
  ABI and width, to `tests/compiler/codegen/mangling.cpp`.
- IMPORTANT: Document class fields creation intent, what will they be doing.
  Document function creation intent. Keep comments down to 1-2 lines.
  - (do not do the following for trivial one-line changes): When implementing
    new features which require creating new code, tag that new code with a very
    short comment that it was created and used for this specific feature
    initially, prefer feature names over feature codes
- The code will be read by humans, keep it readable.
- The cyclomatic complexity of new functions and new files must remain low
  (avoid complex code). Use both Lizard and clang-tidy.
- Before a clean commit, run `cmake --build build/debug --target check-quality`
  in a freshly configured build with both compiler and runtime enabled. Both
  Lizard and clang-tidy must pass; do not bypass findings with threshold
  increases or suppressions merely to pass the gate.
- Keep all code clang-formatted (Use either makefile target 'format' or invoke
  clang-format)
- Before commit: All .ERL and terms files must be formatted. Install local copy
  of [erlfmt](https://github.com/WhatsApp/erlfmt) under 'thirdparty/tools/', if
  missing it should be installed. Always run erlfmt on new and modified .ERL and
  terms files which did not have a syntax error in them planted intentionally.
- IMPORTANT: Do not add "co-authored by" in commit messages.

## Windows Toolchain

Plain shells (PowerShell, Git Bash) do not have MSVC, the SDK or `vswhere` on
`PATH`. Locate Visual Studio with
`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`
(currently `C:\Program Files\Microsoft Visual Studio\18\Community`), then run
build/test/quality commands from a `.cmd` script that first calls
`"<installationPath>\VC\Auxiliary\Build\vcvars64.bat" >nul` and prepends
`C:\Program Files\LLVM\bin` to `PATH`; launch it with `cmd /c <full path>.cmd`.
The `'vswhere.exe' is not recognized` line printed by `vcvars64.bat` is
harmless.

## Code Style Guide

- Internal fields of classes and structs: use trailing underscore.
  - Rename existing fields when they did not have an underscore while you're
    working on them.
- Constants and inline constexpr constants prefer ALL_CAPS_SNAKE_CASE
- Function names and local variables: lower_snake_case
- Class names and struct names: public use CapitalCase, and private can go any
  (suggested lower_snake_case)

## Differences from Erlang/OTP

- Keep `docs/differences.md` up to date: whenever work finds or introduces an
  observable behavior that differs from Erlang/OTP (ordering, error terms, stack
  traces, limits, diagnostics, printing, edge-case semantics), add a row with
  OTP's behavior, Clause's behavior and the owning contract document. Remove or
  update the row when the difference is fixed. Features that are simply not
  implemented yet belong to the plan and `docs/features.md`.

## Testing Strategy

- When testing against golden master, make sure the project owns the fixtures
  used in testing as gold master and that they're generated once from
  Erlang/OTP, but Erlang/OTP should not be required for building and testing
  Clause.
- Minimize unit testing and maximize meaningful black-box and end-to-end
  coverage. Prefer real Erlang source files and project fixtures exercised
  through the compiler CLI, checking exit status, diagnostics, generated
  artifacts, and executable behavior as those features become available.
- Exercise real compiler stages and runtime integrations together; compare
  observable behavior with Erlang/OTP where appropriate. Avoid mocks and
  assertions tied to private implementation details when a real workflow can
  cover the behavior.
- Add focused unit tests only for important edge cases or invariants that cannot
  be covered reliably or practically through black-box or end-to-end tests.
  Avoid duplicating coverage across layers; preserve existing useful tests
  unless equivalent behavioral coverage replaces them.
- IMPORTANT: OTP source and copied files from OTP source remain transient and
  never join the Clause git, if necessary, save observations/oracle data/gold
  master data in Clause git, but not the license-protected files.- Keep test
  disk writes low. Test programs (main-build test executables and the nested
  CMake consumers under `build/*/tests`) are rebuilt by test runs and are built
  without debug information: no PDB files unless `-DCLAUSE_TEST_DEBUG_INFO=ON`.
  A PDB only helps to debug a crash, so when a test program crashes, reconfigure
  with that option, rebuild and rerun the test to get one, then switch it back
  off. `clau`, compiler and runtime libraries keep their debug information.
- Nested test consumers link the runtime the parent build already compiled:
  `include("${RUNTIME_TARGETS}")` (passed by `TestHost.cmake` through
  `host_configure_args`) provides `Clause::generated_program` and `Clause::abi`.
  Never `add_subdirectory` the repository into a new test to rebuild the
  runtime; only `runtime_link` does that, to prove the standalone LLVM-free
  runtime build, and the `examples/compile` example shows it to users.
- A successful test program should not silently exit, it should actively confirm
  success by printing "<program name>: ok". So that there is no confusion on
  whether a test succeeded or silently crashed/exited. Update existing tests to
  this behavior.
  - a program that is supposed to fail (non-zero exit, expected stderr) should
    do anything else other than printing "<program name>: ok", it can silently
    exit, it can crash, it can print an error - everything should not be
    considered a success. Of course for those test steps which already adopted
    such behavior change
