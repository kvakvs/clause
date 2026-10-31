#include "args_file.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace clause::runtime::detail {
namespace {
// erlexec's word separators.
bool blank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// Skip separators and # comments from `at`; the index of the next word, or the end.
std::size_t skip_blanks(std::string_view text, std::size_t at) {
    while (at < text.size()) {
        if (blank(text[at])) {
            ++at;
        } else if (text[at] == '#') {
            at = std::min(text.find('\n', at), text.size());
        } else {
            break;
        }
    }
    return at;
}

// Append the quoted text after the quote at `at` to `word`, without escapes; the index after the closing quote.
std::size_t quoted(std::string_view text, std::size_t at, std::string &word) {
    const auto end = std::min(text.find(text[at], at + 1), text.size());
    word.append(text.substr(at + 1, end - at - 1));
    return std::min(end + 1, text.size());
}

// Read the word at `at` into `word`: it ends at a separator or a # (which starts a comment); the index after it.
std::size_t read_word(std::string_view text, std::size_t at, std::string &word) {
    while (at < text.size() && !blank(text[at]) && text[at] != '#') {
        const char c = text[at];
        if (c == '"' || c == '\'') {
            at = quoted(text, at, word);
        } else if (c == '\\') {
            at = at + 1 < text.size() ? (word.push_back(text[at + 1]), at + 2) : at + 1;
        } else {
            word.push_back(c);
            ++at;
        }
    }
    return at;
}

// Whether `word` starts another flag, ending the values of the one before.
bool is_flag(std::string_view word) { return word.starts_with('-') || word.starts_with('+'); }

// The values of the -Flag at `at`: the words up to the next flag.
std::vector<std::string> flag_values(std::span<const std::string> words, std::size_t at) {
    const auto end = std::find_if(words.begin() + static_cast<std::ptrdiff_t>(at) + 1, words.end(),
                                  [](const std::string &word) { return is_flag(word); });
    return {words.begin() + static_cast<std::ptrdiff_t>(at) + 1, end};
}

// OTP's node start flags with a value and the Clause option each one is.
constexpr std::array<std::pair<std::string_view, std::string_view>, 5> NODE_FLAGS{{
    {"sname", "--sname"},
    {"name", "--name"},
    {"setcookie", "--setcookie"},
    {"proto_dist", "--proto-dist"},
    {"dist_listen", "--dist-listen"},
}};

// The init flags Clause cannot act on: reported by init, and named in the warning line.
constexpr std::array<std::string_view, 15> IGNORED_FLAGS{
    "heart",   "eval", "s",        "run",           "boot",
    "mode",    "pa",   "pz",       "path",          "noshell",
    "noinput", "smp",  "detached", "shutdown_time", "async_shell_start",
};

// A mapped -Flag: its Clause options, or none for a flag without one.
std::optional<std::vector<std::string>> node_options(std::string_view name, const std::vector<std::string> &values) {
    const auto value = values.empty() ? std::string() : values.front();
    if (name == "nocookie" || name == "hidden") {
        return std::vector<std::string>{"--" + std::string(name)};
    }
    const auto node = std::ranges::find(NODE_FLAGS, name, &std::pair<std::string_view, std::string_view>::first);
    if (node != NODE_FLAGS.end()) {
        return std::vector<std::string>{std::string(node->second), value};
    }
    return std::nullopt;
}

// -start_epmd true|false: --epmd bundled or none.
std::expected<OtpFlag, std::string> start_epmd(const std::vector<std::string> &values) {
    const auto value = values.empty() ? std::string() : values.front();
    if (value != "true" && value != "false") {
        return std::unexpected("invalid -start_epmd value '" + value + "' (true or false)");
    }
    return OtpFlag{.used = values.size() + 1, .options = {"--epmd", value == "true" ? "bundled" : "none"}};
}

// -env VAR VALUE: erlexec takes the next two words whatever they are.
std::expected<OtpFlag, std::string> environment_flag(std::span<const std::string> words, std::size_t at) {
    if (words.size() - at < 3) {
        return std::unexpected(std::string("-env needs a variable and a value"));
    }
    return OtpFlag{.used = 3, .variable = std::pair{words[at + 1], words[at + 2]}};
}

// A -Flag: a node start option, -start_epmd, -env, or an init flag (ignored ones named in the warning line).
std::expected<OtpFlag, std::string> init_flag(std::span<const std::string> words, std::size_t at) {
    const auto name = std::string_view(words[at]).substr(1);
    if (name == "env") {
        return environment_flag(words, at);
    }
    auto values = flag_values(words, at);
    if (name == "start_epmd") {
        return start_epmd(values);
    }
    const auto used = values.size() + 1;
    if (auto options = node_options(name, values)) {
        return OtpFlag{.used = used, .options = std::move(*options)};
    }
    const bool ignored = std::ranges::find(IGNORED_FLAGS, name) != IGNORED_FLAGS.end();
    return OtpFlag{.used = used, .flag = ProgramFlag{std::string(name), std::move(values)}, .ignored = ignored};
}

// The value of the emulator flag `prefix` at `at`, attached (`+t5000`) or the next word (`+t 5000`), and the words
// used; none when the flag is another one.
std::optional<std::pair<std::string, std::size_t>> emulator_value(std::span<const std::string> words, std::size_t at,
                                                                  std::string_view prefix) {
    const std::string_view word = words[at];
    if (!word.starts_with(prefix)) {
        return std::nullopt;
    }
    if (word.size() > prefix.size()) {
        return std::pair{std::string(word.substr(prefix.size())), std::size_t{1}};
    }
    return at + 1 < words.size() ? std::pair{words[at + 1], std::size_t{2}} : std::pair{std::string(), std::size_t{1}};
}

// +S Schedulers[:Online]: --schedulers Schedulers; only the online count is ignored.
OtpFlag schedulers_flag(const std::string &value, std::size_t used) {
    const auto count = value.substr(0, value.find(':'));
    return count.empty() ? OtpFlag{.used = used, .ignored = true}
                         : OtpFlag{.used = used, .options = {"--schedulers", count}};
}

// The value of +S Schedulers[:Online] and the words used; none for +SP, +SDcpu, ..., which are other flags: +S takes
// a count, attached only as digits or :Online.
std::optional<std::pair<std::string, std::size_t>> scheduler_value(std::span<const std::string> words, std::size_t at) {
    const std::string_view word = words[at];
    const bool attached = word.size() > 2 && (std::isdigit(static_cast<unsigned char>(word[2])) != 0 || word[2] == ':');
    return word == "+S" || (attached && word.starts_with("+S")) ? emulator_value(words, at, "+S") : std::nullopt;
}

// Emulator flags with a Clause option (+t, +S, +sbt, +hms); none for the others.
std::optional<OtpFlag> mapped_emulator_flag(std::span<const std::string> words, std::size_t at) {
    if (const auto atoms = emulator_value(words, at, "+t")) {
        return OtpFlag{.used = atoms->second, .options = {"--max-atoms", atoms->first}};
    }
    if (const auto schedulers = scheduler_value(words, at)) {
        return schedulers_flag(schedulers->first, schedulers->second);
    }
    if (const auto binding = emulator_value(words, at, "+sbt")) {
        // u (unbound) is the default; every other type binds the workers.
        return binding->first == "u" ? OtpFlag{.used = binding->second}
                                     : OtpFlag{.used = binding->second, .options = {"--scheduler-cpus", "all"}};
    }
    if (const auto heap = emulator_value(words, at, "+hms")) {
        return OtpFlag{.used = heap->second, .options = {"--min-heap-size", heap->first}};
    }
    return std::nullopt;
}

// A +Flag: mapped onto a Clause option, else ignored with its value (the next word unless it is a flag).
OtpFlag emulator_flag(std::span<const std::string> words, std::size_t at) {
    if (auto mapped = mapped_emulator_flag(words, at)) {
        return std::move(*mapped);
    }
    const bool valued = at + 1 < words.size() && !is_flag(words[at + 1]);
    return OtpFlag{.used = valued ? std::size_t{2} : std::size_t{1}, .ignored = true};
}

#ifdef _WIN32
// UTF-8 text as UTF-16 for the Windows API.
std::wstring wide(std::string_view text) {
    std::wstring result(text.size(), L'\0');
    const auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                                          static_cast<int>(result.size()));
    result.resize(static_cast<std::size_t>(size));
    return result;
}
#endif
} // namespace

std::expected<std::vector<std::string>, std::string> args_file_words(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected("cannot read args file '" + path + "'");
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<std::string> words;
    for (auto at = skip_blanks(text, 0); at < text.size(); at = skip_blanks(text, at)) {
        at = read_word(text, at, words.emplace_back());
    }
    return words;
}

std::expected<OtpFlag, std::string> otp_flag(std::span<const std::string> words, std::size_t at) {
    const std::string_view word = words[at];
    if (word.size() > 1 && word.starts_with('-')) {
        return init_flag(words, at);
    }
    if (word.size() > 1 && word.starts_with('+')) {
        return emulator_flag(words, at);
    }
    return std::unexpected("not a flag: '" + std::string(word) + "'");
}

void set_environment(const std::string &name, const std::string &value) {
#ifdef _WIN32
    // The C runtime's copy too, which keeps the process environment in step.
    static_cast<void>(_wputenv_s(wide(name).c_str(), wide(value).c_str()));
#else
    static_cast<void>(::setenv(name.c_str(), value.c_str(), 1));
#endif
}
} // namespace clause::runtime::detail
