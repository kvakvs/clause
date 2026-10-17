#include "memory/persistent_terms.hpp"
#include "memory/heap_object.hpp"
#include "terms.hpp"
#include <array>
#include <atomic>
#include <clause/runtime/output.hpp>
#include <clause/runtime/runtime.hpp>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// The runtime-wide persistent term table (plan step 41M1, docs/runtime.md#persistent-terms): values outlive the
// process that stored them, readers hold them in place across their collections and pass them on without a copy
// (persistent terms in place), a value replaced or erased while a reader holds it stays readable, and concurrent
// readers and writers agree.
namespace {
using clause::abi::v1::Status;
using clause::runtime::ProcessContext;
using clause::runtime::Runtime;
using clause::runtime::Term;
using clause::runtime::TermFactory;
using clause::runtime::detail::PersistentTerms;
using clause::runtime::detail::TermAccess;

// Keep every check active in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// A term's ~w text.
std::string text(const Term &term) {
    return clause::runtime::format_term(term, clause::runtime::TermStyle::write).value();
}

// The table's hash of a key: the hash of its text (the builtins use phash2's term hash).
std::uint64_t hash_of(const Term &key) { return std::hash<std::string>{}(text(key)); }

// A matcher of stored keys exactly equal to `key`.
PersistentTerms::SameKey same_as(const Term &key) {
    return [key](clause::runtime::Word stored) { return TermAccess::make(stored).exactly_equal(key).value(); };
}

// The value stored under `key`, read in place by `reader`, which must admit it.
std::optional<Term> stored_value(PersistentTerms &table, ProcessContext &reader, const Term &key) {
    const auto found = table.find(hash_of(key), same_as(key));
    return found ? std::optional{Term::from_word(*found, reader).value()} : std::nullopt;
}

// A value with a large binary, a long list and a nested tuple.
Term sample(ProcessContext &context, std::int64_t tag) {
    TermFactory factory(context);
    std::vector<Term> items;
    for (std::int64_t index = 0; index < 1000; ++index) {
        items.push_back(factory.integer(index * tag).value());
    }
    const std::array parts{factory.integer(tag).value(), factory.list(items).value(),
                           factory.binary(std::vector(300, std::byte{7})).value()};
    return factory.tuple(parts).value();
}

// Store `value` under `key`, both terms of `writer`.
void put(PersistentTerms &table, ProcessContext &writer, const Term &key, const Term &value) {
    require(table.store(writer, hash_of(key), key, value, same_as(key)).has_value(), "store failed");
}

// A value outlives its writer; readers in other processes hold it in place across their collections and hand it on
// without copying it.
void outlives_writer(Runtime &runtime, PersistentTerms &table) {
    auto &writer = *runtime.create_context().value();
    const auto key = TermFactory(writer).atom("config").value();
    const auto expected = text(sample(writer, 3));
    put(table, writer, key, sample(writer, 3));
    require(runtime.destroy_context(&writer) == Status::ok, "writer teardown failed");
    auto &reader = *runtime.create_context().value();
    const auto lookup = TermFactory(reader).atom("config").value();
    const auto found = stored_value(table, reader, lookup);
    require(found.has_value(), "stored value not found");
    std::array roots{found->word()};
    require(reader.heap().collect(roots).has_value() && reader.heap().verify().has_value(), "reader collection failed");
    require(roots[0] == found->word() && text(Term::from_word(roots[0], reader).value()) == expected,
            "value moved or changed");
    auto &other = *runtime.create_context().value();
    require(found->copy_to(other.heap())->word() == found->word(), "a stored value was copied between processes");
    require(runtime.destroy_context(&other) == Status::ok, "second reader teardown failed");
    require(table.count() == 1 && table.words() > 0, "wrong table size");
    require(runtime.destroy_context(&reader) == Status::ok, "reader teardown failed");
}

// A replaced value held by a reader stays readable; erase removes the entry once.
void replace_and_erase(Runtime &runtime, PersistentTerms &table) {
    auto &context = *runtime.create_context().value();
    const auto key = TermFactory(context).atom("config").value();
    const auto held = stored_value(table, context, key).value();
    put(table, context, key, sample(context, 5));
    require(text(held) == text(sample(context, 3)), "held value changed by replacement");
    require(text(stored_value(table, context, key).value()) == text(sample(context, 5)), "replacement not found");
    require(table.erase(hash_of(key), same_as(key)), "erase found nothing");
    require(!table.erase(hash_of(key), same_as(key)), "erase removed twice");
    require(!stored_value(table, context, key).has_value() && table.count() == 0, "erased key found");
    require(text(held) == text(sample(context, 3)), "held value changed by erasure");
    require(runtime.destroy_context(&context) == Status::ok, "teardown failed");
}

// Readers on several threads read values in place while a writer keeps replacing them; each read is one whole value.
void concurrent(Runtime &runtime, PersistentTerms &table) {
    constexpr int READERS = 4;
    constexpr int ROUNDS = 200;
    auto &writer = *runtime.create_context().value();
    const auto key = TermFactory(writer).atom("shared").value();
    put(table, writer, key, sample(writer, 0));
    std::vector<ProcessContext *> readers;
    for (int index = 0; index < READERS; ++index) {
        readers.push_back(runtime.create_context().value());
    }
    std::atomic<bool> failed = false;
    std::vector<std::thread> threads;
    for (auto *reader : readers) {
        threads.emplace_back([&table, &failed, reader] {
            const auto lookup = TermFactory(*reader).atom("shared").value();
            for (int round = 0; round < ROUNDS; ++round) {
                const auto found = table.find(hash_of(lookup), same_as(lookup));
                const auto value =
                    found ? Term::from_word(*found, *reader) : std::unexpected(clause::runtime::TermError{});
                if (!value || !value->is_tuple() || value->tuple_size().value() != 3) {
                    failed = true;
                }
            }
        });
    }
    for (int round = 1; round <= ROUNDS; ++round) {
        put(table, writer, key, sample(writer, round));
    }
    for (auto &thread : threads) {
        thread.join();
    }
    require(!failed, "a concurrent read saw no whole value");
    require(table.erase(hash_of(key), same_as(key)), "shared key missing");
    for (auto *reader : readers) {
        require(runtime.destroy_context(reader) == Status::ok, "reader teardown failed");
    }
    require(runtime.destroy_context(&writer) == Status::ok, "writer teardown failed");
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        auto &table = runtime->persistent_terms();
        outlives_writer(*runtime, table);
        replace_and_erase(*runtime, table);
        concurrent(*runtime, table);
        // Erased values stay in the shared area until the runtime is torn down.
        require(table.count() == 0 && table.words() == 0 && runtime->memory_bytes() > 0, "wrong area accounting");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "runtime_persistent_terms: ok\n";
    return 0;
}
