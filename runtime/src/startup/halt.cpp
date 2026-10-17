#include "../terms/integers.hpp"
#include "startup.hpp"
#include <array>
#include <clause/abi/startup.hpp>
#include <climits>
#include <cstdlib>

namespace clause::runtime::detail {
namespace {
// OTP truncates longer halt/1 slogans with badarg; keep the same bound.
inline constexpr std::size_t slogan_limit = 1023;

// Append one Unicode scalar value as UTF-8; surrogates and out-of-range values are rejected.
bool append_utf8(std::string &text, std::int64_t point) {
    if (point < 0 || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)) {
        return false;
    }
    const auto code = static_cast<std::uint32_t>(point);
    if (code < 0x80) {
        text.push_back(static_cast<char>(code));
        return true;
    }
    constexpr std::array<std::uint32_t, 4> leads{0x00, 0xC0, 0xE0, 0xF0};
    const int extra = code < 0x800 ? 1 : (code < 0x10000 ? 2 : 3);
    text.push_back(static_cast<char>(leads[static_cast<std::size_t>(extra)] | (code >> (6 * extra))));
    for (int shift = 6 * (extra - 1); shift >= 0; shift -= 6) {
        text.push_back(static_cast<char>(0x80U | ((code >> shift) & 0x3FU)));
    }
    return true;
}

// Map a halt/1 argument to its stop request; std::nullopt means badarg. `abort` never returns.
std::optional<CallFailure> halt_request(const Term &value) {
    if (value.is_integer()) {
        const auto number = integer_read(value);
        if (!number || *number < 0) {
            return std::nullopt;
        }
        // Like OTP, keep the low 31 bits of any non-negative integer as a non-negative int.
        const auto status = (*number % (Integer(INT_MAX) + 1)).convert_to<int>();
        return CallFailure{.code = CallError::halted, .halt_status = status};
    }
    if (value.is_list() && slogan_text(value)) {
        return CallFailure{.code = CallError::halted, .value = value, .halt_status = abi::v1::exit_uncaught};
    }
    if (value.atom_spelling() == "abort") {
        std::abort();
    }
    return std::nullopt;
}

// Record exactly one halt, badarg or infrastructure failure; halting always leaves the channel failed.
void halt(ProcessContext &context, Word word) {
    auto &state = context.generated_calls();
    const auto value = Term::from_word(word, context);
    if (!value) {
        state.fail_service(abi::v1::Status::invalid_argument);
        return;
    }
    const auto request = halt_request(*value);
    state.fail(
        request.value_or(CallFailure{.code = CallError::erlang_exception, .reason = abi::v1::ErrorReason::badarg}));
}
} // namespace

std::optional<std::string> slogan_text(const Term &value) {
    std::string text;
    auto current = value;
    for (std::size_t count = 0; current.is_cons(); ++count) {
        const auto head = current.head();
        const auto point = head ? head->integer_value() : TermResult<std::int64_t>{std::unexpected(head.error())};
        auto tail = current.tail();
        if (count == slogan_limit || !point || !tail || !append_utf8(text, *point)) {
            return std::nullopt;
        }
        current = *tail;
    }
    return current.is_nil() ? std::optional{text} : std::nullopt;
}
} // namespace clause::runtime::detail

std::uint8_t CLAUSE_halt_v1(void *context, clause::abi::v1::TermWord status) noexcept {
    using namespace clause;
    constexpr auto failure = static_cast<std::uint8_t>(abi::v1::ValueOutcome::failure);
    if (!context) {
        return failure;
    }
    auto &owner = *static_cast<runtime::ProcessContext *>(context);
    auto &state = owner.generated_calls();
    if (!state.active() || state.failure()) {
        return failure;
    }
    try {
        runtime::detail::halt(owner, status);
    } catch (const std::bad_alloc &) {
        state.fail_service(abi::v1::Status::out_of_memory);
    } catch (...) {
        state.fail_service(abi::v1::Status::internal_error);
    }
    return failure;
}
