#include <array>
#include <clause/abi/bits.hpp>
#include <clause/abi/containers.hpp>
#include <clause/abi/equality.hpp>
#include <clause/abi/immediate_services.hpp>
#include <clause/abi/maps.hpp>
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/modules.hpp>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <terms.hpp>

using namespace clause;
using namespace clause::runtime;
extern abi::v1::GeneratedRegistration register_answer asm("clausev1_736572766963655f616e73776572__0.register");
extern abi::v1::GeneratedRegistration register_client asm("clausev1_736572766963655f636c69656e74__0.register");
extern std::uint8_t injected(void *, std::uint8_t, Word, Word, Word *) noexcept asm("step7_service");
extern std::uint8_t injected_construct(void *, std::uint8_t, const Word *, std::size_t, Word *) noexcept
    asm("step12_construct");
extern std::uint8_t injected_inspect(void *, std::uint8_t, Word, std::size_t, Word *) noexcept asm("step12_inspect");

extern std::uint8_t injected_map(void *, std::uint8_t, const Word *, std::size_t, Word *) noexcept asm("step15_map");

extern std::uint8_t injected_bits(void *, std::uint8_t, const Word *, std::size_t, Word *) noexcept asm("step16_bits");

namespace {
// Select faults after source-generated code has entered the real invocation scope.
abi::v1::Status fault = abi::v1::Status::ok;
// Count service entry to prove the fault happened in generated code rather than host admission.
unsigned calls = 0;
// Target only the reached predicate when proving that lazy branches skip faults and strict branches do not.
bool predicate_only = false;
unsigned predicate_calls = 0;
// Verify real container services retain every live input/output in the generated root frame.
bool unrooted = false;
// Select extraction faults after successful binary construction and type checking.
bool extraction_only = false;

// Keep ownership, status and recovery assertions enabled in optimized consumers.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Exact infrastructure statuses must survive both head/body services and nested generated calls.
void failures(ProcessContext &context, std::string_view module, std::string_view name) {
    const auto entry = context.code_server().resolve({module, name, 1}).value();
    const std::array arguments{Term::from_word(encode_integer(42).value()).value()};
    for (const auto status :
         {abi::v1::Status::out_of_memory, abi::v1::Status::resource_limit, abi::v1::Status::wrong_owner,
          abi::v1::Status::not_implemented, abi::v1::Status::internal_error}) {
        fault = status;
        calls = 0;
        const auto result = entry.call(context, arguments);
        const auto message = "service failure became semantic rejection or success: " + std::string(name);
        require(!result && result.error().code == CallError::runtime_failure && result.error().status == status,
                message.c_str());
        require(calls == 1 && !context.generated_calls().failure(), "service did not run or stale channel");
        require(context.stack().depth() == 0 && context.stack().words() == 0, "service failure leaked roots");
        fault = abi::v1::Status::ok;
        require(entry.call(context, arguments).has_value(), "service failure poisoned retry");
    }
}

// A rejected source head must reach function_clause before entering any guard service.
void head_mismatch(ProcessContext &context) {
    const std::array arguments{Term::from_word(encode_integer(42).value()).value()};
    fault = abi::v1::Status::out_of_memory;
    calls = 0;
    const auto result =
        context.code_server().resolve({"service_answer", "head_mismatch", 1}).value().call(context, arguments);
    require(!result && result.error().reason == abi::v1::ErrorReason::function_clause && calls == 0,
            "failing head evaluated its guard");
    fault = abi::v1::Status::ok;
}

// Generic builtin registrations cannot replace a guard identity authorized by the compiler.
void registration(ProcessContext &context) {
    auto registry = std::make_unique<ModuleRegistry>();
    auto entries = std::make_shared<unsigned>(0);
    require(registry
                ->add("is_integer", 1,
                      [entries](ProcessContext &, std::span<const Term> arguments) {
                          ++*entries;
                          return CallResult<Term>(arguments.front());
                      })
                .has_value(),
            "builtin seam registration failed");
    require(context.code_server().load({"erlang", CodeImage::linked(), std::move(registry)}).has_value(),
            "builtin seam publication failed");
    const std::array arguments{Term::from_word(encode_integer(42).value()).value()};
    const auto result = context.code_server().resolve({"service_answer", "head", 1}).value().call(context, arguments);
    require(result.has_value() && *entries == 0, "registered builtin replaced guard service");
}

// Infrastructure failure cannot take nested orelse or semicolon recovery, and skipped predicates never run.
void boolean_failures(ProcessContext &context) {
    predicate_only = true;
    const std::array arguments{Term::from_word(encode_integer(42).value()).value()};
    for (const auto name : {"reached", "strict"}) {
        failures(context, "service_answer", name);
    }
    fault = abi::v1::Status::out_of_memory;
    predicate_calls = 0;
    const auto result =
        context.code_server().resolve({"service_answer", "skipped", 1}).value().call(context, arguments);
    require(result && result->atom_spelling() == "true" && predicate_calls == 0,
            "skipped right predicate ran or did not preserve term result");
    fault = abi::v1::Status::ok;
    predicate_only = false;
}

// Count real services to prove a matched RHS runs once and a failed match skips later body work.
void body_matches(ProcessContext &context) {
    const std::array arguments{Term::from_word(encode_integer(42).value()).value()};
    calls = 0;
    const auto once =
        context.code_server().resolve({"service_answer", "match_once", 1}).value().call(context, arguments);
    require(once && once->atom_spelling() == "true" && calls == 1, "matched RHS was not evaluated exactly once");
    calls = 0;
    const auto failed =
        context.code_server().resolve({"service_answer", "match_stop", 1}).value().call(context, arguments);
    require(!failed && failed.error().reason == abi::v1::ErrorReason::badmatch && calls == 1,
            "body mismatch ran later services or lost its reason");
    require(failed.error().value && failed.error().value->atom_spelling() == "true", "badmatch lost the RHS atom");
    failures(context, "service_answer", "match_once");
}

// Guard argument errors are channel-free; malformed/foreign service words are infrastructure failures.
void checked_arguments(ProcessContext &context) {
    auto other = Runtime::start().value();
    auto &foreign = *other->create_context().value();
    const auto atom = foreign.atom_storage().intern("foreign").value();
    for (Word value : {Word{0}, atom.word()}) {
        GeneratedInvocation invocation(context.generated_calls());
        Word output = 123;
        require(CLAUSE_immediate_v1(&context, static_cast<std::uint8_t>(abi::v1::ImmediateOperation::is_atom), value, 0,
                                    &output) == 2,
                "malformed/foreign service input admitted");
        require(output == 123 && context.generated_calls().failure(), "failure published output or lost channel");
    }
    GeneratedInvocation invocation(context.generated_calls());
    Word output = 123;
    require(CLAUSE_immediate_v1(&context, static_cast<std::uint8_t>(abi::v1::ImmediateOperation::hd),
                                abi::v1::empty_list, 0, &output) == 1,
            "semantic badarg lost");
    require(output == 123 && !context.generated_calls().failure(), "semantic rejection polluted channel");
}

// Returned extracted graphs and badmatch payloads stay valid after nested allocations and channel cleanup.
void heap_lifetimes(ProcessContext &context) {
    TermFactory factory(context);
    const auto number = Term::from_word(encode_integer(42).value()).value();
    const auto child = factory.tuple(std::array{number}).value();
    const auto list = factory.list(std::array{child, child}).value();
    const auto input = factory.tuple(std::array{number, list}).value();
    const auto extracted = context.code_server().resolve({"service_answer", "extracted", 1}).value();
    const auto output = extracted.call(context, std::array{input}).value();
    require(output.tuple_element(0)->word() == child.word(), "extracted child was reconstructed");
    const auto failed =
        context.code_server().resolve({"service_answer", "heap_error", 1}).value().call(context, std::array{input});
    require(!failed && failed.error().value && !context.generated_calls().failure(),
            "constructed badmatch lost ownership");
    for (unsigned i = 0; i < 100; ++i) {
        require(extracted.call(context, std::array{input}).has_value(), "later nested allocation failed");
    }
    require(output.tuple_element(0)->exactly_equal(child) == true, "returned extraction damaged by growth");
    require(failed.error().value->tuple_element(0)->exactly_equal(input) == true, "retained badmatch graph damaged");
    require(context.stack().depth() == 0 && context.stack().words() == 0, "heap calls leaked roots");
}

// Record access tests and reads its tuple inline (inline inspections), so no inspection service can fail; a
// mismatching record keeps its owned badrecord payload.
void record_failures(ProcessContext &context) {
    TermFactory factory(context);
    const auto number = factory.integer(42).value();
    const auto child = factory.tuple(std::array{number}).value();
    const auto record = factory.tuple(std::array{context.atom_storage().intern("fault_record").value(), child}).value();
    const auto entry = context.code_server().resolve({"service_answer", "record_inspect", 1}).value();
    calls = 0;
    require(entry.call(context, std::array{record})->word() == child.word() && calls == 0,
            "record access did not read inline");
    const auto error =
        context.code_server().resolve({"service_answer", "record_error", 1}).value().call(context, std::array{child});
    require(!error && error.error().reason == abi::v1::ErrorReason::badrecord && error.error().value,
            "record access lost its owned payload");
    for (unsigned i = 0; i < 32; ++i) {
        require(context.code_server()
                    .resolve({"service_answer", "record_body", 1})
                    .value()
                    .call(context, std::array{record})
                    .has_value(),
                "record allocation after error failed");
    }
    require(error.error().value->tuple_element(1)->exactly_equal(child) == true, "badrecord payload expired");
    require(context.stack().depth() == 0 && !context.generated_calls().failure(), "badrecord leaked state");
}

// A real backing ceiling reached inside generated construction terminates guards and cleans frames. Garbage of
// earlier calls is collected at function entries, so only a single allocation beyond the budget reaches it.
void heap_budget(Runtime &runtime) {
    auto &context = *runtime.create_context({16, 32 * sizeof(Word)}).value();
    const auto entry = context.code_server().resolve({"service_answer", "heap_guard", 1}).value();
    const std::array arguments{Term::from_word(encode_integer(42).value()).value()};
    for (unsigned i = 0; i < 64; ++i) {
        require(entry.call(context, arguments).has_value(), "garbage of earlier calls reached the heap ceiling");
    }
    auto &tiny = *runtime.create_context({1, sizeof(Word)}).value();
    const auto result = tiny.code_server().resolve({"service_answer", "heap_guard", 1}).value().call(tiny, arguments);
    require(!result && result.error().status == abi::v1::Status::resource_limit, "heap ceiling became false guard");
    require(tiny.stack().depth() == 0 && tiny.stack().words() == 0 && !tiny.generated_calls().failure(),
            "heap ceiling leaked call state");
    require(tiny.code_server().resolve({"service_answer", "id", 1}).value().call(tiny, arguments).has_value(),
            "heap failure poisoned nonallocating retry");
    require(runtime.destroy_context(&tiny) == abi::v1::Status::ok &&
                runtime.destroy_context(&context) == abi::v1::Status::ok,
            "bounded context teardown failed");
}

// An integer past the ERTS size limit (error:system_limit) rejects its guard, as in OTP, so the next clause runs; one
// bit less fits, and later invocations stay usable.
void integer_budget(ProcessContext &context) {
    const auto entry = context.code_server().resolve({"service_answer", "integer_budget", 1}).value();
    const auto largest = entry.call(context, std::array{TermFactory(context).integer(4'194'239).value()});
    require(largest && largest->atom_spelling() == "fits", "largest integer rejected its guard");
    const auto passed = entry.call(context, std::array{TermFactory(context).integer(4'194'240).value()});
    require(passed && passed->atom_spelling() == "recovered", "integer size limit escaped its guard");
    require(context.stack().depth() == 0 && !context.generated_calls().failure(), "integer limit leaked state");
    require(entry.call(context, std::array{TermFactory(context).integer(1).value()}).has_value(),
            "integer limit poisoned retry");
}

// A reached extraction failure cannot become badmatch or fallback; retained large tails survive caller cleanup.
void bit_extractions(ProcessContext &context) {
    const auto source = TermFactory(context).binary(std::vector(80, std::byte{42})).value();
    const auto entry = context.code_server().resolve({"service_answer", "bits_extract", 1}).value();
    extraction_only = true;
    for (const auto status : {abi::v1::Status::out_of_memory, abi::v1::Status::resource_limit}) {
        fault = status;
        const auto result = entry.call(context, std::array{source});
        require(!result && result.error().status == status, "extraction fault became pattern mismatch");
        require(context.stack().depth() == 0 && !context.generated_calls().failure(), "extraction fault leaked state");
        fault = abi::v1::Status::ok;
        const auto recovered = entry.call(context, std::array{source});
        require(recovered && recovered->tuple_element(1)->bit_size() == 632,
                "extraction fault poisoned retained-tail retry");
    }
    extraction_only = false;
}
} // namespace

