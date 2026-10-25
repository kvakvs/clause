#include "../../match_spec/functions.hpp"
#include "../io_lib/write.hpp"
#include "forms.hpp"
#include <algorithm>
#include <array>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/code_server.hpp>

// epp's -if and -elif conditions in C++ (plan 21 step LIB34C, docs/builtins.md#epp): the guard expression
// erl_eval:exprs/3 would evaluate with no bindings and a local function handler raising badarg, so epp does not link
// erl_eval. Any error makes the condition false, as epp catches it.
namespace clause::runtime::builtins::front_end {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

// An evaluation error: the condition is false.
struct Failed {};

struct ExprRole;
using ExprArg = Named<ExprRole>;

class GuardEval final {
  public:
    explicit GuardEval(ProcessContext &context) : build_(context) {}

    // The value of an expression node.
    Term value(const Term &node) {
        const auto tag = forms::tag_of(node);
        if (forms::is_one_of(tag, {"integer", "char", "float", "atom"}) && node.tuple_size_unchecked() == 3) {
            return element(node, 2);
        }
        if (is_node(node, "string", 3)) {
            return element(node, 2);
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

  private:
    Term compound(const Term &node, std::string_view tag) {
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
        throw Failed{}; // Variables are unbound; records are undefined.
    }

    std::vector<Term> values(const Term &list) {
        std::vector<Term> out;
        for (const auto &node : forms::elements(list)) {
            out.push_back(value(node));
        }
        return out;
    }

    Term unary(const Term &node) { return guard_call(element(node, 2), {value(element(node, 3))}); }

    // andalso and orelse evaluate their right side only when needed, giving its value as it is; and and or take
    // booleans.
    Term binary(const Term &node) {
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
    Term short_circuit(const Term &node) {
        const auto left = value(element(node, 3));
        if (is_atom(left, is_atom(element(node, 2), "andalso") ? "false" : "true")) {
            return left;
        }
        if (!is_atom(left, "true") && !is_atom(left, "false")) {
            throw Failed{};
        }
        return value(element(node, 4));
    }

    Term logical(bool conjunction, const Term &left, const Term &right) const {
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
    Term call(const Term &callee, const std::vector<Term> &args) {
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
    Term guard_call(const Term &name, const std::vector<Term> &args) {
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
    Term is_record(const Term &term, const Term &tag) const {
        if (!tag.is_atom()) {
            throw Failed{};
        }
        const auto record =
            term.is_tuple() && term.tuple_size_unchecked() > 0 && term.tuple_element_unchecked(0).word() == tag.word();
        return build_.atom(record ? "true"_atom : "false"_atom);
    }

    // #{K => V} over `base`'s associations; K := V only updates a key `base` has.
    Term map_of(std::vector<std::pair<Term, Term>> entries, const Term &fields) {
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

    Term map_update(const Term &map, const Term &fields) {
        if (!map.is_map()) {
            throw Failed{};
        }
        return map_of(io_lib::map_entries(map, io_lib::MapsOrder::undefined), fields);
    }

    // <<Segments>>: each segment built as erl_eval's eval_bits builds it, the parts joined.
    Term bits(const Term &segments) {
        std::vector<Term> parts;
        for (const auto &segment : forms::elements(segments)) {
            add_segment(segment, parts);
        }
        const std::array joined{build_.list(parts)};
        return registered("erlang", "list_to_bitstring", joined);
    }

    // One bin_element's bits; a string literal gives one segment per character.
    void add_segment(const Term &segment, std::vector<Term> &parts) {
        if (!is_node(segment, "bin_element", 5)) {
            throw Failed{};
        }
        const auto size_node = element(segment, 3);
        const auto size = is_atom(size_node, "default") ? size_node : value(size_node);
        const std::array type_args{size, element(segment, 4)};
        const auto type = registered("clause_erl_eval", "bit_type", type_args);
        const auto expr = element(segment, 2);
        const auto items = is_node(expr, "string", 3) ? forms::elements(element(expr, 2)) : std::vector{value(expr)};
        for (const auto &item : items) {
            const std::array segment_args{item, element(type, 0), element(type, 1)};
            const auto built = registered("clause_erl_eval", "bit_segment", segment_args);
            if (built.is_atom()) {
                throw Failed{};
            }
            parts.push_back(built);
        }
    }

    // A registered builtin's result; its error makes the condition false.
    Term registered(std::string_view module, std::string_view function, std::span<const Term> args) const {
        auto &context = build_.context();
        const auto *builtin = context.code_server().builtins().find(module, function, args.size());
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

    forms::Builder build_;
};

// clause_epp:eval_if(Expr): the value of a -if condition, false when it raises.
Term eval_if(ProcessContext &context, const ExprArg &expr) {
    try {
        return GuardEval(context).value(expr.term);
    } catch (const Failed &) {
        return string::Terms(context).atom("false"_atom);
    } catch (const BuiltinFailure &) {
        return string::Terms(context).atom("false"_atom);
    }
}

constexpr std::array EPP_BUILTINS{
    typed_entry<eval_if>("clause_epp", "eval_if"),
};
} // namespace
} // namespace clause::runtime::builtins::front_end

namespace clause::runtime {
std::span<const BuiltinEntry> epp_builtins() noexcept { return builtins::front_end::EPP_BUILTINS; }
} // namespace clause::runtime
