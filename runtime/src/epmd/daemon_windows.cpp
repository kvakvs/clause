#include "daemon.hpp"
#include <cstdio>
#include <string>
#include <windows.h>

namespace clause::runtime::epmd {
namespace {
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
} // namespace

void null_standard_io() {
    FILE *stream = nullptr;
    static_cast<void>(::freopen_s(&stream, "NUL", "r", stdin));
    static_cast<void>(::freopen_s(&stream, "NUL", "w", stdout));
    static_cast<void>(::freopen_s(&stream, "NUL", "w", stderr));
}

Detached detach() {
    const auto program = program_path();
    // Every argument goes on, -daemon too; -detached makes the new process serve.
    std::wstring command = ::GetCommandLineW();
    command += L" -detached";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (program.empty() ||
        ::CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE,
                         DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &process) == 0) {
        return Detached::failed;
    }
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return Detached::started;
}
} // namespace clause::runtime::epmd
