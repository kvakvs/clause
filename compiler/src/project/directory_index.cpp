// Added for the directory index (plan step 62E).
#include "directory_index.hpp"
#include <algorithm>

namespace clause::project {
namespace {
namespace fs = std::filesystem;
using Key = fs::path::string_type;

// Windows and macOS volumes are case-insensitive by default: their name keys fold case (docs/projects.md).
#if defined(_WIN32) || defined(__APPLE__)
constexpr bool FOLD_CASE = true;
#else
constexpr bool FOLD_CASE = false;
#endif

// Spellings Windows resolves to other names: trailing dots and spaces are dropped, `~` may be a short 8.3 name.
bool windows_alias(const Key &name) {
#ifdef _WIN32
    return name.ends_with(L'.') || name.ends_with(L' ') || name.find(L'~') != Key::npos;
#else
    (void)name;
    return false;
#endif
}

// The key of an entry name: exact, or ASCII-folded where volumes fold case; none when folding cannot be exact
// (non-ASCII names) or the name may stand for another one.
std::optional<Key> name_key(const fs::path &name) {
    Key key = name.native();
    if (key.empty() || windows_alias(key)) {
        return std::nullopt;
    }
    if (!FOLD_CASE) {
        return key;
    }
    for (auto &unit : key) {
        if (static_cast<unsigned>(unit) >= 0x80U) {
            return std::nullopt;
        }
        if (unit >= 'A' && unit <= 'Z') {
            unit = static_cast<fs::path::value_type>(unit - 'A' + 'a');
        }
    }
    return key;
}

// The key of an absolute directory without `..` steps (which symlinks make inexact); none otherwise.
std::optional<Key> directory_key(const fs::path &directory) {
    if (!directory.is_absolute() || std::ranges::any_of(directory, [](const fs::path &part) { return part == ".."; })) {
        return std::nullopt;
    }
    auto normal = directory.lexically_normal();
    if (!normal.has_filename() && normal.has_relative_path()) {
        normal = normal.parent_path();
    }
    return normal.native();
}

// The entry names of a directory; none when it cannot be listed, empty when it does not exist.
std::optional<std::vector<fs::path>> list(const fs::path &directory) {
    std::error_code error;
    fs::directory_iterator iterator(directory, error);
    if (error) {
        const bool absent = error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory;
        return absent ? std::optional{std::vector<fs::path>{}} : std::nullopt;
    }
    std::vector<fs::path> names;
    for (; !error && iterator != fs::directory_iterator{}; iterator.increment(error)) {
        names.push_back(iterator->path().filename());
    }
    return error ? std::nullopt : std::optional{std::move(names)};
}
} // namespace

bool DirectoryIndex::may_exist(const fs::path &file) {
    const auto key = name_key(file.filename());
    const auto id = key ? listing(file.parent_path()) : std::nullopt;
    if (!id) {
        ++statistics_.probes_;
        return true;
    }
    ++statistics_.lookups_;
    const auto found = names_.find(*key);
    const bool present = found != names_.end() && std::ranges::binary_search(found->second, *id);
    statistics_.misses_ += present ? 0 : 1;
    return present;
}

void DirectoryIndex::add_listing(const fs::path &directory, const std::vector<fs::path> &names) {
    const auto key = directory_key(directory);
    if (key && !directories_.contains(*key)) {
        directories_.emplace(*key, record(*key, names));
        ++statistics_.walked_;
    }
}

std::optional<std::size_t> DirectoryIndex::listing(const fs::path &directory) {
    const auto key = directory_key(directory);
    if (!key) {
        return std::nullopt;
    }
    if (const auto found = directories_.find(*key); found != directories_.end()) {
        return found->second;
    }
    const auto names = list(directory);
    ++statistics_.listed_;
    const auto id = names ? std::optional{record(*key, *names)} : std::nullopt;
    directories_.emplace(*key, id);
    return id;
}

std::size_t DirectoryIndex::record(const Key &directory, const std::vector<fs::path> &names) {
    const auto id = scanned_.size();
    scanned_.push_back(directory);
    for (const auto &name : names) {
        if (const auto key = name_key(name)) {
            auto &ids = names_[*key];
            if (ids.empty() || ids.back() != id) {
                ids.push_back(id);
            }
        }
    }
    return id;
}
} // namespace clause::project
