#pragma once
#include "integers.hpp"
#include <boost/container/small_vector.hpp>
#include <clause/abi/bits.hpp>
#include <optional>
#include <process_heap.hpp>

namespace clause::runtime::detail {

struct BitView {
    // Borrow only while the checked source Term retains the live owning heap cell.
    std::span<const std::byte> bytes;
    std::size_t offset;
    std::size_t length;
};

struct BitRange {
    // Select a view's bits within a shared buffer.
    std::size_t offset;
    std::size_t length;
};

struct BitWriter;

struct BitAccess {
    // Copy packed MSB-first input, zeroing unused low bits: inline up to 64 bytes, else a new shared buffer.
    static TermResult<Term> make(ProcessHeap &heap, std::span<const std::byte> bytes, std::size_t count);
    // Share an off-heap source's buffer for a checked view; slices of heap binaries are copied.
    static TermResult<Term> slice(ProcessHeap &heap, const Term &source, std::size_t offset, std::size_t count);
    // The binary `accumulator` followed by `rest` (OPT0.6): written in place when the accumulator is this process's
    // appendable view ending at its buffer's used bits and the spare bytes suffice; none when it is not such a view.
    static std::optional<TermResult<Term>> append(ProcessHeap &heap, const Term &accumulator, const BitWriter &rest);
    // Publish constructed bits as an appendable binary: a buffer with spare room, registered for appends (OPT0.6).
    static TermResult<Term> appendable(ProcessHeap &heap, std::span<const std::byte> bytes, std::size_t count);

  private:
    // Publish an inline binary: header, bit length, then the data bytes rounded up to words.
    static TermResult<Term> heap_binary(ProcessHeap &heap, std::span<const std::byte> bytes, std::size_t count);
    // Copy the bits into a new buffer charged to this process, then publish a view of all of it.
    static TermResult<Term> shared_binary(ProcessHeap &heap, std::span<const std::byte> bytes, std::size_t count);
    // Publish an off-heap view; only a published cell receives the buffer reference and joins the list.
    static TermResult<Term> refc_binary(ProcessHeap &heap, std::shared_ptr<const std::vector<std::byte>> buffer,
                                        BitRange range);
};

// Establish kind and lifetime before accessing any bit buffer or cursor.
TermResult<BitView> bit_view(const Term &term);
// Read one proved in-range bit in logical MSB-first order.
bool bit_at(const BitView &view, std::size_t index) noexcept;
// Compare logical sequences independently of backing identity, offsets and padding.
TermResult<int> bit_order(const Term &left, const Term &right);

struct BitWriter {
    // Stage packed bytes privately until every segment has succeeded; short constructions need no allocation
    // (OPT0.9).
    boost::container::small_vector<std::byte, 64> bytes;
    std::size_t length = 0;
    // Append bounded bits with checked growth; no heap Term is published during construction.
    TermResult<void> append(const BitView &source);
    // Emit low integer bits using explicit endian groups, including partial final groups.
    TermResult<void> integer(const Integer &value, std::size_t count, bool little);
};

struct BitSegment {
    // Carry checked metadata and evaluated size/value independently of source syntax.
    abi::v1::BitType type;
    bool little;
    bool signed_value;
    bool all;
    bool empty;
    std::size_t unit;
    Term size;
    Term value;
};

struct BitExtract {
    // Success owns the extracted term and advances the cursor only after the read completes.
    Term value;
    std::size_t cursor;
};

// Validate bounded nonnegative size arithmetic before reading or growing a segment.
TermResult<std::size_t> bit_width(const BitSegment &segment, std::size_t remaining);
// Construct one numeric/binary/UTF segment into a private writer.
TermResult<void> bit_construct(BitWriter &writer, const BitSegment &segment);
// Extract checked numeric/binary/UTF segments, separating mismatch from infrastructure failures.
TermResult<BitExtract> bit_extract(ProcessContext &context, const Term &source, std::size_t cursor,
                                   const BitSegment &segment);
// Encode/decode Unicode scalars through the same numeric endian operations as ordinary fields.
TermResult<void> bit_utf_construct(BitWriter &writer, const BitSegment &segment);
TermResult<BitExtract> bit_utf_extract(ProcessContext &context, const BitView &view, std::size_t cursor,
                                       const BitSegment &segment);
// Encode and extract supported IEEE widths without confusing nonfinite bit patterns with runtime floats.
TermResult<Integer> bit_float_bits(const Term &value, std::size_t width);
TermResult<Term> bit_read_float(ProcessContext &context, const Integer &bits, std::size_t width);
// Decode proved bounded bits to an owned arbitrary integer without narrowing.
Integer bit_integer(const BitView &view, bool little);
} // namespace clause::runtime::detail
