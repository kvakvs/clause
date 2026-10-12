// Added for runtime module replacements (plan step 41K).
#include "library_modules.hpp"
#include "../project/paths.hpp"
#include <algorithm>
#include <fstream>
#include <optional>
#include <string_view>

namespace clause::cli {
namespace {
// The name and reason of a `{Module, Application, missing, "Reason"}.` line; none for any other line.
std::optional<std::pair<std::string, std::string>> missing_entry(std::string_view line) {
    constexpr std::string_view MARK = ", missing, \"";
    const auto mark = line.find(MARK);
    const auto comma = line.find(", ");
    if (!line.starts_with('{') || !line.ends_with("\"}.") || mark == std::string_view::npos || comma >= mark) {
        return std::nullopt;
    }
    const auto reason = line.substr(mark + MARK.size());
    return std::pair{std::string(line.substr(1, comma - 1)), std::string(reason.substr(0, reason.size() - 3))};
}
} // namespace

LibraryModules load_library(const std::filesystem::path &root) {
    LibraryModules library;
    std::error_code error;
    for (const auto &item : std::filesystem::directory_iterator(root, error)) {
        if (item.is_directory(error)) {
            library.directories_.push_back(item.path());
        }
    }
    std::ranges::sort(library.directories_);
    std::ifstream terms(root / "modules.terms");
    for (std::string line; std::getline(terms, line);) {
        if (line.ends_with('\r')) {
            line.pop_back();
        }
        if (auto entry = missing_entry(line)) {
            library.missing_.insert(std::move(*entry));
        }
    }
    return library;
}

std::filesystem::path library_source(const LibraryModules &library, const std::string &name) {
    const auto file = project::native_path(name + ".erl");
    for (const auto &directory : library.directories_) {
        std::error_code error;
        if (std::filesystem::is_regular_file(directory / file, error)) {
            return directory / file;
        }
    }
    return {};
}
} // namespace clause::cli
