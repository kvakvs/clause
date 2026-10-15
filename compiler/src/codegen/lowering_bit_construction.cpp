#include "../semantic/binary_options.hpp"
#include "../semantic/capabilities.hpp"
#include "literals.hpp"
#include "lowering_roots.hpp"
#include "lowering_state.hpp"
#include <clause/abi/term.hpp>
#include <clause/abi/v1.hpp>
#include <cstdint>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <optional>
#include <span>
#include <vector>

// Inline binary construction: a binary of whole-byte integer segments of constant widths, at most an inline
// binary's 64 bytes, is written into the heap block's free words like a small tuple (OPT0.3); such segments after
// an appendable accumulator (<<Acc/binary, ...>>) go into its buffer's spare room (OPT0.11B). The bits service
// builds every other binary, and these when a value is no small integer or the heap or buffer has no room.
namespace clause::codegen {
namespace {
// An inline binary holds at most this many bytes; larger ones live off the heap.
constexpr unsigned INLINE_BINARY_BYTES = 64;

// One segment of an inline construction: its value word, width in bits and byte order.
struct ByteSegment {
    llvm::Value *value;
    unsigned width;
    bool little;
};

// The payload of a constant small integer word; none for any other value.
std::optional<std::int64_t> constant_small(llvm::Value *value) {
    const auto *constant = llvm::dyn_cast<llvm::ConstantInt>(value);
    if (!constant || (constant->getZExtValue() & abi::v1::small_integer_tag) != abi::v1::small_integer_tag) {
        return std::nullopt;
    }
    return constant->getSExtValue() >> 4;
}

// The constant width of an integer segment in whole bytes up to a word; none for any other segment.
std::optional<unsigned> byte_width(ExpressionLowering &state, const ast::BinarySegment &segment,
                                   const semantic::BinaryOptions &options) {
    const auto &syntax = *state.module.syntax;
    const auto size = segment.size ? constant_small(state.values.at(&syntax.expression(*segment.size)))
                                   : std::optional<std::int64_t>{options.size};
    if (options.type != abi::v1::BitType::integer || options.all || !size || *size <= 0 || *size > 64) {
        return std::nullopt;
    }
    const auto width = static_cast<unsigned>(*size) * options.unit;
    return width % 8 == 0 && width <= 64 ? std::optional{width} : std::nullopt;
}

// A segment written inline: an integer of a constant whole-byte width up to a word whose value may be a small
// integer; none for any other segment.
std::optional<ByteSegment> byte_segment(ExpressionLowering &state, const ast::BinarySegment &segment) {
    const auto &syntax = *state.module.syntax;
    if (std::holds_alternative<ast::StringLiteral>(syntax.expression(semantic::ungroup(syntax, segment.value)).value)) {
        return std::nullopt;
    }
    const auto options = semantic::binary_options(segment);
    const auto width = byte_width(state, segment, options);
    auto *value = state.values.at(&syntax.expression(segment.value));
    if (!width || (llvm::isa<llvm::ConstantInt>(value) && !constant_small(value))) {
        return std::nullopt;
    }
    const bool little = options.native ? state.entry.getParent()->getDataLayout().isLittleEndian() : options.little;
    return ByteSegment{value, *width, little};
}

// A segment value known at compile time, its width in bits and byte order (literal binaries).
struct ConstantSegment {
    std::uint64_t value;
    unsigned width;
    bool little;
};

// Append a value's low `width` bits as bytes in the segment's byte order.
void append_bytes(std::vector<std::uint8_t> &bytes, const ConstantSegment segment) {
    const auto count = segment.width / 8;
    for (unsigned i = 0; i < count; ++i) {
        const auto shift = 8 * (segment.little ? i : count - 1 - i);
        bytes.push_back(static_cast<std::uint8_t>(segment.value >> shift));
    }
}

// The bytes of a segment known at compile time: a small integer constant or every character of a string literal,
// at a constant whole-byte width; false for any other segment (literal binaries).
bool constant_bytes(ExpressionLowering &state, const ast::BinarySegment &segment, std::vector<std::uint8_t> &bytes) {
    const auto &syntax = *state.module.syntax;
    const auto options = semantic::binary_options(segment);
    const auto width = byte_width(state, segment, options);
    if (!width) {
        return false;
    }
    const bool little = options.native ? state.entry.getParent()->getDataLayout().isLittleEndian() : options.little;
    const auto &value = syntax.expression(semantic::ungroup(syntax, segment.value)).value;
    if (const auto *string = std::get_if<ast::StringLiteral>(&value)) {
        for (const auto character : string->value) {
            append_bytes(bytes, {character, *width, little});
        }
        return true;
    }
    const auto small = constant_small(state.values.at(&syntax.expression(segment.value)));
    if (small) {
        append_bytes(bytes, {static_cast<std::uint64_t>(*small), *width, little});
    }
    return small.has_value();
}

// Every segment written inline, at most 64 bytes in all; none when any segment needs the service.
std::optional<std::vector<ByteSegment>> byte_segments(ExpressionLowering &state,
                                                      std::span<const ast::BinarySegment> binary) {
    std::vector<ByteSegment> segments;
    unsigned bytes = 0;
    for (const auto &segment : binary) {
        const auto inline_segment = byte_segment(state, segment);
        if (!inline_segment) {
            return std::nullopt;
        }
        bytes += inline_segment->width / 8;
        segments.push_back(*inline_segment);
    }
    if (segments.empty() || bytes > INLINE_BINARY_BYTES) {
        return std::nullopt;
    }
    return segments;
}

// The bytes the segments write.
unsigned segment_bytes(const std::vector<ByteSegment> &segments) {
    unsigned bytes = 0;
    for (const auto &segment : segments) {
        bytes += segment.width / 8;
    }
    return bytes;
}

// An i1 that holds when every non-constant segment value is a small integer; constants were checked already.
llvm::Value *all_small(ExpressionLowering &state, const std::vector<ByteSegment> &segments) {
    auto &builder = state.builder;
    llvm::Value *all = builder.getTrue();
    for (const auto &segment : segments) {
        if (llvm::isa<llvm::ConstantInt>(segment.value)) {
            continue;
        }
        auto *tag = builder.CreateAnd(segment.value, llvm::ConstantInt::get(state.word, abi::v1::small_integer_tag));
        auto *small =
            builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, tag,
                                                 llvm::ConstantInt::get(state.word, abi::v1::small_integer_tag)));
        all = builder.CreateAnd(all, small);
    }
    return all;
}

