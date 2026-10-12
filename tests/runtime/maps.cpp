#include "terms.hpp"
#include <array>
#include <clause/abi/maps.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace clause::runtime;
using Op = clause::abi::v1::MapOperation;
using Outcome = clause::abi::v1::MapOutcome;
using Status = clause::abi::v1::Status;

// Keep ownership/publication assertions active in optimized native builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Inspect the public service's semantic outcome without exposing private map storage.
std::pair<Outcome, Term> invoke(ProcessContext &context, Op operation, std::span<const Term> input) {
    std::vector<Word> words;
    for (const auto &value : input) {
        words.push_back(value.word());
    }
    GeneratedInvocation scope(context.generated_calls());
    Word output = 0;
    const auto result = static_cast<Outcome>(
        CLAUSE_map_v1(&context, static_cast<std::uint8_t>(operation), words.data(), words.size(), &output));
    require(!context.generated_calls().failure(), "unexpected map infrastructure failure");
    return {result, Term::from_word(output, context).value()};
}

// Immutable tables normalize duplicate exact keys, preserve numeric key distinctions and roll back failed updates.
Term construction(ProcessContext &context) {
    TermFactory factory(context);
    const auto one = factory.integer(1).value();
    const auto two = factory.integer(2).value();
    const auto real = factory.floating(1.0).value();
    const auto key = factory.atom("key").value();
    const auto map =
        factory.map(std::array{std::pair{key, one}, std::pair{real, two}, std::pair{one, one}, std::pair{key, two}})
            .value();
    require(map.map_size() == 3, "map duplicate/type identity incorrect");
    require(map.map_find(key)->value().integer_value() == 2, "last association did not win");
    require(map.map_find(real)->value().integer_value() == 2 && map.map_find(one)->value().integer_value() == 1,
            "integer/float keys coalesced");
    const auto reversed =
        factory.map(std::array{std::pair{one, one}, std::pair{real, two}, std::pair{key, two}}).value();
    require(map.word() != reversed.word() && map.exactly_equal(reversed).value(),
            "map equality depended on allocation");
    const auto missing = factory.atom("missing").value();
    const auto used = context.heap().used_words();
    const auto failed = invoke(context, Op::update, std::array{map, missing, one, one});
    require(failed.first == Outcome::bad_key && failed.second.exactly_equal(missing).value(), "exact update lost key");
    require(context.heap().used_words() == used && map.map_size() == 3, "failed update published partial storage");
    const auto updated = invoke(context, Op::update, std::array{map, key, one, one});
    require(updated.first == Outcome::success && updated.second.map_find(key)->value().integer_value() == 1,
            "exact update recovery failed");
    require(map.map_find(key)->value().integer_value() == 2, "update mutated old map");
    return map;
}

// Parent pins keep extracted values and compound keys stable through growth, then reject expired access.
void ownership() {
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context().value();
    TermFactory factory(context);
    const auto map = construction(context);
    const auto nested = factory.map(std::array{std::pair{map, map}}).value();
    const auto child = nested.map_find(map)->value();
    auto &foreign = *runtime->create_context().value();
    require(Term::from_word(map.word(), foreign) == std::unexpected(TermError::wrong_owner), "foreign map admitted");
    const auto foreign_key = TermFactory(foreign).map({}).value();
    require(nested.map_find(foreign_key) == std::unexpected(TermError::wrong_owner), "foreign key admitted");
    for (unsigned i = 0; i < 100; ++i) {
        require(factory.map(std::array{std::pair{map, nested}}).has_value(), "growth failed");
    }
    require(child.exactly_equal(map).value(), "later allocation damaged extracted map");
    require(runtime->destroy_context(&context) == Status::ok, "map owner teardown failed");
    require(map.map_size() == std::unexpected(TermError::expired_context), "expired map accessed");
    require(child.map_entries() == std::unexpected(TermError::expired_context), "expired child accessed");
    require(factory.map({}) == std::unexpected(TermError::expired_context), "expired factory allocated map");
}