// Inject infrastructure errors before allocation, preserving generated cleanup and fallback behavior.
std::uint8_t injected_construct(void *opaque, std::uint8_t operation, const Word *values, std::size_t count,
                                Word *output) noexcept {
    auto &context = *static_cast<ProcessContext *>(opaque);
    ++calls;
    for (const auto value : std::span(values, count)) {
        unrooted |= !context.stack().contains(value);
    }
    if (fault != abi::v1::Status::ok) {
        context.generated_calls().fail_service(fault);
        return 2;
    }
    const auto result = CLAUSE_construct_v1(opaque, operation, values, count, output);
    unrooted |= result == 0 && !context.stack().contains(*output);
    return result;
}

// Verify source-generated binary argument/output roots while injecting selected construction or extraction faults.
std::uint8_t injected_bits(void *opaque, std::uint8_t operation, const Word *values, std::size_t count,
                           Word *output) noexcept {
    auto &context = *static_cast<ProcessContext *>(opaque);
    ++calls;
    for (const auto value : std::span(values, count)) {
        unrooted |= !context.stack().contains(value);
    }
    const bool selected = !extraction_only || operation == static_cast<std::uint8_t>(abi::v1::BitOperation::extract);
    if (fault != abi::v1::Status::ok && selected) {
        context.generated_calls().fail_service(fault);
        return 2;
    }
    const auto result = CLAUSE_bits_v1(opaque, operation, values, count, output);
    unrooted |= result == 0 && (!context.stack().contains(output[0]) || !context.stack().contains(output[1]));
    return result;
}

