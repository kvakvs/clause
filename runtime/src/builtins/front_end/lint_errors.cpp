#include "lint.hpp"
#include <algorithm>
#include <array>

// erl_lint:format_error/1's messages (plan 21 step LIB34C): clause_erl_lint:format_spec/1 answers with what
// format_error_1/1 gives, as {plain, Format, Args}, {nested, Format, Parts} when an argument is itself an
// io_lib:format/2 result ({format, F, Parts} or {value, Term}), or a binary text; erl_lint.erl formats it.
namespace clause::runtime::builtins::front_end::lint {
namespace {
using forms::element;
using forms::is_atom;

// How a message argument is made from the descriptor.
enum class Kind : std::uint8_t { value, fa, mfa, mfa_or_value, where, record, paren, obsolete, mna, max_arguments };

// A path of tuple element indexes into the descriptor (-1 ends it) and the kind of argument made there.
struct Arg {
    Kind kind = Kind::value;
    std::array<std::int8_t, 3> path{-1, -1, -1};
};

constexpr Arg field(std::int8_t a, std::int8_t b = -1, std::int8_t c = -1) { return {Kind::value, {a, b, c}}; }

constexpr Arg made(Kind kind, std::int8_t a, std::int8_t b = -1) { return {kind, {a, b, -1}}; }

// One clause of format_error_1/1: the descriptor's tag and size (0 for an atom), the size element 1 must have (0 for
// any), the text, and its arguments; `tuple_form` marks {Text, []} answers.
struct Message {
    std::string_view tag;
    std::size_t size;
    std::size_t inner;
    std::string_view text;
    std::array<Arg, 6> args;
    std::size_t count;
    bool tuple_form = false;
};

constexpr Message text(std::string_view tag, std::string_view text) { return {tag, 0, 0, text, {}, 0}; }

template <typename... Args>
constexpr Message message(std::string_view tag, std::size_t size, std::string_view text, Args... args) {
    return {tag, size, 0, text, {args...}, sizeof...(Args)};
}

template <typename... Args>
constexpr Message inner_message(std::string_view tag, std::size_t inner, std::string_view text, Args... args) {
    return {tag, 2, inner, text, {args...}, sizeof...(Args)};
}

constexpr Message tuple_text(std::string_view tag, std::size_t size, std::string_view text) {
    return {tag, size, 0, text, {}, 0, true};
}

constexpr auto MESSAGES = std::to_array<Message>({
    text("undefined_module", "no module definition"),
    text("redefine_module", "redefining module"),
    text("pmod_unsupported", "parameterized modules are no longer supported"),
    text("non_latin1_module_unsupported", "module names with non-latin1 characters are not supported"),
    text("empty_module_name", "the module name must not be empty"),
    text("bad_module_name", "the module name must be an atom"),
    text("blank_module_name", "the module name must contain at least one visible character"),
    text("ctrl_chars_in_module_name", "the module name must not contain control characters"),
    text("invalid_call", "invalid function call"),
    text("invalid_record", "invalid record expression"),
    message("future_feature", 3, "atom '~p' is reserved in the experimental feature '~p'", field(2), field(1)),
    message("attribute", 2, "attribute ~tw after function definitions", field(1)),
    message("missing_qlc_hrl", 2, "qlc:q/~w called, but \"qlc.hrl\" not included", field(1)),
    message("redefine_import", 2, "function ~tw/~w already imported from ~w", field(1, 0, 0), field(1, 0, 1),
            field(1, 1)),
    message("bad_inline", 2, "inlined function ~tw/~w undefined", field(1, 0), field(1, 1)),
    message("bad_inline", 3, "inlined function ~tw/~w undefined, did you mean ~s?", field(1, 0), field(1, 1),
            made(Kind::fa, 2)),
    message("undefined_nif", 2, "nif ~tw/~w undefined", field(1, 0), field(1, 1)),
    message("undefined_nif", 3, "nif ~tw/~w undefined, did you mean ~s?", field(1, 0), field(1, 1), made(Kind::fa, 2)),
    tuple_text("no_load_nif", 0, "nifs defined, but no call to erlang:load_nif/2"),
    message("invalid_deprecated", 2, "badly formed deprecated attribute ~tw", field(1)),
    message("bad_deprecated", 2, "deprecated function ~tw/~w undefined or not exported", field(1, 0), field(1, 1)),
    message("invalid_removed", 2, "badly formed removed attribute ~tw", field(1)),
    message("invalid_unsafe", 2, "badly formed unsafe attribute ~tw", field(1)),
    message("bad_unsafe", 2, "unsafe function ~tw/~w undefined or not exported", field(1, 0), field(1, 1)),
    message("invalid_fa_attribute", 2, "badly formed attribute ~tw", field(1)),
    message("bad_nowarn_unused_function", 2, "function ~tw/~w undefined", field(1, 0), field(1, 1)),
    message("bad_nowarn_unused_function", 3, "function ~tw/~w undefined, did you mean ~s?", field(1, 0), field(1, 1),
            made(Kind::fa, 2)),
    message("bad_nowarn_bif_clash", 2, "function ~tw/~w undefined", field(1, 0), field(1, 1)),
    message("bad_nowarn_bif_clash", 3, "function ~tw/~w undefined, did you mean ~s?", field(1, 0), field(1, 1),
            made(Kind::fa, 2)),
    text("disallowed_nowarn_bif_clash",
         "compile directive nowarn_bif_clash is no longer allowed --\nuse explicit module "
         "names or -compile({no_auto_import, [F/A]})"),
    message("bad_on_load", 2, "badly formed on_load attribute: ~tw", field(1)),
    text("multiple_on_loads", "more than one on_load attribute"),
    message("bad_on_load_arity", 2, "function ~tw/~w has wrong arity (must be 0)", field(1, 0), field(1, 1)),
    message("undefined_on_load", 2, "function ~tw/~w undefined", field(1, 0), field(1, 1)),
    message("undefined_on_load", 3, "function ~tw/~w undefined, did you mean ~ts/~w?", field(1, 0), field(1, 1),
            field(2), field(1, 1)),
    text("nif_inline", "inlining is enabled - local calls to NIFs may call their Erlang implementation instead"),
    text("bad_export_record", "badly formed -export_record(); expected a list of record names"),
    text("export_all", "export_all flag enabled - all functions will be exported"),
    message("duplicated_export", 2, "function ~tw/~w already exported", field(1, 0), field(1, 1)),
    message("unused_import", 2, "import ~w:~tw/~w is unused", field(1, 1), field(1, 0, 0), field(1, 0, 1)),
    message("undefined_function", 2, "function ~tw/~w undefined", field(1, 0), field(1, 1)),
    message("undefined_function", 3, "function ~tw/~w undefined, did you mean ~s?", field(1, 0), field(1, 1),
            made(Kind::fa, 2)),
    message("redefine_function", 2, "function ~tw/~w already defined", field(1, 0), field(1, 1)),
    message("define_import", 2, "defining imported function ~tw/~w", field(1, 0), field(1, 1)),
    message("fun_import", 2, "creating a fun from imported name ~tw/~w is not allowed", field(1, 0), field(1, 1)),
    message("unused_function", 2, "function ~tw/~w is unused", field(1, 0), field(1, 1)),
    message("unexported_function", 2, "function ~ts is not exported", made(Kind::mfa, 1)),
    message("call_to_redefined_bif", 2,
            "ambiguous call of overridden auto-imported BIF ~w/~w --\nuse erlang:~w/~w or "
            "\"-compile({no_auto_import,[~w/~w]}).\" to resolve name clash",
            field(1, 0), field(1, 1), field(1, 0), field(1, 1), field(1, 0), field(1, 1)),
    message(
        "redefine_bif_import", 2,
        "import directive overrides auto-imported BIF ~w/~w --\nuse \"-compile({no_auto_import,[~w/~w]}).\" to resolve "
        "name clash",
        field(1, 0), field(1, 1), field(1, 0), field(1, 1)),
    message("deprecated", 4, "~s is deprecated and will be removed in ~s; ~s", made(Kind::mfa_or_value, 1), field(3),
            made(Kind::obsolete, 2)),
    message("deprecated", 3, "~s is deprecated; ~s", made(Kind::mfa_or_value, 1), made(Kind::obsolete, 2)),
    message("unsafe", 4, "~s is unsafe and will be removed in ~s; ~s", made(Kind::mfa, 1), field(3),
            made(Kind::obsolete, 2)),
    message("unsafe", 3, "~s is unsafe; ~s", made(Kind::mfa, 1), made(Kind::obsolete, 2)),
    message("deprecated_type", 4, "the type ~p:~p~s is deprecated and will be removed in ~s; ~s", field(1, 0),
            field(1, 1), made(Kind::paren, 1, 2), field(3), made(Kind::obsolete, 2)),
    message("deprecated_type", 3, "the type ~p:~p~s is deprecated; ~s", field(1, 0), field(1, 1),
            made(Kind::paren, 1, 2), made(Kind::obsolete, 2)),
    message("deprecated_callback", 4, "the callback ~p:~p~s is deprecated and will be removed in ~s; ~s", field(1, 0),
            field(1, 1), made(Kind::paren, 1, 2), field(3), made(Kind::obsolete, 2)),
    message("deprecated_callback", 3, "the callback ~p:~p~s is deprecated; ~s", field(1, 0), field(1, 1),
            made(Kind::paren, 1, 2), made(Kind::obsolete, 2)),
    message("removed", 4, "call to ~s will fail, since it was removed in ~s; use ~s", made(Kind::mfa, 1), field(3),
            made(Kind::mfa, 2)),
    message("removed", 3, "~s is removed; ~s", made(Kind::mfa, 1), made(Kind::obsolete, 2)),
    message("removed_type", 3, "the type ~s is removed; ~s", made(Kind::mna, 1), made(Kind::obsolete, 2)),
    message("removed_callback", 3, "the callback ~s is removed; ~s", made(Kind::mna, 1), made(Kind::obsolete, 2)),
    message("obsolete_guard", 2,
            "~p/~p as a type test is obsolete and will be\nremoved in OTP 30; use is_~p/~p instead.", field(1, 0),
            field(1, 1), field(1, 0), field(1, 1)),
    message("obsolete_guard_overridden", 2,
            "obsolete ~s/1 (meaning is_~s/1) is illegal when there is a\nlocal/imported function named is_~p/1",
            field(1), field(1), field(1)),
    message("too_many_arguments", 2, "too many arguments (~w) -- maximum allowed is ~w", field(1),
            made(Kind::max_arguments, 1)),
    text("update_literal", "expression updates a literal"),
    text("illegal_zip_generator", "only generators are allowed in a zip generator."),
    text("compr_assign",
         "matches using '=' are not allowed in comprehension qualifiers\nunless the experimental "
         "'compr_assign' language feature is enabled.\nWith 'compr_assign' enabled, a match 'P = E' will "
         "behave as a\nstrict generator 'P <:- [E]'.\""),
    text("illegal_map_exact_in_comprehension", "illegal map association, did you mean to use `=>`?"),
    text("illegal_map_assoc_in_pattern", "illegal pattern, did you mean to use `:=`?"),
    text("illegal_pattern", "illegal pattern"),
    text("illegal_expr", "illegal expression"),
    message("illegal_guard_local_call", 2, "call to local/imported function ~tw/~w is illegal in guard", field(1, 0),
            field(1, 1)),
    text("illegal_guard_expr", "illegal guard expression"),
    text("match_float_zero", "matching on the float 0.0 will no longer also match -0.0 in OTP 27.\nIf you specifically "
                             "intend to match 0.0 alone, write +0.0 instead."),
    text("illegal_map_construction", "only association operators '=>' are allowed in map construction"),
    message("undefined_record", 2, "record ~tw undefined", field(1)),
    message("undefined_record", 3, "record ~tw undefined, did you mean ~ts?", field(1), field(2)),
    message("redefine_record", 2, "record ~tw already defined", field(1)),
    message("redefine_field", 3, "field ~tw already defined in record ~ts", field(2), made(Kind::record, 1)),
    message("redefine_imported_record", 2, "record ~tw already imported from ~w", field(1, 1), field(1, 0)),
    message("redefine_local_record", 2, "record ~ts is already defined locally", made(Kind::record, 1)),
    message("undefined_native_record", 2, "native record ~tw undefined", field(1)),
    message("novalue", 3, "field ~tw is not initialized in native record ~ts", field(2), made(Kind::record, 1)),
    message("illegal_native_record_default", 3, "illegal default value for field ~tw in native record ~tw", field(2),
            field(1)),
    message("native_record_illegal_record_index", 3, "syntax #~tw.~tw is only supported for tuple records", field(1),
            field(2)),
    text("native_record_in_guard", "creating a record in a guard is only supported for tuple records"),
    text("native_record_illegal_multi_field_init",
         "multi-field initialization (assigning to _) is only supported for tuple records"),
    message("native_record_already_exported", 2, "native record ~tw already exported", field(1)),
    text("native_record_field_types", "native records do not allow special field types"),
    text("tuple_record_export", "tuple records cannot be exported; only native records can"),
    tuple_text("bad_multi_field_init", 0, "'_' initializes no omitted fields"),
    message("undefined_field", 3, "field ~tw undefined in record ~ts", field(2), made(Kind::record, 1)),
    message("undefined_field", 4, "field ~tw undefined in record ~tw, did you mean ~ts?", field(2),
            made(Kind::record, 1), field(3)),
    text("native_record_illegal_record_info", "record_info/2 is only supported for tuple records"),
    text("illegal_record_info", "illegal record info"),
    message("field_name_is_variable", 3, "field ~tw is not an atom or _ in record ~tw", field(2), field(1)),
    message("wildcard_in_update", 2, "meaningless use of _ in update of record ~tw", field(1)),
    message("unused_record", 2, "record ~tw is unused", field(1)),
    message("untyped_record", 2, "record ~tw has field(s) without type information", field(1)),
    message("native_record_header", 2, "record ~tw is defined in a header file", field(1)),
    message("unbound_var", 2, "variable ~w is unbound", field(1)),
    message("unbound_var", 3, "variable ~w is unbound, did you mean '~s'?", field(1), field(2)),
    message("unsafe_var", 3, "variable ~w unsafe in ~w ~s", field(1), field(2, 0), made(Kind::where, 2, 1)),
    message(
        "export_var_subexpr", 3,
        "variable ~w exported from ~w ~s.\nExporting bindings from subexpressions other than block expressions "
        "is\ndeprecated and may yield an error in a future version of Erlang/OTP.\nPlease move the binding of ~w out "
        "of "
        "the ~w.\nCompile directive 'nowarn_export_var_subexpr' can be used to suppress\nwarnings in selected modules.",
        field(1), field(2, 0), made(Kind::where, 2, 1), field(1), field(2, 0)),
    message("exported_var", 3, "variable ~w exported from ~w ~s", field(1), field(2, 0), made(Kind::where, 2, 1)),
    message("match_underscore_var", 2,
            "variable ~w is already bound. If you mean to ignore this\nvalue, use '_' or a different "
            "underscore-prefixed name",
            field(1)),
    message("match_underscore_var_pat", 2,
            "variable ~w is bound multiple times in this pattern.\nIf you mean to ignore this value, use '_' or\na "
            "different "
            "underscore-prefixed name",
            field(1)),
    text("match_alias_pats",
         "a pattern `P1=P2` where both sides are constructors\nmay yield an error in a future version "
         "of Erlang/OTP.\nNote that a pattern such as `{a,B}={Y,Z} -> ...` can always be\nwritten "
         "in a unified form `{a=Y,B=Z} -> ...`.\nCompile directive 'nowarn_match_alias_pats' can be "
         "used to suppress\nwarnings in selected modules."),
    message("shadowed_var", 3, "variable ~w shadowed in ~w", field(1), field(2)),
    message("unused_var", 2, "variable ~w is unused", field(1)),
    message("variable_in_record_def", 2, "variable ~w in record definition", field(1)),
    message("stacktrace_guard", 2, "stacktrace variable ~w must not be used in a guard", field(1)),
    message("stacktrace_bound", 2, "stacktrace variable ~w must not be previously bound", field(1)),
    message("undefined_bittype", 2, "bit type ~tw undefined", field(1)),
    message("bittype_mismatch", 4, "conflict in ~s specification for bit field: '~p' and '~p'", field(3), field(1),
            field(2)),
    text("bittype_unit", "a bit unit size must not be specified unless a size is specified too"),
    text("illegal_bitsize", "illegal bit size"),
    message("illegal_bitsize_local_call", 2,
            "call to local/imported function ~tw/~w is illegal\nin a size expression for a binary segment", field(1, 0),
            field(1, 1)),
    text("non_integer_bitsize",
         "a size expression in a pattern evaluates to a non-integer value;\nthis pattern cannot possibly match"),
    text("unsized_binary_not_at_end", "a binary field without size is only allowed at the end of a binary pattern"),
    text("typed_literal_string", "a literal string in a binary pattern must not have a type or a size"),
    text("utf_bittype_size_or_unit", "neither size nor unit must be given for segments of type utf8/utf16/utf32"),
    message("bad_bitsize", 2, "bad ~s bit size", field(1)),
    text("unsized_binary_in_bin_gen_pattern",
         "binary fields without size are not allowed in patterns of bit string generators"),
    message("conflicting_behaviours", 5, "conflicting behaviours -- callback ~tw/~w required by both '~p' and '~p' ~s",
            field(1, 0), field(1, 1), field(2), field(4), made(Kind::where, 3)),
    message("undefined_behaviour_func", 3, "undefined callback function ~tw/~w (behaviour '~w')", field(1, 0),
            field(1, 1), field(2)),
    message("undefined_behaviour", 2, "behaviour ~tw undefined", field(1)),
    message("undefined_behaviour_callbacks", 2, "behaviour ~w callback functions are undefined", field(1)),
    message("ill_defined_behaviour_callbacks", 2, "behaviour ~w callback functions erroneously defined", field(1)),
    message("ill_defined_optional_callbacks", 2, "behaviour ~w optional callback functions erroneously defined",
            field(1)),
    message("behaviour_info", 2, "cannot define callback attibute for ~tw/~w when behaviour_info is defined",
            field(1, 1), field(1, 2)),
    message("redefine_optional_callback", 2, "optional callback ~tw/~w duplicated", field(1, 0), field(1, 1)),
    message("undefined_callback", 2, "callback ~tw/~w is undefined", field(1, 1), field(1, 2)),
    message("singleton_typevar", 2, "type variable ~w is only used once (is unbound)", field(1)),
    tuple_text("bad_export_type", 2, "bad export_type declaration"),
    message("duplicated_export_type", 2, "type ~tw/~w already exported", field(1, 0), field(1, 1)),
    message("undefined_type", 2, "type ~tw~s undefined", field(1, 0), made(Kind::paren, 1, 1)),
    message("unused_type", 2, "type ~tw~s is unused", field(1, 0), made(Kind::paren, 1, 1)),
    message("redefine_builtin_type", 2, "local redefinition of built-in type: ~w~s", field(1, 0),
            made(Kind::paren, 1, 1)),
    message("renamed_type", 3, "type ~w() is now called ~w(); please use the new name instead", field(1), field(2)),
    message("redefine_type", 2, "type ~tw~s already defined", field(1, 0), made(Kind::paren, 1, 1)),
    message("type_syntax", 2, "bad ~tw type", field(1)),
    text("old_abstract_code",
         "abstract code generated before Erlang/OTP 19.0 and\nhaving typed record fields cannot be compiled"),
    inner_message("redefine_spec", 3, "spec for ~tw:~tw/~w already defined", field(1, 0), field(1, 1), field(1, 2)),
    inner_message("redefine_spec", 2, "spec for ~tw/~w already defined", field(1, 0), field(1, 1)),
    inner_message("redefine_callback", 2, "callback ~tw/~w already defined", field(1, 0), field(1, 1)),
    inner_message("bad_callback", 3, "explicit module not allowed for callback ~tw:~tw/~w", field(1, 0), field(1, 1),
                  field(1, 2)),
    inner_message("bad_module", 3, "spec for function ~w:~tw/~w from other module", field(1, 0), field(1, 1),
                  field(1, 2)),
    inner_message("spec_fun_undefined", 2, "spec for undefined function ~tw/~w", field(1, 0), field(1, 1)),
    inner_message("missing_spec", 2, "missing specification for function ~tw/~w", field(1, 0), field(1, 1)),
    text("spec_wrong_arity", "spec has wrong arity"),
    text("callback_wrong_arity", "callback has wrong arity"),
    text("deprecated_catch",
         "'catch ...' is deprecated; please use 'try ... catch ... end' instead.\nCompile directive "
         "'nowarn_deprecated_catch' can be used to suppress\nwarnings in selected modules."),
    message("not_exported_opaque", 2, "opaque type ~tw~s is not exported", field(1, 0), made(Kind::paren, 1, 1)),
    message("bad_dialyzer_attribute", 2, "badly formed dialyzer attribute: ~tw", field(1)),
    message("bad_dialyzer_option", 2, "unknown dialyzer warning option: ~tw", field(1)),
});

// The parts of one answer: the arguments, and whether one of them is a nested format.
class Answer final {
  public:
    explicit Answer(Lint &lint) : lint_(lint) {}

