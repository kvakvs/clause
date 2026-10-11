#include "identities.hpp"
#include "../memory/heap_object.hpp"
#include "../memory/heap_storage.hpp"
#include "../process/identities.hpp"
#include "term_layout.hpp"
#include <atomic>
#include <clause/runtime/process_context.hpp>
#include <cstring>
#include <string>

namespace clause::runtime {
namespace {
// OTP splits a pid's data into a 28-bit number and the serial above it.
constexpr unsigned PID_NUMBER_BITS = 28;
// OTP's three reference numbers: 18 low bits, then two 32-bit words.
constexpr unsigned REFERENCE_LOW_BITS = 18;
constexpr std::uint64_t REFERENCE_WORD_MASK = 0xffff'ffff;

// Never reuse a reference number within the program run, across runtimes too.
std::uint64_t next_reference() noexcept {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

// Keep allocation failures distinct from the configured backing ceiling.
TermError heap_error(HeapError error) {
    return error == HeapError::out_of_memory ? TermError::out_of_memory : TermError::resource_limit;
}

// Order two unsigned numbers.
int numbers(std::uint64_t left, std::uint64_t right) {
    if (left == right) {
        return 0;
    }
    return left < right ? -1 : 1;
}
} // namespace

TermResult<Term> TermFactory::pid(const ProcessIdentity &identity) {
    const auto owner = heap();
    if (!owner) {
        return std::unexpected(owner.error());
    }
    if (identity.runtime_ != (*owner)->owner_.identity().runtime_) {
        return std::unexpected(TermError::wrong_owner);
    }
    return Term::from_word(detail::pid_word(static_cast<Word>(identity.serial_)), (*owner)->owner_);
}

TermResult<Term> TermFactory::port(const PortIdentity &identity) {
    const auto owner = heap();
    if (!owner) {
        return std::unexpected(owner.error());
    }
    return Term::from_word(detail::port_word(identity.number_), (*owner)->owner_);
}

TermResult<PortIdentity> Term::port_value() const {
    if (!is_port()) {
        return std::unexpected(TermError::wrong_type);
    }
    return PortIdentity(detail::port_number(value_));
}

TermResult<Term> TermFactory::make_reference() { return reference(ReferenceIdentity(next_reference())); }

TermResult<Term> TermFactory::reference_number(std::uint64_t number) { return reference(ReferenceIdentity(number)); }

TermResult<Term> TermFactory::reference(const ReferenceIdentity &identity) {
    const auto owner = heap();
    if (!owner) {
        return std::unexpected(owner.error());
    }
    constexpr auto payload = detail::layout::reference_payload_words(sizeof(Word));
    auto reserved = (*owner)->reserve(1 + payload);
    if (!reserved) {
        return std::unexpected(heap_error(reserved.error()));
    }
    auto *cell = ::new (reserved->bytes().data()) detail::layout::ReferenceCell{};
    cell->header_.value_ = detail::layout::BoxHeader::make(BoxedKind::reference, payload);
    std::memcpy(cell->number_.data(), &identity.number_, sizeof(identity.number_));
    return detail::publish((*owner)->storage_, *reserved,
                           reinterpret_cast<Word>(cell) | static_cast<Word>(TermKindPrimary::boxed));
}

TermResult<ReferenceIdentity> Term::reference_value() const {
    return detail::reference_number(*this).transform([](std::uint64_t number) { return ReferenceIdentity(number); });
}

bool Term::is_pid() const { return !heap_ && !atom_ && TermTag{value_}.get_kind() == TermKind::local_pid; }

bool Term::is_reference() const { return kind() == TermKind::local_reference; }

bool Term::is_port() const { return !heap_ && !atom_ && TermTag{value_}.get_kind() == TermKind::local_port; }

namespace detail {
TermResult<std::uint64_t> reference_number(const Term &value) noexcept {
    const auto object = TermAccess::object(value);
    if (!object) {
        return std::unexpected(object.error());
    }
    if (object->kind != TermKind::local_reference) {
        return std::unexpected(TermError::wrong_type);
    }
    std::uint64_t number = 0;
    std::memcpy(&number, object->words.subspan(1).data(), sizeof(number));
    return number;
}

TermResult<int> identity_order(const Term &left, const Term &right) noexcept {
    if (left.is_pid() || left.is_port()) {
        // Pid and port words keep their number above the same four tag bits.
        return numbers(pid_number(left.word()), pid_number(right.word()));
    }
    const auto lhs = reference_number(left);
    const auto rhs = reference_number(right);
    if (!lhs || !rhs) {
        return std::unexpected(lhs ? rhs.error() : lhs.error());
    }
    return numbers(*lhs, *rhs);
}

TermResult<void> print_identity(const Term &value, TextOutput &out) {
    if (value.is_port()) {
        out.append("#Port<0." + std::to_string(port_number(value.word())) + ">");
        return {};
    }
    if (value.is_pid()) {
        const std::uint64_t number = pid_number(value.word());
        const auto serial = number >> PID_NUMBER_BITS;
        const auto low = number & ((std::uint64_t{1} << PID_NUMBER_BITS) - 1);
        out.append("<0." + std::to_string(low) + "." + std::to_string(serial) + ">");
        return {};
    }
    const auto number = reference_number(value);
    if (!number) {
        return std::unexpected(number.error());
    }
    const auto low = *number & ((std::uint64_t{1} << REFERENCE_LOW_BITS) - 1);
    const auto middle = (*number >> REFERENCE_LOW_BITS) & REFERENCE_WORD_MASK;
    const auto high = *number >> (REFERENCE_LOW_BITS + 32);
    out.append("#Ref<0." + std::to_string(high) + "." + std::to_string(middle) + "." + std::to_string(low) + ">");
    return {};
}
} // namespace detail
} // namespace clause::runtime
