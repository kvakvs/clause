#include "terms.hpp"
#include <array>
#include <clause/abi/immediate_services.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace clause::runtime;
using clause::abi::v1::ImmediateOperation;
using clause::abi::v1::Status;

namespace {
// Source tests cover numeric answers; this consumer covers host ownership, canonicalization and limits.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// A checked service invocation can publish only a complete value, with no stale failure after return.
Term arithmetic(ProcessContext &context, ImmediateOperation operation, const Term &left, const Term &right) {
    GeneratedInvocation call(context.generated_calls());
    Word output = 0;
    require(CLAUSE_immediate_v1(&context, static_cast<std::uint8_t>(operation), left.word(), right.word(), &output) ==
                0,
            "integer service failed");
    return Term::from_word(output, context).value();
}

// Zero/sign variants and results at the payload boundary always use the canonical smallest representation.
Term canonical(ProcessContext &context) {
    TermFactory factory(context);
    require(factory.integer_decimal("-000")->word() == encode_integer(0).value(), "negative zero was boxed");
    require(factory.integer_decimal("+00042")->word() == encode_integer(42).value(),
            "leading zero canonicalization failed");
    const auto maximum = factory.integer(std::numeric_limits<std::int64_t>::max()).value();
    const auto minimum = factory.integer(std::numeric_limits<std::int64_t>::min()).value();
    require(maximum.integer_value() == std::numeric_limits<std::int64_t>::max(), "int64 maximum truncated");
    require(minimum.integer_value() == std::numeric_limits<std::int64_t>::min(), "int64 minimum truncated");
    const auto huge = factory.integer_decimal("1361129467683753853853498429727072845824").value();
    const auto copy = factory.integer_decimal(huge.integer_decimal().value()).value();
    require(huge.word() != copy.word() && huge.exactly_equal(copy) == true, "bignum equality used identity");
    require(huge.integer_value() == std::unexpected(TermError::out_of_range), "large host read silently narrowed");
    require(arithmetic(context, ImmediateOperation::subtract, huge, copy).kind() == TermKind::smallint,
            "arithmetic zero failed to demote");
    require(factory.integer_decimal("1x") == std::unexpected(TermError::invalid_argument), "invalid decimal accepted");
    // One digit more than 2^limit - 1 has (ERTS: 4,194,240 bits on 64-bit targets, 4,194,272 on 32-bit).
    const std::size_t digits = sizeof(Word) == 8 ? 1'262'594 : 1'262'603;
    require(factory.integer_decimal(std::string(digits, '1')) == std::unexpected(TermError::system_limit),
            "decimal past the integer limit accepted");
    require(factory.integer_decimal("-" + std::string(digits - 1, '0') + "7")->integer_value() == -7,
            "leading zeros counted against the integer limit");
    return huge;
}

// Retained bignums and error payloads remain owned across backing growth and reject foreign admission.
void ownership(Runtime &runtime, ProcessContext &context, const Term &value) {
    TermFactory factory(context);
    for (unsigned i = 0; i < 100; ++i) {
        require(factory.tuple(std::array{value, value}).has_value(), "backing growth failed");
    }
    auto &foreign = *runtime.create_context().value();
    require(Term::from_word(value.word(), foreign) == std::unexpected(TermError::wrong_owner),
            "foreign bignum admitted");
    require(value.integer_decimal() == "1361129467683753853853498429727072845824", "growth damaged magnitude");
    require(runtime.destroy_context(&context) == Status::ok, "owner teardown failed");
    require(factory.integer(1) == std::unexpected(TermError::expired_context), "expired factory admitted integer");
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        auto &context = *runtime->create_context({32, 4096 * sizeof(Word)}).value();
        const auto value = canonical(context);
        ownership(*runtime, context, value);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
