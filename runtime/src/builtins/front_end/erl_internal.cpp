#include "../lists/list_walk.hpp"
#include "../string/string.hpp"
#include "front_end.hpp"
#include <array>
#include <initializer_list>

// erl_internal (plan 21 step LIB8, docs/builtins.md#compiler-tables): OTP's tables of BIFs, guard BIFs, type tests
// and operators, and add_predefined_functions/1 building behaviour_info/1 and module_info/0,1 as abstract forms.
namespace clause::runtime::builtins::front_end {
namespace {
using lists::no_clause;
using string::Terms;

// The roles of the name and arity arguments (typed.hpp's Named).
struct NameRole;
struct ArityRole;
using NameArg = Named<NameRole>;
using ArityArg = Named<ArityRole>;

// One auto-imported erlang function and whether guards may call it.
struct BifRow {
    std::string_view name;
    std::int64_t arity;
    bool guard;
};

constexpr std::array BIFS{
#include "erlang_bifs.inc"
    // Still auto-imported by OTP's erl_internal, though erlang no longer exports it.
    BifRow{"bitsize", 1, false},
};

// A table entry by name and arity.
struct Entry {
    std::string_view name;
    std::int64_t arity;
};

constexpr std::array<Entry, 18> NEW_TYPE_TESTS{{
    {"is_atom", 1},
    {"is_binary", 1},
    {"is_bitstring", 1},
    {"is_boolean", 1},
    {"is_float", 1},
    {"is_function", 1},
    {"is_function", 2},
    {"is_integer", 1},
    {"is_list", 1},
    {"is_map", 1},
    {"is_number", 1},
    {"is_pid", 1},
    {"is_port", 1},
    {"is_record", 2},
    {"is_record", 3},
    {"is_reference", 1},
    {"is_record", 1},
    {"is_tuple", 1},
}};

constexpr std::array<Entry, 12> OLD_TYPE_TESTS{{
    {"integer", 1},
    {"float", 1},
    {"number", 1},
    {"atom", 1},
    {"list", 1},
    {"tuple", 1},
    {"pid", 1},
    {"reference", 1},
    {"port", 1},
    {"binary", 1},
    {"record", 2},
    {"function", 1},
}};

// is_type/2: the predefined types by name and number of type variables.
constexpr std::array<Entry, 47> TYPES{{
    {"any", 0},
    {"arity", 0},
    {"atom", 0},
    {"binary", 0},
    {"bitstring", 0},
    {"bool", 0},
    {"boolean", 0},
    {"byte", 0},
    {"char", 0},
    {"dynamic", 0},
    {"float", 0},
    {"function", 0},
    {"identifier", 0},
    {"integer", 0},
    {"iodata", 0},
    {"iolist", 0},
    {"list", 0},
    {"list", 1},
    {"map", 0},
    {"maybe_improper_list", 0},
    {"maybe_improper_list", 2},
    {"mfa", 0},
    {"module", 0},
    {"neg_integer", 0},
    {"nil", 0},
    {"no_return", 0},
    {"node", 0},
    {"non_neg_integer", 0},
    {"none", 0},
    {"nonempty_binary", 0},
    {"nonempty_bitstring", 0},
    {"nonempty_improper_list", 2},
    {"nonempty_list", 0},
    {"nonempty_list", 1},
    {"nonempty_maybe_improper_list", 0},
    {"nonempty_maybe_improper_list", 2},
    {"nonempty_string", 0},
    {"number", 0},
    {"pid", 0},
    {"port", 0},
    {"pos_integer", 0},
    {"record", 0},
    {"reference", 0},
    {"string", 0},
    {"term", 0},
    {"timeout", 0},
    {"tuple", 0},
}};

// One operator and its op_type/2.
struct Operator {
    std::string_view name;
    std::int64_t arity;
    std::string_view type;
};

constexpr std::array<Operator, 29> OPERATORS{{
    {"+", 1, "arith"},   {"-", 1, "arith"},    {"*", 2, "arith"},   {"/", 2, "arith"},   {"+", 2, "arith"},
    {"-", 2, "arith"},   {"bnot", 1, "arith"}, {"div", 2, "arith"}, {"rem", 2, "arith"}, {"band", 2, "arith"},
    {"bor", 2, "arith"}, {"bxor", 2, "arith"}, {"bsl", 2, "arith"}, {"bsr", 2, "arith"}, {"not", 1, "bool"},
    {"and", 2, "bool"},  {"or", 2, "bool"},    {"xor", 2, "bool"},  {"==", 2, "comp"},   {"/=", 2, "comp"},
    {"=<", 2, "comp"},   {"<", 2, "comp"},     {">=", 2, "comp"},   {">", 2, "comp"},    {"=:=", 2, "comp"},
    {"=/=", 2, "comp"},  {"++", 2, "list"},    {"--", 2, "list"},   {"!", 2, "send"},
}};

// A query's name and arity: an atom and an integer (a bignum arity matches no entry), else none.
std::optional<Entry> query_of(const Term &name, const Term &arity) {
    if (!name.is_atom() || !arity.is_integer()) {
        return std::nullopt;
    }
    return Entry{name.atom_spelling_unchecked(), string::small_of(arity).value_or(-1)};
}

// The query of a table whose last clause is guarded by is_atom(Name), is_integer(A): function_clause otherwise.
Entry checked(const NameArg &name, const ArityArg &arity) {
    const auto query = query_of(name.term, arity.term);
    if (!query) {
        no_clause();
    }
    return *query;
}

template <typename Row> bool matches(const Row &row, const Entry &query) {
    return row.name == query.name && row.arity == query.arity;
}

template <typename Table> bool listed(const Table &table, const Entry &query) {
    return std::ranges::any_of(table, [&](const auto &row) { return matches(row, query); });
}

const BifRow *bif_row(const Entry &query) {
    const auto found = std::ranges::find_if(BIFS, [&](const BifRow &row) { return matches(row, query); });
    return found == BIFS.end() ? nullptr : &*found;
}

const Operator *operator_row(const Entry &query) {
    const auto found = std::ranges::find_if(OPERATORS, [&](const Operator &row) { return matches(row, query); });
    return found == OPERATORS.end() ? nullptr : &*found;
}

Term boolean(ProcessContext &context, bool value) { return Terms(context).boolean(value); }

// bif(Name, Arity): an auto-imported erlang function.
Term bif2(ProcessContext &context, const NameArg &name, const ArityArg &arity) {
    return boolean(context, bif_row(checked(name, arity)) != nullptr);
}

// bif(Module, Name, Arity): bif/2 for erlang, false for other modules.
Term bif3(ProcessContext &context, const Term &module, const NameArg &name, const ArityArg &arity) {
    if (module.is_atom() && module.atom_spelling_unchecked() == "erlang") {
        return bif2(context, name, arity);
    }
    if (!module.is_atom()) {
        no_clause();
    }
    checked(name, arity);
    return boolean(context, false);
}

// guard_bif(Name, Arity): a BIF guards may call, type tests included.
Term guard_bif(ProcessContext &context, const NameArg &name, const ArityArg &arity) {
    const auto *row = bif_row(checked(name, arity));
    return boolean(context, row != nullptr && row->guard);
}

Term new_type_test(ProcessContext &context, const NameArg &name, const ArityArg &arity) {
    return boolean(context, listed(NEW_TYPE_TESTS, checked(name, arity)));
}

Term old_type_test(ProcessContext &context, const NameArg &name, const ArityArg &arity) {
    return boolean(context, listed(OLD_TYPE_TESTS, checked(name, arity)));
}

// type_test(Name, Arity): a new (is_atom/1) or old (atom/1) type test.
Term type_test(ProcessContext &context, const NameArg &name, const ArityArg &arity) {
    const auto query = checked(name, arity);
    return boolean(context, listed(NEW_TYPE_TESTS, query) || listed(OLD_TYPE_TESTS, query));
}

// is_type(Name, NumberOfTypeVariables): a predefined type; false for anything else.
Term is_type(ProcessContext &context, const NameArg &name, const ArityArg &arity) {
    const auto query = query_of(name.term, arity.term);
    return boolean(context, query && listed(TYPES, *query));
}

// The op_type/2 of each operator query function.
constexpr std::array<std::string_view, 5> KINDS{"arith", "bool", "comp", "list", "send"};

// arith_op/2, bool_op/2, comp_op/2, list_op/2, send_op/2: an operator of the op_type KINDS[Kind].
template <std::size_t Kind> Term op_of(ProcessContext &context, const NameArg &name, const ArityArg &arity) {
    const auto *row = operator_row(checked(name, arity));
    return boolean(context, row != nullptr && row->type == std::get<Kind>(KINDS));
}

// op_type(OpName, Arity): arith, bool, comp, list or send; function_clause for anything else.
Term op_type(ProcessContext &context, const NameArg &name, const ArityArg &arity) {
    const auto query = query_of(name.term, arity.term);
    const auto *row = query ? operator_row(*query) : nullptr;
    if (row == nullptr) {
        no_clause();
    }
    return Terms(context).atom(row->type);
}

// Abstract forms with anno 0, as add_predefined_functions/1 builds them.
class Forms final {
  public:
    explicit Forms(ProcessContext &context) : terms_(context) {}