    void value(const Term &term) { parts_.push_back({term, false}); }

    void nested(const Term &format, std::span<const Term> args) {
        std::vector<Term> values;
        values.reserve(args.size());
        for (const auto &arg : args) {
            values.push_back(lint_.pair(lint_.atom("value"_atom), arg));
        }
        parts_.push_back({lint_.triple(lint_.atom("format"_atom), format, lint_.build_.list(values)), true});
    }

    Term result(const Term &format) const {
        const auto nested = std::ranges::any_of(parts_, [](const auto &part) { return part.second; });
        std::vector<Term> items;
        items.reserve(parts_.size());
        for (const auto &[term, is_nested] : parts_) {
            items.push_back(nested && !is_nested ? lint_.pair(lint_.atom("value"_atom), term) : term);
        }
        return lint_.triple(lint_.atom(nested ? "nested"_atom : "plain"_atom), format, lint_.build_.list(items));
    }

  private:
    Lint &lint_;
    std::vector<std::pair<Term, bool>> parts_;
};

// format_error_1/1's helpers and table lookup.
class Messages final {
  public:
    explicit Messages(Lint &lint) : lint_(lint) {}

    Term spec(const Term &error) {
        if (error.is_atom()) {
            return plain(error);
        }
        if (!error.is_tuple() || error.tuple_size_unchecked() == 0) {
            lists::no_clause();
        }
        if (auto special = special_spec(error)) {
            return *special;
        }
        const auto tag = element(error, 0);
        for (const auto &message : MESSAGES) {
            if (message.size == error.tuple_size_unchecked() && is_atom(tag, message.tag) &&
                (message.inner == 0 || inner_size(error) == message.inner)) {
                return build(message, error);
            }
        }
        lists::no_clause();
    }

