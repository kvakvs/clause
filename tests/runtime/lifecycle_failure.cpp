#include "terms.hpp"
#include <array>
#include <clause/abi/bits.hpp>
#include <clause/abi/immediate_services.hpp>
#include <clause/runtime/code_server.hpp>
#include <clause/runtime/modules.hpp>
#include <clause/runtime/runtime.hpp>
#include <clause/runtime/scheduler.hpp>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <vector>

using clause::abi::v1::Status;
using clause::runtime::Runtime;

namespace {
// Only this isolated test executable replaces allocation; production has no failpoint hooks.
std::size_t remaining = std::numeric_limits<std::size_t>::max();
// Count live allocations to prove cleanup at every partially completed construction boundary.
std::size_t live_allocations = 0;
} // namespace

// Fail a chosen allocation ordinal before delegating to the host allocator.
void *operator new(std::size_t size) {
    if (remaining == 0) {
        throw std::bad_alloc();
    }
    if (remaining != std::numeric_limits<std::size_t>::max()) {
        --remaining;
    }
    void *memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    ++live_allocations;
    return memory;
}

// Balance tracked single-object allocations, including failed shared-token/control-block construction.
void operator delete(void *memory) noexcept {
    if (memory != nullptr) {
        --live_allocations;
        std::free(memory);
    }
}

// Compilers selecting sized deallocation must balance the same tracked allocation count.
void operator delete(void *memory, std::size_t) noexcept { ::operator delete(memory); }

// Keep failure diagnostics outside the allocation-injection window.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// A failed construction followed by a retry must leave a heap whose every area parses and resolves.
void require_walkable(clause::runtime::ProcessContext &context) {
    require(context.heap().verify().has_value(), "failed construction left an unparseable heap");
}

// Every startup allocation, builtin registration included, must roll back before an owner can escape to the host.
void check_startup() {
    bool succeeded = false;
    // Each registered builtin adds registry allocations, so the sweep bound grows with the catalog.
    for (std::size_t ordinal = 0; ordinal < 1024 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        remaining = ordinal;
        auto runtime = Runtime::start();
        remaining = std::numeric_limits<std::size_t>::max();
        succeeded = runtime.has_value();
        if (succeeded) {
            require((*runtime)->shutdown() == Status::ok, "startup cleanup failed");
            runtime->reset();
        } else {
            require(runtime.error() == Status::out_of_memory, "wrong startup failure");
        }
        require(live_allocations == baseline, "partial runtime initialization leaked");
    }
    require(succeeded, "startup failpoint sweep never reached success");
}

// Memory service rejection and immediate copying must work even when host allocation is exhausted.
void check_heap_without_allocation(clause::runtime::ProcessHeap &heap) {
    using namespace clause::runtime;
    const auto baseline = live_allocations;
    remaining = 0;
    const auto allocation = heap.allocate(1);
    const auto collection = heap.collect();
    const auto copied = Term::from_word(*encode_integer(7))->copy_to(heap);
    const auto invalid = heap.add(Term{});
    remaining = std::numeric_limits<std::size_t>::max();
    require(allocation == std::unexpected(HeapError::out_of_memory), "allocation OOM lost");
    require(collection && collection->heap_words == 0, "collecting an empty heap allocated");
    require(copied && copied->integer_value() == 7, "immediate copy allocated bookkeeping");
    require(invalid == std::unexpected(TermError::invalid_encoding), "invalid copy changed under OOM");
    require(live_allocations == baseline, "memory boundary retained allocations");
}

// A collection whose new heap block cannot be allocated fails with every word, count and host Term intact.
void check_collection_without_allocation(clause::runtime::ProcessContext &context) {
    using namespace clause::runtime;
    TermFactory factory(context);
    const auto value = factory.tuple(std::array{factory.integer(5).value()}).value();
    std::array roots{value.word()};
    const auto used = context.heap().used_words();
    const auto capacity = context.heap().capacity_words();
    const auto baseline = live_allocations;
    remaining = 0;
    const auto collection = context.heap().collect(roots);
    remaining = std::numeric_limits<std::size_t>::max();
    require(collection == std::unexpected(HeapError::out_of_memory), "new-block OOM lost");
    require(live_allocations == baseline && roots[0] == value.word(), "failed collection changed roots or memory");
    require(context.heap().used_words() == used && context.heap().capacity_words() == capacity,
            "failed collection changed accounting");
    require(value.tuple_element(0)->integer_value() == 5, "failed collection made host terms stale");
    require(context.heap().verify().has_value(), "failed collection damaged the heap");
}

