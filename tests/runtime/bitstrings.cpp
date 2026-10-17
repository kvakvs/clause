#include "terms.hpp"
#include <array>
#include <clause/abi/bits.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <stdexcept>

namespace {
using namespace clause::runtime;
using Op = clause::abi::v1::BitOperation;
using Status = clause::abi::v1::Status;

// Keep lifetime, publication and malformed-input checks active in optimized consumers.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Public checked extraction retains a tail; no private buffer addresses enter this test.
Term slice(ProcessContext &context, const Term &value, std::size_t offset, std::size_t count) {
    TermFactory factory(context);
    const auto descriptor = factory.integer(256 + 2).value();
    const std::array input{value.word(), factory.integer(static_cast<std::int64_t>(offset))->word(), descriptor.word(),
                           factory.integer(static_cast<std::int64_t>(count))->word(), factory.integer(0)->word()};
    std::array<Word, 2> output{};
    GeneratedInvocation call(context.generated_calls());
    require(CLAUSE_bits_v1(&context, static_cast<std::uint8_t>(Op::extract), input.data(), input.size(),
                           output.data()) == 0,
            "checked slice failed");
    require(decode_integer(output[1]) == offset + count, "slice cursor changed incorrectly");
    return Term::from_word(output[0], context).value();
}

// Inline padding and nonaligned shared views compare by value, and survive growth.
void ownership() {
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context().value();
    TermFactory factory(context);
    const std::array data{std::byte{0xbf}};
    const auto small = factory.bitstring(data, 3).value();
    require(small.bitstring_bytes() == std::vector{std::byte{0xa0}}, "partial padding not normalized");
    require(small.is_bitstring() && !small.is_binary(), "partial byte classified as binary");
    const auto bytes = std::vector(100, std::byte{0xb6});
    auto original = factory.binary(bytes).value();
    const auto first = slice(context, original, 3, 790);
    const auto second = slice(context, first, 5, 784);
    const auto independent = factory.binary(std::vector(98, std::byte{0xb6})).value();
    require(second.is_binary() && second.exactly_equal(independent) == true, "nonaligned view value mismatch");
    original = Term{};
    auto &foreign = *runtime->create_context().value();
    require(Term::from_word(first.word(), foreign) == std::unexpected(TermError::wrong_owner),
            "foreign slice admitted");
    for (unsigned i = 0; i < 100; ++i) {
        require(factory.binary(bytes).has_value(), "later buffer allocation failed");
    }
    require(second.exactly_equal(independent) == true && first.bit_size() == 790, "retained tail damaged by growth");
    require(runtime->destroy_context(&context) == Status::ok, "binary context teardown failed");
    require(factory.binary({}) == std::unexpected(TermError::expired_context), "expired factory allocated bits");
}

// Malformed arrays and forged terms cannot publish either output; later calls have a clean failure channel.
void malformed() {
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context().value();
    for (const auto operation : {Op::make, Op::extract, Op::part}) {
        GeneratedInvocation call(context.generated_calls());
        const Word invalid = 0;
        std::array<Word, 2> output{123, 456};
        require(CLAUSE_bits_v1(&context, static_cast<std::uint8_t>(operation), &invalid, 1, output.data()) == 2,
                "malformed binary service admitted");
        require(output == std::array<Word, 2>{123, 456} && context.generated_calls().failure(),
                "fault published output");
    }
    std::array<Word, 2> output{123, 456};
    GeneratedInvocation retry(context.generated_calls());
    require(CLAUSE_bits_v1(&context, static_cast<std::uint8_t>(Op::make), nullptr, 0, output.data()) == 0,
            "malformed call poisoned retry");
    require(Term::from_word(output[0], context)->bit_size() == 0 && decode_integer(output[1]) == 0,
            "empty binary result incorrect");
}

// External backing counts against the process budget, and invalid input never publishes a partial object.
void budgets() {
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context({32, 64 * sizeof(Word)}).value();
    TermFactory factory(context);
    require(factory.bitstring({}, 1) == std::unexpected(TermError::invalid_argument), "short host input accepted");
    const auto large = std::vector(1024, std::byte{0});
    require(factory.binary(large) == std::unexpected(TermError::resource_limit), "shared backing escaped heap budget");
    require(context.heap().used_words() == 0 && context.heap().capacity_words() == 0, "failed binary retained storage");
    require(factory.binary({})->bit_size() == 0, "budget rejection poisoned small retry");
}
} // namespace

// Source differential tests own segment semantics; this consumer verifies inaccessible ownership invariants.
int main() {
    try {
        ownership();
        malformed();
        budgets();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
