#pragma once
// Added for ssl (plan 21 step LIB81A): the string module functions ssl_cipher_format's name walks use, on code points.
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace clause::runtime::builtins::ssl {
using Text = std::u32string;
using TextView = std::u32string_view;

// string:to_upper/1 and to_lower/1: Latin-1 letters only.
inline Text upper(TextView text) {
    Text result(text);
    for (auto &c : result) {
        const bool lower = (c >= U'a' && c <= U'z') || (c >= 0xE0 && c <= 0xF6) || (c >= 0xF8 && c <= 0xFE);
        c = lower ? c - 32 : c;
    }
    return result;
}

inline Text lower(TextView text) {
    Text result(text);
    for (auto &c : result) {
        const bool upper = (c >= U'A' && c <= U'Z') || (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xDE);
        c = upper ? c + 32 : c;
    }
    return result;
}

// lists:append(string:replace(Text, From, To, all)) for one-character patterns.
inline Text replace_all(TextView text, char32_t from, char32_t to) {
    Text result(text);
    for (auto &c : result) {
        c = c == from ? to : c;
    }
    return result;
}

// string:split(Text, Separator, trailing) of a two-part result; none when the separator is missing.
inline std::optional<std::pair<Text, Text>> split_trailing(TextView text, char32_t separator) {
    const auto at = text.rfind(separator);
    if (at == TextView::npos) {
        return std::nullopt;
    }
    return std::pair{Text(text.substr(0, at)), Text(text.substr(at + 1))};
}
} // namespace clause::runtime::builtins::ssl
