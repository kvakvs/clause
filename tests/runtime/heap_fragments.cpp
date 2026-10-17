#include "terms.hpp"
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

// The process heap is one block; while it may not move, requests that do not fit go to fragments
// chained to the same process. Fragments obey the shared budget, admission, the walker and rollback.
namespace {
using namespace clause::runtime;
using clause::abi::v1::Status;

// Keep every check active in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Build a tuple of size elements {Tag, Tag, ...}, size + 1 heap words.
TermResult<Term> tuple(ProcessContext &context, std::size_t size, std::int64_t tag) {
    TermFactory factory(context);
    const std::vector elements(size, factory.integer(tag).value());
    return factory.tuple(elements);
}

// Every listed tuple is still admitted and holds its own tag.
void require_intact(ProcessContext &context, const std::vector<Term> &values) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto admitted = Term::from_word(values[i].word(), context);
        require(admitted && admitted->tuple_element(0)->integer_value() == static_cast<std::int64_t>(i),
                "earlier object lost or damaged");
    }
    require(context.heap().verify().has_value(), "heap with fragments does not verify");
}

// Requests that do not fit the heap go to the newest fragment, else a new one of at least the minimum size.
void overflow(ProcessContext &context, std::vector<Term> &values) {
    auto &heap = context.heap();
    values.push_back(tuple(context, 3, 0).value());
    require(heap.capacity_words() == 4 && heap.used_words() == 4, "first object did not create the heap block");
    values.push_back(tuple(context, 1, 1).value());
    require(heap.capacity_words() == 8 && heap.used_words() == 6, "overflow did not create a minimum fragment");
    values.push_back(tuple(context, 1, 2).value());
    require(heap.capacity_words() == 8 && heap.used_words() == 8, "newest fragment tail was not used");
    values.push_back(tuple(context, 8, 3).value());
    require(heap.capacity_words() == 17 && heap.used_words() == 17, "large request got a wrong fragment");
    require_intact(context, values);
    const auto census = heap.verify().value();
    require(census.boxed_objects == 4 && census.words == 17, "walker missed fragment objects");
}

// A reservation that opens a fragment, or bumps the newest one, rolls back exactly and its words are not admitted.
void rollback(ProcessContext &context, const std::vector<Term> &values) {
    auto &heap = context.heap();
    Word abandoned = 0;
    {
        auto reservation = heap.reserve(2).value();
        require(heap.capacity_words() == 21, "reservation did not open a fragment");
        abandoned = reinterpret_cast<Word>(reservation.bytes().data()) | 2U;
    }
    require(heap.capacity_words() == 17 && heap.used_words() == 17, "fragment rollback kept accounting");
    require(!clause::runtime::CHECKED_ADMISSION ||
                Term::from_word(abandoned, context) == std::unexpected(TermError::wrong_owner),
            "dropped fragment word admitted");
    require(tuple(context, 70, 0) == std::unexpected(TermError::resource_limit), "budget ignored");
    require(heap.capacity_words() == 17 && heap.used_words() == 17, "failed construction kept accounting");
    require_intact(context, values);
}

// Filling the budget with fragments fails cleanly and leaves every earlier object usable.
void exhaustion(ProcessContext &context, std::vector<Term> &values, std::size_t limit_words) {
    auto &heap = context.heap();
    for (;;) {
        auto next = tuple(context, 1, static_cast<std::int64_t>(values.size()));
        if (!next) {
            require(next.error() == TermError::resource_limit, "exhaustion was not a resource limit");
            break;
        }
        values.push_back(*next);
    }
    require(heap.capacity_words() <= limit_words && heap.used_words() + 2 > limit_words,
            "budget not filled before exhaustion");
    require_intact(context, values);
    require(heap.allocate(1).has_value() || heap.used_words() == limit_words, "exhaustion poisoned the heap");
}

// Many processes with the default minimum heap each own one small block.
void many_contexts(Runtime &runtime) {
    std::vector<ProcessContext *> contexts;
    for (std::size_t i = 0; i < 1000; ++i) {
        auto *context = runtime.create_context().value();
        require(tuple(*context, 2, 0).has_value(), "small tuple failed");
        require(context->heap().capacity_words() == HeapOptions{}.min_heap_words && context->heap().used_words() == 3,
                "default heap is not one minimum block");
        contexts.push_back(context);
    }
    for (auto *context : contexts) {
        require(context->heap().verify().has_value(), "default heap does not verify");
        require(runtime.destroy_context(context) == Status::ok, "teardown failed");
    }
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        constexpr std::size_t limit_words = 64;
        auto &context = *runtime->create_context({4, limit_words * sizeof(Word)}).value();
        std::vector<Term> values;
        overflow(context, values);
        rollback(context, values);
        exhaustion(context, values, limit_words);
        many_contexts(*runtime);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