    Term tuple(std::initializer_list<Term> items) const { return terms_.tuple(std::span(items.begin(), items.size())); }

    Term list(std::initializer_list<Term> items) const { return terms_.list(std::span(items.begin(), items.size())); }

    Term atom(const AtomName name) const { return terms_.atom(name); }

    Term zero() const { return terms_.integer(0); }

    Term nil() const { return terms_.nil(); }

    const Terms &terms() const { return terms_; }

    // {Tag, 0, Value}: an atom, integer or var form.
    Term leaf(AtomName tag, const Term &value) const { return tuple({atom(tag), zero(), value}); }

    // {function, 0, Name, Arity, Clauses}.
    Term function(AtomName name, std::int64_t arity, const Term &clauses) const {
        return tuple({atom("function"_atom), zero(), atom(name), terms_.integer(arity), clauses});
    }

    // {clause, 0, Patterns, [], [Body]}.
    Term clause(const Term &patterns, const Term &body) const {
        return tuple({atom("clause"_atom), zero(), patterns, nil(), list({body})});
    }

    // make_list/1: a cons chain of {Name, Arity} tuples.
    Term fa_list(std::span<const Term> pairs) const {
        auto chain = tuple({atom("nil"_atom), zero()});
        for (auto it = pairs.rbegin(); it != pairs.rend(); ++it) {
            const auto elements = list({leaf("atom"_atom, it->tuple_element_unchecked(0)),
                                        leaf("integer"_atom, it->tuple_element_unchecked(1))});
            chain = tuple({atom("cons"_atom), zero(), tuple({atom("tuple"_atom), zero(), elements}), chain});
        }
        return chain;
    }