// Fail each context/mailbox/token/registry allocation; existing contexts must survive every rollback.
void check_context_creation() {
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 32 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        auto runtime = Runtime::start();
        require(runtime.has_value(), "fixture startup failed");
        auto existing = (*runtime)->create_context();
        require(existing.has_value(), "fixture context failed");
        check_heap_without_allocation((*existing)->heap());
        check_collection_without_allocation(**existing);
        const auto retained = live_allocations;
        remaining = ordinal;
        auto created = (*runtime)->create_context();
        remaining = std::numeric_limits<std::size_t>::max();
        succeeded = created.has_value();
        if (succeeded) {
            require((*runtime)->destroy_context(*created) == Status::ok, "new context cleanup failed");
        } else {
            require(created.error() == Status::out_of_memory, "wrong context failure");
            require(live_allocations == retained, "partial context initialization leaked");
        }
        require((*runtime)->destroy_context(*existing) == Status::ok, "failed creation damaged existing context");
        require((*runtime)->shutdown() == Status::ok, "runtime cleanup failed");
        runtime->reset();
        require(live_allocations == baseline, "failed construction retained memory after shutdown");
    }
    require(succeeded, "context failpoint sweep never reached success");
}

// Fail registry key/type-vector/node allocations without disturbing existing signatures.
void check_registry_creation() {
    using namespace clause::runtime;
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 32 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            ModuleRegistry registry;
            const Callable target = [](ProcessContext &, std::span<const Term>) { return CallResult<Term>(Term{}); };
            require(registry.add("existing", 0, Callable{target}).has_value(), "registry fixture failed");
            const auto retained = live_allocations;
            remaining = ordinal;
            auto added = registry.add("allocation_failure_signature", 2, Callable{target});
            remaining = std::numeric_limits<std::size_t>::max();
            succeeded = added.has_value();
            if (!succeeded) {
                require(added.error() == RegistryError::resource_limit, "wrong registry failure");
                require(live_allocations == retained, "partial signature leaked");
                require(!registry.find("allocation_failure_signature", 2), "failed signature published");
            }
            require(registry.find("existing", 0).has_value(), "failed add damaged existing signature");
        }
        require(live_allocations == baseline, "registry cleanup leaked");
    }
    require(succeeded, "registry allocation sweep never succeeded");
}

// Sweep publication allocations, proving neither partial modules nor leaked captures survive failure.
void check_module_publication() {
    using namespace clause::runtime;
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 32 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            CodeServer server;
            require(server.load({"existing", CodeImage::linked(), std::make_unique<ModuleRegistry>()}).has_value(),
                    "module fixture failed");
            const auto retained = live_allocations;
            CodeResult<std::shared_ptr<const LoadedModule>> loaded = std::unexpected(CodeError::resource_limit);
            {
                ModuleDefinition definition{"allocation_failure_module", CodeImage::linked(),
                                            std::make_unique<ModuleRegistry>()};
                remaining = ordinal;
                loaded = server.load(std::move(definition));
                remaining = std::numeric_limits<std::size_t>::max();
            }
            succeeded = loaded.has_value();
            if (!succeeded) {
                require(loaded.error() == CodeError::resource_limit, "wrong publication failure");
                require(live_allocations == retained, "failed module retained draft storage");
                require(!server.find_module("allocation_failure_module"), "failed module published");
            }
            require(server.find_module("existing").has_value(), "failed publication damaged existing module");
        }
        require(live_allocations == baseline, "module cleanup leaked");
    }
    require(succeeded, "module allocation sweep never succeeded");
}

