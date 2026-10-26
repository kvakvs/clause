#pragma once
// Added for runtime module replacements (plan step 41K): the modules Clause's library provides, by application
// directory, and those library/modules.terms records as missing, whose names are reserved
// (docs/libraries/library.md#runtime-module-replacements).
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace clause::cli {
struct LibraryModules {
    // The application directories below the library root (erts, kernel, stdlib), searched in this order.
    std::vector<std::filesystem::path> directories_;
    // Modules Clause does not provide, by name, with the reason modules.terms gives; never searched for elsewhere.
    std::map<std::string, std::string> missing_;
    // Modules modules.terms records as provided, in its order; --shell links them all. Added for the shell option
    // (plan 21 step LIB74).
    std::vector<std::string> provided_;
};

// The library below `root`; without a readable modules.terms no name is reserved.
LibraryModules load_library(const std::filesystem::path &root, const std::filesystem::path &generated);
// The library source of module `name`; empty when Clause provides none.
std::filesystem::path library_source(const LibraryModules &library, const std::string &name);
} // namespace clause::cli