    // behaviour_info/1 answering callbacks and optional_callbacks.
    Term behaviour_info(std::span<const Term> callbacks, std::span<const Term> optional) const {
        return function("behaviour_info"_atom, 1,
                        list({clause(list({leaf("atom"_atom, atom("callbacks"_atom))}), fa_list(callbacks)),
                              clause(list({leaf("atom"_atom, atom("optional_callbacks"_atom))}), fa_list(optional))}));
    }

    // module_info/0 and module_info/1 calling erlang:get_module_info/1,2.
    std::array<Term, 2> module_info(const Term &module) const {
        const auto remote = tuple({atom("remote"_atom), zero(), leaf("atom"_atom, atom("erlang"_atom)),
                                   leaf("atom"_atom, atom("get_module_info"_atom))});
        const auto name = leaf("atom"_atom, module);
        const auto x = leaf("var"_atom, atom("X"_atom));
        const auto call0 = tuple({atom("call"_atom), zero(), remote, list({name})});
        const auto call1 = tuple({atom("call"_atom), zero(), remote, list({name, x})});
        return {function("module_info"_atom, 0, list({clause(nil(), call0)})),
                function("module_info"_atom, 1, list({clause(list({x}), call1)}))};
    }

  private:
    Terms terms_;
};

bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// The {Name, Value} of every {attribute, _, Name, Value} form; a non-list tail raises {bad_generator, Tail}.
std::vector<std::pair<Term, Term>> attributes(const Term &forms) {
    std::vector<std::pair<Term, Term>> found;
    auto rest = forms;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto form = rest.head_unchecked();
        if (form.is_tuple() && form.tuple_size_unchecked() == 4 &&
            is_atom(form.tuple_element_unchecked(0), "attribute")) {
            found.emplace_back(form.tuple_element_unchecked(2), form.tuple_element_unchecked(3));
        }
    }
    if (!rest.is_nil()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::bad_generator, .payload = rest.word()};
    }
    return found;
}

// is_fa_list/1: a proper list of {Name, Arity} with an atom name and a non-negative integer arity.
bool is_fa_list(const Term &list, std::vector<Term> &pairs) {
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto pair = rest.head_unchecked();
        if (!pair.is_tuple() || pair.tuple_size_unchecked() != 2 || !pair.tuple_element_unchecked(0).is_atom()) {
            return false;
        }
        const auto arity = string::small_of(pair.tuple_element_unchecked(1));
        if (!arity || *arity < 0) {
            return false;
        }
        pairs.push_back(pair);
    }
    return rest.is_nil();
}

// The behaviour's callbacks: {F, A} of each callback attribute {{F, A}, Types}.
std::vector<Term> callback_pairs(std::span<const Term> callbacks) {
    std::vector<Term> pairs;
    for (const auto &callback : callbacks) {
        if (callback.is_tuple() && callback.tuple_size_unchecked() == 2) {
            const auto fa = callback.tuple_element_unchecked(0);
            if (fa.is_tuple() && fa.tuple_size_unchecked() == 2) {
                pairs.push_back(fa);
            }
        }
    }
    return pairs;
}

