#pragma once
// Added for the ASN.1 compiler (plan 21 step LIB84): checking parsed modules into the model the generator writes.
#include "model.hpp"
#include <functional>

namespace clause::asn1 {
// Finds and parses the module an IMPORTS clause names; returns null when no source defines it.
using ModuleLoader = std::function<std::shared_ptr<ModuleSyntax>(const std::string &name)>;

// Check `modules` (one, or the several of a .set.asn) into one compiled module named `name`. Imported modules are
// read through `loader`. Throws Error on the first problem.
CompiledModule check(const std::vector<std::shared_ptr<ModuleSyntax>> &modules, const std::string &name,
                     const CompileOptions &options, const ModuleLoader &loader);
} // namespace clause::asn1
