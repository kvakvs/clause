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
// erl_anno's setters (set_generated/2, set_record/2, ...): `item` of `anno` set to `value`. Added for the C++
// erl_expand_records.
Term anno_set(ProcessContext &context, std::string_view item, const Term &value, const Term &anno);
// erl_internal's bif/2, guard_bif/2 and type_test/2, and op_type/2 (empty for no operator), for the C++ walks.
bool is_bif(std::string_view name, std::int64_t arity);
bool is_guard_bif(std::string_view name, std::int64_t arity);
bool is_type_test(std::string_view name, std::int64_t arity);
std::string_view operator_type(std::string_view name, std::int64_t arity);
} // namespace clause::runtime::builtins::front_end