// Failed registry growth must leave both the entry and the once-only context marker unpublished.
void check_scheduler_registration() {
    using namespace clause::runtime;
    const auto baseline = live_allocations;
    auto runtime = Runtime::start();
    require(runtime.has_value(), "scheduler fixture startup failed");
    auto context = (*runtime)->create_context();
    require(context.has_value(), "scheduler fixture context failed");
    auto &scheduler = *(*runtime)->scheduler();
    const auto retained = live_allocations;
    remaining = 0;
    const auto failed = scheduler.register_process(**context);
    remaining = std::numeric_limits<std::size_t>::max();
    require(failed == std::unexpected(SchedulerError::resource_limit), "wrong scheduler allocation failure");
    require(live_allocations == retained && scheduler.process_count() == 0, "partial registration retained resources");
    require(scheduler.register_process(**context).has_value(), "failed registration consumed once-only identity");
    require((*runtime)->destroy_context(*context) == Status::ok, "registered context cleanup failed");
    require(scheduler.process_count() == 0, "context cleanup retained registration");
    runtime->reset();
    require(live_allocations == baseline, "scheduler cleanup leaked");
}

// Both spelling/word indexes must roll back together at every allocation ordinal.
void check_atom_interning() {
    using namespace clause::runtime;
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 32 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            auto runtime = Runtime::start().value();
            auto &atoms = *runtime->atom_storage();
            const auto existing = atoms.intern("existing").value();
            const auto retained = live_allocations;
            remaining = ordinal;
            const auto created = atoms.intern("long_atom_spelling_requiring_owned_storage");
            remaining = std::numeric_limits<std::size_t>::max();
            succeeded = created.has_value();
            if (!succeeded) {
                require(atoms.size() == 1 && live_allocations == retained, "partial atom insertion leaked");
            }
            require(atoms.intern("existing")->word() == existing.word(), "failed insertion damaged deduplication");
            require(atoms.intern("long_atom_spelling_requiring_owned_storage").has_value(), "atom retry failed");
            require(atoms.size() == 2, "retry retained a partial index entry");
        }
        require(live_allocations == baseline, "atom storage teardown leaked");
    }
    require(succeeded, "atom allocation sweep never succeeded");
}

// Registration may retain valid atoms after failure, but never exposes draft slots or callable modules.
void check_atom_registration() {
    using namespace clause::runtime;
    using namespace clause::abi::v1;
    bool succeeded = false;
    const AtomDescriptor atom{"registered_atom_literal", 23};
    const ModuleDescriptor descriptor{version, sizeof(Word) * 8, "atom_module", 11, nullptr, 0, &atom, 1};
    for (std::size_t ordinal = 0; ordinal < 64 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            auto runtime = Runtime::start().value();
            const auto image = CodeImage::linked();
            remaining = ordinal;
            const auto loaded = register_module(*runtime, descriptor, image);
            remaining = std::numeric_limits<std::size_t>::max();
            succeeded = loaded.has_value();
            if (!succeeded) {
                require(!runtime->code_server()->find_module("atom_module"), "failed atom module published");
                require(!runtime->code_server()->atom_word(&descriptor, 0), "failed atom slots published");
                require(register_module(*runtime, descriptor, image).has_value(), "atom module retry failed");
            }
            require(runtime->atom_storage()->size() == 2, "registration retry failed to deduplicate");
        }
        require(live_allocations == baseline, "atom module teardown leaked");
    }
    require(succeeded, "atom registration sweep never succeeded");
}