// What add_predefined_functions/1 reads from the attributes: the module, callbacks and optional callbacks.
struct Predefined {
    std::optional<Term> module;
    std::vector<Term> callbacks;
    std::vector<Term> optional;
};

Predefined predefined_of(const std::vector<std::pair<Term, Term>> &attributes) {
    Predefined found;
    for (const auto &[name, value] : attributes) {
        if (is_atom(name, "module") && !found.module) {
            found.module = value;
        } else if (is_atom(name, "callback")) {
            found.callbacks.push_back(value);
        } else if (is_atom(name, "optional_callbacks")) {
            std::vector<Term> pairs;
            if (is_fa_list(value, pairs)) {
                found.optional.insert(found.optional.end(), pairs.begin(), pairs.end());
            }
        }
    }
    return found;
}

// add_predefined_functions(Forms): Forms followed by the export attribute and the predefined functions.
Term add_predefined_functions(ProcessContext &context, const Term &forms) {
    const auto found = predefined_of(attributes(forms));
    if (!found.module) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch, .payload = Terms(context).boolean(false).word()};
    }
    const Forms build(context);
    std::vector<Term> functions;
    std::vector<Term> exports;
    if (!found.callbacks.empty()) {
        functions.push_back(build.behaviour_info(callback_pairs(found.callbacks), found.optional));
        exports.push_back(build.tuple({build.atom("behaviour_info"_atom), build.terms().integer(1)}));
    }
    const auto info = build.module_info(*found.module);
    functions.insert(functions.end(), info.begin(), info.end());
    exports.push_back(build.tuple({build.atom("module_info"_atom), build.zero()}));
    exports.push_back(build.tuple({build.atom("module_info"_atom), build.terms().integer(1)}));
    const auto export_form = build.tuple(
        {build.atom("attribute"_atom), build.zero(), build.atom("export"_atom), build.terms().list(exports)});
    functions.insert(functions.begin(), export_form);
    std::vector<Term> original;
    for (auto rest = forms; rest.is_cons(); rest = rest.tail_unchecked()) {
        original.push_back(rest.head_unchecked());
    }
    return build.terms().list(original, build.terms().list(functions));
}

constexpr std::array ERL_INTERNAL_BUILTINS{
    typed_entry<add_predefined_functions>("erl_internal", "add_predefined_functions"),
    typed_entry<op_of<0>>("erl_internal", "arith_op"),
    typed_entry<bif2>("erl_internal", "bif"),
    typed_entry<bif3>("erl_internal", "bif"),
    typed_entry<op_of<1>>("erl_internal", "bool_op"),
    typed_entry<op_of<2>>("erl_internal", "comp_op"),
    typed_entry<guard_bif>("erl_internal", "guard_bif"),
    typed_entry<is_type>("erl_internal", "is_type"),
    typed_entry<op_of<3>>("erl_internal", "list_op"),
    typed_entry<new_type_test>("erl_internal", "new_type_test"),
    typed_entry<old_type_test>("erl_internal", "old_type_test"),
    typed_entry<op_type>("erl_internal", "op_type"),
    typed_entry<op_of<4>>("erl_internal", "send_op"),
    typed_entry<type_test>("erl_internal", "type_test"),
};
} // namespace

bool is_predefined_type(std::string_view name, std::int64_t arity) { return listed(TYPES, Entry{name, arity}); }

bool is_bif(std::string_view name, std::int64_t arity) { return bif_row(Entry{name, arity}) != nullptr; }

bool is_guard_bif(std::string_view name, std::int64_t arity) {
    const auto *row = bif_row(Entry{name, arity});
    return row != nullptr && row->guard;
}

bool is_type_test(std::string_view name, std::int64_t arity) {
    const Entry query{name, arity};
    return listed(NEW_TYPE_TESTS, query) || listed(OLD_TYPE_TESTS, query);
}

bool is_old_type_test(std::string_view name, std::int64_t arity) { return listed(OLD_TYPE_TESTS, Entry{name, arity}); }

std::string_view operator_type(std::string_view name, std::int64_t arity) {
    const auto *row = operator_row(Entry{name, arity});
    return row == nullptr ? std::string_view{} : row->type;
}
} // namespace clause::runtime::builtins::front_end

namespace clause::runtime {
std::span<const BuiltinEntry> erl_internal_builtins() noexcept { return builtins::front_end::ERL_INTERNAL_BUILTINS; }
} // namespace clause::runtime