// Store a small integer's low `width` bits at `at` in the segment's byte order (a little-endian target's view).
void store_segment(ExpressionLowering &state, const ByteSegment &segment, llvm::Value *at) {
    auto &builder = state.builder;
    auto *payload = builder.CreateAShr(segment.value, 4);
    if (!segment.little) {
        // The value's low bytes, most significant first: shift them to the top, then swap them to the bottom.
        auto *top = builder.CreateShl(payload, 64 - segment.width);
        payload = builder.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, top);
    }
    builder.CreateAlignedStore(builder.CreateTrunc(payload, builder.getIntNTy(segment.width)), at, llvm::Align(1));
}

// Store each segment's bytes in order from `data`.
void store_segments(ExpressionLowering &state, const std::vector<ByteSegment> &segments, llvm::Value *data) {
    unsigned offset = 0;
    for (const auto &segment : segments) {
        store_segment(state, segment, state.builder.CreateConstGEP1_64(state.builder.getInt8Ty(), data, offset));
        offset += segment.width / 8;
    }
}

// The boxed word of a cell written at `top`, marked as a term.
llvm::Value *boxed_term(ExpressionLowering &state, llvm::Value *top) {
    auto *made = state.builder.CreateOr(state.builder.CreatePtrToInt(top, state.word),
                                        llvm::ConstantInt::get(state.word, abi::v1::boxed_tag));
    mark_term(state, made);
    return made;
}

