#include "text.hpp"
#include "typed.hpp"
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
extern char **environ;
#endif

// The os builtins of the bridge (docs/ports.md#subprocesses): os:type/0, os:getenv/0,1, os:env/0, os:putenv/2,
// os:unsetenv/1, os:getpid/0 and os:version/0; os:cmd/1 and the time functions are library Erlang.
namespace clause::runtime::builtins {
namespace {
// The atom `name`.
#ifdef _WIN32
// UTF-8 text as UTF-16 for the Windows API, and back.
std::wstring wide(std::string_view text) {
    std::wstring result(text.size(), L'\0');
    const auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                                          static_cast<int>(result.size()));
    result.resize(static_cast<std::size_t>(size));
    return result;
}

std::string narrow(std::wstring_view text) {
    std::string result(text.size() * 3, '\0');
    result.resize(static_cast<std::size_t>(WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                                               result.data(), static_cast<int>(result.size()), nullptr,
                                                               nullptr)));
    return result;
}
#endif

// os:type(): {win32, nt}, {unix, linux} or {unix, darwin}.
TermResult<Term> type(ProcessContext &context) {
#ifdef _WIN32
    const std::array names{atom(context, "win32"_atom), atom(context, "nt"_atom)};
#elif defined(__APPLE__)
    const std::array names{atom(context, "unix"_atom), atom(context, "darwin"_atom)};
#else
    const std::array names{atom(context, "unix"_atom), atom(context, "linux"_atom)};
#endif
    return TermFactory(context).tuple(names);
}

// The value of environment variable `name` as UTF-8, if it is set.
std::optional<std::string> variable(const std::string &name) {
#ifdef _WIN32
    const auto key = wide(name);
    const auto size = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (size == 0) {
        return std::nullopt;
    }
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(key.c_str(), value.data(), size));
    return narrow(value);
#else
    const auto *value = std::getenv(name.c_str());
    return value ? std::optional<std::string>(value) : std::nullopt;
#endif
}

// Every environment entry as UTF-8 "Name=Value" (step 41X); Windows' per-drive "=C:=C:\..." entries are left out.
std::vector<std::string> environment_entries() {
    std::vector<std::string> entries;
#ifdef _WIN32
    const std::unique_ptr<wchar_t, decltype(&FreeEnvironmentStringsW)> block(GetEnvironmentStringsW(),
                                                                             &FreeEnvironmentStringsW);
    for (const auto *entry = block.get(); entry && *entry != L'\0'; entry += std::wcslen(entry) + 1) {
        if (*entry != L'=') {
            entries.push_back(narrow(entry));
        }
    }
#else
    for (auto **entry = environ; entry && *entry; ++entry) {
        entries.emplace_back(*entry);
    }
#endif
    return entries;
}

// A string argument as UTF-8: a proper list of Unicode code points; badarg for anything else.
std::string text_argument(const Term &list) {
    std::u32string codes;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto value = small(rest.head_unchecked().word());
        if (!value || !unicode_character(*value)) {
            bad_argument();
        }
        codes.push_back(static_cast<char32_t>(*value));
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return utf8(codes);
}

// A variable name argument: a string without '=' and not empty.
std::string name_argument(const ListArgument &list) {
    auto name = text_argument(list.term);
    if (name.empty() || name.contains('=')) {
        bad_argument();
    }
    return name;
}

// UTF-8 text as an Erlang string.
TermResult<Term> string_term(ProcessContext &context, std::string_view text) {
    std::vector<Term> characters;
    for (const auto code : code_points(text)) {
        characters.push_back(need(Term::from_word(need(encode_integer(code)))));
    }
    return TermFactory(context).list(characters);
}

// os:getenv(Name): the value of the variable as a string, or false when it is not set; badarg unless Name is a
// string.
TermResult<Term> getenv(ProcessContext &context, const ListArgument &name) {
    const auto value = variable(text_argument(name.term));
    return value ? string_term(context, *value) : atom(context, "false"_atom);
}

