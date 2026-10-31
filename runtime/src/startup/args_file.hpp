#pragma once
#include <clause/runtime/program_arguments.hpp>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Args files of --args-file, a release's vm.args read as OTP's erlexec reads -args_file
// (docs/executables.md#args-files). Added for args files (plan 30 step DIST4a3a).
namespace clause::runtime::detail {
// The words of an args file split as erlexec splits them: whitespace, # comments, quotes and backslash escapes.
std::expected<std::vector<std::string>, std::string> args_file_words(const std::string &path);

// What one OTP flag of an args file does.
struct OtpFlag {
    // The words of the flag and its values.
    std::size_t used = 0;
    // The same in Clause's runtime option spelling (`+t N` is `--max-atoms N`), applied by the caller.
    std::vector<std::string> options = {};
    // The init flag a flag without a Clause option is reported as (`-kernel K V`, user flags).
    std::optional<ProgramFlag> flag = {};
    // The environment variable of `-env VAR VALUE`, set before the runtime starts.
    std::optional<std::pair<std::string, std::string>> variable = {};
    // Whether the warning line names the flag: emulator tuning or a flag Clause cannot act on.
    bool ignored = false;
};

// Read the OTP flag at `at` (`-Flag Values...` or `+Flag [Value]`); an error for a word that is no flag.
std::expected<OtpFlag, std::string> otp_flag(std::span<const std::string> words, std::size_t at);
// Set an environment variable of the process (-env VAR VALUE), seen by os:getenv/1 and the C runtime.
void set_environment(const std::string &name, const std::string &value);
} // namespace clause::runtime::detail