// Invalid service arrays leave output untouched; a fresh invocation can construct an empty map.
void malformed() {
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context().value();
    const auto one = TermFactory(context).integer(1)->word();
    Word output = 123;
    {
        GeneratedInvocation scope(context.generated_calls());
        require(CLAUSE_map_v1(&context, static_cast<std::uint8_t>(Op::make), &one, 1, &output) ==
                    static_cast<std::uint8_t>(Outcome::failure),
                "odd map array accepted");
        require(output == 123 && context.generated_calls().failure().has_value(), "invalid map array published output");
    }
    const auto value = invoke(context, Op::make, {});
    require(value.first == Outcome::success && value.second.map_size() == 0, "map service retry failed");
}

// A list of `length` sevens ending in `last`, as a map key that takes a long comparison.
Term long_key(TermFactory &factory, std::size_t length, std::int64_t last) {
    std::vector elements(length, factory.integer(7).value());
    elements.back() = factory.integer(last).value();
    return factory.list(elements).value();
}

// Descending keys with duplicates sort into key order, and the last value of each key wins.
void unsorted_keys(ProcessContext &context) {
    TermFactory factory(context);
    std::vector<std::pair<Term, Term>> entries;
    for (std::int64_t key = 999; key >= 0; --key) {
        entries.emplace_back(factory.integer(key % 500).value(), factory.integer(key).value());
    }
    const auto map = factory.map(entries).value();
    require(map.map_size() == 500, "duplicate keys survived");
    for (std::size_t i = 0; i < 500; ++i) {
        const auto entry =
            invoke(context, Op::key_at, std::array{map, factory.integer(static_cast<std::int64_t>(i)).value()});
        require(entry.second.integer_value() == static_cast<std::int64_t>(i), "keys not in order");
    }
    require(map.map_find(factory.integer(7).value()).value()->integer_value() == 7, "last duplicate did not win");
}

// Maps have no size or key-work cap (plan 11 step 27D): one entry past the former 1,000,000 cap builds, grows by an
// update and finds keys; keys that are long lists are compared in full.
void large_maps() {
    constexpr std::int64_t size = 1'000'001;
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context().value();
    unsorted_keys(context);
    TermFactory factory(context);
    std::vector<std::pair<Term, Term>> entries;
    for (std::int64_t key = 0; key < size; ++key) {
        entries.emplace_back(factory.integer(key).value(), factory.integer(key).value());
    }
    const auto map = factory.map(entries).value();
    require(map.map_size() == static_cast<std::size_t>(size), "large map lost entries");
    require(map.map_find(factory.integer(size / 2).value()).value()->integer_value() == size / 2,
            "large map lookup failed");
    const auto grown =
        invoke(context, Op::update,
               std::array{map, factory.integer(size).value(), factory.atom("new").value(), factory.integer(0).value()});
    require(grown.first == Outcome::success && grown.second.map_size() == static_cast<std::size_t>(size) + 1,
            "large map update failed");

    const auto length = static_cast<std::size_t>(size);
    const auto lists = factory
                           .map(std::array{std::pair{long_key(factory, length, 1), factory.atom("one").value()},
                                           std::pair{long_key(factory, length, 2), factory.atom("two").value()}})
                           .value();
    const auto found = lists.map_find(long_key(factory, length, 2)).value();
    require(lists.map_size() == 2 && found && found->atom_spelling() == "two", "long list key not found");
}

// The admitted term of a root word after a collection rewrote it.
Term rooted(ProcessContext &context, Word word) { return Term::from_word(word, context).value(); }