// os:getenv(): every variable as "Name=Value" (step 41X).
TermResult<Term> getenv0(ProcessContext &context) {
    std::vector<Term> items;
    for (const auto &entry : environment_entries()) {
        items.push_back(need(string_term(context, entry)));
    }
    return TermFactory(context).list(items);
}

// os:env(): every variable as {Name, Value} (step 41X).
TermResult<Term> env(ProcessContext &context) {
    TermFactory factory(context);
    std::vector<Term> items;
    for (const auto &entry : environment_entries()) {
        const auto split = entry.find('=');
        const std::array pair{need(string_term(context, entry.substr(0, split))),
                              need(string_term(context, split == std::string::npos ? "" : entry.substr(split + 1)))};
        items.push_back(need(factory.tuple(pair)));
    }
    return factory.list(items);
}

// os:putenv(Name, Value): set the variable; true (step 41X).
TermResult<Term> putenv(ProcessContext &context, const ListArgument &name, const Term &value) {
    const auto key = name_argument(name);
    const auto text = text_argument(value);
#ifdef _WIN32
    const bool set = SetEnvironmentVariableW(wide(key).c_str(), wide(text).c_str()) != 0;
#else
    const bool set = ::setenv(key.c_str(), text.c_str(), 1) == 0;
#endif
    if (!set) {
        bad_argument();
    }
    return TermFactory(context).boolean(true);
}

// os:unsetenv(Name): remove the variable; true (step 41X).
TermResult<Term> unsetenv(ProcessContext &context, const ListArgument &name) {
    const auto key = name_argument(name);
#ifdef _WIN32
    static_cast<void>(SetEnvironmentVariableW(wide(key).c_str(), nullptr));
#else
    static_cast<void>(::unsetenv(key.c_str()));
#endif
    return TermFactory(context).boolean(true);
}

// os:getpid(): the operating system's process identifier as a decimal string (step 41X).
TermResult<Term> getpid(ProcessContext &context) {
#ifdef _WIN32
    const auto pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
    const auto pid = static_cast<unsigned long>(::getpid());
#endif
    return string_term(context, std::to_string(pid));
}

// The operating system's version as major, minor and release numbers.
std::array<std::int64_t, 3> system_version() {
#ifdef _WIN32
    // GetVersionEx reports the version an application manifests; RtlGetVersion reports the real one.
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    using Query = LONG(WINAPI *)(OSVERSIONINFOW *);
    if (auto *ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (const auto query = reinterpret_cast<Query>(GetProcAddress(ntdll, "RtlGetVersion"))) {
            static_cast<void>(query(&info));
        }
    }
    return {info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber};
#else
    utsname names{};
    std::array<std::int64_t, 3> numbers{};
    if (::uname(&names) == 0) {
        std::size_t index = 0;
        for (const char *text = names.release; *text != '\0' && index < numbers.size(); ++index) {
            char *end = nullptr;
            numbers.at(index) = std::strtoll(text, &end, 10);
            text = *end == '.' ? end + 1 : end + std::char_traits<char>::length(end);
        }
    }
    return numbers;
#endif
}

// os:version(): {Major, Minor, Release} (step 41X).
TermResult<Term> version(ProcessContext &context) {
    TermFactory factory(context);
    const auto numbers = system_version();
    const std::array parts{need(factory.integer(numbers[0])), need(factory.integer(numbers[1])),
                           need(factory.integer(numbers[2]))};
    return factory.tuple(parts);
}

constexpr std::array OS_BUILTINS{
    typed_entry<type>("os", "type"),     typed_entry<getenv>("os", "getenv"),   typed_entry<getenv0>("os", "getenv"),
    typed_entry<env>("os", "env"),       typed_entry<putenv>("os", "putenv"),   typed_entry<unsetenv>("os", "unsetenv"),
    typed_entry<getpid>("os", "getpid"), typed_entry<version>("os", "version"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> os_builtins() noexcept { return builtins::OS_BUILTINS; }
} // namespace clause::runtime
