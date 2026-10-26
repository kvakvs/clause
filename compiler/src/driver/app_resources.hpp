#pragma once
// Created for application resources (plan 21 step LIB24): the application specifications of a program, read from
// `.app` and `.app.src` files at compile time and compiled into the generated module clause_applications, which
// Clause's application_controller reads instead of searching a code path (docs/libraries/library.md#applications).
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace clause::cli {
// Where a program's application resources come from.
struct ApplicationPlaces {
    // The program's modules with the directories of their sources; resources are looked for beside them.
    std::vector<std::pair<std::filesystem::path, std::string>> modules_;
    // The project's application directories by name (`[targets.options.applications]`).
    std::vector<std::pair<std::string, std::filesystem::path>> applications_;
    // The library's application directories (erts, kernel, stdlib), whose modules the virtual applications list.
    std::vector<std::filesystem::path> library_;
};

// The Erlang source of clause_applications: specs/0 gives each resource's term with the modules beside it, and
// library_modules/1 the modules of Clause's library applications.
std::string applications_source(const ApplicationPlaces &places);
// The synthetic path naming the generated module in diagnostics and progress.
std::filesystem::path applications_path();
} // namespace clause::cli
