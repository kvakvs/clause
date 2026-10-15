#include "terms.hpp"
#include <array>
#include <chrono>
#include <clause/runtime/runtime.hpp>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

// Descriptive heap measurements for docs/runtime-heap.md; values are printed, never gated.
using namespace clause::runtime;

namespace {
// Count live host bytes so heap backing and side metadata (fragment chain, buffers) are both visible.
std::size_t live_bytes = 0;
// Prefix each block with its size; the prefix keeps returned storage aligned as operator new must
// (__STDCPP_DEFAULT_NEW_ALIGNMENT__, 16 bytes on x64, more than max_align_t on Windows).
constexpr std::size_t prefix = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
} // namespace

// Record the block size ahead of the returned storage before delegating to the host allocator.
void *operator new(std::size_t size) {
    auto *block = static_cast<std::byte *>(std::malloc(size + prefix));
    if (block == nullptr) {
        throw std::bad_alloc();
    }
    *reinterpret_cast<std::size_t *>(block) = size;
    live_bytes += size;
    return block + prefix;
}

// Release a counted block; sized and unsized deletes share the recorded size.
void operator delete(void *memory) noexcept {
    if (memory != nullptr) {
        auto *block = static_cast<std::byte *>(memory) - prefix;
        live_bytes -= *reinterpret_cast<std::size_t *>(block);
        std::free(block);
    }
}

void operator delete(void *memory, std::size_t) noexcept { ::operator delete(memory); }

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t kernel_length = 100'000;
constexpr std::size_t footprint_contexts = 1'000;

// Keep measurement failures loud while leaving the numbers themselves ungated.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Milliseconds elapsed since start, for descriptive output only.
long long elapsed_ms(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

// Build a cons list of {Index, Float} tuples one cell at a time, as generated code would.
Term build(TermFactory &factory) {
    auto list = factory.nil().value();
    for (std::size_t i = 0; i < kernel_length; ++i) {
        const std::array fields{factory.integer(static_cast<std::int64_t>(i)).value(),
                                factory.floating(static_cast<double>(i) * 0.5).value()};
        list = factory.cons(factory.tuple(fields).value(), list).value();
    }
    return list;
}

// Read every element back through checked accessors so admission cost is part of the kernel.
double walk(Term list) {
    double sum = 0;
    while (list.is_cons()) {
        sum += list.head()->tuple_element(1)->float_value().value();
        list = list.tail().value();
    }
    return sum;
}

// Collect with the kernel list as the only root: time, live words, merged fragments, then walk the copy.
void collection(ProcessContext &context, const Term &list, std::size_t baseline) {
    std::array roots{list.word()};
    const auto start = Clock::now();
    const auto stats = context.heap().collect(roots).value();
    const auto collect_ms = elapsed_ms(start);
    const auto walk_start = Clock::now();
    require(walk(Term::from_word(roots[0], context).value()) > 0, "collected walk lost values");
    const auto walk_ms = elapsed_ms(walk_start);
    const auto heap_bytes = stats.heap_words * sizeof(Word);
    std::cout << "collect_ms=" << collect_ms << " walk_ms=" << walk_ms << " live_words=" << stats.live_words
              << " heap_words=" << stats.heap_words << " merged_fragment_words=" << stats.fragment_words
              << " side_bytes=" << (live_bytes - baseline - heap_bytes) << '\n';
}

// Allocation-heavy kernel: time, heap words and host bytes beyond the heap backing itself.
void kernel(Runtime &runtime) {
    auto *context = runtime.create_context().value();
    TermFactory factory(*context);
    const auto before = live_bytes;
    const auto start = Clock::now();
    const auto list = build(factory);
    const auto build_ms = elapsed_ms(start);
    const auto walk_start = Clock::now();
    require(walk(list) > 0, "kernel walk lost values");
    const auto walk_ms = elapsed_ms(walk_start);
    const auto &heap = context->heap();
    const auto heap_bytes = heap.capacity_words() * sizeof(Word);
    std::cout << "kernel_cells=" << kernel_length << " build_ms=" << build_ms << " walk_ms=" << walk_ms
              << " used_words=" << heap.used_words() << " capacity_words=" << heap.capacity_words()
              << " host_bytes=" << (live_bytes - before) << " side_bytes=" << (live_bytes - before - heap_bytes)
              << '\n';
    collection(*context, list, before);
    require(runtime.destroy_context(context) == clause::abi::v1::Status::ok, "kernel teardown failed");
}

// Per-context footprint: many processes each holding one small tuple.
void footprint(Runtime &runtime) {
    std::vector<ProcessContext *> contexts;
    contexts.reserve(footprint_contexts);
    const auto before = live_bytes;
    std::size_t capacity = 0;
    for (std::size_t i = 0; i < footprint_contexts; ++i) {
        auto *context = runtime.create_context().value();
        TermFactory factory(*context);
        const std::array fields{factory.atom("ok").value(), factory.integer(1).value(), factory.nil().value()};
        require(factory.tuple(fields).has_value(), "footprint tuple failed");
        capacity += context->heap().capacity_words();
        contexts.push_back(context);
    }
    std::cout << "contexts=" << footprint_contexts
              << " host_bytes_per_context=" << (live_bytes - before) / footprint_contexts
              << " heap_capacity_words_per_context=" << capacity / footprint_contexts << '\n';
    for (auto *context : contexts) {
        require(runtime.destroy_context(context) == clause::abi::v1::Status::ok, "footprint teardown failed");
    }
}
} // namespace

// Print one line per measurement; CTest only checks that the program completes.
int main() {
    try {
        auto runtime = Runtime::start().value();
        std::cout << "word_bytes=" << sizeof(Word) << '\n';
        kernel(*runtime);
        footprint(*runtime);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
