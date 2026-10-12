// Added for versioned include_lib directories: `<application>-<version>` directories as Erlang releases name them.
#include <algorithm>
#include <clause/compiler/preprocessor.hpp>

namespace clause {
namespace {
namespace fs = std::filesystem;
using Name = fs::path::string_type;
using Char = Name::value_type;

// Where the version of a directory name starts: after its last hyphen, when the rest is dot-separated digit groups
// (OTP's code_server `is_vsn`, without empty groups); npos for any other name.
std::size_t version_start(const Name &name) {
    const auto hyphen = name.rfind(Char('-'));
    if (hyphen == Name::npos || hyphen == 0) {
        return Name::npos;
    }
    bool digit = false;
    for (auto i = hyphen + 1; i < name.size(); ++i) {
        const bool is_digit = name[i] >= Char('0') && name[i] <= Char('9');
        if (!is_digit && (name[i] != Char('.') || !digit)) {
            return Name::npos;
        }
        digit = is_digit;
    }
    return digit ? hyphen + 1 : Name::npos;
}

// The digit groups of a versioned name without leading zeros, so a longer group is a larger number.
std::vector<Name> version_groups(const Name &name) {
    std::vector<Name> groups;
    for (auto begin = version_start(name); begin <= name.size();) {
        const auto end = std::min(name.find(Char('.'), begin), name.size());
        const auto digits = std::min(name.find_first_not_of(Char('0'), begin), end);
        groups.push_back(name.substr(digits, end - digits));
        begin = end + 1;
    }
    return groups;
}

// OTP orders release versions as integer lists: group by group, a longer list winning over its prefix. Equal
// versions spelled differently (`1.01`, `1.1`) fall back to name order, so the choice stays deterministic.
bool older(const Name &left, const Name &right) {
    const auto by_value = [](const Name &a, const Name &b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    };
    const auto a = version_groups(left);
    const auto b = version_groups(right);
    const bool less = std::ranges::lexicographical_compare(a, b, by_value);
    return less || (!std::ranges::lexicographical_compare(b, a, by_value) && left < right);
}

// The versioned directory names in `base`; empty when it does not exist or cannot be listed.
std::vector<Name> versioned_directories(const fs::path &base) {
    std::vector<Name> names;
    std::error_code error;
    for (fs::directory_iterator entry(base, error), end; !error && entry != end; entry.increment(error)) {
        auto name = entry->path().filename().native();
        std::error_code kind;
        if (version_start(name) != Name::npos && entry->is_directory(kind)) {
            names.push_back(std::move(name));
        }
    }
    return names;
}

// Diagnostic text of a directory name.
std::string text(const Name &name) {
    const auto bytes = fs::path(name).u8string();
    return {bytes.begin(), bytes.end()};
}
} // namespace

std::optional<ApplicationVersions::Match> ApplicationVersions::find(const fs::path &base, const fs::path &application) {
    const auto key = base.lexically_normal().native();
    auto listing = listings_.find(key);
    if (listing == listings_.end()) {
        listing = listings_.emplace(key, versioned_directories(base)).first;
    }
    std::vector<Name> matches;
    std::ranges::copy_if(listing->second, std::back_inserter(matches), [&](const Name &name) {
        return name.substr(0, version_start(name) - 1) == application.native();
    });
    if (matches.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(matches, older);
    Match result{base / matches.back(), {}};
    if (matches.size() > 1) {
        std::ranges::transform(matches, std::back_inserter(result.duplicates_), text);
    }
    return result;
}

bool ApplicationVersions::first_report(const fs::path &base, const fs::path &application) {
    return reported_.emplace(base.lexically_normal().native(), application.native()).second;
}
} // namespace clause
