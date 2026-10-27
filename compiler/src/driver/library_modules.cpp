// Added for runtime module replacements (plan step 41K).
#include "library_modules.hpp"
#include "../project/paths.hpp"
#include <algorithm>
#include <array>
#include <iterator>
#include <string_view>

namespace clause::cli {
namespace {
// A catalog entry: a module and, when Clause leaves it missing, the reason. Added for the library catalog in clau.
struct CatalogEntry {
    // The module's name, an ASCII atom.
    std::string_view module_;
    // Why Clause does not provide the module; empty for a provided module.
    std::string_view reason_;
};

// The modules Clause decides about, in library_modules.inc's order. Added for the library catalog in clau.
// clang-format off
constexpr std::array CATALOG{
#define PROVIDED(module, application) CatalogEntry{.module_ = #module, .reason_ = {}},
#define MISSING(module, application, reason) CatalogEntry{.module_ = #module, .reason_ = (reason)},
#include "library_modules.inc"
#undef MISSING
#undef PROVIDED
};
// clang-format on

// The application directories of a library root, sorted.
std::vector<std::filesystem::path> application_directories(const std::filesystem::path &root) {
    std::vector<std::filesystem::path> directories;
    std::error_code error;
    for (const auto &item : std::filesystem::directory_iterator(root, error)) {
        if (item.is_directory(error)) {
            directories.push_back(item.path());
        }
    }
    std::ranges::sort(directories);
    return directories;
}
} // namespace

LibraryModules load_library(const std::filesystem::path &root, const std::filesystem::path &generated) {
    LibraryModules library;
    library.directories_ = application_directories(root);
    // Added for public_key: the generated modules come after the library's sources.
    std::ranges::copy(application_directories(generated), std::back_inserter(library.directories_));
    for (const auto &entry : CATALOG) {
        if (entry.reason_.empty()) {
            library.provided_.emplace_back(entry.module_);
        } else {
            library.missing_.emplace(entry.module_, entry.reason_);
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
