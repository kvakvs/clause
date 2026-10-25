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
// An item of an annotation (file, text, ...); none for a plain location or a missing item. Added for erl_lint.
std::optional<Term> anno_item(const Term &anno, std::string_view item);
// The configurable feature a keyword belongs to (erl_lint's feature_keywords/0); empty for no keyword.
std::string_view feature_of_keyword(std::string_view keyword);
// erl_internal's bif/2, guard_bif/2 and type_test/2, and op_type/2 (empty for no operator), for the C++ walks.
bool is_bif(std::string_view name, std::int64_t arity);
bool is_guard_bif(std::string_view name, std::int64_t arity);
bool is_type_test(std::string_view name, std::int64_t arity);
bool is_old_type_test(std::string_view name, std::int64_t arity);
std::string_view operator_type(std::string_view name, std::int64_t arity);

// erl_parse's precedences, for the C++ erl_pp: an infix operator's {Left, Prec, Right}, a prefix one's {Prec, Right}
// (left 0); function_clause for an operator erl_parse does not know.
struct Precedences {
    std::int64_t left = 0;
    std::int64_t precedence = 0;
    std::int64_t right = 0;
};

Precedences inop_precedence(std::string_view op);
Precedences preop_precedence(std::string_view op);
Precedences type_inop_precedence(std::string_view op);
Precedences type_preop_precedence(std::string_view op);
// func_prec/0 ({FUNC_LEFT_PRECEDENCE, FUNC_PRECEDENCE}) and max_prec/0.
inline constexpr std::int64_t FUNC_LEFT_PRECEDENCE = 800;
inline constexpr std::int64_t FUNC_PRECEDENCE = 700;
inline constexpr std::int64_t MAX_PRECEDENCE = 900;
// clause_erl_parse:abstract(Term, Anno, Encoding), Encoding latin1, unicode, utf8 or none.
Term abstract_term(ProcessContext &context, const Term &term, const Term &anno, const Term &encoding);
} // namespace clause::runtime::builtins::front_end
