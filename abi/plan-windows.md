# Windows development host and remaining platform work

CMake provides a `windows` preset using clang-cl and Ninja Multi-Config, with
`windows-debug` and `windows-release` build/test presets. Run these from a
Visual Studio developer environment with the C++ tools, Windows SDK and
installed Clang on PATH. Native Windows builds require the MSVC ABI; MinGW is
rejected. A configure probe links the Windows SDK and checks C++23
`std::expected` support. MSVC-style project builds use `/W4 /WX`, conforming
language flags and UTF-8 source encoding.

The default CRT is `/MDd` in Debug and `/MD` otherwise. Explicit
`CMAKE_MSVC_RUNTIME_LIBRARY` choices are preserved and must match all linked C++
libraries, including LLVM. The developer environment selects the Ninja target
architecture; Visual Studio generators use `-A`. Use one matching architecture,
configuration and CRT for the runtime and generated-program consumers. Nested
consumer/SDK tests inherit the parent toolchain and active configuration. The
runtime remains a static `.lib`; no DLL export contract is introduced.
Cross-builds still omit tests that execute target programs.

Validation on this Windows x64 host: clang-cl 23.1.2 runtime/ABI Debug and
Release builds pass. Release passes all 18 CTests; Debug passes 17/18, with the
existing `runtime_lifecycle_failure` allocation-failure test failing silently.
The generated consumer link test passes in both configurations. Full
compiler/CLI tests and the combined quality gate remain blocked by the absent
global LLVM 23.1.x C++ SDK (`LLVMConfig.cmake`); installed command-line tools
are insufficient.

Remaining work before claiming full Windows support:

- Validate the full compiler and CLI with the required global LLVM SDK in both
  configurations, resolve the Debug allocation-failure test, and validate x86.
- Add Unicode command-line/path handling (the initial CLI uses narrow `main`
  arguments), Windows path tests, and safe process invocation for external
  tools.
- Implement runtime platform facilities: threads, synchronization, event polling
  (for example IOCP), timers, and any context-switch or executable-memory
  support.
- Design symbol exports/imports if a DLL runtime is introduced. Account for
  `.exe`, `.lib`, and `.dll` naming when compiler linking is implemented.
- Add Windows CI for both independent targets, CLI tests, and eventually
  generated program/runtime integration tests. Audit test execution for
  cross-builds.

Project APIs use C++23 and C++ linkage. Generated entries follow the
word/pointer contract in [v1.hpp](include/clause/abi/v1.hpp) and the native
free-function calling convention. Validate that agreement on Windows before
claiming support; C-compatible headers and wrappers remain deferred until
needed.
