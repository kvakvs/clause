#include "identities.hpp"
#include "../memory/heap_object.hpp"
#include "../memory/heap_storage.hpp"
#include "../memory/off_heap.hpp"
#include "../process/identities.hpp"
#include "term_layout.hpp"
#include <algorithm>
#include <atomic>
#include <clause/runtime/process_context.hpp>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

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

bool Term::is_pid() const { return TermTag{value_}.get_kind() == TermKind::local_pid; }

bool Term::is_reference() const { return kind() == TermKind::local_reference; }

bool Term::is_port() const { return TermTag{value_}.get_kind() == TermKind::local_port; }

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

namespace {
// The buffers of live handle references by number, so a decoded copy of a reference names its buffer again, as
// binary_to_term/1 revives OTP's magic references (step 41N). Entries of freed buffers are dropped as the table grows.
class HandleRegistry final {
  public:
    // Remember the buffer of a new handle reference.
    void add(std::uint64_t number, const std::shared_ptr<const layout::BinaryBuffer> &buffer) {
        const std::scoped_lock lock(mutex_);
        if (buffers_.size() >= sweep_at_) {
            std::erase_if(buffers_, [](const auto &entry) { return entry.second.expired(); });
            sweep_at_ = std::max<std::size_t>(64, 2 * buffers_.size());
        }
        buffers_.insert_or_assign(number, buffer);
    }

    // The buffer of a live handle reference; null for any other number.
    std::shared_ptr<const layout::BinaryBuffer> find(std::uint64_t number) const {
        const std::scoped_lock lock(mutex_);
        const auto found = buffers_.find(number);
        return found == buffers_.end() ? nullptr : found->second.lock();
    }

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::weak_ptr<const layout::BinaryBuffer>> buffers_;
    // The table size at which expired entries are dropped next.
    std::size_t sweep_at_ = 64;
};

HandleRegistry &handle_registry() {
    static HandleRegistry registry;
    return registry;
}

} // namespace

TermResult<Term> HandleAccess::publish_handle(ProcessHeap &heap, std::uint64_t number,
                                              std::shared_ptr<const ByteBuffer> buffer) {
    auto &storage = *heap.storage_;
    using layout::BoxHeader;
    using layout::RefcBinaryCell;
    if (const auto held = heap.hold_off_heap(*buffer); !held) {
        return std::unexpected(heap_error(held.error()));
    }
    constexpr auto total = sizeof(RefcBinaryCell) / sizeof(Word);
    auto reserved = heap.reserve(total);
    if (!reserved) {
        drop_off_heap(storage, *buffer);
        return std::unexpected(heap_error(reserved.error()));
    }
    auto *cell = std::construct_at(reinterpret_cast<RefcBinaryCell *>(reserved->bytes().data()));
    cell->header_ = {BoxHeader::make(BoxedKind::reference, total - 1)};
    // The number fills offset_ (and bits_ on 32-bit targets), where a reference cell keeps it.
    std::memcpy(reserved->bytes().data() + offsetof(RefcBinaryCell, offset_), &number, sizeof(number));
    auto published =
        publish(heap.storage_, *reserved, reinterpret_cast<Word>(cell) | static_cast<Word>(TermKindPrimary::boxed));
    if (published) {
        link_off_heap(storage, *cell, std::move(buffer));
    } else {
        drop_off_heap(storage, *buffer);
    }
    return published;
}

TermResult<Term> HandleAccess::make(ProcessHeap &heap, std::span<const std::byte> bytes, Finalizer finalizer) {
    auto created = heap.off_heap_buffer(bytes);
    if (!created) {
        return std::unexpected(heap_error(created.error()));
    }
    std::shared_ptr<const layout::BinaryBuffer> buffer = std::move(*created);
    if (finalizer != nullptr) {
        // The finalizer runs first; the inner reference then frees the buffer and returns its charge.
        auto *raw = buffer.get();
        buffer = std::shared_ptr<const layout::BinaryBuffer>(
            raw, [inner = buffer, finalizer](const layout::BinaryBuffer *data) mutable noexcept {
                finalizer(std::span(const_cast<std::byte *>(data->data()), data->size()));
                inner.reset();
            });
    }
    const auto number = next_reference();
    handle_registry().add(number, buffer);
    return publish_handle(heap, number, std::move(buffer));
}

TermResult<Term> HandleAccess::reference(ProcessContext &context, std::uint64_t number) {
    if (auto buffer = handle_registry().find(number)) {
        return publish_handle(context.heap(), number, std::move(buffer));
    }
    return TermFactory(context).reference_number(number);
}

TermResult<const ByteBuffer *> HandleAccess::buffer(const Term &value) noexcept {
    const auto object = TermAccess::object(value);
    if (!object) {
        return std::unexpected(object.error());
    }
    if (object->kind != TermKind::local_reference || !layout::off_heap_cell(object->words[0])) {
        return std::unexpected(TermError::wrong_type);
    }
    return reinterpret_cast<const layout::RefcBinaryCell *>(object->words.data())->buffer_.get();
}
} // namespace detail
} // namespace clause::runtime