  private:
    static std::size_t inner_size(const Term &error) {
        const auto inner = element(error, 1);
        return inner.is_tuple() ? inner.tuple_size_unchecked() : 0;
    }

    Term binary(std::string_view text) const {
        return lint_.build_.terms().binary(std::as_bytes(std::span(text.data(), text.size())));
    }

    Term plain(const Term &error) {
        for (const auto &message : MESSAGES) {
            if (message.size == 0 && is_atom(error, message.tag)) {
                if (message.tuple_form) {
                    return lint_.triple(lint_.atom("plain"_atom), binary(message.text), lint_.build_.nil());
                }
                return binary(message.text);
            }
        }
        lists::no_clause();
    }

    Term build(const Message &message, const Term &error) {
        if (message.tuple_form) {
            return lint_.triple(lint_.atom("plain"_atom), binary(message.text), lint_.build_.nil());
        }
        Answer answer(lint_);
        for (std::size_t index = 0; index < message.count; ++index) {
            add(answer, message.args[index], error);
        }
        return answer.result(binary(message.text));
    }

    static Term at(const Term &term, const std::array<std::int8_t, 3> &path) {
        auto current = term;
        for (const auto index : path) {
            if (index < 0) {
                break;
            }
            if (!current.is_tuple() || current.tuple_size_unchecked() <= static_cast<std::size_t>(index)) {
                lists::no_clause();
            }
            current = element(current, static_cast<std::size_t>(index));
        }
        return current;
    }

