#include "memory/heap_collect.hpp"
#include "terms.hpp"
#include "terms/term_layout.hpp"
#include <array>
#include <clause/abi/bits.hpp>
#include <clause/runtime/output.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Copying collection at an explicit host safe point: explicit roots are rewritten, every layout and
// internal sharing survives, garbage and dead off-heap binaries are reclaimed, the heap follows the
// ERTS size policy, and host Terms taken before a collection become stale. At a declared safe point
// inside generated frames, every root owner of the execution model is rewritten too.
namespace {
using namespace clause::runtime;
using clause::abi::v1::frame_header_words;
using clause::abi::v1::frame_resume_word;
using clause::abi::v1::FrameDescriptor;
using detail::layout::BinaryBuffer;
using detail::layout::RefcBinaryCell;
using Status = clause::abi::v1::Status;

// Keep every check active in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Admit a root word again after a collection rewrote it.
Term current(ProcessContext &context, Word word) { return Term::from_word(word, context).value(); }

// Render a value; text compares values across collections, since older Terms are stale.
std::string text(const Term &value) { return format_term(value, TermStyle::write).value(); }

// Observe the buffer of a large binary's cell without holding a reference to it.
std::weak_ptr<const BinaryBuffer> buffer(const Term &value) {
    return reinterpret_cast<const RefcBinaryCell *>(value.word() & ~Word{3})->buffer_;
}

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

// Words charged for an off-heap buffer of bytes.
std::size_t charge(std::size_t bytes) { return (bytes + sizeof(Word) - 1) / sizeof(Word); }

// Allocate unrooted tuples so the live graph is interleaved with garbage across many fragments.
void garbage(ProcessContext &context, std::size_t count) {
    TermFactory factory(context);
    for (std::size_t i = 0; i < count; ++i) {
        require(factory.tuple(std::array{factory.integer(static_cast<std::int64_t>(i)).value()}).has_value(),
                "garbage allocation failed");
    }
}

// Build one root per heap layout plus nested values that share a tuple and a list tail.
std::vector<Word> layouts(ProcessContext &context) {
    TermFactory f(context);
    garbage(context, 20);
    const auto shared = f.tuple(std::array{f.integer(7).value(), f.atom("shared").value()}).value();
    const auto big = f.integer_decimal("-123456789012345678901234567890").value();
    const auto real = f.floating(2.5).value();
    garbage(context, 20);
    const auto small = f.binary(std::vector(10, std::byte{1})).value();
    const auto large = f.binary(std::vector(100, std::byte{2})).value();
    const auto bits = f.bitstring(std::array{std::byte{0xff}, std::byte{0x80}}, 9).value();
    const auto tail = f.list(std::array{shared, big}).value();
    const auto first = f.cons(f.integer(1).value(), tail).value();
    const auto second = f.cons(real, tail).value();
    garbage(context, 20);
    const auto improper = f.list(std::array{small, bits}, large).value();
    const auto map = f.map(std::array{std::pair{big, first}, std::pair{real, improper}}).value();
    const auto pair = f.tuple(std::array{shared, shared, map, second}).value();
    garbage(context, 20);
    return {pair.word(), first.word(), second.word(), map.word(), big.word(), real.word(), large.word()};
}

// Every rewritten root still prints as before, and the nested aliases name one copy each.
void require_graph(ProcessContext &context, const std::vector<Word> &roots, const std::vector<std::string> &texts) {
    for (std::size_t i = 0; i < roots.size(); ++i) {
        require(text(current(context, roots[i])) == texts[i], "rooted value changed");
    }
    const auto pair = current(context, roots[0]);
    const auto first = current(context, roots[1]);
    const auto second = current(context, roots[2]);
    require(pair.tuple_element(0)->word() == pair.tuple_element(1)->word(), "shared tuple was duplicated");
    require(first.tail()->word() == second.tail()->word(), "shared list tail was duplicated");
    require(pair.tuple_element(3)->word() == roots[2], "root and nested alias diverged");
    require(first.tail()->head()->word() == pair.tuple_element(0)->word(), "shared tuple alias diverged");
    const auto found = current(context, roots[3]).map_find(current(context, roots[4])).value();
    require(found && found->word() == roots[1], "map value lost its identity");
    const auto census = context.heap().verify().value();
    require(census.filler_words == 0 && census.words == context.heap().used_words(), "new heap holds garbage");
}

// Repeated collections keep the rooted graph and drop garbage; fragments merge into the heap block.
void repeated(Runtime &runtime) {
    auto &context = *runtime.create_context({16, std::size_t{1} << 20}).value();
    auto roots = layouts(context);
    std::vector<std::string> texts;
    for (const auto word : roots) {
        texts.push_back(text(current(context, word)));
    }
    std::size_t live = 0;
    for (int round = 0; round < 3; ++round) {
        const auto before = context.heap().used_words();
        const auto host = current(context, roots[0]);
        const auto stats = context.heap().collect(roots).value();
        require(stats.words_before == before && stats.live_words == context.heap().used_words(), "wrong stats");
        require(stats.heap_words == context.heap().capacity_words(), "fragments survived the collection");
        require(round != 0 || (stats.fragment_words > 0 && stats.live_words < before), "garbage survived");
        require(round == 0 || stats.live_words == live, "live size changed without allocation");
        require(host.tuple_size() == std::unexpected(TermError::stale_term), "pre-collection term stayed valid");
        live = stats.live_words;
        require_graph(context, roots, texts);
        garbage(context, 50);
    }
    require(runtime.destroy_context(&context) == Status::ok, "teardown failed");
}

// Dead binary cells release their buffer reference once; a buffer shared by a live tail stays.
void binaries(Runtime &runtime) {
    auto &context = *runtime.create_context().value();
    TermFactory factory(context);
    const auto kept = factory.binary(std::vector(100, std::byte{3})).value();
    const auto dropped = factory.binary(std::vector(200, std::byte{4})).value();
    const auto tail = slice(context, kept, 8, 700);
    const std::weak_ptr<const BinaryBuffer> kept_buffer = buffer(kept);
    const std::weak_ptr<const BinaryBuffer> dropped_buffer = buffer(dropped);
    require(kept_buffer.use_count() == 2 && context.heap().off_heap_words() == charge(100) + charge(200),
            "binary fixture wrong");
    std::array roots{tail.word()};
    const auto stats = context.heap().collect(roots).value();
    require(dropped_buffer.expired() && kept_buffer.use_count() == 1, "dead binary cells kept references");
    require(stats.off_heap_words == charge(100) && context.heap().off_heap_words() == charge(100),
            "released buffer kept its charge");
    require(context.heap().verify()->off_heap_cells == 1, "off-heap list not rebuilt");
    require(current(context, roots[0]).bit_size() == 700, "rooted tail lost its view");
    require(context.heap().collect().has_value() && kept_buffer.expired(), "last reference not released");
    require(context.heap().off_heap_words() == 0 && context.heap().verify()->off_heap_cells == 0, "list kept cells");
    require(runtime.destroy_context(&context) == Status::ok, "teardown failed");
}

// Build a proper list of count small integers, 2 words per element.
Term numbers(ProcessContext &context, std::size_t count) {
    TermFactory factory(context);
    std::vector<Term> elements;
    for (std::size_t i = 0; i < count; ++i) {
        elements.push_back(factory.integer(static_cast<std::int64_t>(i)).value());
    }
    return factory.list(elements).value();
}

// The new block keeps live data below 75% along the ERTS sizes; a block under 25% live shrinks.
void policy(Runtime &runtime) {
    require(detail::heap_size_at_least(1) == 12 && detail::heap_size_at_least(233) == 233 &&
                detail::heap_size_at_least(234) == 376 && detail::heap_size_at_least(833'027) == 999'631,
            "heap size sequence differs from ERTS");
    auto &context = *runtime.create_context().value();
    std::array roots{numbers(context, 2000).word()};
    const auto grown = context.heap().collect(roots).value();
    require(grown.live_words == 4000 && grown.heap_words == 6772, "heap did not grow per policy");
    roots[0] = current(context, roots[0]).tail().value().word();
    for (int i = 0; i < 1990; ++i) {
        roots[0] = current(context, roots[0]).tail().value().word();
    }
    const auto shrunk = context.heap().collect(roots).value();
    require(shrunk.live_words == 18 && shrunk.heap_words == HeapOptions{}.min_heap_words, "heap did not shrink");
    require(current(context, roots[0]).list_length() == 9, "shrinking lost the live tail");
    require(context.heap().verify().has_value(), "shrunk heap does not verify");
    require(runtime.destroy_context(&context) == Status::ok, "teardown failed");
}

// Allocate unrooted off-heap binaries of `words` words each until the heap asks for a collection; each must fit.
void binary_garbage_until_due(ProcessContext &context, std::size_t words) {
    TermFactory factory(context);
    while (!context.heap().wants_collection()) {
        require(factory.binary(std::vector(words * sizeof(Word), std::byte{3})).has_value(),
                "off-heap budget ran out before a collection was due");
    }
}

// Near the budget a collection leaves half of the free budget for fragments and off-heap buffers, so garbage
// allocated after it asks for the next collection before the budget runs out (docs/runtime-heap.md#sizing-and-budget).
void near_budget(Runtime &runtime) {
    constexpr std::size_t budget = 10'000;
    auto &heap_context = *runtime.create_context({16, budget * sizeof(Word)}).value();
    std::array roots{numbers(heap_context, 3000).word()};
    const auto first = heap_context.heap().collect(roots).value();
    require(first.live_words == 6000 && first.heap_words <= 8000, "heap block left no room for fragments");
    while (!heap_context.heap().wants_collection()) {
        garbage(heap_context, 1);
    }
    require(heap_context.heap().collect(roots).value().live_words == 6000, "garbage survived near the budget");
    require(runtime.destroy_context(&heap_context) == Status::ok, "teardown failed");

    // Six live 1,000-word buffers: a virtual binary heap of twice the survivors would exceed the budget.
    auto &binary_context = *runtime.create_context({2000, budget * sizeof(Word)}).value();
    TermFactory factory(binary_context);
    std::vector<Word> kept;
    for (int i = 0; i < 6; ++i) {
        kept.push_back(factory.binary(std::vector(1000 * sizeof(Word), std::byte{2})).value().word());
    }
    require(binary_context.heap().collect(kept).has_value(), "binary collection failed");
    binary_garbage_until_due(binary_context, 100);
    require(binary_context.heap().collect(kept).value().off_heap_words == 6000, "dead buffers survived");
    require(binary_context.heap().verify().has_value(), "heap does not verify near the budget");
    require(runtime.destroy_context(&binary_context) == Status::ok, "teardown failed");
}

// Two processes share a runtime-wide limit (docs/runtime-heap.md#runtime-memory-limit): garbage near the limit is
// collected, the process asking past it fails with resource_limit while the other keeps running, and teardown
// returns every charge.
void shared_limit() {
    constexpr std::size_t limit = 40'000;
    auto runtime = Runtime::start({.memory_limit_bytes = limit * sizeof(Word)}).value();
    auto &holder = *runtime->create_context().value();
    std::array kept{numbers(holder, 8000).word()};
    const auto text_kept = text(current(holder, kept[0]));
    // Half of the 24,000 words the limit leaves after 16,000 live ones stays free for other storage.
    require(holder.heap().collect(kept).value().heap_words == 28'000, "holder ignored the runtime-wide limit");
    auto &churner = *runtime->create_context().value();
    std::array mine{numbers(churner, 1000).word()};
    for (int round = 0; round < 20; ++round) {
        while (!churner.heap().wants_collection()) {
            garbage(churner, 1);
        }
        require(churner.heap().collect(mine).value().live_words == 2000, "garbage survived near the limit");
    }
    require(runtime->memory_bytes() <= limit * sizeof(Word), "storage passed the runtime-wide limit");
    TermFactory factory(churner);
    const std::vector too_many(8000, factory.integer(1).value());
    require(factory.list(too_many) == std::unexpected(TermError::resource_limit), "allocation passed the limit");
    garbage(holder, 100);
    require(holder.heap().collect(kept).has_value() && text(current(holder, kept[0])) == text_kept,
            "the other process stopped working");
    require(runtime->destroy_context(&churner) == Status::ok &&
                runtime->memory_bytes() == holder.heap().capacity_words() * sizeof(Word),
            "churner teardown kept charges");
    require(runtime->destroy_context(&holder) == Status::ok && runtime->memory_bytes() == 0,
            "holder teardown kept charges");
}

// Generated code running or an open reservation is not a safe point; nothing changes.
void unsafe(Runtime &runtime) {
    auto &context = *runtime.create_context().value();
    const auto value = numbers(context, 3);
    std::array roots{value.word()};
    const auto used = context.heap().used_words();
    {
        GeneratedInvocation invocation(context.generated_calls());
        require(context.heap().collect(roots) == std::unexpected(HeapError::unsafe_point), "collected in a call");
        require(!context.generated_calls().failure(), "refused collection failed the generated call");
    }
    {
        auto open = context.heap().reserve(1).value();
        require(context.heap().collect(roots) == std::unexpected(HeapError::unsafe_point), "collected a reservation");
    }
    require(roots[0] == value.word() && value.list_length() == 3 && context.heap().used_words() == used,
            "refused collection changed the heap");
    require(runtime.destroy_context(&context) == Status::ok, "teardown failed");
}

// The layout roots spread over every root owner by the hand-written frame bodies below, and what they observed.
struct Owners {
    ProcessContext *context = nullptr;
    // layouts() words and their texts before any collection.
    std::vector<Word> roots;
    std::vector<std::string> texts;
    // Each layout root read back from its owner after the collections, in layouts() order.
    std::vector<Word> current;
    // The outer frame's two term slots and its raw slot once its callee returned.
    std::vector<Word> outer;
    // Root words visited before collecting inside the callee and after returning to the outer frame.
    std::size_t visited_inside = 0;
    std::size_t visited_outside = 0;
};

Owners owners;

// Run continuation code; hand-written bodies use ordinary calls where generated code uses tail calls.
void run(clause::abi::v1::Code *code, void *context) {
    const auto *x = static_cast<ProcessContext *>(context)->stack().registers();
    code(context, x[0], x[1], x[2], x[3], x[4], x[5]);
}

// Count the root words the context enumerates beyond the given explicit ones.
std::size_t root_count(std::span<Word> explicit_roots) {
    std::size_t count = 0;
    owners.context->visit_roots(explicit_roots, [&](Word &) { ++count; });
    return count;
}

// Collect repeatedly at a declared safe point while frames, registers, the failure channel and explicit
// roots share one graph: pair and first in the caller's slots, second and map in this frame's slots, big
// and pair in live registers, first, real and map in the failure channel, large and second as explicit roots.
CLAUSE_CODE_CONVENTION void collecting_body(void *, clause::abi::v1::TermWord, clause::abi::v1::TermWord,
                                            clause::abi::v1::TermWord, clause::abi::v1::TermWord,
                                            clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    auto &context = *owners.context;
    auto &stack = context.stack();
    auto &calls = context.generated_calls();
    const auto &roots = owners.roots;
    auto *slots = stack.frame() + frame_header_words;
    slots[1] = roots[3];
    stack.registers()[0] = roots[4];
    stack.registers()[1] = roots[0];
    stack.keep_registers(2);
    calls.fail({.code = CallError::erlang_exception,
                .reason = clause::abi::v1::ErrorReason::badmatch,
                .value = current(context, roots[1]),
                .arguments = current(context, roots[5]),
                .stack = current(context, roots[3])});
    std::array explicit_roots{roots[6], roots[2]};
    owners.visited_inside = root_count(explicit_roots);
    const auto &failure = *calls.failure();
    for (int round = 0; round < 2; ++round) {
        {
            SafePoint safe(calls);
            require(context.heap().collect(explicit_roots).has_value(), "safe point refused the collection");
        }
        slots = stack.frame() + frame_header_words;
        require(explicit_roots[1] == slots[0] && failure.stack->word() == slots[1], "shared root diverged");
        owners.current = {stack.registers()[1], failure.value->word(),     slots[0],         slots[1],
                          stack.registers()[0], failure.arguments->word(), explicit_roots[0]};
        require_graph(context, owners.current, owners.texts);
        garbage(context, 50);
    }
    require(context.heap().collect(explicit_roots) == std::unexpected(HeapError::unsafe_point),
            "collected after the safe point ended");
    calls.clear();
    run(stack.leave(slots[0]), &context);
}

// The callee taking second as its argument, with one more term slot.
const FrameDescriptor collecting{nullptr, 0, 0, 1, &collecting_body, 2, 2};

// Hold pair and first in term slots and a stale copy of pair in a raw slot across the collecting call.
CLAUSE_CODE_CONVENTION void holding_body(void *, clause::abi::v1::TermWord, clause::abi::v1::TermWord,
                                         clause::abi::v1::TermWord, clause::abi::v1::TermWord,
                                         clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    auto &stack = owners.context->stack();
    auto *header = stack.frame();
    auto *slots = header + frame_header_words;
    if (header[frame_resume_word] == 0) {
        slots[0] = owners.roots[0];
        slots[1] = owners.roots[1];
        slots[2] = owners.roots[0];
        header[frame_resume_word] = 1;
        stack.registers()[0] = owners.roots[2];
        run(stack.enter(collecting), owners.context);
        return;
    }
    owners.outer = {slots[0], slots[1], slots[2]};
    owners.visited_outside = root_count({});
    run(stack.leave(stack.registers()[0]), owners.context);
}

// The caller: no arguments, two term slots and one raw slot.
const FrameDescriptor holding{nullptr, 0, 0, 0, &holding_body, 3, 2};

// Every root owner of the execution model is enumerated and rewritten; raw slots and dead registers are not.
void root_owners(Runtime &runtime) {
    auto &context = *runtime.create_context({16, std::size_t{1} << 20}).value();
    owners = Owners{};
    owners.context = &context;
    owners.roots = layouts(context);
    // Merge the fragments first, so the function entries below (safepoints) have nothing to collect.
    require(context.heap().collect(owners.roots).has_value() && !context.heap().wants_collection(),
            "host collection left fragments");
    for (const auto word : owners.roots) {
        owners.texts.push_back(text(current(context, word)));
    }
    {
        GeneratedInvocation invocation(context.generated_calls());
        const auto result = context.stack().invoke(holding, nullptr);
        require(!context.generated_calls().failure() && result == owners.current[2], "frames returned a stale word");
    }
    require(owners.visited_inside == 11 && owners.visited_outside == 2, "root owners enumerated wrongly");
    require(owners.outer[0] == owners.current[0] && owners.outer[1] == owners.current[1], "caller slots not rewritten");
    require(owners.outer[2] == owners.roots[0], "raw slot was treated as a root");
    require(runtime.destroy_context(&context) == Status::ok, "teardown failed");
}

// Record the argument slot after the entry safepoint, then after a loop-head safepoint that follows garbage.
CLAUSE_CODE_CONVENTION void safepoint_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord,
                                           clause::abi::v1::TermWord, clause::abi::v1::TermWord,
                                           clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    auto &stack = owners.context->stack();
    auto *slots = stack.frame() + frame_header_words;
    owners.current = {slots[0]};
    garbage(*owners.context, 1000);
    const auto before = current(*owners.context, slots[0]);
    CLAUSE_safepoint_v1(context);
    owners.visited_inside = before.tuple_size() == std::unexpected(TermError::stale_term) ? 1 : 0;
    owners.current.push_back(slots[0]);
    run(stack.leave(slots[0]), context);
}

// One argument, kept in its slot.
const FrameDescriptor safepointed{nullptr, 0, 0, 1, &safepoint_body, 1, 1};

// A function entry collects with its arguments as register roots, and a loop-head safepoint rewrites term slots
// in place: the frame address taken before it stays valid.
void safepoints(Runtime &runtime) {
    auto &context = *runtime.create_context({16, std::size_t{1} << 20}).value();
    owners = Owners{};
    owners.context = &context;
    owners.roots = layouts(context);
    owners.texts = {text(current(context, owners.roots[0]))};
    require(context.heap().wants_collection(), "fixture left no fragment");
    {
        GeneratedInvocation invocation(context.generated_calls());
        const auto result = context.stack().invoke(safepointed, owners.roots.data());
        require(!context.generated_calls().failure() && result == owners.current[1], "safepoint result lost");
    }
    require(owners.current[0] != owners.roots[0], "the entry did not collect");
    require(owners.visited_inside == 1, "the loop head did not collect");
    require(!context.heap().wants_collection() && text(current(context, owners.current[1])) == owners.texts[0],
            "collected argument changed");
    require(context.heap().verify().has_value(), "heap does not verify after the safepoints");
    require(runtime.destroy_context(&context) == Status::ok, "teardown failed");
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        repeated(*runtime);
        binaries(*runtime);
        policy(*runtime);
        near_budget(*runtime);
        shared_limit();
        unsafe(*runtime);
        root_owners(*runtime);
        safepoints(*runtime);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
