#include "../builtins/text.hpp"
#include "startup.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <corecrt_startup.h>
#include <stdlib.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <filesystem>
#include <system_error>
#endif

namespace clause::runtime::detail {
namespace {
// Unicode code points of one argument, in order.
using Points = std::u32string;

// True for UTF-16 surrogate code points, which are never Unicode scalar values.
bool surrogate(std::uint32_t point) { return point >= 0xD800 && point <= 0xDFFF; }

#ifdef _WIN32
// Combine the surrogate pair starting at `at`, or return 0 when the units do not form one.
std::uint32_t pair(std::wstring_view text, std::size_t at) {
    if (at + 1 >= text.size()) {
        return 0;
    }
    const std::uint32_t high = text[at];
    const std::uint32_t low = text[at + 1];
    const bool valid = high >= 0xD800 && high <= 0xDBFF && low >= 0xDC00 && low <= 0xDFFF;
    return valid ? 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00) : 0;
}

// Decode UTF-16 code units; an unpaired surrogate becomes U+FFFD.
Points decode(std::wstring_view text) {
    Points points;
    points.reserve(text.size());
    for (std::size_t at = 0; at < text.size(); ++at) {
        const auto combined = pair(text, at);
        const std::uint32_t unit = text[at];
        points.push_back(combined != 0 ? combined : (surrogate(unit) ? 0xFFFD : unit));
        at += combined != 0 ? 1 : 0;
    }
    return points;
}

// Ask the CRT for its wide argument vector; the narrow argv of `main` would lose non-ANSI characters.
std::vector<Points> native_arguments(int, char **) {
    if (_configure_wide_argv(_crt_argv_unexpanded_arguments) != 0 || !__wargv) {
        throw std::runtime_error("wide command line unavailable");
    }
    std::vector<Points> arguments;
    arguments.reserve(__argc > 1 ? static_cast<std::size_t>(__argc - 1) : 0);
    for (auto **argument = __wargv; *argument; ++argument) {
        if (argument != __wargv) {
            arguments.push_back(decode(*argument));
        }
    }
    return arguments;
}

// The path of the running executable from the module table, growing the buffer until it fits (step 41L).
Points executable_path(char **) {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (size == 0) {
            return {};
        }
        if (size < path.size()) {
            path.resize(size);
            return decode(path);
        }
        path.resize(path.size() * 2);
    }
}

// The value of environment variable `name`; none when it is not set (step 41L).
std::optional<std::wstring> variable(const wchar_t *name) {
    const auto size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) {
        return std::nullopt;
    }
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), size));
    return value;
}

// OTP's home directory on Windows: HOMEDRIVE and HOMEPATH, else the user profile (step 41L).
std::optional<Points> home_directory() {
    const auto drive = variable(L"HOMEDRIVE");
    const auto path = variable(L"HOMEPATH");
    if (drive && path) {
        return decode(*drive + *path);
    }
    const auto profile = variable(L"USERPROFILE");
    return profile ? std::optional<Points>(decode(*profile)) : std::nullopt;
}
#else
// Sequence length announced by a UTF-8 lead byte; 0 for ASCII, continuation and never-valid leads.
std::size_t lead_size(unsigned char lead) {
    if (lead < 0xC2 || lead > 0xF4) {
        return 0;
    }
    return lead >= 0xF0 ? 4 : (lead >= 0xE0 ? 3 : 2);
}

// Length of a valid UTF-8 sequence starting at `at` (shortest form, no surrogates), or 0.
std::size_t sequence(std::string_view bytes, std::size_t at, std::uint32_t &point) {
    const auto lead = static_cast<unsigned char>(bytes[at]);
    const auto size = lead_size(lead);
    if (size == 0 || at + size > bytes.size()) {
        return 0;
    }
    point = lead & (0x7FU >> size);
    for (std::size_t i = 1; i < size; ++i) {
        const auto byte = static_cast<unsigned char>(bytes[at + i]);
        if ((byte & 0xC0U) != 0x80U) {
            return 0;
        }
        point = (point << 6) | (byte & 0x3FU);
    }
    constexpr std::array<std::uint32_t, 5> minimum{0, 0, 0x80, 0x800, 0x10000};
    return point >= minimum[size] && point <= 0x10FFFF && !surrogate(point) ? size : 0;
}

// Decode UTF-8; a byte that does not start a valid sequence becomes its own (Latin-1) code point.
Points decode(std::string_view bytes) {
    Points points;
    points.reserve(bytes.size());
    for (std::size_t at = 0; at < bytes.size();) {
        std::uint32_t point = 0;
        const auto size = static_cast<unsigned char>(bytes[at]) < 0x80 ? 0 : sequence(bytes, at, point);
        points.push_back(size == 0 ? static_cast<unsigned char>(bytes[at]) : point);
        at += size == 0 ? 1 : size;
    }
    return points;
}

// POSIX arguments are bytes; skip the program name.
std::vector<Points> native_arguments(int argc, char **argv) {
    std::vector<Points> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int i = 1; i < argc && argv; ++i) {
        arguments.push_back(decode(argv[i] ? argv[i] : ""));
    }
    return arguments;
}

#ifdef __APPLE__
// The path of the running executable as dyld loaded it, resolved to an absolute path (step 41L).
Points executable_path(char **) {
    std::array<char, PATH_MAX> path{};
    auto size = static_cast<std::uint32_t>(path.size());
    std::array<char, PATH_MAX> resolved{};
    if (_NSGetExecutablePath(path.data(), &size) != 0 || !realpath(path.data(), resolved.data())) {
        return {};
    }
    return decode(resolved.data());
}
#else
// The path of the running executable from /proc, else argv[0] made absolute (step 41L).
Points executable_path(char **argv) {
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error && argv && argv[0]) {
        path = std::filesystem::absolute(argv[0], error);
    }
    return error ? Points{} : decode(path.native());
}
#endif

// OTP's home directory on Unix: HOME, none when it is not set (step 41L).
std::optional<Points> home_directory() {
    const auto *home = std::getenv("HOME");
    return home ? std::optional<Points>(decode(home)) : std::nullopt;
}
#endif

// Build one string term from UTF-8 text.
TermResult<Term> string_term(TermFactory &factory, std::string_view text) {
    std::vector<Term> characters;
    for (const auto point : builtins::code_points(text)) {
        const auto character = factory.integer(point);
        if (!character) {
            return character;
        }
        characters.push_back(*character);
    }
    return factory.list(characters);
}
} // namespace

ProgramArguments command_line(int argc, char **argv, std::size_t skip) {
    const auto arguments = native_arguments(argc, argv);
    ProgramArguments program;
    for (const auto &points : std::span(arguments).subspan(std::min(skip, arguments.size()))) {
        program.plain.push_back(builtins::utf8(points));
    }
    program.executable = builtins::utf8(executable_path(argv));
    if (const auto home = home_directory()) {
        program.home = builtins::utf8(*home);
    }
    return program;
}

TermResult<Term> argument_list(ProcessContext &context, std::span<const std::string> arguments) {
    TermFactory factory(context);
    std::vector<Term> strings;
    strings.reserve(arguments.size());
    for (const auto &argument : arguments) {
        const auto string = string_term(factory, argument);
        if (!string) {
            return string;
        }
        strings.push_back(*string);
    }
    return factory.list(strings);
}
} // namespace clause::runtime::detail