    void add(Answer &answer, const Arg &arg, const Term &error) {
        const auto term = at(error, arg.path);
        switch (arg.kind) {
        case Kind::value:
            answer.value(term);
            return;
        case Kind::where:
            where(answer, term);
            return;
        case Kind::record:
            record_name(answer, term);
            return;
        case Kind::max_arguments:
            answer.value(lint_.build_.integer(255));
            return;
        default:
            answer.value(text_arg(arg.kind, term));
        }
    }

    Term text_arg(Kind kind, const Term &term) {
        switch (kind) {
        case Kind::fa:
            return lint_.chars(format_fa(term));
        case Kind::mfa:
            return lint_.chars(format_mfa(term));
        case Kind::mfa_or_value:
            return term.is_tuple() ? lint_.chars(format_mfa(term)) : term;
        case Kind::paren:
            return lint_.chars(paren(term));
        case Kind::obsolete:
            return obsolete_text(term);
        default:
            return lint_.chars(format_mna(term));
        }
    }

    static std::string atom_text(const Term &atom) {
        if (!atom.is_atom()) {
            lists::no_clause();
        }
        return std::string(atom.atom_spelling_unchecked());
    }

    static std::string integer_text(const Term &integer) {
        const auto value = string::small_of(integer);
        if (!value) {
            lists::no_clause();
        }
        return std::to_string(*value);
    }