// Integer temporaries, backing and publication all fail transactionally before an ordinary retry.
void check_integer_construction() {
    using namespace clause::runtime;
    // MSVC initializes two process-wide stream locale facets on first use; exclude those caches from heap accounting.
    {
        std::ostringstream warmup;
        warmup << "";
    }
    const std::string digits(400, '9');
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 256 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            auto runtime = Runtime::start().value();
            auto &context = *runtime->create_context().value();
            TermFactory factory(context);
            remaining = ordinal;
            const auto result = factory.integer_decimal(digits);
            remaining = std::numeric_limits<std::size_t>::max();
            succeeded = result.has_value();
            if (!succeeded) {
                require(result.error() == TermError::out_of_memory, "integer allocation status lost");
                require(context.heap().used_words() == 0 && context.heap().capacity_words() == 0,
                        "failed integer left published storage");
                require(factory.integer_decimal(digits)->integer_decimal() == digits,
                        "integer retry corrupted magnitude");
                require_walkable(context);
            }
        }
        require(live_allocations == baseline, "integer allocation sweep leaked");
    }
    require(succeeded, "integer allocation sweep never succeeded");
}

// Integer temporaries, backing and publication all fail transactionally before an ordinary retry.
void check_map_construction() {
    using namespace clause::runtime;
    const auto key = Term::from_word(encode_integer(42).value()).value();
    const auto value = Term::from_word(encode_integer(7).value()).value();
    const std::array entries{std::pair{key, value}, std::pair{value, key}};
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 256 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            auto runtime = Runtime::start().value();
            auto &context = *runtime->create_context().value();
            TermFactory factory(context);
            remaining = ordinal;
            const auto result = factory.map(entries);
            remaining = std::numeric_limits<std::size_t>::max();
            succeeded = result.has_value();
            if (!succeeded) {
                require(result.error() == TermError::out_of_memory, "map allocation status lost");
                require(context.heap().used_words() == 0 && context.heap().capacity_words() == 0,
                        "failed map left published storage");
                require(factory.map(entries)->map_size() == 2, "map retry lost entries");
                require_walkable(context);
            }
        }
        require(live_allocations == baseline, "map allocation sweep leaked");
    }
    require(succeeded, "map allocation sweep never succeeded");
}

// Integer temporaries, backing and publication all fail transactionally before an ordinary retry.
void check_float_construction() {
    using namespace clause::runtime;
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 256 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            auto runtime = Runtime::start().value();
            auto &context = *runtime->create_context().value();
            TermFactory factory(context);
            remaining = ordinal;
            const auto result = factory.floating(1.5);
            remaining = std::numeric_limits<std::size_t>::max();
            succeeded = result.has_value();
            if (!succeeded) {
                require(result.error() == TermError::out_of_memory, "float allocation status lost");
                require(context.heap().used_words() == 0 && context.heap().capacity_words() == 0,
                        "failed float left published storage");
                require(factory.floating(1.5)->float_value() == 1.5, "float retry corrupted magnitude");
                require_walkable(context);
            }
        }
        require(live_allocations == baseline, "float allocation sweep leaked");
    }
    require(succeeded, "float allocation sweep never succeeded");
}

// Sweep inline/shared binary publication and checked tail extraction without adding production failpoint hooks.
void check_bitstrings(bool extraction, bool large) {
    using namespace clause::runtime;
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 64 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            auto runtime = Runtime::start().value();
            auto &context = *runtime->create_context().value();
            TermFactory factory(context);
            const auto bytes = std::vector(large ? 100U : 2U, std::byte{0xab});
            const auto original = extraction ? factory.binary(bytes).value() : factory.integer(42).value();
            const std::array input{original.word(), factory.integer(3)->word(), factory.integer(256 + 2)->word(),
                                   factory.integer(static_cast<std::int64_t>(bytes.size() * 8 - 3))->word(),
                                   factory.integer(0)->word()};
            const auto used = context.heap().used_words();
            const auto capacity = context.heap().capacity_words();
            const auto off_heap = context.heap().off_heap_words();
            std::array<Word, 2> output{123, 456};
            {
                GeneratedInvocation call(context.generated_calls());
                remaining = ordinal;
                const auto result =
                    extraction
                        ? CLAUSE_bits_v1(&context, 1, input.data(), input.size(), output.data())
                        : factory.binary(bytes).transform([](const Term &) { return std::uint8_t{0}; }).value_or(2);
                remaining = std::numeric_limits<std::size_t>::max();
                succeeded = result == 0;
                if (!succeeded) {
                    require(context.heap().used_words() == used && context.heap().capacity_words() == capacity &&
                                context.heap().off_heap_words() == off_heap,
                            "failed bit publication retained backing");
                    require(output == std::array<Word, 2>{123, 456}, "failed extraction published an output");
                    require(!extraction || context.generated_calls().failure()->status == Status::out_of_memory,
                            "extraction allocation became mismatch");
                }
            }
            require(factory.binary(bytes).has_value(), "bit allocation rejection poisoned retry");
            require_walkable(context);
        }
        require(live_allocations == baseline, "bit construction/extraction sweep leaked");
    }
    require(succeeded, "bit allocation sweep never reached success");
}

