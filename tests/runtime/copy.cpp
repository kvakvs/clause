#include "terms.hpp"
#include "terms/term_layout.hpp"
#include <array>
#include <clause/abi/bits.hpp>
#include <clause/runtime/output.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Copying term graphs between process heaps of one runtime (docs/runtime-heap.md#copying-between-heaps): copies
// compare equal, copy shared subterms once per reach (flat, as OTP), share off-heap buffers, outlive the source
// process, and a failed copy leaves both heaps and every charge unchanged.
namespace {
using namespace clause::runtime;
using detail::layout::BinaryBuffer;
using detail::layout::RefcBinaryCell;
using Status = clause::abi::v1::Status;

// Keep every check active in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Render a value; text compares values across processes and collections.
std::string text(const Term &value) { return format_term(value, TermStyle::write).value(); }

// Observe the buffer of a large binary's cell without holding a reference to it.
std::weak_ptr<const BinaryBuffer> buffer(const Term &value) {
    return reinterpret_cast<const RefcBinaryCell *>(value.word() & ~Word{3})->buffer_;
}

// Words charged for an off-heap buffer of bytes.
std::size_t charge(std::size_t bytes) { return (bytes + sizeof(Word) - 1) / sizeof(Word); }

// Extract a sub-binary through the generated bit service, as source matching does.
Term slice(ProcessContext &context, const Term &value, std::size_t offset, std::size_t count) {
    TermFactory factory(context);
    const std::array input{value.word(), factory.integer(static_cast<std::int64_t>(offset))->word(),
                           factory.integer(256 + 2)->word(), factory.integer(static_cast<std::int64_t>(count))->word(),
                           factory.integer(0)->word()};
    std::array<Word, 2> output{};
    GeneratedInvocation call(context.generated_calls());
    require(CLAUSE_bits_v1(&context, static_cast<std::uint8_t>(clause::abi::v1::BitOperation::extract), input.data(),
                           input.size(), output.data()) == 0,
            "slice failed");
    return Term::from_word(output[0], context).value();
}

// A proper list of count small integers, 2 words per element.
Term numbers(ProcessContext &context, std::size_t count) {
    TermFactory factory(context);
    std::vector<Term> elements;
    for (std::size_t index = 0; index < count; ++index) {
        elements.push_back(factory.integer(static_cast<std::int64_t>(index)).value());
    }
    return factory.list(elements).value();
}

// A tuple holding every admitted heap layout: bignum, float, cons, improper list, map with compound keys, inline
// and off-heap binaries, partial-byte bitstrings of both kinds and an off-heap slice.
Term layouts(ProcessContext &context) {
    TermFactory factory(context);
    const auto large = factory.binary(std::vector(100, std::byte{0x5a})).value();
    const std::array key_fields{factory.atom("key").value(), factory.integer(1).value()};
    const std::array key_list{factory.integer(1).value(), factory.integer(2).value()};
    const std::array entries{
        std::pair{factory.tuple(key_fields).value(), factory.list(key_list).value()},
        std::pair{factory.binary(std::vector(3, std::byte{'k'})).value(), factory.floating(1.5).value()}};
    const std::array fields{factory.integer(7).value(),
                            factory.atom("copied").value(),
                            factory.integer_decimal("-123456789012345678901234567890").value(),
                            factory.floating(-2.25).value(),
                            factory.tuple({}).value(),
                            factory.nil().value(),
                            factory.cons(factory.integer(1).value(), factory.integer(2).value()).value(),
                            factory.map(entries).value(),
                            factory.binary(std::vector(10, std::byte{0x11})).value(),
                            factory.bitstring(std::vector(2, std::byte{0xff}), 13).value(),
                            large,
                            slice(context, large, 3, 790),
                            factory.bitstring(std::vector(101, std::byte{0xf0}), 801).value()};
    return factory.tuple(fields).value();
}

// Every layout copies equal; the copy survives collection and destruction of the source and its own collection.
void layouts_survive(Runtime &runtime) {
    auto &source = *runtime.create_context().value();
    auto &destination = *runtime.create_context().value();
    const auto original = layouts(source);
    const auto expected = text(original);
    const auto copy = original.copy_to(destination.heap()).value();
    require(copy.word() != original.word() && copy.exactly_equal(original).value() && text(copy) == expected,
            "copy differs from its source");
    require(Term::from_word(copy.word(), destination).has_value() && destination.heap().verify().has_value(),
            "copy is not a well-formed destination term");
    require(source.heap().collect().has_value() && runtime.destroy_context(&source) == Status::ok,
            "source collection or teardown failed");
    require(text(copy) == expected, "copy changed with its source");
    std::array roots{copy.word()};
    require(destination.heap().collect(roots).has_value(), "destination collection failed");
    require(text(Term::from_word(roots[0], destination).value()) == expected, "copy lost by its collection");
    require(runtime.destroy_context(&destination) == Status::ok && runtime.memory_bytes() == 0,
            "teardown kept charges");
}

// Copies are flat, as OTP copies messages: a 12-level tuple tower over one shared child copies every reach of it, 3
// words per tuple, and a deeply nested list needs no recursion.
void sharing(Runtime &runtime) {
    constexpr std::size_t levels = 12;
    constexpr std::size_t depth = 100'000;
    auto &source = *runtime.create_context().value();
    auto &destination = *runtime.create_context().value();
    TermFactory factory(source);
    auto tower = factory.nil().value();
    for (std::size_t level = 0; level < levels; ++level) {
        tower = factory.tuple(std::array{tower, tower}).value();
    }
    auto used = destination.heap().used_words();
    const auto tower_copy = tower.copy_to(destination.heap()).value();
    require(destination.heap().used_words() - used == 3 * ((std::size_t{1} << levels) - 1),
            "a shared subterm was not copied once per reach");
    require(tower_copy.exactly_equal(tower).value(), "tower copy differs");
    require(tower_copy.tuple_element(0)->word() != tower_copy.tuple_element(1)->word(),
            "copied children still share one copy");
    auto nested = factory.nil().value();
    for (std::size_t level = 0; level < depth; ++level) {
        nested = factory.cons(nested, factory.nil().value()).value();
    }
    used = destination.heap().used_words();
    const auto nested_copy = nested.copy_to(destination.heap()).value();
    require(destination.heap().used_words() - used == 2 * depth, "nested list size wrong");
    require(nested_copy.exactly_equal(nested).value(), "nested copy differs");
    require(destination.heap().verify().has_value(), "copies broke the destination heap");
    require(runtime.destroy_context(&source) == Status::ok && runtime.destroy_context(&destination) == Status::ok,
            "teardown failed");
}

// A copy shares the off-heap buffer, charged once per process and once runtime-wide, until the last process
// referencing it drops it.
void shared_buffers(Runtime &runtime) {
    auto &source = *runtime.create_context().value();
    auto &destination = *runtime.create_context().value();
    TermFactory factory(source);
    const auto large = factory.binary(std::vector(1000, std::byte{7})).value();
    const auto tail = slice(source, large, 8, 7000);
    const auto graph = factory.tuple(std::array{large, tail, large}).value();
    const std::weak_ptr observed = buffer(large);
    const auto before = runtime.memory_bytes();
    const auto capacity = destination.heap().capacity_words();
    const auto first = graph.copy_to(destination.heap()).value();
    const auto second = graph.copy_to(destination.heap()).value();
    // A flat copy makes one off-heap cell per reach of the large binary: two elements and the original of its tail, a
    // sub-binary (OPT1.4B); all of them share its buffer.
    require(observed.use_count() == 7 && destination.heap().verify()->off_heap_cells == 6,
            "copies did not share the buffer cell by cell");
    require(destination.heap().off_heap_words() == charge(1000), "destination charged the buffer more than once");
    require(runtime.memory_bytes() - before == (destination.heap().capacity_words() - capacity) * sizeof(Word),
            "runtime account charged the shared buffer again");
    require(runtime.destroy_context(&source) == Status::ok && !observed.expired(), "source took the shared buffer");
    require(text(first) == text(second) && first.tuple_element(1)->bit_size() == 7000, "copy lost its view");
    require(runtime.memory_bytes() == (destination.heap().capacity_words() + charge(1000)) * sizeof(Word),
            "surviving buffer lost its runtime charge");
    require(destination.heap().collect().has_value() && observed.expired() && destination.heap().off_heap_words() == 0,
            "dead copies kept the buffer");
    require(runtime.destroy_context(&destination) == Status::ok && runtime.memory_bytes() == 0,
            "teardown kept charges");
}

// Same-heap values keep their identity; immediates and atoms need no storage.
void retained(Runtime &runtime) {
    auto &context = *runtime.create_context().value();
    auto &other = *runtime.create_context().value();
    TermFactory factory(context);
    const auto value = numbers(context, 3);
    const auto used = context.heap().used_words();
    require(value.copy_to(context.heap())->word() == value.word() && context.heap().used_words() == used,
            "same-heap value copied");
    const auto atom = factory.atom("shared").value();
    require(atom.copy_to(other.heap())->word() == atom.word() && other.heap().used_words() == 0, "atom used storage");
    require(factory.integer(5)->copy_to(other.heap())->integer_value() == 5, "immediate copy failed");
    // Factories still refuse foreign inputs; only add/copy_to copies.
    require(TermFactory(other).tuple(std::array{value}) == std::unexpected(TermError::wrong_owner),
            "factory accepted a foreign graph");
    require(runtime.destroy_context(&context) == Status::ok && runtime.destroy_context(&other) == Status::ok,
            "teardown failed");
}

// What a failed copy must leave unchanged in one heap.
struct Snapshot {
    std::size_t used;
    std::size_t capacity;
    std::size_t off_heap;

