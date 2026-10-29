#include "eval.hpp"
#include "ms_transform.hpp"
#include <algorithm>
#include <array>
#include <initializer_list>

// ms_transform's guards and bodies in C++ (C++ ms_transform): tg/2, tg_list/3, the record forms and
// fold_constant/1 of library/stdlib/ms_transform.erl.
namespace clause::runtime::builtins::front_end::ms {
using forms::element;
using forms::is_atom;
using forms::is_node;

namespace {
// A name and arity a match specification may call.
struct Callable {
    std::string_view name;
    std::size_t arity;
};

// guard_function/2, type_test/2, action_function/2 and operator/2 of ms_transform.erl.
constexpr std::array GUARD_FUNCTIONS{
    Callable{"abs", 1},      Callable{"element", 2}, Callable{"hd", 1},          Callable{"length", 1},
    Callable{"max", 2},      Callable{"min", 2},     Callable{"node", 0},        Callable{"node", 1},
    Callable{"round", 1},    Callable{"size", 1},    Callable{"bit_size", 1},    Callable{"byte_size", 1},
    Callable{"map_size", 1}, Callable{"map_get", 2}, Callable{"binary_part", 2}, Callable{"binary_part", 3},
    Callable{"tl", 1},       Callable{"trunc", 1},   Callable{"self", 0},        Callable{"float", 1},
};
constexpr std::array TYPE_TESTS{
    Callable{"is_atom", 1},     Callable{"is_float", 1},  Callable{"is_integer", 1},   Callable{"is_list", 1},
    Callable{"is_number", 1},   Callable{"is_pid", 1},    Callable{"is_port", 1},      Callable{"is_reference", 1},
    Callable{"is_tuple", 1},    Callable{"is_map", 1},    Callable{"is_map_key", 2},   Callable{"is_binary", 1},
    Callable{"is_function", 1}, Callable{"is_record", 2}, Callable{"is_seq_trace", 0},
};
constexpr std::array ACTION_FUNCTIONS{
    Callable{"set_seq_token", 2},
    Callable{"get_seq_token", 0},
    Callable{"message", 1},
    Callable{"return_trace", 0},
    Callable{"exception_trace", 0},
    Callable{"process_dump", 0},
    Callable{"enable_trace", 1},
    Callable{"enable_trace", 2},
    Callable{"disable_trace", 1},
    Callable{"disable_trace", 2},
    Callable{"display", 1},
    Callable{"caller", 0},
    Callable{"set_tcw", 1},
    Callable{"silent", 1},
    Callable{"trace", 2},
    Callable{"trace", 3},
    Callable{"caller_line", 0},
    Callable{"current_stacktrace", 0},
    Callable{"current_stacktrace", 1},
};
constexpr std::array BINARY_OPERATORS{"and", "or",  "xor", "andalso", "orelse", "+",    "-",   "*",
                                      "/",   "div", "rem", "band",    "bor",    "bxor", "bsl", "bsr",
                                      ">",   ">=",  "<",   "=<",      "==",     "=:=",  "/=",  "=/="};
constexpr std::array UNARY_OPERATORS{"not", "+", "-", "bnot"};

// The old type tests a guard's top level renames: {Old, Arity, New}.
struct OldTest {
    std::string_view old;
    std::size_t arity;
    std::string_view current;
};

constexpr std::array OLD_TESTS{
    OldTest{"atom", 1, "is_atom"},     OldTest{"float", 1, "is_float"},         OldTest{"integer", 1, "is_integer"},
    OldTest{"list", 1, "is_list"},     OldTest{"number", 1, "is_number"},       OldTest{"pid", 1, "is_pid"},
    OldTest{"port", 1, "is_port"},     OldTest{"reference", 1, "is_reference"}, OldTest{"tuple", 1, "is_tuple"},
    OldTest{"binary", 1, "is_binary"}, OldTest{"function", 1, "is_function"},   OldTest{"record", 2, "is_record"},
};

// element_name/1's texts.
struct ElementName {
    std::string_view tag;
    std::string_view text;
};

constexpr std::array ELEMENT_NAMES{
    ElementName{"lc", "list comprehension"},
    ElementName{"bc", "binary comprehension"},
    ElementName{"block", "begin/end block"},
    ElementName{"if", "if"},
    ElementName{"case", "case"},
    ElementName{"receive", "receive"},
    ElementName{"try", "try"},
    ElementName{"catch", "catch"},
    ElementName{"match", "match (=)"},
    ElementName{"remote", "external function call"},
};

template <std::size_t N> bool listed(const std::array<Callable, N> &table, const Term &name, std::size_t arity) {
    return name.is_atom() && std::ranges::any_of(table, [&](const Callable &entry) {
               return entry.arity == arity && entry.name == name.atom_spelling_unchecked();
           });
}

bool is_operator(const Term &name, std::size_t arity) {
    if (!name.is_atom() || (arity != 1 && arity != 2)) {
        return false;
    }
    const auto text = name.atom_spelling_unchecked();
    return arity == 2 ? std::ranges::find(BINARY_OPERATORS, text) != BINARY_OPERATORS.end()
                      : std::ranges::find(UNARY_OPERATORS, text) != UNARY_OPERATORS.end();
}

// from_erlang/2: the erlang functions a match specification can call by their remote name.
bool from_erlang(const Term &name, std::size_t arity) {
    return listed(GUARD_FUNCTIONS, name, arity) || listed(TYPE_TESTS, name, arity) || is_operator(name, arity);
}

// ms_function/3: the local calls a guard or body can make; get_tcw/0 is a pseudo guard function.
bool ms_function(const Term &name, std::size_t arity, Part part) {
    const auto tcw = arity == 0 && is_atom(name, "get_tcw");
    return listed(GUARD_FUNCTIONS, name, arity) || tcw || listed(TYPE_TESTS, name, arity) ||
           (part == Part::body && listed(ACTION_FUNCTIONS, name, arity));
}

// is_record/2's [Object, {atom, _, Name}].
bool record_test_args(const Term &args) {
    if (!args.is_cons() || !args.tail_unchecked().is_cons()) {
        return false;
    }
    const auto second = args.tail_unchecked();
    return second.tail_unchecked().is_nil() && is_node(second.head_unchecked(), "atom", 3);
}

// length/1 of a call's arguments; badarg for an improper list.
std::size_t arity_of(const Term &args) { return forms::elements(args).size(); }

// The text of an ASCII string as a character list.
Term chars(const Builder &build, std::string_view text) {
    std::vector<Term> codes;
    for (const auto c : text) {
        codes.push_back(build.integer(static_cast<unsigned char>(c)));
    }
    return build.list(codes);
}
} // namespace

Term Translator::transform_guards(const Term &anno, const Term &guards, const PartScope &scope) {
    if (guards.is_nil()) {
        return build_.node("nil"_atom, anno);
    }
    if (guards.is_cons() && guards.tail_unchecked().is_nil()) {
        return part_list(anno, forms::elements(guards.head_unchecked()), scope);
    }
    fail(anno, build_.integer(ERR_SEMI_GUARD));
}

// tg_list/3: a guard or body as an abstract list; old type tests at a guard's top level get their is_ names.
Term Translator::part_list(const Term &anno, std::span<const Term> exprs, const PartScope &scope) {
    std::vector<Term> translated;
    translated.reserve(exprs.size());
    for (auto expr : exprs) {
        if (scope.part == Part::guard && is_node(expr, "call", 4) && is_node(element(expr, 2), "atom", 3)) {
            const auto callee = element(expr, 2);
            const auto arity = arity_of(element(expr, 3));
            const auto old = std::ranges::find_if(OLD_TESTS, [&](const OldTest &test) {
                return test.arity == arity && is_atom(element(callee, 2), test.old);
            });
            if (old != OLD_TESTS.end()) {
                const auto renamed = build_.atom_node(element(callee, 1), old->current);
                expr = build_.node("call"_atom, element(expr, 1), renamed, element(expr, 3));
            }
        }
        translated.push_back(part(expr, scope));
    }
    auto list = build_.node("nil"_atom, anno);
    for (auto item = translated.rbegin(); item != translated.rend(); ++item) {
        list = build_.node("cons"_atom, anno, *item, list);
    }
    return list;
}

// tg/2 by the node's tag and size; anything else is a language element match specifications lack.
Term Translator::part(const Term &expr, const PartScope &scope) {
    using Handler = Term (Translator::*)(const Term &, const PartScope &);

    struct Rule {
        std::string_view tag;
        std::size_t size;
        Handler handler;
    };

    static constexpr std::array RULES{
        Rule{"match", 4, &Translator::part_match},
        Rule{"op", 5, &Translator::part_op},
        Rule{"op", 4, &Translator::part_op},
        Rule{"call", 4, &Translator::part_call},
        Rule{"cons", 4, &Translator::part_cons},
        Rule{"nil", 2, &Translator::part_literal},
        Rule{"tuple", 3, &Translator::part_tuple_node},
        Rule{"integer", 3, &Translator::part_literal},
        Rule{"char", 3, &Translator::part_literal},
        Rule{"float", 3, &Translator::part_literal},
        Rule{"string", 3, &Translator::part_literal},
        Rule{"atom", 3, &Translator::part_atom},
        Rule{"var", 3, &Translator::part_var},
        Rule{"record_field", 5, &Translator::part_record_field},
        Rule{"record", 4, &Translator::part_record},
        Rule{"record_index", 4, &Translator::part_record_index},
        Rule{"record", 5, &Translator::part_record_update},
        Rule{"bin_element", 5, &Translator::part_bin_element},
        Rule{"bin", 3, &Translator::part_bin},
        Rule{"map_field_assoc", 4, &Translator::part_map_field},
        Rule{"map", 3, &Translator::part_bin},
    };
    const auto tag = forms::tag_of(expr);
    const auto size = expr.is_tuple() ? expr.tuple_size_unchecked() : 0;
    const auto rule = std::ranges::find_if(RULES, [&](const Rule &r) { return r.tag == tag && r.size == size; });
    if (rule != RULES.end()) {
        return (this->*(rule->handler))(expr, scope);
    }
    return is_atom(expr, "default") ? expr : part_other(expr, scope);
}

Term Translator::part_match(const Term &node, const PartScope &scope) {
    fail_part(element(node, 1), ERR_GENMATCH, scope, {});
}

Term Translator::part_literal(const Term &node, const PartScope & /*scope*/) { return node; }

// An operator over constants is its value; any other is {Op, Operands...}.
Term Translator::part_op(const Term &node, const PartScope &scope) {
    if (auto value = fold_constant(build_.context(), node)) {
        return *value;
    }
    const auto anno = element(node, 1);
    std::vector<Term> operands{element(node, 3)};
    if (node.tuple_size_unchecked() == 5) {
        operands.push_back(element(node, 4));
    }
    return part_tuple(anno, {build_.atom_node(anno, element(node, 2))}, build_.list(operands), scope);
}

// {Lead..., Translated arguments...} as an abstract tuple.
Term Translator::part_tuple(const Term &anno, std::vector<Term> lead, const Term &args, const PartScope &scope) {
    for (const auto &arg : forms::elements(args)) {
        lead.push_back(part(arg, scope));
    }
    return build_.node("tuple"_atom, anno, build_.list(lead));
}

Term Translator::part_tuple_node(const Term &node, const PartScope &scope) {
    const auto anno = element(node, 1);
    const auto inner = part_tuple(anno, {}, element(node, 2), scope);
    return build_.node("tuple"_atom, anno, build_.list(std::array{inner}));
}

// A cons chain's heads, then its last tail, translated in order.
Term Translator::part_cons(const Term &node, const PartScope &scope) {
    std::vector<std::pair<Term, Term>> cells;
    auto rest = node;
    for (; is_node(rest, "cons", 4); rest = element(rest, 3)) {
        cells.emplace_back(element(rest, 1), part(element(rest, 2), scope));
    }
    auto list = part(rest, scope);
    for (auto cell = cells.rbegin(); cell != cells.rend(); ++cell) {
        list = build_.node("cons"_atom, cell->first, cell->second, list);
    }
    return list;
}

// Atoms starting with $ are quoted as {const, Atom}.
Term Translator::part_atom(const Term &node, const PartScope & /*scope*/) {
    const auto atom = element(node, 2);
    if (!atom.is_atom()) {
        bad_argument();
    }
    if (!atom.atom_spelling_unchecked().starts_with('$')) {
        return node;
    }
    const auto anno = element(node, 1);
    return build_.node("tuple"_atom, anno, build_.list(std::array{build_.atom_node(anno, "const"_atom), node}));
}

// A head variable is its '$N'; a variable from around the fun is {const, Var}.
Term Translator::part_var(const Term &node, const PartScope &scope) {
    const auto anno = element(node, 1);
    const auto name = element(node, 2).word();
    const auto known = std::ranges::find_if(scope.names, [&](const auto &entry) { return entry.first == name; });
    if (known != scope.names.end()) {
        return build_.atom_node(anno, known->second);
    }
    return build_.node("tuple"_atom, anno, build_.list(std::array{build_.atom_node(anno, "const"_atom), node}));
}

Term Translator::part_call(const Term &call, const PartScope &scope) {
    const auto callee = element(call, 2);
    const auto args = element(call, 3);
    if (is_node(callee, "remote", 4)) {
        return part_remote(call, scope);
    }
    if (!is_node(callee, "atom", 3)) {
        return part_other(call, scope);
    }
    const auto name = element(callee, 2);
    if (args.is_nil() && (is_atom(name, "bindings") || is_atom(name, "object"))) {
        return build_.atom_node(element(callee, 1), is_atom(name, "bindings") ? "$*"_atom : "$_"_atom);
    }
    if (is_atom(name, "is_record") && record_test_args(args)) {
        return part_record_test(call, scope);
    }
    const auto arity = arity_of(args);
    if (!ms_function(name, arity, scope.part)) {
        fail_part(element(call, 1), ERR_GENLOCALCALL, scope,
                  std::array{name, build_.integer(static_cast<std::int64_t>(arity))});
    }
    return part_tuple(element(call, 1), {build_.atom_node(element(callee, 1), name)}, args, scope);
}

// erlang:F(...) of an operator is the operator, of another match specification function the local call.
Term Translator::part_remote(const Term &call, const PartScope &scope) {
    const auto anno = element(call, 1);
    const auto remote = element(call, 2);
    const auto module = element(remote, 2);
    const auto function = element(remote, 3);
    if (!is_node(module, "atom", 3) || !is_node(function, "atom", 3)) {
        return part_other(call, scope);
    }
    const auto args = element(call, 3);
    const auto arity = arity_of(args);
    const auto name = element(function, 2);
    const auto arity_term = build_.integer(static_cast<std::int64_t>(arity));
    if (!is_atom(element(module, 2), "erlang") || !from_erlang(name, arity)) {
        fail_part(anno, ERR_GENREMOTECALL, scope, std::array{element(module, 2), name, arity_term});
    }
    const auto name_anno = element(function, 1);
    if (is_operator(name, arity)) {
        std::vector<Term> items{build_.atom("op"_atom), name_anno, name};
        const auto operands = forms::elements(args);
        items.insert(items.end(), operands.begin(), operands.end());
        return part(build_.tuple(items), scope);
    }
    return part(build_.node("call"_atom, anno, build_.atom_node(name_anno, name), args), scope);
}

// is_record(Object, Name): {is_record, Object, Name, Size} with the record's size.
Term Translator::part_record_test(const Term &call, const PartScope &scope) {
    const auto args = forms::elements(element(call, 3));
    const auto object = part(args[0], scope);
    const auto name_node = args[1];
    const auto *definition = record(element(name_node, 2));
    if (definition == nullptr) {
        fail_part(element(name_node, 1), ERR_GENBADREC, scope, std::array{element(name_node, 2)});
    }
    const auto size =
        build_.integer_node(element(name_node, 1), static_cast<std::int64_t>(definition->fields.size()) + 1);
    return build_.node("tuple"_atom, element(call, 1),
                       build_.list(std::array{element(call, 2), object, name_node, size}));
}

// Object#name.field: element(Position, Object).
Term Translator::part_record_field(const Term &node, const PartScope &scope) {
    const auto field_node = element(node, 4);
    if (!is_node(field_node, "atom", 3)) {
        return part_other(node, scope);
    }
    const auto anno = element(node, 1);
    const auto name = element(node, 3);
    const auto *definition = record(name);
    if (definition == nullptr) {
        fail_part(anno, ERR_GENBADREC, scope, std::array{name});
    }
    const auto field = element(field_node, 2);
    const auto found = std::ranges::find_if(definition->fields.rbegin(), definition->fields.rend(),
                                            [&](const Field &f) { return f.name.word() == field.word(); });
    if (found == definition->fields.rend()) {
        fail_part(anno, ERR_GENBADFIELD, scope, std::array{name, field});
    }
    const auto position = found->position;
    const auto object = part(element(node, 2), scope);
    const std::array items{build_.atom_node(anno, "element"_atom), build_.integer_node(anno, position), object};
    return build_.node("tuple"_atom, anno, build_.list(items));
}

// #name.field: the field's position.
Term Translator::part_record_index(const Term &node, const PartScope &scope) {
    const auto field_node = element(node, 3);
    if (!is_node(field_node, "atom", 3)) {
        return part_other(node, scope);
    }
    const auto name = element(node, 2);
    const auto *definition = record(name);
    if (definition == nullptr) {
        fail_part(element(node, 1), ERR_GENBADREC, scope, std::array{name});
    }
    const auto field = element(field_node, 2);
    const auto found = std::ranges::find_if(definition->fields.rbegin(), definition->fields.rend(),
                                            [&](const Field &f) { return f.name.word() == field.word(); });
    if (found == definition->fields.rend()) {
        fail_part(element(field_node, 1), ERR_GENBADFIELD, scope, std::array{name, field});
    }
    return build_.integer_node(element(field_node, 1), found->position);
}

// The fields of #name{...}, their values translated; a field that is not `field = Value` or `_ = Value` names no
// known record.
GivenFields Translator::construction_fields(const Term &node, const PartScope &scope) {
    GivenFields given;
    for (const auto &field : forms::elements(element(node, 3))) {
        const auto key = is_node(field, "record_field", 4) ? element(field, 2) : build_.nil();
        if (is_node(key, "atom", 3)) {
            given.named.emplace_back(element(key, 2), part(element(field, 3), scope));
        } else if (is_node(key, "var", 3) && is_atom(element(key, 2), "_")) {
            given.defaults.push_back(part(element(field, 3), scope));
        } else {
            fail_part(element(node, 1), ERR_GENBADREC, scope, std::array{element(node, 2)});
        }
    }
    return given;
}

// A record construction as its tuple: unnamed fields are the `_ = Value` field's value or the record's defaults.
Term Translator::part_record(const Term &node, const PartScope &scope) {
    const auto anno = element(node, 1);
    const auto name = element(node, 2);
    const auto given = construction_fields(node, scope);
    const auto base = scope.part == Part::body ? BODY_BASE : 0;
    const auto fallback = split_default(build_, anno, name, given, ERR_GENMULTIFIELD + base);
    const auto *definition = record(name);
    if (definition == nullptr) {
        fail_part(anno, ERR_GENBADREC, scope, std::array{name});
    }
    std::vector<Term> values{build_.atom_node(anno, name)};
    for (const auto &field : definition->fields) {
        values.push_back(field_value(given, field, fallback.value_or(field.default_value)));
    }
    check_fields(build_, anno, *definition, given, {.multi = ERR_GENMULTIFIELD + base, .bad = ERR_GENBADFIELD + base});
    const auto tuple = build_.node("tuple"_atom, anno, build_.list(values));
    return build_.node("tuple"_atom, anno, build_.list(std::array{tuple}));
}

// Var#name{...}: the named fields' values and element/2 of the record for the others.
Term Translator::part_record_update(const Term &node, const PartScope &scope) {
    const auto var = element(node, 2);
    if (!is_node(var, "var", 3)) {
        return part_other(node, scope);
    }
    const auto anno = element(node, 1);
    const auto name = element(node, 3);
    const auto object = part(var, scope);
    GivenFields given;
    for (const auto &field : forms::elements(element(node, 4))) {
        const auto key = is_node(field, "record_field", 4) ? element(field, 2) : build_.nil();
        if (!is_node(key, "atom", 3)) {
            fail(anno, build_.integer(ERR_HEADBADREC));
        }
        given.named.emplace_back(element(key, 2), part(element(field, 3), scope));
    }
    const auto *definition = record(name);
    if (definition == nullptr) {
        fail_part(anno, ERR_GENBADREC, scope, std::array{name});
    }
    const auto var_anno = element(var, 1);
    std::vector<Term> values{build_.atom_node(anno, name)};
    for (const auto &field : definition->fields) {
        const std::array old{build_.atom_node(var_anno, "element"_atom), build_.integer_node(var_anno, field.position),
                             object};
        values.push_back(field_value(given, field, build_.node("tuple"_atom, var_anno, build_.list(old))));
    }
    const auto base = scope.part == Part::body ? BODY_BASE : 0;
    check_fields(build_, anno, *definition, given, {.multi = ERR_GENMULTIFIELD + base, .bad = ERR_GENBADFIELD + base});
    const auto tuple = build_.node("tuple"_atom, anno, build_.list(values));
    return build_.node("tuple"_atom, anno, build_.list(std::array{tuple}));
}

// A segment of a variable bound around the fun stays as it is; one of a head variable cannot be constructed.
Term Translator::part_bin_element(const Term &node, const PartScope &scope) {
    const auto value = element(node, 2);
    if (is_node(value, "var", 3)) {
        const auto name = element(value, 2).word();
        if (std::ranges::any_of(scope.names, [&](const auto &entry) { return entry.first == name; })) {
            fail_part(element(value, 1), ERR_GENBINCONSTRUCT, scope, std::array{element(value, 2)});
        }
        return node;
    }
    const auto translated = part(value, scope);
    const auto size = part(element(node, 3), scope);
    return build_.node("bin_element"_atom, element(node, 1), translated, size, element(node, 4));
}

// {bin, Anno, Elements} and {map, Anno, Fields}: the elements translated in order.
Term Translator::part_bin(const Term &node, const PartScope &scope) {
    std::vector<Term> items;
    for (const auto &item : forms::elements(element(node, 2))) {
        items.push_back(part(item, scope));
    }
    return forms::with_element(build_, node, 2, build_.list(items));
}

Term Translator::part_map_field(const Term &node, const PartScope &scope) {
    const auto key = part(element(node, 2), scope);
    const auto value = part(element(node, 3), scope);
    return build_.node("map_field_assoc"_atom, element(node, 1), key, value);
}

// element_name/1 of a node's tag, or "unknown element ~tw" of anything that is no node.
Term Translator::part_other(const Term &node, const PartScope &scope) {
    auto &context = build_.context();
    if (node.is_tuple() && node.tuple_size_unchecked() >= 2) {
        const auto tag = element(node, 0);
        if (!tag.is_atom()) {
            bad_argument();
        }
        const auto known =
            std::ranges::find_if(ELEMENT_NAMES, [&](const ElementName &entry) { return is_atom(tag, entry.tag); });
        const auto text =
            known != ELEMENT_NAMES.end()
                ? chars(build_, known->text)
                : eval::call_registered(context, {.module = "erlang", .function = "atom_to_list"}, std::array{tag});
        fail_part(element(node, 1), ERR_GENELEMENT, scope, std::array{text});
    }
    const std::array args{chars(build_, "unknown element ~tw"), build_.list(std::array{node})};
    const auto text = eval::call_registered(context, {.module = "io_lib", .function = "format"}, args);
    fail_part(build_.integer(0), ERR_GENELEMENT, scope, std::array{text});
}

namespace {
std::optional<Term> constant(ProcessContext &context, const Term &node);

// apply(erlang, Operator, Arguments); none when it raises.
std::optional<Term> applied(ProcessContext &context, const Term &op, std::span<const Term> args) {
    if (!op.is_atom()) {
        return std::nullopt;
    }
    try {
        return eval::call_registered(context, {.module = "erlang", .function = op.atom_spelling_unchecked()}, args);
    } catch (const eval::Failed &) {
        return std::nullopt;
    }
}

std::optional<Term> constant_op(ProcessContext &context, const Term &node) {
    std::vector<Term> args;
    for (std::size_t index = 3; index < node.tuple_size_unchecked(); ++index) {
        const auto value = constant(context, element(node, index));
        if (!value) {
            return std::nullopt;
        }
        args.push_back(*value);
    }
    return applied(context, element(node, 2), args);
}

// The elements of {tuple, _, Es} or the cells of a cons chain as values.
std::optional<std::vector<Term>> constants(ProcessContext &context, std::span<const Term> nodes) {
    std::vector<Term> values;
    for (const auto &item : nodes) {
        const auto value = constant(context, item);
        if (!value) {
            return std::nullopt;
        }
        values.push_back(*value);
    }
    return values;
}

// A cons chain's heads as values, then its last tail.
std::optional<Term> constant_list(ProcessContext &context, const Term &node) {
    std::vector<Term> heads;
    auto rest = node;
    for (; is_node(rest, "cons", 4); rest = element(rest, 3)) {
        heads.push_back(element(rest, 2));
    }
    const auto values = constants(context, heads);
    const auto tail = constant(context, rest);
    return values && tail ? std::optional{string::Terms(context).list(*values, *tail)} : std::nullopt;
}

// constant/1: the value of an operator over integers, characters, floats, atoms, tuples and lists of them.
std::optional<Term> constant(ProcessContext &context, const Term &node) {
    if (is_node(node, "op", 5) || is_node(node, "op", 4)) {
        return constant_op(context, node);
    }
    const auto tag = forms::tag_of(node);
    if (forms::is_one_of(tag, {"integer", "char", "float", "atom"}) && node.tuple_size_unchecked() == 3) {
        return element(node, 2);
    }
    if (is_node(node, "tuple", 3)) {
        const auto values = constants(context, forms::elements(element(node, 2)));
        return values ? std::optional{string::Terms(context).tuple(*values)} : std::nullopt;
    }
    if (is_node(node, "nil", 2)) {
        return string::Terms(context).nil();
    }
    return is_node(node, "cons", 4) ? constant_list(context, node) : std::nullopt;
}
} // namespace

std::optional<Term> fold_constant(ProcessContext &context, const Term &expr) {
    std::optional<Term> value;
    try {
        value = constant(context, expr);
    } catch (const BuiltinFailure &) {
        return std::nullopt;
    }
    if (!value) {
        return std::nullopt;
    }
    const forms::Builder build(context);
    const auto anno = element(expr, 1);
    if (value->is_integer()) {
        return build.node("integer"_atom, anno, *value);
    }
    if (value->is_float()) {
        return build.node("float"_atom, anno, *value);
    }
    if (value->is_atom()) {
        return build.node("atom"_atom, anno, *value);
    }
    return std::nullopt;
}
} // namespace clause::runtime::builtins::front_end::ms
