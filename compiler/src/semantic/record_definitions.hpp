#pragma once
#include "declarations.hpp"
#include <string>

// Native record definitions as records:get_definition/2 returns them (plan 21 step LIB8B,
// docs/native-records.md#representation).
namespace clause::semantic {
// Whether a field default folds as OTP's compiler folds it: literals, containers and the arithmetic, comparison and
// boolean operators; not anything else (a call, a variable, ++, andalso) nor an operator that fails.
bool default_folds(const ast::Module &syntax, const ast::ExprId &id);
// term_to_binary of the record's field list: {Field, Default} for a field with a default, else the bare Field.
std::string record_definition_etf(const ast::Module &syntax, const RecordLayout &layout);
} // namespace clause::semantic
