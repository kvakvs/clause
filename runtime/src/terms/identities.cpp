#include "identities.hpp"
#include "../memory/heap_object.hpp"
#include "../memory/heap_storage.hpp"
#include "../memory/off_heap.hpp"
#include "../process/identities.hpp"
#include "atom_table.hpp"
#include "term_layout.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <clause/runtime/atom_names.hpp>
#include <clause/runtime/process_context.hpp>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
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

bool Term::is_external_pid() const { return kind() == TermKind::external_pid; }

bool Term::is_external_port() const { return kind() == TermKind::external_port; }

bool Term::is_external_reference() const { return kind() == TermKind::external_reference; }

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

bool is_identity(const Term &value) noexcept {
    if (value.is_pid() || value.is_port() || value.is_reference()) {
        return true;
    }
    const auto kind = value.kind();
    return kind == TermKind::external_pid || kind == TermKind::external_port || kind == TermKind::external_reference;
}

namespace {
// The kind of identity a term is, telling pids, ports and references apart whichever node they belong to.
enum class Family : std::uint8_t { pid, port, reference };

// An identity as order and printing see it: its node and numbers, the same parts for every node (plan 30 step DIST1).
struct Parts {
    Family family = Family::pid;
    // The node's name and creation; the local node's for an identity of this node.
    std::string_view node;
    std::uint32_t creation = 0;
    // The node name atom's number, which OTP prints as the node part; 0 for this node.
    std::uint64_t channel = 0;
    // As ExternalIdentity::numbers: a pid's number and serial, a port's number low word first, a reference's words
    // low first.
    std::array<std::uint32_t, layout::MAX_EXTERNAL_NUMBERS> numbers{};
    std::size_t count = 0;
};

// The family of an identity of another node.
Family family_of(TermKind kind) noexcept {
    if (kind == TermKind::external_pid) {
        return Family::pid;
    }
    return kind == TermKind::external_port ? Family::port : Family::reference;
}

// The parts of a pid, port or reference of this node: its number split as OTP splits it.
TermResult<Parts> local_parts(const Term &value) {
    Parts parts;
    parts.node = this_node_name();
    parts.creation = this_node_creation();
    if (value.is_pid()) {
        const std::uint64_t number = pid_number(value.word());
        parts.numbers = {static_cast<std::uint32_t>(number & ((std::uint64_t{1} << PID_NUMBER_BITS) - 1)),
                         static_cast<std::uint32_t>(number >> PID_NUMBER_BITS)};
        parts.count = 2;
        return parts;
    }
    if (value.is_port()) {
        const std::uint64_t number = port_number(value.word());
        parts.family = Family::port;
        parts.numbers = {static_cast<std::uint32_t>(number), static_cast<std::uint32_t>(number >> 32U)};
        parts.count = 2;
        return parts;
    }
    const auto number = reference_number(value);
    if (!number) {
        return std::unexpected(number.error());
    }
    parts.family = Family::reference;
    parts.numbers = {static_cast<std::uint32_t>(*number & ((std::uint64_t{1} << REFERENCE_LOW_BITS) - 1)),
                     static_cast<std::uint32_t>((*number >> REFERENCE_LOW_BITS) & REFERENCE_WORD_MASK),
                     static_cast<std::uint32_t>(*number >> (REFERENCE_LOW_BITS + 32))};
    parts.count = 3;
    return parts;
}

// The parts of any identity: OTP prints this node's name, under any creation, with node part 0.
TermResult<Parts> parts_of(const Term &value) {
    const auto external = ExternalAccess::read(value);
    if (!external) {
        return local_parts(value);
    }
    const auto spelling = atom_table::spelling(external->node);
    if (!spelling) {
        return std::unexpected(TermError::expired_context);
    }
    Parts parts;
    parts.family = family_of(external->kind);
    parts.node = *spelling;
    parts.creation = external->creation;
    parts.numbers = external->numbers;
    parts.count = external->count;
    parts.channel = *spelling == this_node_name() ? 0 : external->node >> abi::v1::atom_tag_bits;
    return parts;
}

// Order two nodes as OTP does: by name, then by creation.
int node_order(const Parts &left, const Parts &right) {
    if (left.node != right.node) {
        return left.node < right.node ? -1 : 1;
    }
    return numbers(left.creation, right.creation);
}

// Order two references' words from the most significant, a missing word counting as zero, as OTP compares references
// of different lengths.
int words_order(const Parts &left, const Parts &right) {
    for (auto index = std::max(left.count, right.count); index-- > 0;) {
        const auto lhs = index < left.count ? left.numbers.at(index) : 0;
        const auto rhs = index < right.count ? right.numbers.at(index) : 0;
        if (lhs != rhs) {
            return numbers(lhs, rhs);
        }
    }
    return 0;
}

// Order two identities of one family: pids by serial, number and then node; ports and references by node first.
int parts_order(const Parts &left, const Parts &right) {
    if (left.family == Family::pid) {
        if (const auto serial = numbers(left.numbers[1], right.numbers[1]); serial != 0) {
            return serial;
        }
        if (const auto number = numbers(left.numbers[0], right.numbers[0]); number != 0) {
            return number;
        }
        return node_order(left, right);
    }
    if (const auto node = node_order(left, right); node != 0) {
        return node;
    }
    return words_order(left, right);
}

// Whether both terms are identities of this node, which order by their numbers alone.
bool both_local(const Term &left, const Term &right) noexcept {
    const auto local = [](const Term &value) { return value.is_pid() || value.is_port() || value.is_reference(); };
    return local(left) && local(right);
}

// Order two identities of this node of one kind by number.
TermResult<int> local_order(const Term &left, const Term &right) noexcept {
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
} // namespace

TermResult<int> identity_order(const Term &left, const Term &right) noexcept {
    if (both_local(left, right)) {
        return local_order(left, right);
    }
    try {
        const auto lhs = parts_of(left);
        const auto rhs = parts_of(right);
        if (!lhs || !rhs) {
            return std::unexpected(lhs ? rhs.error() : lhs.error());
        }
        return parts_order(*lhs, *rhs);
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
}

TermResult<void> print_identity(const Term &value, TextOutput &out) {
    const auto parts = parts_of(value);
    if (!parts) {
        return std::unexpected(parts.error());
    }
    const auto &numbers = parts->numbers;
    auto text = std::to_string(parts->channel);
    switch (parts->family) {
    case Family::pid:
        out.append("<" + text + "." + std::to_string(numbers[0]) + "." + std::to_string(numbers[1]) + ">");
        break;
    case Family::port:
        out.append("#Port<" + text + "." + std::to_string(std::uint64_t{numbers[1]} << 32U | numbers[0]) + ">");
        break;
    case Family::reference:
        for (auto index = parts->count; index-- > 0;) {
            text += "." + std::to_string(numbers.at(index));
        }
        out.append("#Ref<" + text + ">");
        break;
    }
    return {};
}

namespace {
// This node's name, an atom word or 0 for nonode@nohost, and its creation (plan 30 step DIST1); erlang:setnode/2
// sets them (DIST2a). The creation is stored before the name and cleared after it.
std::atomic<Word> this_node_word{0};
std::atomic<std::uint32_t> this_node_creation_value{0};
// The name of a node that is not distributed.
constexpr std::string_view UNDISTRIBUTED_NODE = "nonode@nohost";
} // namespace

std::string_view this_node_name() noexcept {
    const auto word = this_node_word.load(std::memory_order_acquire);
    return word == 0 ? UNDISTRIBUTED_NODE : atom_table::spelling(word).value_or(UNDISTRIBUTED_NODE);
}

std::uint32_t this_node_creation() noexcept { return this_node_creation_value.load(std::memory_order_acquire); }

TermResult<Term> this_node_atom(ProcessContext &context) {
    if (this_node_word.load(std::memory_order_acquire) == 0) {
        return TermFactory(context).atom("nonode@nohost"_atom);
    }
    return TermFactory(context).atom(AtomName(this_node_name()));
}

bool this_node_named() noexcept { return this_node_word.load(std::memory_order_acquire) != 0; }

bool is_this_node(const Term &node) noexcept {
    return node.is_atom() && node.atom_spelling_unchecked() == this_node_name();
}

void name_this_node(const Term &node, std::uint32_t creation) noexcept {
    this_node_creation_value.store(creation, std::memory_order_release);
    this_node_word.store(node.word(), std::memory_order_release);
}

void unname_this_node() noexcept {
    this_node_word.store(0, std::memory_order_release);
    this_node_creation_value.store(0, std::memory_order_release);
}

TermResult<Term> ExternalAccess::make(ProcessHeap &heap, const ExternalIdentity &identity) {
    using layout::BoxHeader;
    static constexpr std::array<std::pair<TermKind, BoxedKind>, 3> KINDS{
        {{TermKind::external_pid, BoxedKind::ext_pid},
         {TermKind::external_port, BoxedKind::ext_port},
         {TermKind::external_reference, BoxedKind::ext_ref}}};
    const auto kind = std::ranges::find(KINDS, identity.kind, &std::pair<TermKind, BoxedKind>::first);
    if (kind == KINDS.end() || identity.count > layout::MAX_EXTERNAL_NUMBERS) {
        return std::unexpected(TermError::invalid_encoding);
    }
    const auto payload = layout::external_payload_words(identity.count);
    auto reserved = heap.reserve(1 + payload);
    if (!reserved) {
        return std::unexpected(heap_error(reserved.error()));
    }
    auto *words = reinterpret_cast<Word *>(reserved->bytes().data());
    words[0] = BoxHeader::make(kind->second, payload);
    words[1] = identity.creation;
    for (std::size_t index = 0; index < identity.count; ++index) {
        words[2 + index] = identity.numbers.at(index);
    }
    // The node atom is the last payload word.
    words[payload] = identity.node;
    return publish(heap.storage_, *reserved, reinterpret_cast<Word>(words) | static_cast<Word>(TermKindPrimary::boxed));
}

TermResult<ExternalIdentity> ExternalAccess::read(const Term &value) noexcept {
    const auto object = TermAccess::object(value);
    if (!object) {
        return std::unexpected(object.error());
    }
    if (object->kind != TermKind::external_pid && object->kind != TermKind::external_port &&
        object->kind != TermKind::external_reference) {
        return std::unexpected(TermError::wrong_type);
    }
    const auto words = object->words;
    ExternalIdentity identity;
    identity.kind = object->kind;
    identity.node = words.back();
    identity.creation = static_cast<std::uint32_t>(words[1]);
    identity.count = words.size() - 3;
    for (std::size_t index = 0; index < identity.count; ++index) {
        identity.numbers.at(index) = static_cast<std::uint32_t>(words[2 + index]);
    }
    return identity;
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