    // The ","-joined arities of a non-empty list.
    static std::string arities(const Term &list) {
        if (!list.is_cons()) {
            lists::no_clause();
        }
        std::string out;
        for (const auto &arity : forms::elements(list)) {
            out += (out.empty() ? "" : ",") + integer_text(arity);
        }
        return out;
    }

    static std::string format_fa(const Term &fa) {
        if (!fa.is_tuple() || fa.tuple_size_unchecked() != 2) {
            lists::no_clause();
        }
        return atom_text(element(fa, 0)) + "/" + arities(element(fa, 1));
    }

    static std::string format_mfa(const Term &mfa) {
        if (!mfa.is_tuple() || mfa.tuple_size_unchecked() != 3) {
            lists::no_clause();
        }
        const auto arity = element(mfa, 2);
        return atom_text(element(mfa, 0)) + ":" + atom_text(element(mfa, 1)) + "/" +
               (arity.is_cons() ? arities(arity) : integer_text(arity));
    }

    static std::string paren(const Term &arity) {
        const auto value = string::small_of(arity);
        if (!value || *value < 0) {
            lists::no_clause();
        }
        std::string out = "(";
        for (std::int64_t at = 0; at < *value; ++at) {
            out += at == 0 ? "_" : ",_";
        }
        return out + ")";
    }

