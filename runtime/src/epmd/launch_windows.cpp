#include "launch.hpp"
#include <windows.h>

namespace clause::runtime::epmd {
namespace {
// UTF-16 of a UTF-8 path.
std::wstring wide(const std::string &text) {
    const auto size = static_cast<int>(text.size());
    std::wstring result(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0)),
                        L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), size, result.data(), static_cast<int>(result.size()));
    return result;
}
} // namespace

bool start_daemon(const std::string &program) {
    const auto application = wide(program);
    std::wstring command = L"\"" + application + L"\" -daemon";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(application.c_str(), command.data(), nullptr, nullptr, FALSE,
                       DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &process) == 0) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
} // namespace clause::runtime::epmd
