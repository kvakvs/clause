#include "terms.hpp"
#include <array>
#include <clause/abi/containers.hpp>
#include <clause/abi/equality.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace clause::runtime;
using clause::abi::v1::Status;

namespace {
// Public host invariants complement source execution for ownership and forged-word cases.
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

// Keep independently constructed cells equal by value across stable backing growth.
Term values(ProcessContext &context) {
    TermFactory factory(context);
    const auto number = Term::from_word(encode_integer(42).value()).value();
    const auto atom = factory.atom("nested").value();
    const std::array elements{number, atom, factory.nil().value()};
    const auto proper = factory.list(elements).value();
    const auto improper = factory.list(elements, atom).value();
    require(proper.list_length() == 3 && proper.head()->integer_value() == 42, "proper list access failed");
    require(improper.list_length() == std::unexpected(TermError::improper_list), "improper tail accepted");
    require(improper.is_list() && improper.is_proper_list() == false, "list category confused with properness");
    const std::array nested{proper, improper, atom};
    const auto result = factory.tuple(nested).value();
    require(result.tuple_size() == 3 && result.tuple_element(1)->exactly_equal(improper) == true,
            "nested tuple access changed ownership");
    const auto separate =
        factory.tuple(std::array{factory.list(elements).value(), factory.list(elements, atom).value(), atom}).value();
    require(result.word() != separate.word() && result.exactly_equal(separate) == true,
            "pointer equality used for tuples");
    for (unsigned i = 0; i < 100; ++i) {
        require(factory.tuple(elements).has_value(), "heap growth failed");
    }
    require(result.tuple_element(0)->tail()->head()->atom_spelling() == "nested", "growth damaged retained extraction");
    require(result.tuple_element(3) == std::unexpected(TermError::out_of_range), "tuple bound not checked");
    require(number.head() == std::unexpected(TermError::wrong_type), "wrong shape was dereferenced");
    return result;
}

// A badmatch owns the graph before scopes clear, independently of later allocations and calls.
void failure_payload(ProcessContext &context, const Term &value) {
    std::optional<CallFailure> retained;
    {
        GeneratedInvocation call(context.generated_calls());
        require(CLAUSE_raise_v2(&context, clause::abi::v1::ErrorReason::badmatch, value.word()) == 0,
                "compound error rejected");
        retained = context.generated_calls().failure();
    }
    require(retained && retained->value && !context.generated_calls().failure(),
            "error payload/channel ownership failed");
    values(context);
    require(retained->value->exactly_equal(value) == true, "later allocation damaged retained error");
}

// Comparison has no work cap, as in OTP: two separately built shared graphs of 2^20 leaf pairs compare equal, and
// a different innermost leaf still makes them unequal.
void unbounded_comparison(ProcessContext &context) {
    TermFactory factory(context);
    auto left = factory.nil().value();
    auto right = factory.nil().value();
    auto other = factory.atom("other").value();
    for (unsigned i = 0; i < 20; ++i) {
        left = factory.tuple(std::array{left, left}).value();
        right = factory.tuple(std::array{right, right}).value();
        other = factory.tuple(std::array{right, other}).value();
    }
    require(left.exactly_equal(right) == true, "shared graph comparison failed");
    require(left.exactly_equal(other) == false, "different leaf compared equal");
    GeneratedInvocation invocation(context.generated_calls());
    require(CLAUSE_exact_v1(&context, left.word(), right.word()) ==
                static_cast<std::uint8_t>(clause::abi::v1::Equality::equal),
            "generated comparison stopped on a large graph");
}

// Lists have no length cap (plan 11 step 27B): one element past the former 1,000,000 cap builds through the
// factory and the construction service, measures, reverses and compares.
void long_lists(Runtime &runtime) {
    constexpr std::size_t length = 1'000'001;
    auto &context = *runtime.create_context().value();
    TermFactory factory(context);
    const std::vector elements(length, factory.integer(7).value());
    const auto built = factory.list(elements).value();
    require(built.list_length() == length, "long list length failed");
    std::vector<Word> words(length + 1, factory.integer(7)->word());
    words.back() = factory.nil()->word();
    {
        // The invocation scope ends before its context is destroyed.
        GeneratedInvocation invocation(context.generated_calls());
        Word constructed = 0;
        require(CLAUSE_construct_v1(&context, static_cast<std::uint8_t>(clause::abi::v1::ContainerConstruction::list),
                                    words.data(), words.size(), &constructed) == 0,
                "construction service refused a long list");
        const std::array reverse{constructed, factory.nil()->word()};
        Word reversed = 0;
        require(CLAUSE_construct_v1(&context,
                                    static_cast<std::uint8_t>(clause::abi::v1::ContainerConstruction::reverse),
                                    reverse.data(), reverse.size(), &reversed) == 0,
                "reverse service refused a long list");
        require(Term::from_word(reversed, context)->exactly_equal(built) == true, "long lists compared unequal");
    }
    require(runtime.destroy_context(&context) == Status::ok, "long list context teardown failed");
}

// Tuples hold up to OTP's 16,777,215 elements (plan 11 step 27C); the construction service refuses one more with
// resource_limit before allocating.
void tuple_arity(Runtime &runtime) {
    auto &context = *runtime.create_context().value();
    std::vector<Word> words(MAX_TUPLE_ARITY + 1, encode_integer(5).value());
    const auto construct = [&](std::size_t count, Word &output) {
        GeneratedInvocation invocation(context.generated_calls());
        const auto outcome =
            CLAUSE_construct_v1(&context, static_cast<std::uint8_t>(clause::abi::v1::ContainerConstruction::tuple),
                                words.data(), count, &output);
        const auto failure = context.generated_calls().failure();
        return outcome == 0 ? Status::ok : failure ? failure->status.value_or(Status::internal_error) : Status::busy;
    };
    Word refused = 0;
    require(construct(words.size(), refused) == Status::resource_limit && context.heap().used_words() == 0,
            "tuple above the arity limit accepted");
    Word largest = 0;
    require(construct(MAX_TUPLE_ARITY, largest) == Status::ok, "tuple at the arity limit refused");
    const auto tuple = Term::from_word(largest, context).value();
    require(tuple.tuple_size() == MAX_TUPLE_ARITY && tuple.tuple_element(MAX_TUPLE_ARITY - 1)->integer_value() == 5,
            "largest tuple lost fields");
    require(runtime.destroy_context(&context) == Status::ok, "tuple context teardown failed");
}

// Foreign or out-of-heap words never reach a header read, though add copies a foreign graph; an expired factory
// allocates nothing.
void ownership(Runtime &runtime, ProcessContext &context, const Term &value) {
    auto &other = *runtime.create_context().value();
    require(Term::from_word(value.word(), other) == std::unexpected(TermError::wrong_owner), "foreign heap admitted");
    require(value.copy_to(other.heap())->exactly_equal(value) == true, "foreign graph not copied");
    require(Term::from_word(Word{1}, context) == std::unexpected(TermError::wrong_owner), "forged cons admitted");
    TermFactory factory(context);
    require(runtime.destroy_context(&context) == Status::ok, "context removal failed");
    require(factory.tuple({}) == std::unexpected(TermError::expired_context), "expired factory accessed");
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        auto &context = *runtime->create_context({32, 4096 * sizeof(Word)}).value();
        const auto value = values(context);
        failure_payload(context, value);
        unbounded_comparison(context);
        long_lists(*runtime);
        tuple_arity(*runtime);
        ownership(*runtime, context, value);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