// Write the inline binary cell at `top`: header, bit length, zeroed data words, then each segment's bytes.
llvm::Value *write_binary(ExpressionLowering &state, const std::vector<ByteSegment> &segments, llvm::Value *top,
                          const unsigned bytes) {
    auto &builder = state.builder;
    const llvm::Align align(8);
    const auto data_words = (bytes + 7) / 8;
    const auto header = (static_cast<std::uint64_t>(1 + data_words) << abi::v1::header_count_shift) |
                        (static_cast<std::uint64_t>(abi::v1::HeaderKind::heap_binary) << abi::v1::header_kind_shift);
    builder.CreateAlignedStore(llvm::ConstantInt::get(state.word, header), top, align);
    builder.CreateAlignedStore(llvm::ConstantInt::get(state.word, std::uint64_t{bytes} * 8),
                               builder.CreateConstGEP1_64(state.word, top, abi::v1::heap_binary_bits_word), align);
    auto *data = builder.CreateConstGEP1_64(state.word, top, abi::v1::heap_binary_data_word);
    for (unsigned word = 0; word < data_words; ++word) {
        builder.CreateAlignedStore(llvm::ConstantInt::get(state.word, 0),
                                   builder.CreateConstGEP1_64(state.word, data, word), align);
    }
    store_segments(state, segments, data);
    return boxed_term(state, top);
}

// The accumulator of an append and the appendable buffer state under it (OPT0.11B): the refc cell word and
// address, the accumulator's offset into that cell's bits, and where in the buffer its bits start and end.
struct AppendView {
    llvm::Value *original;
    llvm::Value *cell;
    llvm::Value *offset;
    llvm::Value *start;
    llvm::Value *end;
};

// A word of a cell at `address`.
llvm::Value *cell_word(ExpressionLowering &state, llvm::Value *address, const unsigned index, const char *name) {
    return state.builder.CreateAlignedLoad(state.word, state.builder.CreateConstGEP1_64(state.word, address, index),
                                           llvm::Align(8), name);
}

// An i1 that holds when a cell header has `kind`.
llvm::Value *has_kind(ExpressionLowering &state, llvm::Value *header, const abi::v1::HeaderKind kind) {
    auto &builder = state.builder;
    auto *bits = builder.CreateAnd(builder.CreateLShr(header, abi::v1::header_kind_shift), abi::v1::header_kind_mask);
    return builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, bits,
                                                llvm::ConstantInt::get(state.word, static_cast<unsigned>(kind))));
}

// Continue in a new block when `condition` holds, else go to `fallback`.
void require(ExpressionLowering &state, llvm::Value *condition, const char *name, llvm::BasicBlock *fallback) {
    auto *next = llvm::BasicBlock::Create(state.entry.getContext(), name, &state.entry);
    state.builder.CreateCondBr(condition, next, fallback);
    state.builder.SetInsertPoint(next);
}

// Find the refc cell under an accumulator that is an off-heap binary or a sub-binary of one; anything else goes to
// `fallback`. Each cell's words are read only after its header shows a kind that has them.
AppendView append_view(ExpressionLowering &state, llvm::Value *accumulator, llvm::BasicBlock *fallback) {
    using abi::v1::HeaderKind;
    auto &builder = state.builder;
    auto *tag = builder.CreateAnd(accumulator, abi::v1::primary_mask);
    require(state,
            builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, tag,
                                                 llvm::ConstantInt::get(state.word, abi::v1::boxed_tag))),
            "append.boxed", fallback);
    auto *address = cell_address(state, accumulator, abi::v1::boxed_tag);
    auto *header = cell_word(state, address, 0, "append.header");
    auto *sub = has_kind(state, header, HeaderKind::sub_binary);
    require(state, builder.CreateOr(sub, has_kind(state, header, HeaderKind::refc_binary)), "append.binary", fallback);
    auto *original = builder.CreateSelect(sub, cell_word(state, address, abi::v1::sub_original_word, "append.original"),
                                          accumulator);
    auto *offset = builder.CreateSelect(sub, cell_word(state, address, abi::v1::sub_offset_word, "append.offset"),
                                        llvm::ConstantInt::get(state.word, 0));
    auto *bits = cell_word(state, address, abi::v1::sub_bits_word, "append.bits");
    auto *cell = cell_address(state, original, abi::v1::boxed_tag);
    require(state, has_kind(state, cell_word(state, cell, 0, "append.cell"), HeaderKind::refc_binary), "append.refc",
            fallback);
    auto *start = builder.CreateAdd(cell_word(state, cell, abi::v1::refc_offset_word, "append.base"), offset);
    return {original, cell, offset, start, builder.CreateAdd(start, bits)};
}

