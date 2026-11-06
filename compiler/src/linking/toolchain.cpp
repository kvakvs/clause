#include "toolchain.hpp"
#include "../project/paths.hpp"
#include <array>
#include <fstream>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>
#include <stdexcept>
#include <vector>

namespace clause::linking {
namespace {
// Keep linker output in diagnostics readable even when a broken link reports every symbol.
constexpr std::size_t output_limit = std::size_t{64} * 1024;

// Search PATH (or the given directories) for one name, accepting only an executable file.
std::optional<std::string> find_program(const std::string &name,
                                        const llvm::ArrayRef<llvm::StringRef> directories = {}) {
    auto found = llvm::sys::findProgramByName(name, directories);
    if (!found || !llvm::sys::fs::can_execute(*found)) {
        return std::nullopt;
    }
    return *found;
}

// Prefer clang++, then clang; the forced g++ driver mode makes both link C++ runtime dependencies.
std::optional<std::string> find_clang(const llvm::ArrayRef<llvm::StringRef> directories) {
    for (const auto *name : {"clang++", "clang"}) {
        if (auto found = find_program(name, directories)) {
            return found;
        }
    }
    return std::nullopt;
}

// Read the captured linker output, truncated to the diagnostic limit.
std::string read_output(const std::filesystem::path &log) {
    std::ifstream file(log, std::ios::binary);
    std::string text(output_limit, ' ');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}
} // namespace

std::string utf8_path(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::string find_linker(const std::optional<std::filesystem::path> &linker) {
    if (linker) {
        if (auto found = find_program(utf8_path(*linker))) {
            return *found;
        }
        throw std::runtime_error("linker not found: " + project::path_text(*linker));
    }
    if (auto found = find_clang({})) {
        return *found;
    }
    // Windows installers may leave LLVM off PATH; try its default installation directory.
    if (const auto root = llvm::sys::Process::GetEnv("ProgramFiles")) {
        const auto directory = *root + "/LLVM/bin";
        if (auto found = find_clang({directory})) {
            return *found;
        }
    }
    throw std::runtime_error("cannot find clang++ or clang on PATH; install LLVM/Clang or pass --linker");
}

namespace {
// One argument quoted as GNU response files are read, by the Clang driver in g++ mode on every host too: a backslash
// escapes the next character. Added for linker response files.
std::string gnu_quoted(const std::string &argument) {
    std::string quoted = "\"";
    for (const char c : argument) {
        if (c == '\\' || c == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(c);
    }
    return quoted + "\"";
}

// The arguments in a response file beside the log, which the linker driver reads from `@file`: a link of many
// objects, as the whole library's, outgrows Windows' command line. Added for long link lines.
std::string response_file(std::span<const std::string> arguments, const std::filesystem::path &log) {
    auto path = log;
    path.replace_extension(".rsp");
    std::ofstream file(path, std::ios::binary);
    for (const auto &argument : arguments) {
        file << gnu_quoted(argument) << '\n';
    }
    if (!file.flush()) {
        throw std::runtime_error("cannot write linker response file " + utf8_path(path));
    }
    return "@" + utf8_path(path);
}
} // namespace

LinkerRun run_linker(const std::string &program, std::span<const std::string> arguments,
                     const std::filesystem::path &log) {
    std::vector<llvm::StringRef> argv;
    argv.reserve(arguments.size() + 1);
    argv.push_back(program);
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    std::string response;
    if (!llvm::sys::commandLineFitsWithinSystemLimits(program, argv)) {
        response = response_file(arguments, log);
        argv = {program, response};
    }
    const auto log_text = utf8_path(log);
    const std::array<std::optional<llvm::StringRef>, 3> redirects{llvm::StringRef(), llvm::StringRef(log_text),
                                                                  llvm::StringRef(log_text)};
    std::string error;
    bool failed = false;
    const int status = llvm::sys::ExecuteAndWait(program, argv, std::nullopt, redirects, 0, 0, &error, &failed);
    if (failed) {
        throw std::runtime_error("cannot run linker " + program + ": " + error);
    }
    return {status, read_output(log)};
}
} // namespace clause::linking