    static std::string format_mna(const Term &mna) {
        if (!mna.is_tuple() || mna.tuple_size_unchecked() != 3) {
            lists::no_clause();
        }
        return atom_text(element(mna, 0)) + ":" + atom_text(element(mna, 1)) + paren(element(mna, 2));
    }

    Term obsolete_text(const Term &info) {
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 5> TEXTS{{
            {"undefined", "see the documentation for details"},
            {"next_version", "will be removed in the next version. See the documentation for details"},
            {"next_major_release", "will be removed in the next major release. See the documentation for details"},
            {"eventually", "will be removed in a future release. See the documentation for details"},
            {"possibly", "however, it is possible to use it safely under some conditions. See the documentation for "
                         "details"},
        }};
        if (info.is_list()) {
            return info;
        }
        for (const auto &[name, text] : TEXTS) {
            if (is_atom(info, name)) {
                return lint_.chars(text);
            }
        }
        lists::no_clause();
    }

    // format_where/1: "(line L)" or "(line L, column C)" made by io_lib:format/2.
    void where(Answer &answer, const Term &location) {
        if (location.is_integer()) {
            answer.nested(lint_.chars("(line ~p)"), std::array{location});
            return;
        }
        if (!location.is_tuple() || location.tuple_size_unchecked() != 2) {
            lists::no_clause();
        }
        answer.nested(lint_.chars("(line ~p, column ~p)"), std::array{element(location, 0), element(location, 1)});
    }