// An i1 that holds when the view ends at its buffer's bits in use, on byte boundaries, with room for `bits` more.
llvm::Value *appendable(ExpressionLowering &state, const AppendView &view, const unsigned bits) {
    auto &builder = state.builder;
    const auto equal = [&](llvm::Value *left, llvm::Value *right) {
        return builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, left, right));
    };
    auto *used = cell_word(state, view.cell, abi::v1::refc_append_end_word, "append.used");
    auto *limit = cell_word(state, view.cell, abi::v1::refc_append_limit_word, "append.limit");
    auto *zero = llvm::ConstantInt::get(state.word, 0);
    auto *aligned = equal(builder.CreateAnd(builder.CreateOr(view.start, view.end), 7), zero);
    auto *fits = builder.Insert(
        llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_ULE,
                              builder.CreateAdd(view.end, llvm::ConstantInt::get(state.word, bits)), limit));
    auto *ends = builder.CreateAnd(equal(used, view.end), builder.CreateNot(equal(used, zero)));
    return builder.CreateAnd(builder.CreateAnd(ends, aligned), fits);
}

// Write the segments into the buffer's spare bytes, advance its bits in use, and build the longer sub-binary.
llvm::Value *write_append(ExpressionLowering &state, const AppendView &view, const std::vector<ByteSegment> &segments,
                          llvm::Value *top, const unsigned bits) {
    auto &builder = state.builder;
    const llvm::Align align(8);
    auto *data = builder.CreateIntToPtr(cell_word(state, view.cell, abi::v1::refc_data_word, "append.data"),
                                        builder.getPtrTy(), "append.bytes");
    store_segments(state, segments, builder.CreateGEP(builder.getInt8Ty(), data, builder.CreateLShr(view.end, 3)));
    auto *end = builder.CreateAdd(view.end, llvm::ConstantInt::get(state.word, bits));
    builder.CreateAlignedStore(end, builder.CreateConstGEP1_64(state.word, view.cell, abi::v1::refc_append_end_word),
                               align);
    const auto header = (std::uint64_t{3} << abi::v1::header_count_shift) |
                        (static_cast<std::uint64_t>(abi::v1::HeaderKind::sub_binary) << abi::v1::header_kind_shift);
    builder.CreateAlignedStore(llvm::ConstantInt::get(state.word, header), top, align);
    builder.CreateAlignedStore(view.offset, builder.CreateConstGEP1_64(state.word, top, abi::v1::sub_offset_word),
                               align);
    builder.CreateAlignedStore(builder.CreateSub(end, view.start),
                               builder.CreateConstGEP1_64(state.word, top, abi::v1::sub_bits_word), align);
    builder.CreateAlignedStore(view.original, builder.CreateConstGEP1_64(state.word, top, abi::v1::sub_original_word),
                               align);
    return boxed_term(state, top);
}

// Branch to writing the cell into `room` when `inline_ok` holds, else to the service; join both results.
llvm::Value *with_service(ExpressionLowering &state, llvm::Value *inline_ok, llvm::BasicBlock *fallback,
                          const HeapRoom &room, const llvm::function_ref<llvm::Value *(llvm::Value *)> write,
                          const llvm::function_ref<llvm::Value *()> service) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    auto *direct = llvm::BasicBlock::Create(context, "binary.inline", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "binary.join", &state.entry);
    builder.CreateCondBr(inline_ok, direct, fallback);
    builder.SetInsertPoint(direct);
    auto *made = write(room.top);
    heap_commit(state, room);
    auto *direct_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    auto *serviced = service();
    auto *fallback_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {direct_end, made}, {fallback_end, serviced});
}

// Whether a segment is a whole binary, as the accumulator of <<Acc/binary, ...>> is.
bool accumulator(const ExpressionLowering &state, const ast::BinarySegment &segment) {
    const auto &syntax = *state.module.syntax;
    const auto options = semantic::binary_options(segment);
    return options.type == abi::v1::BitType::binary && options.all && !segment.size &&
           !std::holds_alternative<ast::StringLiteral>(
               syntax.expression(semantic::ungroup(syntax, segment.value)).value);
}

