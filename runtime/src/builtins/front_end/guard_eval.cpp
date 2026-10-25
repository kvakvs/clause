#include "../../match_spec/functions.hpp"
#include "../collections/collections.hpp"
#include "../io_lib/write.hpp"
#include "eval.hpp"
#include <algorithm>
#include <array>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/code_server.hpp>

// Guard expressions and bindings of the C++ erl_eval parts (plan 21 step LIB34C): -if conditions of epp, and the
// map keys and segment sizes of erl_eval's patterns.
namespace clause::runtime::builtins::front_end::eval {
using forms::element;
using forms::is_atom;
using forms::is_node;

std::optional<Term> Bindings::find(const Term &name) const {
    if (term_.is_map()) {
        return need(term_.map_find(name));
    }
    // orddict:find/2: the walk stops at the first larger key, as an unsorted list makes OTP stop too.
    for (auto rest = term_; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto entry = rest.head_unchecked();
        const auto order = collections::compare(name, element(entry, 0));
        if (order < 0) {
            return std::nullopt;
        }
        if (order == 0) {
            return element(entry, 1);
        }
    }
    return std::nullopt;
}

void Bindings::add(const Term &name, const Term &value) {
    if (term_.is_map()) {
        auto entries = io_lib::map_entries(term_, io_lib::MapsOrder::undefined);
        const auto found =
            std::ranges::find_if(entries, [&](const auto &entry) { return string::same(entry.first, name); });
        if (found != entries.end()) {
            found->second = value;
        } else {
            entries.emplace_back(name, value);
        }
        term_ = need(TermFactory(build_->context()).map(entries));
        return;
    }
    // orddict:store/3: before the first larger key, in place of an equal one.
    std::vector<Term> entries;
    auto rest = term_;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto entry = rest.head_unchecked();
        const auto order = collections::compare(element(entry, 0), name);
        if (order >= 0) {
            break;
        }
        entries.push_back(entry);
    }
    const std::array pair{name, value};
    entries.push_back(build_->tuple(pair));
    if (rest.is_cons() && collections::compare(element(rest.head_unchecked(), 0), name) == 0) {
        rest = rest.tail_unchecked();
    }
    term_ = build_->terms().list(entries, rest);
}

Term GuardEval::value(const Term &node) {
    const auto tag = forms::tag_of(node);
    if (forms::is_one_of(tag, {"integer", "char", "float", "atom", "string"}) && node.tuple_size_unchecked() == 3) {
        return element(node, 2);
    }
    if (is_node(node, "var", 3)) {
        return variable(node);
    }
    if (is_node(node, "nil", 2)) {
        return build_.nil();
    }
    if (is_node(node, "cons", 4)) {
        const auto head = value(element(node, 2));
        return build_.terms().cons(head, value(element(node, 3)));
    }
    if (is_node(node, "tuple", 3)) {
        return build_.tuple(values(element(node, 2)));
    }
    return compound(node, tag);
}

Term GuardEval::compound(const Term &node, std::string_view tag) {
    if (tag == "op") {
        return node.tuple_size_unchecked() == 4 ? unary(node) : binary(node);
    }
    if (is_node(node, "call", 4)) {
        return call(element(node, 2), values(element(node, 3)));
    }
    if (is_node(node, "map", 3)) {
        return map_of({}, element(node, 2));
    }
    if (is_node(node, "map", 4)) {
        return map_update(value(element(node, 2)), element(node, 3));
    }
    if (is_node(node, "bin", 3)) {
        return bits(element(node, 2));
    }
    throw Failed{}; // Records are undefined without the module's definitions.
}

Term GuardEval::variable(const Term &node) const {
    const auto name = element(node, 2);
    const auto found = bindings_ ? bindings_->find(name) : std::nullopt;
    if (!found) {
        throw Unbound{name};
    }
    return *found;
}

std::vector<Term> GuardEval::values(const Term &list) {
    std::vector<Term> out;
    for (const auto &node : forms::elements(list)) {
        out.push_back(value(node));
    }
    return out;
}

Term GuardEval::unary(const Term &node) { return guard_call(element(node, 2), {value(element(node, 3))}); }

// andalso and orelse evaluate their right side only when needed, giving its value as it is; and and or take booleans.
Term GuardEval::binary(const Term &node) {
    const auto op = element(node, 2);
    if (is_atom(op, "andalso") || is_atom(op, "orelse")) {
        return short_circuit(node);
    }
    const auto left = value(element(node, 3));
    const auto right = value(element(node, 4));
    if (is_atom(op, "and") || is_atom(op, "or")) {
        return logical(is_atom(op, "and"), left, right);
    }
    return guard_call(op, {left, right});
}

// The left side decides andalso when false and orelse when true; otherwise the value of the right side.
Term GuardEval::short_circuit(const Term &node) {
    const auto left = value(element(node, 3));
    if (is_atom(left, is_atom(element(node, 2), "andalso") ? "false" : "true")) {
        return left;
    }
    if (!is_atom(left, "true") && !is_atom(left, "false")) {
        throw Failed{};
    }
    return value(element(node, 4));
}

Term GuardEval::logical(bool conjunction, const Term &left, const Term &right) const {
    const auto truth = [](const Term &term) {
        if (!is_atom(term, "true") && !is_atom(term, "false")) {
            throw Failed{};
        }
        return is_atom(term, "true");
    };
    // Both operands must be booleans, whatever the first one is.
    const auto first = truth(left);
    const auto second = truth(right);
    const auto result = conjunction ? first && second : first || second;
    return build_.atom(result ? "true"_atom : "false"_atom);
}

