#include "../../match_spec/compiler.hpp"
#include "../../memory/heap_object.hpp"
#include "select.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>

// Helpers of the select builtins (plan 21 step LIB11): compiling a specification argument and finding the keys its
// heads bind, as OTP's analyze_pattern does, so a select of bound keys looks them up instead of walking the table.
namespace clause::runtime::builtins::ets {
namespace {
namespace match_spec = detail::match_spec;

// Whether a key pattern names one term: no variable and no '_' anywhere, and no map (a map pattern matches every map
// holding its keys), as OTP's db_is_fully_bound decides.
bool fully_bound(const Term &term) {
    std::vector<Term> pending{term};
    while (!pending.empty()) {
        const auto node = pending.back();
        pending.pop_back();
        if (node.is_cons()) {
            pending.push_back(node.head_unchecked());
            pending.push_back(node.tail_unchecked());
        } else if (node.is_tuple()) {
            for (std::size_t at = 0; at < node.tuple_size_unchecked(); ++at) {
                pending.push_back(node.tuple_element_unchecked(at));
            }
        } else if (node.is_map() || match_spec::is_atom(node, "_") || match_spec::variable_number(node)) {
            return false;
        }
    }
    return true;
}

// Sort an ordered_set's keys in term order, each key once (keys that compare equal are one key there).
void ordered(std::vector<Term> &keys) {
    const auto order = [](const Term &left, const Term &right) {
        return detail::ets::key_order(left.word(), right.word()) < 0;
    };
    std::ranges::sort(keys, order);
    const auto equal = [](const Term &left, const Term &right) {
        return detail::ets::key_order(left.word(), right.word()) == 0;
    };
    keys.erase(std::ranges::unique(keys, equal).begin(), keys.end());
}

// A hash table's keys, each once, in the order the clauses name them.
void distinct(std::vector<Term> &keys) {
    std::vector<Term> kept;
    for (const auto &key : keys) {
        const auto seen = std::ranges::any_of(
            kept, [&key](const Term &other) { return detail::ets::same_key(other.word(), key.word()); });
        if (!seen) {
            kept.push_back(key);
        }
    }
    keys = std::move(kept);
}
} // namespace

std::shared_ptr<const Program> compiled(ProcessContext &context, const Term &spec) {
    auto result = match_spec::compile(context, spec, match_spec::Dialect::table, false);
    if (result.system_limit) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::system_limit};
    }
    if (!result.program) {
        bad_argument();
    }
    return std::move(result.program);
}

Term pattern_spec(ProcessContext &context, const Term &pattern, std::string_view result) {
    TermFactory factory(context);
    const auto body = need(factory.list(std::array{atom(context, result)}));
    const auto clause = need(factory.tuple(std::array{pattern, need(factory.nil()), body}));
    return need(factory.list(std::array{clause}));
}

namespace {
// What a clause's head says of the key: it leaves it open, matches no object of the table, or binds it.
struct HeadKey {
    bool open = false;
    std::optional<Term> key;
};

HeadKey head_key(const Term &head, std::size_t keypos) {
    if (!head.is_tuple()) {
        return {.open = match_spec::variable_number(head).has_value() || match_spec::is_atom(head, "_"),
                .key = std::nullopt};
    }
    if (head.tuple_size_unchecked() < keypos) {
        return {.open = false, .key = std::nullopt};
    }
    const auto key = head.tuple_element_unchecked(keypos - 1);
    return fully_bound(key) ? HeadKey{.open = false, .key = key} : HeadKey{.open = true, .key = std::nullopt};
}
} // namespace

std::optional<std::vector<Term>> bound_keys(const Table &table, const Program &program) {
    std::vector<Term> keys;
    for (auto rest = program.source.term(); rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto found = head_key(rest.head_unchecked().tuple_element_unchecked(0), table.options().keypos);
        if (found.open) {
            return std::nullopt;
        }
        if (found.key) {
            keys.push_back(*found.key);
        }
    }
    if (table.hashed()) {
        distinct(keys);
    } else {
        ordered(keys);
    }
    return keys;
}

std::optional<Term> run_stored(ProcessContext &context, const Program &program, Word object) {
    return match_spec::run(context, program, object, match_spec::Source::stored);
}
} // namespace clause::runtime::builtins::ets
