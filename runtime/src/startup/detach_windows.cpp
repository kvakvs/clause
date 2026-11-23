#include "detach.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace clause::runtime::detail {
namespace {
// Marks the detached copy, as erlexec marks it with ERL_CONSOLE_MODE; removed at once so its own children lack it.
constexpr const wchar_t *DETACHED_VARIABLE = L"CLAUSE_DETACHED_NODE";

// The path of this program's executable; empty when it cannot be read.
std::wstring program_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const auto length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

// Whether this program is the detached copy; removes the mark.
bool detached_copy() {
    if (::GetEnvironmentVariableW(DETACHED_VARIABLE, nullptr, 0) == 0) {
        return false;
    }
    // An empty value removes the variable from the C runtime's copy and the process's.
    static_cast<void>(::_wputenv_s(DETACHED_VARIABLE, L""));
    return true;
}

// Standard input, output and error on the null device.
void null_standard_io() {
    FILE *stream = nullptr;
    static_cast<void>(::freopen_s(&stream, "NUL", "r", stdin));
    static_cast<void>(::freopen_s(&stream, "NUL", "w", stdout));
    static_cast<void>(::freopen_s(&stream, "NUL", "w", stderr));
}
} // namespace

Detached detach() {
    if (detached_copy()) {
        null_standard_io();
        return Detached::run;
    }
    const auto program = program_path();
    std::wstring command = ::GetCommandLineW();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    static_cast<void>(::_wputenv_s(DETACHED_VARIABLE, L"1"));
    const bool started = !program.empty() && ::CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE,
                                                              DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr,
                                                              nullptr, &startup, &process) != 0;
    static_cast<void>(::_wputenv_s(DETACHED_VARIABLE, L""));
    if (!started) {
        return Detached::failed;
    }
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return Detached::started;
}
} // namespace clause::runtime::detail