    // format_record_name/1: #Mod:Name or Name, made by io_lib:format/2.
    void record_name(Answer &answer, const Term &name) {
        if (name.is_tuple() && name.tuple_size_unchecked() == 2) {
            answer.nested(binary("#~tw:~tw"), std::array{element(name, 0), element(name, 1)});
            return;
        }
        if (!name.is_atom()) {
            lists::no_clause();
        }
        answer.nested(binary("~ts"), std::array{name});
    }

    // The clauses the table cannot express.
    std::optional<Term> special_spec(const Term &error) {
        if (error.tuple_size_unchecked() == 3 && is_atom(element(error, 1), "duplicate_doc_attribute")) {
            return duplicate_doc_attribute(error);
        }
        if (forms::is_node(error, "bad_removed", 2)) {
            return bad_removed(error);
        }
        if (forms::is_node(error, "format_error", 2)) {
            return format_error(error);
        }
        if (forms::is_node(error, "obsolete_bool_op", 3)) {
            return obsolete_bool_op(error);
        }
        if (forms::is_node(error, "deprecated_builtin_type", 4)) {
            return deprecated_builtin_type(error);
        }
        return std::nullopt;
    }

    Term duplicate_doc_attribute(const Term &error) {
        Answer answer(lint_);
        answer.value(element(error, 0));
        answer.value(element(error, 2));
        return answer.result(binary("redefining documentation attribute (~p) previously set at line ~p"));
    }