    explicit Snapshot(ProcessContext &context)
        : used(context.heap().used_words()), capacity(context.heap().capacity_words()),
          off_heap(context.heap().off_heap_words()) {}

    bool operator==(const Snapshot &) const = default;
};

// Copy graph into a destination of limit words and require resource_limit with nothing changed anywhere.
void refused(Runtime &runtime, ProcessContext &source, const Term &graph, std::size_t limit, const char *message) {
    auto &destination = *runtime.create_context({8, limit * sizeof(Word)}).value();
    const Snapshot before(destination);
    const Snapshot source_before(source);
    const auto memory = runtime.memory_bytes();
    const auto expected = text(graph);
    require(graph.copy_to(destination.heap()) == std::unexpected(TermError::resource_limit), message);
    require(Snapshot(destination) == before && Snapshot(source) == source_before && runtime.memory_bytes() == memory,
            "failed copy changed a heap or a charge");
    require(destination.heap().verify()->off_heap_cells == 0 && text(graph) == expected,
            "failed copy left cells or changed its source");
    require(runtime.destroy_context(&destination) == Status::ok, "teardown failed");
}

// Destination exhaustion before the reservation, while holding buffers, and after them; other owners refuse.
void failures(Runtime &runtime) {
    auto &source = *runtime.create_context().value();
    TermFactory factory(source);
    const auto list = numbers(source, 100);
    const auto small = factory.binary(std::vector(100, std::byte{1})).value();
    const auto large = factory.binary(std::vector(1000, std::byte{2})).value();
    const std::weak_ptr observed = buffer(small);
    // 200 list words, a 2-word tuple header plus 6-word cells, and 13 + 125 buffer words.
    refused(runtime, source, list, 150, "list larger than the destination accepted");
    refused(runtime, source, factory.tuple(std::array{small, large}).value(), 100, "second buffer accepted");
    refused(runtime, source, factory.tuple(std::array{small, list}).value(), 100, "words after a buffer accepted");
    require(observed.use_count() == 1, "failed copies kept buffer references");
    auto &destination = *runtime.create_context().value();
    require(list.copy_to(destination.heap())->exactly_equal(list) == true, "copy after refusals failed");
    require(runtime.destroy_context(&source) == Status::ok, "source teardown failed");
    require(runtime.destroy_context(&destination) == Status::ok && runtime.memory_bytes() == 0,
            "teardown kept charges");
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        layouts_survive(*runtime);
        sharing(*runtime);
        shared_buffers(*runtime);
        retained(*runtime);
        failures(*runtime);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
