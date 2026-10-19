#include "compiler.hpp"
#include <terms.hpp>

// Heads of match specifications (plan 21 step LIB11): variables bind on first use and compare after, '_' matches
// anything, tuples and lists match element by element, maps match when they hold every key of the pattern, and
// everything else matches an exactly equal term, as OTP's dmc_one_term compiles them.
namespace clause::runtime::detail::match_spec {
std::size_t Compiler::add(Pattern pattern) {
    program_.patterns.push_back(std::move(pattern));
    return program_.patterns.size() - 1;
}

std::size_t Compiler::pattern(const Term &term, std::size_t depth) {
    if (depth > DEPTH_LIMIT) {
        system_limit_ = true;
        throw Stop{};
    }
    if (const auto number = variable_number(term)) {
        return pattern_variable(*number);
    }
    if (is_atom(term, "_")) {
        return add({.kind = Pattern::Kind::skip, .slot = 0, .word = 0, .children = {}});
    }
    if (term.is_tuple()) {
        return pattern_tuple(term, depth);
    }
    if (term.is_cons()) {
        return pattern_list(term, depth);
    }
    if (term.is_map()) {
        return pattern_map(term, depth);
    }
    return add({.kind = Pattern::Kind::constant, .slot = 0, .word = keep(term), .children = {}});
}

std::size_t Compiler::pattern_variable(std::uint64_t number) {
    const auto kind = bound_.insert(number).second ? Pattern::Kind::bind : Pattern::Kind::compare;
    return add({.kind = kind, .slot = slot(number), .word = 0, .children = {}});
}

std::size_t Compiler::pattern_tuple(const Term &term, std::size_t depth) {
    std::vector<std::size_t> children;
    children.reserve(term.tuple_size_unchecked());
    for (std::size_t at = 0; at < term.tuple_size_unchecked(); ++at) {
        children.push_back(pattern(term.tuple_element_unchecked(at), depth + 1));
    }
    return add({.kind = Pattern::Kind::tuple, .slot = 0, .word = 0, .children = std::move(children)});
}

// A list's cells become a chain of cons nodes, built from its last cell, so a long list does not nest the compiler.
std::size_t Compiler::pattern_list(const Term &term, std::size_t depth) {
    std::vector<std::size_t> heads;
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        heads.push_back(pattern(rest.head_unchecked(), depth + 1));
    }
    auto node = pattern(rest, depth + 1);
    for (auto at = heads.rbegin(); at != heads.rend(); ++at) {
        node = add({.kind = Pattern::Kind::cons, .slot = 0, .word = 0, .children = {*at, node}});
    }
    return node;
}

// A map pattern's keys are constants (a variable or '_' as a key stops compiling); its values are patterns.
std::size_t Compiler::pattern_map(const Term &term, std::size_t depth) {
    std::vector<Term> keys;
    std::vector<std::size_t> values;
    for (const auto &[key, value] : need(term.map_entries())) {
        if (variable_number(key)) {
            error("Variable found in map key.");
            throw Stop{};
        }
        if (is_atom(key, "_")) {
            error("Underscore found in map key.");
            throw Stop{};
        }
        keys.push_back(key);
        values.push_back(pattern(value, depth + 1));
    }
    const auto list = need(TermFactory(context_).list(keys));
    return add({.kind = Pattern::Kind::map, .slot = 0, .word = keep(list), .children = std::move(values)});
}
} // namespace clause::runtime::detail::match_spec
