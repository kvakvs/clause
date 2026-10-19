#pragma once
#include "program.hpp"
#include <clause/abi/immediate_services.hpp>
#include <optional>
#include <span>
#include <string_view>

// The functions guards and bodies of match specifications call (plan 21 step LIB11): OTP's guard BIF table
// (erl_db_util.c's guard_tab), with the contexts each may be called in, and their evaluation.
namespace clause::runtime::detail::match_spec {
// Where a function may be called: guards and bodies of either dialect.
enum Contexts : std::uint8_t {
    TABLE_GUARD = 1,
    TABLE_BODY = 2,
    TRACE_GUARD = 4,
    TRACE_BODY = 8,
    ANYWHERE = TABLE_GUARD | TABLE_BODY | TRACE_GUARD | TRACE_BODY
};

struct GuardFunction {
    std::string_view name;
    std::size_t arity;
    Function function;
    abi::v1::ImmediateOperation operation;
    std::uint8_t contexts;
};

// The function `name`/`arity`, if guards and bodies may call one.
const GuardFunction *guard_function(std::string_view name, std::size_t arity);

// The value of a call on argument words, or none when it raises (a guard then fails; a body gives 'EXIT').
std::optional<Word> call_function(ProcessContext &context, const Expression &call, std::span<const Word> arguments);
} // namespace clause::runtime::detail::match_spec
