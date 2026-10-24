#include "../../terms/funs.hpp"
#include "../text.hpp"
#include "../typed.hpp"
#include "conversion_text.hpp"
#include "md5.hpp"
#include "terms.hpp"
#include <array>
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/code_server.hpp>
#include <clause/runtime/output.hpp>

// fun_info/1,2, fun_to_list/1 and is_builtin/3 (plan step 41G3, docs/funs.md#fun-information). A local fun's
// new_uniq is the MD5 of its module name and its uniq the low 27 bits of that, Clause's own stable values; pid is
// undefined.
namespace clause::runtime::builtins {
namespace {
Term integer(ProcessContext &context, std::int64_t value) { return need(TermFactory(context).integer(value)); }

// A fun argument and its definition; badarg for anything else.
detail::FunView fun_of(const Term &fun) {
    const auto view = fun.is_function() ? detail::fun_view(fun) : std::unexpected(TermError::wrong_type);
    if (!view) {
        bad_argument();
    }
    return *view;
}

// The MD5 of a module name: a local fun's new_uniq.
std::array<std::byte, 16> module_digest(ProcessContext &context, Word module) {
    Md5 state;
    const auto name = need(need(context.atom_storage().lookup(module)).atom_spelling());
    state.update(std::as_bytes(std::span(name)));
    return state.finish();
}

// The captured values of a fun as a list.
Term environment(ProcessContext &context, const detail::FunView &view) {
    std::vector<Term> captures;
    captures.reserve(view.captures.size());
    for (const auto word : view.captures) {
        captures.push_back(need(Term::from_word(word, context)));
    }
    return need(TermFactory(context).list(captures));
}

// The items every fun has: module, name, arity, type and env; none for others.
std::optional<Term> common_item(ProcessContext &context, const detail::FunView &view, std::string_view item) {
    const auto &definition = *view.definition;
    if (item == "module" || item == "name") {
        return need(Term::from_word(item == "module" ? definition.module : definition.function, context));
    }
    if (item == "arity") {
        return integer(context, static_cast<std::int64_t>(definition.arity));
    }
    if (item == "type") {
        return atom(context, definition.external ? "external"_atom : "local"_atom);
    }
    if (item == "env") {
        return environment(context, view);
    }
    return std::nullopt;
}

// The identity items of a local fun (undefined for an external one): index and new_index, uniq and new_uniq.
Term identity_item(ProcessContext &context, const FunDefinition &definition, std::string_view item) {
    if (definition.external) {
        return atom(context, "undefined"_atom);
    }
    if (item == "index" || item == "new_index") {
        return integer(context, static_cast<std::int64_t>(definition.index));
    }
    const auto digest = module_digest(context, definition.module);
    if (item == "new_uniq") {
        return need(TermFactory(context).binary(digest));
    }
    return integer(context, std::to_integer<std::int64_t>(digest[0]) << 16 |
                                std::to_integer<std::int64_t>(digest[1]) << 8 |
                                std::to_integer<std::int64_t>(digest[2]));
}

// The value of one fun_info item; badarg for an unknown one.
Term item_value(ProcessContext &context, const Term &fun, std::string_view item) {
    const auto view = fun_of(fun);
    if (auto common = common_item(context, view, item)) {
        return *common;
    }
    if (item == "pid") {
        return atom(context, "undefined"_atom);
    }
    static constexpr std::array<std::string_view, 4> IDENTITY{"index", "new_index", "uniq", "new_uniq"};
    if (!std::ranges::contains(IDENTITY, item)) {
        bad_argument();
    }
    return identity_item(context, *view.definition, item);
}

Term item_pair(ProcessContext &context, const Term &fun, AtomName item) {
    const std::array parts{atom(context, item), item_value(context, fun, item.spelling())};
    return need(TermFactory(context).tuple(parts));
}

// fun_info(Fun): OTP's item lists for a local and an external fun.
TermResult<Term> fun_info1(ProcessContext &context, const Term &fun) {
    static constexpr std::array<std::string_view, 10> LOCAL{"pid",  "module", "new_index", "new_uniq", "index",
                                                            "uniq", "name",   "arity",     "env",      "type"};
    static constexpr std::array<std::string_view, 5> EXTERNAL{"module", "name", "arity", "env", "type"};
    const auto local = !fun_of(fun).definition->external;
    std::vector<Term> items;
    for (const auto item :
         local ? std::span<const std::string_view>(LOCAL) : std::span<const std::string_view>(EXTERNAL)) {
        items.push_back(item_pair(context, fun, item));
    }
    return TermFactory(context).list(items);
}

// fun_info(Fun, Item).
TermResult<Term> fun_info2(ProcessContext &context, const Term &fun, const AtomArgument &item) {
    return item_pair(context, fun, item.spelling);
}

// fun_to_list(Fun): the text ~w prints.
TermResult<Term> fun_to_list(ProcessContext &context, const Term &fun) {
    (void)fun_of(fun);
    return char_list(context, code_points(need(format_term(fun, TermStyle::write))));
}

// is_builtin(Module, Function, Arity): whether the runtime implements the function itself.
TermResult<Term> is_builtin(ProcessContext &context, const AtomArgument &module, const AtomArgument &function,
                            std::int64_t arity) {
    if (arity < 0 || arity > 255) {
        bad_argument();
    }
    const auto count = static_cast<std::size_t>(arity);
    const bool apply = module.spelling == "erlang" && function.spelling == "apply" && (count == 2 || count == 3);
    const bool builtin = context.code_server().builtins().find(module.spelling, function.spelling, count) != nullptr;
    return TermFactory(context).boolean(apply || builtin);
}

// make_fun(Module, Function, Arity): the external fun Module:Function/Arity, as erl_eval builds `fun M:F/A` (plan 21
// step LIB32).
TermResult<Term> make_fun(ProcessContext &context, const AtomArgument &module, const AtomArgument &function,
                          std::int64_t arity) {
    if (arity < 0 || arity > 255) {
        bad_argument();
    }
    const auto &definition =
        context.code_server().external_fun(module.term, function.term, static_cast<std::size_t>(arity));
    return TermFactory(context).fun(definition, {});
}

constexpr std::array FUN_INFO_BUILTINS{
    typed_entry<fun_info1>("erlang", "fun_info"),      typed_entry<fun_info2>("erlang", "fun_info"),
    typed_entry<fun_to_list>("erlang", "fun_to_list"), typed_entry<is_builtin>("erlang", "is_builtin"),
    typed_entry<make_fun>("erlang", "make_fun"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> fun_info_builtins() noexcept { return builtins::FUN_INFO_BUILTINS; }
} // namespace clause::runtime