    Term bad_removed(const Term &error) {
        const auto function = element(error, 1);
        const auto wild = is_atom(at(function, {0, -1, -1}), "_") || is_atom(at(function, {1, -1, -1}), "_");
        Answer answer(lint_);
        answer.value(element(function, 0));
        answer.value(element(function, 1));
        return answer.result(binary(wild ? "at least one function matching ~tw/~w is still exported"
                                         : "removed function ~tw/~w is still exported"));
    }

    Term format_error(const Term &error) {
        const auto format = element(error, 1);
        if (!format.is_tuple() || format.tuple_size_unchecked() != 2) {
            lists::no_clause();
        }
        return lint_.triple(lint_.atom("plain"_atom), element(format, 0), element(format, 1));
    }

    Term obsolete_bool_op(const Term &error) {
        const auto old_op = element(error, 1);
        const auto new_op = element(error, 2);
        const auto rest = binary(
            "operator, which always evaluates both sides, could be\nremoved in a future version of Erlang/OTP.\nNote "
            "that the 'and' and 'or' operators have unexpected precedence, so\nthat e.g. `X > 3 or is_tuple(X)` parses "
            "as `X > (3 or is_tuple(X))`.\nCompile directive 'nowarn_obsolete_bool_op' can be used to "
            "suppress\nwarnings in selected modules.");
        // "use the short circuiting " ++ NewOp ++ " instead.\nThe " ++ OldOp ++ " " ++ Text.
        std::vector<Term> codes;
        const auto append = [&](const Term &list) {
            for (const auto &code : forms::elements(list)) {
                codes.push_back(code);
            }
        };
        append(lint_.chars("use the short circuiting "));
        append(new_op);
        append(lint_.chars(" instead.\nThe "));
        append(old_op);
        append(lint_.chars(" "));
        Answer answer(lint_);
        answer.value(old_op);
        answer.value(lint_.build_.terms().list(codes, rest));
        return answer.result(binary("~s is deprecated; ~s"));
    }

    Term deprecated_builtin_type(const Term &error) {
        const auto type = element(error, 1);
        const auto replacement = element(error, 2);
        const auto arity = at(type, {1, -1, -1});
        Answer answer(lint_);
        answer.value(at(type, {0, -1, -1}));
        answer.value(arity);
        answer.value(element(error, 3));
        if (replacement.is_tuple() && replacement.tuple_size_unchecked() == 2) {
            answer.nested(binary("use ~w:~w/~w"), std::array{element(replacement, 0), element(replacement, 1), arity});
        } else if (replacement.is_tuple() && replacement.tuple_size_unchecked() == 3) {
            answer.nested(binary("use ~w:~w/~w or preferably ~w:~w/~w"),
                          std::array{element(replacement, 0), element(replacement, 1), arity, element(replacement, 0),
                                     element(replacement, 1), element(replacement, 2)});
        } else {
            lists::no_clause();
        }
        return answer.result(binary("type ~w/~w is deprecated and will be removed in ~s; use ~s"));
    }

    Lint &lint_;
};
} // namespace

Term Lint::format_spec(const Term &error) { return Messages(*this).spec(error); }
} // namespace clause::runtime::builtins::front_end::lint