// Map constructors and key lookups share rooted arguments, semantic payloads and exact infrastructure statuses.
std::uint8_t injected_map(void *opaque, std::uint8_t operation, const Word *values, std::size_t count,
                          Word *output) noexcept {
    auto &context = *static_cast<ProcessContext *>(opaque);
    ++calls;
    for (const auto value : std::span(values, count)) {
        unrooted |= !context.stack().contains(value);
    }
    if (fault != abi::v1::Status::ok) {
        context.generated_calls().fail_service(fault);
        return 3;
    }
    const auto result = CLAUSE_map_v1(opaque, operation, values, count, output);
    unrooted |= result != 3 && !context.stack().contains(*output);
    return result;
}

// Wrong shape remains a mismatch; a reached ownership/allocation fault must terminate selection.
std::uint8_t injected_inspect(void *opaque, std::uint8_t operation, Word value, std::size_t index,
                              Word *output) noexcept {
    auto &context = *static_cast<ProcessContext *>(opaque);
    ++calls;
    unrooted |= !context.stack().contains(value);
    if (fault != abi::v1::Status::ok) {
        context.generated_calls().fail_service(fault);
        return 2;
    }
    const auto result = CLAUSE_inspect_v1(opaque, operation, value, index, output);
    unrooted |= result == 0 && !context.stack().contains(*output);
    return result;
}