// Body of the frame pushed under allocation faults: return the argument.
void identity_body(void *context) {
    auto &stack = static_cast<clause::runtime::ProcessContext *>(context)->stack();
    stack.leave(stack.frame()[clause::abi::v1::frame_header_words])(context);
}

// An isolated allocator override verifies real failure cleanup without adding production test switches.
void check_root_allocation() {
    using namespace clause::runtime;
    const clause::abi::v1::FrameDescriptor identity{nullptr, 0, 0, 1, &identity_body, 4, 4};
    const auto argument = encode_integer(5).value();
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 8 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            auto runtime = Runtime::start().value();
            auto &context = *runtime->create_context().value();
            GeneratedInvocation invocation(context.generated_calls());
            const auto retained = live_allocations;
            remaining = ordinal;
            const auto result = context.stack().invoke(identity, &argument);
            remaining = std::numeric_limits<std::size_t>::max();
            succeeded = result == argument;
            if (!succeeded) {
                require(context.generated_calls().failure()->status == Status::out_of_memory, "stack OOM lost");
                require(context.stack().depth() == 0 && context.stack().words() == 0, "failed frame published");
                require(live_allocations <= retained + 1, "partial stack leaked");
            }
        }
        require(live_allocations == baseline, "stack teardown leaked");
    }
    require(succeeded, "stack allocation sweep never succeeded");
}

// Sweep heap block allocation; a failed reservation keeps no backing and allows retry.
void check_heap_construction() {
    using namespace clause::runtime;
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 8 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            auto runtime = Runtime::start().value();
            auto &heap = runtime->create_context().value()->heap();
            remaining = ordinal;
            auto reserved = heap.reserve(1);
            succeeded = reserved && reserved->commit().has_value();
            remaining = std::numeric_limits<std::size_t>::max();
            if (!succeeded) {
                require(heap.used_words() == 0 && heap.capacity_words() == 0, "failed construction kept backing");
                require(heap.allocate(1).has_value(), "failed construction poisoned retry");
            }
        }
        require(live_allocations == baseline, "heap construction leaked");
    }
    require(succeeded, "heap construction sweep never succeeded");
}

// Every failed tuple/list publication restores its entire reservation and index before a successful retry.
void check_container_construction(bool list) {
    using namespace clause::runtime;
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 64 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            auto runtime = Runtime::start().value();
            auto &context = *runtime->create_context().value();
            TermFactory factory(context);
            const auto number = Term::from_word(encode_integer(42).value()).value();
            const std::array elements{number, number, number, number, number, number, number, number};
            remaining = ordinal;
            const auto result = list ? factory.list(elements) : factory.tuple(elements);
            remaining = std::numeric_limits<std::size_t>::max();
            succeeded = result.has_value();
            if (!succeeded) {
                require(result.error() == TermError::out_of_memory, "compound allocation status lost");
                require(context.heap().used_words() == 0 && context.heap().capacity_words() == 0,
                        "partial compound allocation survived rollback");
                require((list ? factory.list(elements) : factory.tuple(elements)).has_value(),
                        "failed object index poisoned retry");
                require_walkable(context);
            }
        }
        require(live_allocations == baseline, "compound allocation sweep leaked");
    }
    require(succeeded, "compound allocation sweep never reached success");
}

