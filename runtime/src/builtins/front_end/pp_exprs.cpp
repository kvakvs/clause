#include "../text.hpp"
#include "front_end.hpp"
#include "pp.hpp"
#include <array>

// The expressions of the C++ erl_pp's first pass (plan 21 step LIB34C): item/3's clauses, in erl_pp.erl's order.
namespace clause::runtime::builtins::front_end::pp {
using forms::element;
using forms::is_atom;
using forms::is_node;

namespace {
// hash_space/2: an integer or fun before # is kept apart from it by a space.
bool hash_spaced(const Term &node) {
    if (is_node(node, "integer", 3)) {
        return true;
    }
    if (!is_node(node, "fun", 3)) {
        return false;
    }
    const auto function = element(node, 2);
    return is_node(function, "function", 3) || is_node(function, "function", 4);
}
} // namespace

Index Printer::variable(const Term &node, std::int64_t /*precedence*/, const Opts & /*opts*/) {
    const std::array args{element(node, 2)};
    if (args[0].is_integer()) {
        return text("_~w", args);
    }
    return text("~ts", args);
}

Index Printer::char_expr(const Term &node, std::int64_t /*precedence*/, const Opts & /*opts*/) {
    return items_.of_term(Kind::char_value, element(node, 2));
}

Index Printer::number(const Term &node, std::int64_t /*precedence*/, const Opts & /*opts*/) {
    return items_.leaf(write_text(build_.context(), element(node, 2)));
}

Index Printer::atom_expr(const Term &node, std::int64_t /*precedence*/, const Opts & /*opts*/) {
    return atom(element(node, 2));
}

Index Printer::string_expr(const Term &node, std::int64_t /*precedence*/, const Opts & /*opts*/) {
    return items_.of_term(Kind::string, element(node, 2));
}

Index Printer::nil(const Term & /*node*/, std::int64_t /*precedence*/, const Opts & /*opts*/) { return word("[]"); }

Index Printer::cons(const Term &node, std::int64_t /*precedence*/, const Opts &opts) { return list_items(node, opts); }

// list_items/3: [E1, E2 | Tail].
Index Printer::list_items(const Term &node, const Opts &opts) {
    std::vector<Index> parts;
    auto rest = node;
    for (; is_node(rest, "cons", 4); rest = element(rest, 3)) {
        parts.push_back(item(element(rest, 2), 0, opts));
    }
    const auto comma = listed(ch(','));
    if (is_node(rest, "nil", 2)) {
        return seq({.open = ch('['), .close = ch(']')}, ch(','), std::move(parts));
    }
    parts.push_back(item(rest, 0, opts));
    return items_.seq(
        {.before = ch('['), .after = ch(']'), .separator = comma, .last_separator = listed(word(" |")), .spaced = true},
        std::move(parts));
}

// comprehension/5: [Template || Qualifiers], <<...>> and #{...}.
Index Printer::comprehension(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    const auto tag = forms::tag_of(node);
    const auto expr = element(node, 2);
    Index open{};
    Index close{};
    Index templ{};
    if (tag == "lc") {
        open = ch('[');
        templ = item(expr, 0, opts);
        close = ch(']');
    } else if (tag == "bc") {
        open = word("<<");
        templ = item(expr, MAX_PRECEDENCE, opts);
        close = word(">>");
    } else {
        open = word("#{");
        templ = map_field(expr, opts);
        close = ch('}');
    }
    const auto inner =
        items_.list({items_.step(Kind::step, cat({templ, leaf(" ||")}), qualifiers(element(node, 3), opts))});
    const auto group =
        seq({.open = open, .close = items_.empty()}, items_.empty(), {items_.force_nl(leaf(" "), cat({inner}))});
    return items_.list({group, close});
}

Index Printer::tuple_expr(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    return tuple(items(element(node, 2), opts));
}

Index Printer::record_index(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto prec = preop_precedence("#");
    auto parts = record_name(element(node, 2));
    parts.push_back(ch('.'));
    parts.push_back(item(element(node, 3), prec.right, opts));
    return paren(prec.precedence, precedence, cat(std::move(parts)));
}

Index Printer::record(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto prec = preop_precedence("#");
    return paren(
        prec.precedence, precedence,
        items_.first({.before = cat(record_name(element(node, 2))), .part = record_fields(element(node, 3), opts)}));
}

// Rec#name.field.
Index Printer::record_field(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto prec = inop_precedence("#");
    const auto rec = element(node, 2);
    auto name = record_name(element(node, 3));
    std::vector<Index> middle;
    if (hash_spaced(rec)) {
        middle.push_back(ch(' '));
    }
    middle.push_back(ch('#'));
    middle.insert(middle.end(), name.begin() + 1, name.end());
    middle.push_back(ch('.'));
    return paren(prec.precedence, precedence,
                 cat({item(rec, prec.left, opts), cat(std::move(middle)), item(element(node, 4), prec.right, opts)}));
}

// Rec#name{Fields}.
Index Printer::record_update(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto prec = inop_precedence("#");
    const auto rec = element(node, 2);
    std::vector<Index> before{item(rec, prec.left, opts)};
    before.push_back(hash_spaced(rec) ? cat({ch(' ')}) : items_.empty());
    before.push_back(cat(record_name(element(node, 3))));
    return paren(prec.precedence, precedence,
                 items_.first({.before = cat(std::move(before)), .part = record_fields(element(node, 4), opts)}));
}

// Rec.field, and .field of the anonymous record.
Index Printer::field_access(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto prec = inop_precedence(".");
    const auto rec = element(node, 2);
    const auto field = item(element(node, 3), prec.right, opts);
    if (is_node(rec, "atom", 3) && is_atom(element(rec, 2), "")) {
        return paren(prec.precedence, precedence, cat({ch('.'), field}));
    }
    return paren(prec.precedence, precedence, cat({item(rec, prec.left, opts), ch('.'), field}));
}

Index Printer::map(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto prec = preop_precedence("#");
    return paren(prec.precedence, precedence,
                 items_.first({.before = ch('#'), .part = map_fields(element(node, 2), opts)}));
}

Index Printer::map_update(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto prec = inop_precedence("#");
    const auto map = element(node, 2);
    std::vector<Index> before{item(map, prec.left, opts)};
    if (hash_spaced(map)) {
        before.push_back(ch(' '));
    }
    before.push_back(ch('#'));
    return paren(prec.precedence, precedence,
                 items_.first({.before = cat(std::move(before)), .part = map_fields(element(node, 3), opts)}));
}

Index Printer::map_fields(const Term &fields, const Opts &opts) {
    std::vector<Index> parts;
    for (const auto &field : forms::elements(fields)) {
        parts.push_back(map_field(field, opts));
    }
    return tuple(std::move(parts));
}

Index Printer::block(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    return items_.list({items_.step(Kind::step, word("begin"), body(element(node, 2), opts)), reserved("end")});
}

Index Printer::if_expr(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    return items_.list(
        {items_.step(Kind::step, word("if"), clauses(&Printer::if_clause, element(node, 2), opts)), reserved("end")});
}

Index Printer::case_expr(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    const auto head =
        items_.list({items_.step(Kind::step, word("case"), item(element(node, 2), 0, opts)), reserved("of")});
    return items_.list(
        {items_.step(Kind::step, head, clauses(&Printer::case_clause, element(node, 3), opts)), reserved("end")});
}

Index Printer::cond_expr(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    return items_.list({items_.step(Kind::step, leaf("cond"), clauses(&Printer::cond_clause, element(node, 2), opts)),
                        reserved("end")});
}

Index Printer::receive(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    return items_.list(
        {items_.step(Kind::step, word("receive"), clauses(&Printer::case_clause, element(node, 2), opts)),
         reserved("end")});
}

Index Printer::receive_after(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    const auto after = items_.list(
        {items_.step(Kind::step, cat({item(element(node, 3), 0, opts), word(" ->")}), body(element(node, 4), opts))});
    return items_.list(
        {items_.step(Kind::step, word("receive"), clauses(&Printer::case_clause, element(node, 2), opts)),
         items_.step(Kind::step, word("after"), after), reserved("end")});
}

// fun F/A and fun M:F/A; none for any other function.
std::optional<Index> Printer::fun_function(const Term &function, const Opts &opts) {
    if (is_node(function, "function", 3)) {
        return local_fun(function);
    }
    if (is_node(function, "function", 4)) {
        return cat({items_.codes(U"fun "), item(element(function, 1), 0, opts), ch(':'),
                    item(element(function, 2), 0, opts), ch('/'), item(element(function, 3), 0, opts)});
    }
    return std::nullopt;
}

// fun F/A.
Index Printer::local_fun(const Term &function) {
    const std::array arity{element(function, 2)};
    return cat({leaf("fun "), atom(element(function, 1)), text("/~w", arity)});
}

// fun Clauses end, named when `name` is an atom.
Index Printer::fun_items(const FunClauses &fun, const Opts &opts) {
    const auto &name = fun.name;
    const auto keyword = name.is_atom() ? cat({word("fun"), items_.codes(U" ")}) : word("fun");
    return items_.list({items_.first({.before = keyword, .part = fun_clauses(fun, opts)}), reserved("end")});
}

Index Printer::fun3(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto function = element(node, 2);
    if (const auto printed = fun_function(function, opts)) {
        return *printed;
    }
    if (is_node(function, "clauses", 2)) {
        return fun_items({.clauses = element(function, 1), .name = build_.nil()}, opts);
    }
    return unknown(node, precedence, opts);
}

// A fun with its fun-info comment.
Index Printer::fun4(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto function = element(node, 2);
    if (is_node(function, "function", 3)) {
        return items_.force_nl(fun_info(element(node, 3)), local_fun(function));
    }
    if (is_node(function, "clauses", 2)) {
        return items_.force_nl(fun_info(element(node, 3)),
                               fun_items({.clauses = element(function, 1), .name = build_.nil()}, opts));
    }
    return unknown(node, precedence, opts);
}

Index Printer::named_fun(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    return fun_items({.clauses = element(node, 3), .name = element(node, 2)}, opts);
}

Index Printer::named_fun_info(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    return items_.force_nl(fun_info(element(node, 4)),
                           fun_items({.clauses = element(node, 3), .name = element(node, 2)}, opts));
}

Index Printer::fun_info(const Term &extra) { return cat({leaf("% fun-info: "), items_.of_term(Kind::value, extra)}); }

// An auto-imported BIF prints without its module, except float/1.
Index Printer::call_expr(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto callee = element(node, 2);
    const auto args = element(node, 3);
    if (is_node(callee, "remote", 4) && is_node(element(callee, 2), "atom", 3) &&
        is_node(element(callee, 3), "atom", 3)) {
        const auto module = element(element(callee, 2), 2);
        const auto local = element(callee, 3);
        const auto name = element(local, 2);
        const auto arity = std::ssize(forms::elements(args));
        if (is_atom(module, "erlang") && name.is_atom() && is_bif(name.atom_spelling_unchecked(), arity) &&
            !is_atom(name, "float")) {
            return call(item(local, FUNC_LEFT_PRECEDENCE, opts), args, precedence, opts);
        }
    }
    return call(item(callee, FUNC_LEFT_PRECEDENCE, opts), args, precedence, opts);
}

// try_items/5.
Index Printer::try_expr(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    const auto body_items = body(element(node, 2), opts);
    const auto cases = element(node, 3);
    const auto catches = element(node, 4);
    const auto after = element(node, 5);
    std::vector<Index> parts;
    if (cases.is_nil()) {
        parts.push_back(items_.step(Kind::step, word("try"), body_items));
    } else {
        const auto head = items_.list({items_.step(Kind::step, word("try"), body_items), reserved("of")});
        parts.push_back(items_.step(Kind::step, head, clauses(&Printer::case_clause, cases, opts)));
    }
    if (!catches.is_nil()) {
        parts.push_back(items_.step(Kind::step, word("catch"), clauses(&Printer::catch_clause, catches, opts)));
    }
    if (!after.is_nil()) {
        parts.push_back(items_.step(Kind::step, word("after"), body(after, opts)));
    }
    parts.push_back(reserved("end"));
    return items_.list(std::move(parts));
}

Index Printer::catch_expr(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto prec = preop_precedence("catch");
    return paren(prec.precedence, precedence,
                 items_.list({items_.step(Kind::step, word("catch"), item(element(node, 2), prec.right, opts))}));
}

Index Printer::maybe(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    return items_.list({items_.step(Kind::step, word("maybe"), body(element(node, 2), opts)), reserved("end")});
}

Index Printer::maybe_else(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto otherwise = element(node, 3);
    if (!is_node(otherwise, "else", 3)) {
        return unknown(node, precedence, opts);
    }
    return items_.list(
        {items_.step(Kind::step, word("maybe"), body(element(node, 2), opts)),
         items_.step(Kind::step, word("else"), clauses(&Printer::case_clause, element(otherwise, 2), opts)),
         reserved("end")});
}

Index Printer::maybe_match(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    return items_.list({items_.step(Kind::cstep, cat({item(element(node, 2), 0, opts), leaf(" ?=")}),
                                    item(element(node, 3), 0, opts))});
}

Index Printer::match(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto prec = inop_precedence("=");
    return paren(prec.precedence, precedence,
                 items_.list({items_.step(Kind::cstep, cat({item(element(node, 2), prec.left, opts), word(" =")}),
                                          item(element(node, 3), prec.right, opts))}));
}

Index Printer::prefix_op(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto op = element(node, 2);
    const auto prec = preop_precedence(op.is_atom() ? op.atom_spelling_unchecked() : std::string_view{});
    const std::array args{op};
    const auto sign = is_atom(op, "+") || is_atom(op, "-");
    const auto name = sign ? leaf(op.atom_spelling_unchecked()) : text("~s ", args);
    return paren(prec.precedence, precedence, cat({items_.reserved(name), item(element(node, 3), prec.right, opts)}));
}

Index Printer::infix_op(const Term &node, std::int64_t precedence, const Opts &opts) {
    const auto op = element(node, 2);
    const auto prec = inop_precedence(op.is_atom() ? op.atom_spelling_unchecked() : std::string_view{});
    const std::array args{op};
    std::vector<Index> parts{item(element(node, 3), prec.left, opts), items_.reserved(text("~s", args)),
                             item(element(node, 4), prec.right, opts)};
    if (is_atom(op, "andalso") || is_atom(op, "orelse")) {
        return paren(prec.precedence, precedence, items_.prefer_nl(listed(items_.empty()), true, std::move(parts)));
    }
    return paren(prec.precedence, precedence, items_.list(std::move(parts)));
}

Index Printer::remote(const Term &node, std::int64_t precedence, const Opts &opts) {
    return remote_items(element(node, 2), element(node, 3), precedence, opts);
}

Index Printer::remote_items(const Term &module, const Term &function, std::int64_t precedence, const Opts &opts) {
    const auto prec = inop_precedence(":");
    return paren(prec.precedence, precedence,
                 cat({item(module, prec.left, opts), ch(':'), item(function, prec.right, opts)}));
}

Index Printer::binary(const Term &node, std::int64_t /*precedence*/, const Opts &opts) {
    const auto fields = element(node, 2);
    if (fields.is_nil()) {
        return leaf("<<>>");
    }
    std::vector<Index> parts;
    for (const auto &field : forms::elements(fields)) {
        parts.push_back(bin_element(field, opts));
    }
    return cat({word("<<"), separated(ch(','), false, std::move(parts)), word(">>")});
}

Index Printer::bin_element(const Term &field, const Opts &opts) {
    if (!is_node(field, "bin_element", 5)) {
        lists::no_clause();
    }
    auto sized = item(element(field, 2), MAX_PRECEDENCE, opts);
    if (const auto size = element(field, 3); !is_atom(size, "default")) {
        sized = cat({sized, ch(':'), item(size, MAX_PRECEDENCE, opts)});
    }
    const auto types = element(field, 4);
    if (is_atom(types, "default")) {
        return sized;
    }
    std::vector<Index> parts{sized, ch('/')};
    for (const auto &type : forms::elements(types)) {
        if (parts.size() > 2) {
            parts.push_back(ch('-'));
        }
        parts.push_back(bin_type(type));
    }
    return cat(std::move(parts));
}

Index Printer::bin_type(const Term &type) {
    const auto abstract_of = [&](const Term &term) {
        return item(abstract_term(build_.context(), term, build_.integer(0), build_.atom("utf8"_atom)), 0, NO_HOOK);
    };
    if (type.is_tuple() && type.tuple_size_unchecked() == 2) {
        return cat({abstract_of(element(type, 0)), ch(':'), abstract_of(element(type, 1))});
    }
    return abstract_of(type);
}

Index Printer::debug_line(const Term &node, std::int64_t /*precedence*/, const Opts & /*opts*/) {
    const std::array args{element(node, 1), element(node, 2)};
    return is_atom(element(node, 0), "debug_line") ? text("beam_instruction:debug_line(~p, ~p)", args)
                                                   : text("beam_instruction:executable_line(~p, ~p)", args);
}

Index Printer::value(const Term &node, std::int64_t /*precedence*/, const Opts & /*opts*/) {
    return items_.of_term(Kind::value, element(node, 2));
}

// record_name/1: #name, #module:name or #_.
std::vector<Index> Printer::record_name(const Term &name) {
    if (name.is_tuple() && name.tuple_size_unchecked() == 2 && element(name, 0).is_atom() &&
        element(name, 1).is_atom()) {
        return {ch('#'), atom(element(name, 0)), ch(':'), atom(element(name, 1))};
    }
    if (name.is_nil()) {
        return {ch('#'), ch('_')};
    }
    if (!name.is_atom()) {
        lists::no_clause();
    }
    return {ch('#'), atom(name)};
}

Index Printer::record_fields(const Term &fields, const Opts &opts) {
    std::vector<Index> parts;
    for (const auto &field : forms::elements(fields)) {
        parts.push_back(record_field_def(field, opts));
    }
    return tuple(std::move(parts));
}

// record_field/2: Field = Value, typed fields and bare fields.
Index Printer::record_field_def(const Term &field, const Opts &opts) {
    const auto prec = inop_precedence("=");
    if (is_node(field, "record_field", 4)) {
        return items_.list({items_.step(Kind::cstep, cat({item(element(field, 2), prec.left, opts), word(" =")}),
                                        item(element(field, 3), prec.right, opts))});
    }
    if (is_node(field, "typed_record_field", 3)) {
        const auto inner = element(field, 1);
        if (is_node(inner, "record_field", 4)) {
            const auto value = typed(item(element(inner, 3), prec.right, opts), element(field, 2));
            return items_.list(
                {items_.step(Kind::cstep, cat({item(element(inner, 2), prec.left, opts), word(" =")}), value)});
        }
        return typed(record_field_def(inner, opts), element(field, 2));
    }
    if (is_node(field, "record_field", 3)) {
        return item(element(field, 2), 0, opts);
    }
    lists::no_clause();
}

Index Printer::map_field(const Term &field, const Opts &opts) {
    const auto assoc = is_node(field, "map_field_assoc", 4);
    if (!assoc && !is_node(field, "map_field_exact", 4)) {
        lists::no_clause();
    }
    return items_.list({items_.step(Kind::step, cat({item(element(field, 2), 0, opts), leaf(assoc ? " =>" : " :=")}),
                                    item(element(field, 3), 0, opts))});
}
} // namespace clause::runtime::builtins::front_end::pp