// A local call is an auto-imported BIF (others raise badarg); a remote one must be erlang's.
Term GuardEval::call(const Term &callee, const std::vector<Term> &args) {
    if (is_node(callee, "atom", 3)) {
        return guard_call(element(callee, 2), args);
    }
    if (is_node(callee, "remote", 4) && is_node(element(callee, 2), "atom", 3) &&
        is_atom(element(element(callee, 2), 2), "erlang") && is_node(element(callee, 3), "atom", 3)) {
        return guard_call(element(element(callee, 3), 2), args);
    }
    throw Failed{};
}

// The guard BIF or operator `name` on argument values, as match specifications call them.
Term GuardEval::guard_call(const Term &name, const std::vector<Term> &args) {
    auto &context = build_.context();
    if (!name.is_atom()) {
        throw Failed{};
    }
    const auto spelling = name.atom_spelling_unchecked();
    if (spelling == "self" && args.empty()) {
        return need(TermFactory(context).pid(context.identity()));
    }
    if (spelling == "is_record" && args.size() == 2) {
        return is_record(args[0], args[1]);
    }
    const auto *function = detail::match_spec::guard_function(spelling, args.size());
    if (function == nullptr) {
        throw Failed{};
    }
    detail::match_spec::Expression call;
    call.kind = detail::match_spec::Expression::Kind::call;
    call.function = function->function;
    call.operation = static_cast<decltype(call.operation)>(function->operation);
    std::vector<Word> words;
    words.reserve(args.size());
    for (const auto &arg : args) {
        words.push_back(arg.word());
    }
    const auto result = detail::match_spec::call_function(context, call, words);
    if (!result) {
        throw Failed{};
    }
    return need(Term::from_word(*result, context));
}

// erlang:is_record/2: badarg unless the tag is an atom.
Term GuardEval::is_record(const Term &term, const Term &tag) const {
    if (!tag.is_atom()) {
        throw Failed{};
    }
    const auto record =
        term.is_tuple() && term.tuple_size_unchecked() > 0 && term.tuple_element_unchecked(0).word() == tag.word();
    return build_.atom(record ? "true"_atom : "false"_atom);
}

// #{K => V} over `entries`; K := V only updates a key they have.
Term GuardEval::map_of(std::vector<std::pair<Term, Term>> entries, const Term &fields) {
    for (const auto &field : forms::elements(fields)) {
        const auto exact = is_node(field, "map_field_exact", 4);
        if (!exact && !is_node(field, "map_field_assoc", 4)) {
            throw Failed{};
        }
        const auto key = value(element(field, 2));
        const auto item = value(element(field, 3));
        const auto found =
            std::ranges::find_if(entries, [&](const auto &entry) { return string::same(entry.first, key); });
        if (found != entries.end()) {
            found->second = item;
        } else if (exact) {
            throw Failed{};
        } else {
            entries.emplace_back(key, item);
        }
    }
    return need(TermFactory(build_.context()).map(entries));
}

Term GuardEval::map_update(const Term &map, const Term &fields) {
    if (!map.is_map()) {
        throw Failed{};
    }
    return map_of(io_lib::map_entries(map, io_lib::MapsOrder::undefined), fields);
}

// <<Segments>>: each segment built as erl_eval's eval_bits builds it, the parts joined.
Term GuardEval::bits(const Term &segments) {
    std::vector<Term> parts;
    for (const auto &segment : forms::elements(segments)) {
        add_segment(segment, parts);
    }
    const std::array joined{build_.list(parts)};
    return call_registered(build_.context(), {.module = "erlang", .function = "list_to_bitstring"}, joined);
}

// One bin_element's bits; a string literal gives one segment per character.
void GuardEval::add_segment(const Term &segment, std::vector<Term> &parts) {
    if (!is_node(segment, "bin_element", 5)) {
        throw Failed{};
    }
    auto &context = build_.context();
    const auto size_node = element(segment, 3);
    const std::array type_args{is_atom(size_node, "default") ? size_node : value(size_node), element(segment, 4)};
    const auto type = bit_type(context, type_args);
    const std::array spec{element(type, 0), element(type, 1)};
    const auto expr = element(segment, 2);
    const auto items = is_node(expr, "string", 3) ? forms::elements(element(expr, 2)) : std::vector{value(expr)};
    for (const auto &item : items) {
        const auto built = bit_segment(context, item, spec);
        if (built.is_atom()) {
            throw Failed{};
        }
        parts.push_back(built);
    }
}

bool evaluation_failure(const std::exception_ptr &failure) {
    try {
        std::rethrow_exception(failure);
    } catch (const Failed &) {
        return true;
    } catch (const Unbound &) {
        return true;
    } catch (const BuiltinFailure &) {
        return true;
    } catch (...) {
        return false;
    }
}

Term call_registered(ProcessContext &context, const Callee &callee, std::span<const Term> args) {
    const auto *builtin = context.code_server().builtins().find(callee.module, callee.function, args.size());
    if (builtin == nullptr) {
        throw Failed{};
    }
    std::vector<Word> words;
    words.reserve(args.size());
    for (const auto &arg : args) {
        words.push_back(arg.word());
    }
    const auto result = call_builtin(context, *builtin, words.data());
    if (result == 0) {
        context.generated_calls().clear();
        throw Failed{};
    }
    return need(Term::from_word(result, context));
}
} // namespace clause::runtime::builtins::front_end::eval
