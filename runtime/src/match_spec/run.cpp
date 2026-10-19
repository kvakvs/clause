#include "../builtins/support.hpp"
#include "../memory/heap_object.hpp"
#include "../terms/structural_order.hpp"
#include "functions.hpp"
#include "running.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>

// The interpreter of match specifications (plan 21 step LIB11): clauses are tried in order; a head binds the object's
// parts in place, guards read them in place, and a body copies every value it gives into the caller's heap, as OTP's
// db_prog_match does with ERTS_PAM_COPY_RESULT. A call that raises fails the clause in a guard and is 'EXIT' in a
// body; and/or of a term that is no boolean count as raising.
namespace clause::runtime::detail::match_spec {
namespace {
using Kind = Expression::Kind;

Term term_of(Word word) { return TermAccess::make(word); }

// Whether two words are exactly equal terms (=:=), as head constants and repeated variables match.
bool same(Word left, Word right) {
    if (left == right) {
        return true;
    }
    const auto order = structural_order(term_of(left), term_of(right), true);
    return order && *order == 0;
}

Word atom_word(ProcessContext &context, AtomName name) {
    return builtins::need(TermFactory(context).atom(name)).word();
}
} // namespace

Word Run::built(const TermResult<Term> &term) { return builtins::need(term).word(); }

Word Run::copied(Word word) {
    if (source_ == Source::heap) {
        return word;
    }
    return builtins::need(term_of(word).copy_to(context_.heap())).word();
}

// A list's cells are followed in a loop, so a long list in a head does not nest the interpreter.
bool Run::match(std::size_t index, const Term &value) {
    auto term = value;
    for (;;) {
        const auto &node = program_.patterns[index];
        if (node.kind != Pattern::Kind::cons) {
            return match_node(node, term.word());
        }
        if (!term.is_cons() || !match(node.children[0], term.head_unchecked())) {
            return false;
        }
        index = node.children[1];
        term = term.tail_unchecked();
    }
}

bool Run::match_node(const Pattern &node, Word value) {
    switch (node.kind) {
    case Pattern::Kind::skip:
        return true;
    case Pattern::Kind::bind:
        values_[node.slot] = value;
        return true;
    case Pattern::Kind::compare:
        return same(values_[node.slot], value);
    case Pattern::Kind::constant:
        return same(node.word, value);
    case Pattern::Kind::tuple:
        return match_tuple(node, value);
    default:
        return match_map(node, value);
    }
}

bool Run::match_tuple(const Pattern &node, Word value) {
    const auto term = term_of(value);
    if (!term.is_tuple() || term.tuple_size_unchecked() != node.children.size()) {
        return false;
    }
    for (std::size_t at = 0; at < node.children.size(); ++at) {
        if (!match(node.children[at], term.tuple_element_unchecked(at))) {
            return false;
        }
    }
    return true;
}

// A map pattern matches a map holding at least its keys (exactly equal), each value matching.
bool Run::match_map(const Pattern &node, Word value) {
    const auto term = term_of(value);
    if (!term.is_map() || term.map_size_unchecked() < node.children.size()) {
        return false;
    }
    auto key = term_of(node.word);
    for (const auto child : node.children) {
        const auto found = builtins::need(term.map_find(key.head_unchecked()));
        if (!found || !match(child, *found)) {
            return false;
        }
        key = key.tail_unchecked();
    }
    return true;
}

// A trace head's patterns against the arguments: as many, or at least as many for a head ending in '_'.
bool Run::match_arguments(const Clause &clause, std::span<const Word> arguments) {
    const auto &head = program_.patterns[clause.head];
    const auto count = head.children.size();
    if (clause.prefix ? arguments.size() < count : arguments.size() != count) {
        return false;
    }
    for (std::size_t at = 0; at < count; ++at) {
        if (!match(head.children[at], term_of(arguments[at]))) {
            return false;
        }
    }
    return true;
}

void Run::start(const Clause &clause) {
    clause_ = &clause;
    std::ranges::fill(copies_, Word{0});
    whole_copy_ = 0;
}

bool Run::guards_hold(const Clause &clause) {
    in_guard_ = true;
    const auto held = std::ranges::all_of(clause.guards, [this](std::size_t guard) {
        const auto value = evaluate(guard);
        return value && *value == abi::v1::true_atom;
    });
    in_guard_ = false;
    return held;
}

Word Run::body(const Clause &clause) {
    Word value = abi::v1::true_atom;
    for (const auto expression : clause.body) {
        value = *evaluate(expression);
    }
    return value;
}

std::optional<Term> Run::object(Word object) {
    object_ = object;
    for (const auto &clause : program_.clauses) {
        start(clause);
        if (match(clause.head, term_of(object)) && guards_hold(clause)) {
            return term_of(body(clause));
        }
    }
    return std::nullopt;
}

std::optional<Term> Run::arguments(const Term &list) {
    object_ = list.word();
    std::vector<Word> words;
    for (auto rest = list; rest.is_cons(); rest = rest.tail_unchecked()) {
        words.push_back(rest.head_unchecked().word());
    }
    for (const auto &clause : program_.clauses) {
        start(clause);
        const auto matched = clause.whole_list ? match(clause.head, list) : match_arguments(clause, words);
        if (matched && guards_hold(clause)) {
            result_ = abi::v1::true_atom;
            static_cast<void>(body(clause));
            return term_of(result_);
        }
    }
    return std::nullopt;
}

Word Run::constant(Word word) {
    return in_guard_ ? word : builtins::need(term_of(word).copy_to(context_.heap())).word();
}

Word Run::variable(std::size_t slot) {
    if (in_guard_) {
        return values_[slot];
    }
    if (copies_[slot] == 0) {
        copies_[slot] = copied(values_[slot]);
    }
    return copies_[slot];
}

Word Run::whole() {
    if (in_guard_) {
        return object_;
    }
    if (whole_copy_ == 0) {
        whole_copy_ = copied(object_);
    }
    return whole_copy_;
}

// '$$': the values of the variables the clause's head binds, in variable number order.
Word Run::bindings() {
    std::vector<Word> values;
    values.reserve(clause_->bindings.size());
    for (const auto slot : clause_->bindings) {
        values.push_back(variable(slot));
    }
    TermFactory factory(context_);
    return built(factory.list_of_words(values, builtins::need(factory.nil())));
}

std::optional<std::vector<Word>> Run::operands(const Expression &node) {
    std::vector<Word> values;
    values.reserve(node.operands.size());
    for (const auto operand : node.operands) {
        const auto value = evaluate(operand);
        if (!value) {
            return std::nullopt;
        }
        values.push_back(*value);
    }
    return values;
}

// A tuple, cons or map of operand values; a map's keys are the first half of its operands.
std::optional<Word> Run::construct(const Expression &node) {
    const auto values = operands(node);
    if (!values) {
        return std::nullopt;
    }
    TermFactory factory(context_);
    if (node.kind == Kind::tuple) {
        return built(factory.tuple_of_words(*values));
    }
    if (node.kind == Kind::cons) {
        return built(factory.list_of_words(std::span(*values).first(1), term_of((*values)[1])));
    }
    std::vector<std::pair<Term, Term>> entries;
    const auto half = values->size() / 2;
    for (std::size_t at = 0; at < half; ++at) {
        // A guard's operands may be stored words; the map is built of copies.
        const auto key = (*values)[at];
        const auto value = (*values)[half + at];
        entries.emplace_back(term_of(in_guard_ ? copied(key) : key), term_of(in_guard_ ? copied(value) : value));
    }
    return built(factory.map(entries));
}

// and/or: every operand evaluated, each a boolean, or the whole raises.
std::optional<Word> Run::logic(const Expression &node) {
    const auto values = operands(node);
    if (!values) {
        return std::nullopt;
    }
    const auto boolean = [](Word value) { return value == abi::v1::true_atom || value == abi::v1::false_atom; };
    if (!std::ranges::all_of(*values, boolean)) {
        return std::nullopt;
    }
    const auto conjunction = node.kind == Kind::and_all;
    const auto stop = conjunction ? abi::v1::false_atom : abi::v1::true_atom;
    const auto stopped = std::ranges::find(*values, stop) != values->end();
    return stopped ? stop : (conjunction ? abi::v1::true_atom : abi::v1::false_atom);
}

// andalso/orelse: operands in order until one decides; the last one's value is the result as it is.
std::optional<Word> Run::sequence(const Expression &node) {
    const auto conjunction = node.kind == Kind::and_also;
    const auto stop = conjunction ? abi::v1::false_atom : abi::v1::true_atom;
    const auto go_on = conjunction ? abi::v1::true_atom : abi::v1::false_atom;
    for (std::size_t at = 0; at + 1 < node.operands.size(); ++at) {
        const auto value = evaluate(node.operands[at]);
        if (!value || (*value != stop && *value != go_on)) {
            return std::nullopt;
        }
        if (*value == stop) {
            return stop;
        }
    }
    return evaluate(node.operands.back());
}

std::optional<Word> Run::call(const Expression &node) {
    const auto values = operands(node);
    if (!values) {
        return std::nullopt;
    }
    return call_function(context_, node, *values);
}

// The value of a node without operands: a constant, a variable, '$_', '$$' or self().
Word Run::leaf(const Expression &node) {
    switch (node.kind) {
    case Kind::constant:
        return constant(node.word);
    case Kind::variable:
        return variable(node.slot);
    case Kind::whole:
        return whole();
    case Kind::bindings:
        return bindings();
    default:
        return built(TermFactory(context_).pid(context_.identity()));
    }
}

std::optional<Word> Run::compound(const Expression &node) {
    switch (node.kind) {
    case Kind::and_all:
    case Kind::or_all:
        return logic(node);
    case Kind::and_also:
    case Kind::or_else:
        return sequence(node);
    case Kind::call:
        return call(node);
    case Kind::action:
        return run_action(*this, node);
    default:
        return construct(node);
    }
}

std::optional<Word> Run::evaluate(std::size_t index) {
    const auto &node = program_.expressions[index];
    if (node.kind <= Kind::bindings || node.kind == Kind::self) {
        return leaf(node);
    }
    const auto value = compound(node);
    // A raising call in a body gives 'EXIT' in its place, and the body goes on.
    if (!value && !in_guard_) {
        return atom_word(context_, "EXIT"_atom);
    }
    return value;
}

std::optional<Term> run(ProcessContext &context, const Program &program, Word object, Source source) {
    Run run(context, program, source);
    return run.object(object);
}

std::optional<Term> run_trace(ProcessContext &context, const Program &program, const Term &arguments,
                              TraceFlags &flags) {
    Run run(context, program, Source::heap, &flags);
    return run.arguments(arguments);
}
} // namespace clause::runtime::detail::match_spec
