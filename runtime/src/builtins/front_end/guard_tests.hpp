#pragma once
#include "../support.hpp"
#include <cstdint>
#include <map>
#include <set>
#include <utility>

// erl_lint's guard checks over abstract expressions (plan 21 step LIB34C): is_guard_test/1,3 and is_guard_expr/1,
// shared by the C++ erl_expand_records and clause_erl_lint's builtins.
namespace clause::runtime::builtins::front_end {
// A local function by its name's atom word and its arity.
using FunctionKey = std::pair<Word, std::int64_t>;

// What a guard check knows of the module.
struct GuardScope {
    // Record definitions by name's atom word: the field list as written.
    const std::map<Word, Term> *records = nullptr;
    // Local and imported functions: calls of them are never guard BIFs.
    const std::set<FunctionKey> *overridden = nullptr;
};

// Whether `expr` is a legal guard test (record/2 and the old type tests included).
bool is_guard_test(ProcessContext &context, const Term &expr, const GuardScope &scope);
// Whether `expr` is an expression guards may contain.
bool is_guard_expr(ProcessContext &context, const Term &expr, const GuardScope &scope);
} // namespace clause::runtime::builtins::front_end