// Maps above 32 entries are off-heap trees (plan 20 step OPT0.5A): an update copies one path and leaves its source
// intact, a failed update releases what it built, and collections rewrite shared trees and free dead versions.
void tree_maps() {
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context().value();
    TermFactory factory(context);
    std::vector<std::pair<Term, Term>> entries;
    for (std::int64_t key = 0; key < 100; ++key) {
        const auto boxed = factory.tuple(std::array{factory.integer(key).value()}).value();
        entries.emplace_back(factory.integer(key).value(), boxed);
    }
    const auto map = factory.map(entries).value();
    const auto built = context.heap().off_heap_words();
    require(map.map_size() == 100 && built > 0, "large map stayed on the heap");
    const auto key = factory.integer(7).value();
    const auto one = factory.integer(1).value();
    const auto updated = invoke(context, Op::update, std::array{map, key, factory.atom("one").value(), one});
    require(updated.first == Outcome::success && updated.second.map_size() == 100, "tree update failed");
    const auto grown = context.heap().off_heap_words();
    require(grown > built && grown < 2 * built, "update did not share the unchanged nodes");
    const auto used = context.heap().used_words();
    const auto failed =
        invoke(context, Op::update, std::array{map, key, one, one, factory.atom("absent").value(), one, one});
    require(failed.first == Outcome::bad_key && context.heap().used_words() == used &&
                context.heap().off_heap_words() == grown,
            "failed tree update kept storage");

    std::vector<Word> roots{map.word(), updated.second.word()};
    require(context.heap().collect(roots).has_value() && context.heap().verify().has_value(), "collection failed");
    const auto old_value = rooted(context, roots[0]).map_find(rooted(context, key.word())).value();
    require(old_value && old_value->tuple_element(0)->integer_value() == 7, "source map changed or lost a value");
    const auto new_value = rooted(context, roots[1]).map_find(rooted(context, key.word())).value();
    require(new_value && new_value->atom_spelling() == "one", "updated map lost its value");
    require(rooted(context, roots[1]).map_entries()->size() == 100, "tree walk lost entries");

    roots.pop_back();
    require(context.heap().collect(roots).has_value() && context.heap().off_heap_words() == built,
            "dead version kept its nodes");
    roots.clear();
    require(context.heap().collect(roots).has_value() && context.heap().off_heap_words() == 0,
            "dead map kept its tree");
}

// An owned update (plan 20 step OPT0.5B) changes the tree of a map nothing else reads in place: no node is copied, its
// source stub is consumed, a failed owned update leaves the stub holding the tree, and collections stay consistent.
void owned_updates() {
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context().value();
    TermFactory factory(context);
    std::vector<std::pair<Term, Term>> entries;
    for (std::int64_t key = 0; key < 100; ++key) {
        entries.emplace_back(factory.integer(key).value(), factory.integer(key).value());
    }
    const auto map = factory.map(entries).value();
    const auto built = context.heap().off_heap_words();
    const auto one = factory.integer(1).value();
    const auto key = factory.integer(7).value();
    const auto updated = invoke(context, Op::update_owned, std::array{map, key, factory.atom("seven").value(), one});
    require(updated.first == Outcome::success && updated.second.map_find(key)->value().atom_spelling() == "seven",
            "owned update failed");
    require(context.heap().off_heap_words() == built && map.map_size() == 0, "owned update copied its tree");

    const auto missing = factory.atom("missing").value();
    const auto failed = invoke(context, Op::update_owned, std::array{updated.second, missing, one, one});
    require(failed.first == Outcome::bad_key && updated.second.map_size() == 100, "failed owned update lost the tree");
    std::vector<Word> roots{updated.second.word()};
    require(context.heap().collect(roots).has_value() && context.heap().verify().has_value() &&
                context.heap().off_heap_words() == built,
            "collection after owned updates failed");
    roots.clear();
    require(context.heap().collect(roots).has_value() && context.heap().off_heap_words() == 0, "tree not freed");
}
} // namespace

// Source differential tests own semantic coverage; this native consumer owns inaccessible host invariants.
int main() {
    try {
        ownership();
        malformed();
        large_maps();
        tree_maps();
        owned_updates();
        std::cout << "runtime_maps: ok\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
