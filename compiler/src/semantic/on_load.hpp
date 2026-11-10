#pragma once
// Created for -on_load (plan 21 step MIM9d): the function a program runs before its entry, checked as erl_lint checks
// the attribute (docs/compile.md).
#include "declarations.hpp"

namespace clause::semantic {
// Check the module's -on_load attributes and record the function the first one names in `module.on_load_`.
void index_on_load(Module &module, const Reporter &out);
} // namespace clause::semantic