// This native seam changes only the service outcome and deliberately leaves success output untouched on faults.
std::uint8_t injected(void *context, std::uint8_t operation, Word left, Word right, Word *output) noexcept {
    ++calls;
    // Type tests are inline since OPT0.2B; the fixture's predicates reach the service through abs/1.
    const bool predicate = operation == static_cast<std::uint8_t>(abi::v1::ImmediateOperation::absolute);
    auto &roots = static_cast<ProcessContext *>(context)->stack();
    if (roots.depth() == 0 || (predicate && !roots.contains(left))) {
        static_cast<ProcessContext *>(context)->generated_calls().fail_service(abi::v1::Status::internal_error);
        return 2;
    }
    predicate_calls += predicate;
    if (fault != abi::v1::Status::ok && (!predicate_only || predicate)) {
        static_cast<ProcessContext *>(context)->generated_calls().fail_service(fault);
        return 2;
    }
    const auto result = CLAUSE_immediate_v1(context, operation, left, right, output);
    unrooted |= result == 0 && !roots.contains(*output);
    return result;
}

int main() {
    try {
        auto runtime = Runtime::start().value();
        require(register_answer(runtime.get()) == 0 && register_client(runtime.get()) == 0,
                "service registration failed");
        // Host Terms stay valid across calls only while no safepoint collects: a large heap never fills here.
        // A heap budget keeps inline allocation off (OPT0.3), so construction still reaches its faulted service.
        auto &context =
            *runtime->create_context(HeapOptions{.min_heap_words = std::size_t{1} << 16, .limit_bytes = 64U << 20})
                 .value();
        failures(context, "service_answer", "head");
        failures(context, "service_answer", "fallback");
        failures(context, "service_answer", "body");
        failures(context, "service_client", "nested");
        head_mismatch(context);
        registration(context);
        boolean_failures(context);
        body_matches(context);
        checked_arguments(context);
        // inspect/1 tests its tuple shape inline since OPT0.2A, so no service of it can fail.
        for (const auto name : {"construct", "heap_guard", "integer_guard", "integer_body", "float_guard", "float_body",
                                "map_guard", "map_body", "map_pattern", "bits_guard", "bits_body", "record_guard",
                                "record_body", "range_guard", "range_body"}) {
            failures(context, "service_answer", name);
        }
        bit_extractions(context);
        record_failures(context);
        heap_lifetimes(context);
        heap_budget(*runtime);
        integer_budget(context);
        require(!unrooted, "container input/output was not rooted at a reached service");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
