#pragma once
#include "../support.hpp"
#include <string_view>

// Answers of erl_anno and erl_internal that erl_parse's actions use (plan 21 step LIB31): the same functions the
// builtins call, so the parser agrees with them on odd annotations.
namespace clause::runtime::builtins::front_end {
// erl_anno:location/1.
Term anno_location(ProcessContext &context, const Term &anno);
// erl_anno:end_location/1: the atom undefined when the annotation has no end.
Term anno_end_location(ProcessContext &context, const Term &anno);
// erl_anno:set_location/2.
Term anno_set_location(ProcessContext &context, const Term &location, const Term &anno);
// erl_internal:is_type/2 for a type name and its number of arguments.
bool is_predefined_type(std::string_view name, std::int64_t arity);
} // namespace clause::runtime::builtins::front_end