// A copy failing at any host allocation, while discovering the graph or holding its binary buffer, leaves the
// destination empty and the source intact; a retry succeeds.
void check_graph_copy() {
    using namespace clause::runtime;
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 64 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            auto runtime = Runtime::start().value();
            auto &source = *runtime->create_context().value();
            auto &destination = *runtime->create_context().value();
            TermFactory factory(source);
            const auto large = factory.binary(std::vector(100, std::byte{0x3c})).value();
            const std::array numbers{factory.integer(1).value(), factory.integer(2).value()};
            const std::array fields{large, factory.list(numbers).value(), large};
            const auto graph = factory.tuple(fields).value();
            const auto source_words = source.heap().used_words();
            remaining = ordinal;
            const auto result = graph.copy_to(destination.heap());
            remaining = std::numeric_limits<std::size_t>::max();
            succeeded = result.has_value();
            if (!succeeded) {
                require(result.error() == TermError::out_of_memory, "copy allocation status lost");
                require(destination.heap().used_words() == 0 && destination.heap().capacity_words() == 0 &&
                            destination.heap().off_heap_words() == 0 && source.heap().used_words() == source_words,
                        "failed copy changed a heap");
                require(graph.copy_to(destination.heap())->exactly_equal(graph) == true, "failed copy poisoned retry");
                require_walkable(destination);
            }
        }
        require(live_allocations == baseline, "graph copy sweep leaked");
    }
    require(succeeded, "graph copy sweep never reached success");
}

// Sweep temporary arithmetic limbs and final publication through the real checked fallback boundary.
void check_integer_arithmetic() {
    using namespace clause::runtime;
    bool succeeded = false;
    for (std::size_t ordinal = 0; ordinal < 256 && !succeeded; ++ordinal) {
        const auto baseline = live_allocations;
        {
            auto runtime = Runtime::start().value();
            auto &context = *runtime->create_context().value();
            const auto value = TermFactory(context).integer_decimal(std::string(400, '9')).value();
            const auto used = context.heap().used_words();
            const auto capacity = context.heap().capacity_words();
            Word output = 123;
            {
                GeneratedInvocation call(context.generated_calls());
                remaining = ordinal;
                const auto result = CLAUSE_immediate_v1(
                    &context, static_cast<std::uint8_t>(clause::abi::v1::ImmediateOperation::multiply), value.word(),
                    value.word(), &output);
                remaining = std::numeric_limits<std::size_t>::max();
                succeeded = result == 0;
                if (!succeeded) {
                    require(result == 2 && context.generated_calls().failure()->status == Status::out_of_memory,
                            "arithmetic allocation became badarith");
                    require(output == 123 && context.heap().used_words() == used &&
                                context.heap().capacity_words() == capacity,
                            "failed arithmetic published or retained a partial integer");
                }
            }
            GeneratedInvocation retry(context.generated_calls());
            require(CLAUSE_immediate_v1(&context,
                                        static_cast<std::uint8_t>(clause::abi::v1::ImmediateOperation::subtract),
                                        value.word(), value.word(), &output) == 0 &&
                        output == encode_integer(0).value(),
                    "arithmetic allocation failure poisoned exact retry");
            require_walkable(context);
        }
        require(live_allocations == baseline, "integer arithmetic sweep leaked");
    }
    require(succeeded, "integer arithmetic allocation sweep never succeeded");
}

// An isolated allocator override verifies real failure cleanup without adding production test switches.
int main() {
    try {
        check_startup();
        check_context_creation();
        check_registry_creation();
        check_module_publication();
        check_scheduler_registration();
        check_atom_interning();
        check_atom_registration();
        check_heap_construction();
        check_root_allocation();
        check_container_construction(false);
        check_container_construction(true);
        check_integer_construction();
        check_float_construction();
        check_map_construction();
        check_integer_arithmetic();
        check_graph_copy();
        for (const bool large : {false, true}) {
            check_bitstrings(false, large);
            check_bitstrings(true, large);
        }
    } catch (const std::exception &error) {
        remaining = std::numeric_limits<std::size_t>::max();
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    } catch (...) {
        remaining = std::numeric_limits<std::size_t>::max();
        std::fputs("unexpected allocation test exception\n", stderr);
        return 1;
    }
}
