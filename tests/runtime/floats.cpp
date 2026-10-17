#include "terms.hpp"
#include <array>
#include <bit>
#include <clause/abi/floats.hpp>
#include <clause/runtime/runtime.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace clause::runtime;

// Retain host-invariant checks in optimized consumer builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Host admission rejects unsupported bits, preserves independent exact values, and checks owner lifetimes.
void ownership() {
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context().value();
    TermFactory factory(context);
    const auto value = factory.floating(-0.0).value();
    require(std::signbit(value.float_value().value()), "lost negative zero");
    const auto second = factory.floating(-0.0).value();
    require(value.word() != second.word() && value.exactly_equal(second).value(), "float equality used identity");
    require(!value.exactly_equal(factory.floating(0.0).value()).value(), "exact signed zero collapsed");
    for (double invalid : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        const auto used = context.heap().used_words();
        require(factory.floating(invalid) == std::unexpected(TermError::invalid_argument), "nonfinite float admitted");
        require(context.heap().used_words() == used, "invalid float reserved storage");
    }
    auto &foreign = *runtime->create_context().value();
    require(Term::from_word(value.word(), foreign) == std::unexpected(TermError::wrong_owner),
            "foreign float admitted");
    for (unsigned i = 0; i < 100; ++i) {
        require(factory.tuple(std::array{value, second}).has_value(), "growth failed");
    }
    require(std::signbit(value.float_value().value()), "growth damaged retained float");
    require(runtime->destroy_context(&context) == clause::abi::v1::Status::ok, "teardown failed");
    require(factory.floating(1) == std::unexpected(TermError::expired_context), "expired factory used");
}

// Malformed literal service inputs leave output untouched, and a later independent call recovers.
void service() {
    auto runtime = Runtime::start().value();
    auto &context = *runtime->create_context().value();
    const std::array<char, 8> one{0x3f, static_cast<char>(0xf0), 0, 0, 0, 0, 0, 0};
    Word output = 123;
    {
        GeneratedInvocation scope(context.generated_calls());
        require(CLAUSE_float_v1(&context, one.data(), 7, &output) == 2 && output == 123,
                "invalid literal published output");
        require(context.generated_calls().failure().has_value(), "literal failure lost");
    }
    {
        GeneratedInvocation scope(context.generated_calls());
        require(CLAUSE_float_v1(&context, one.data(), 8, &output) == 0, "literal recovery failed");
        require(Term::from_word(output, context)->float_value() == 1.0, "literal byte order wrong");
    }
}
} // namespace

// Source oracles cover numeric semantics; this consumer covers ownership and service invariants.
int main() {
    try {
        ownership();
        service();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