// <<Acc/binary, Segments...>> written into the accumulator's appendable buffer (OPT0.11B); null when a segment
// after the accumulator is not written inline.
llvm::Value *inline_append(ExpressionLowering &state, const ast::Bitstring &binary,
                           const llvm::function_ref<llvm::Value *()> service) {
    const auto segments = byte_segments(state, std::span(binary.segments).subspan(1));
    if (!segments) {
        return nullptr;
    }
    const auto bits = segment_bytes(*segments) * 8;
    auto *fallback = llvm::BasicBlock::Create(state.entry.getContext(), "binary.service", &state.entry);
    auto *accumulator = state.values.at(&state.module.syntax->expression(binary.segments.front().value));
    const auto view = append_view(state, accumulator, fallback);
    const auto room = heap_room(state, llvm::ConstantInt::get(state.word, 4));
    auto *ok = state.builder.CreateAnd(
        state.builder.CreateAnd(appendable(state, view, bits), all_small(state, *segments)), room.fits);
    return with_service(
        state, ok, fallback, room, [&](llvm::Value *top) { return write_append(state, view, *segments, top, bits); },
        service);
}

// The words of an inline binary cell holding `bytes`: header, bit length, then the bytes packed into words in the
// target's byte order.
std::vector<llvm::Constant *> binary_words(ExpressionLowering &state, const std::vector<std::uint8_t> &bytes) {
    const auto word_bytes = state.word->getBitWidth() / 8;
    const auto data_words = (bytes.size() + word_bytes - 1) / word_bytes;
    const auto header = (static_cast<std::uint64_t>(1 + data_words) << abi::v1::header_count_shift) |
                        (static_cast<std::uint64_t>(abi::v1::HeaderKind::heap_binary) << abi::v1::header_kind_shift);
    std::vector<llvm::Constant *> words{llvm::ConstantInt::get(state.word, header),
                                        llvm::ConstantInt::get(state.word, bytes.size() * 8)};
    const bool little = state.entry.getParent()->getDataLayout().isLittleEndian();
    std::vector<std::uint64_t> packed(data_words, 0);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto lane = i % word_bytes;
        packed[i / word_bytes] |= std::uint64_t{bytes[i]} << (8 * (little ? lane : word_bytes - 1 - lane));
    }
    for (const auto value : packed) {
        words.push_back(llvm::ConstantInt::get(state.word, value));
    }
    return words;
}

// A binary whose every byte is known at compile time, at most an inline binary's 64 bytes, as a cell of the module
// literal area: built once, never at run time (literal binaries); null for any other binary.
llvm::Value *literal_binary(ExpressionLowering &state, const ast::Bitstring &binary) {
    std::vector<std::uint8_t> bytes;
    for (const auto &segment : binary.segments) {
        if (!constant_bytes(state, segment, bytes) || bytes.size() > INLINE_BINARY_BYTES) {
            return nullptr;
        }
    }
    return literal_cell(*state.entry.getParent(), state.word, binary_words(state, bytes), abi::v1::boxed_tag);
}
} // namespace

llvm::Value *inline_binary(ExpressionLowering &state, const ast::Bitstring &binary,
                           const llvm::function_ref<llvm::Value *()> service) {
    if (!binary.segments.empty()) {
        if (auto *literal = literal_binary(state, binary)) {
            return literal;
        }
    }
    if (state.word->getBitWidth() != 64) {
        return nullptr;
    }
    if (binary.segments.size() > 1 && accumulator(state, binary.segments.front())) {
        return inline_append(state, binary, service);
    }
    const auto segments = byte_segments(state, binary.segments);
    if (!segments) {
        return nullptr;
    }
    const auto bytes = segment_bytes(*segments);
    const auto room = heap_room(state, llvm::ConstantInt::get(state.word, 2 + (bytes + 7) / 8));
    auto *fallback = llvm::BasicBlock::Create(state.entry.getContext(), "binary.service", &state.entry);
    return with_service(
        state, state.builder.CreateAnd(all_small(state, *segments), room.fits), fallback, room,
        [&](llvm::Value *top) { return write_binary(state, *segments, top, bytes); }, service);
}
} // namespace clause::codegen
