#include "../semantic/binary_options.hpp"
#include "../semantic/capabilities.hpp"
#include "lowering_roots.hpp"
#include "lowering_state.hpp"
#include <clause/abi/term.hpp>
#include <clause/abi/v1.hpp>
#include <cstdint>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <optional>
#include <vector>

// Inline binary construction: a binary of whole-byte integer segments of constant widths, at most an inline
// binary's 64 bytes, is written into the heap block's free words like a small tuple (OPT0.3); the bits service
// builds every other binary, and this one when a value is no small integer or the heap has no room.
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

// Every segment of a binary written inline, at most 64 bytes in all; none when any segment needs the service.
std::optional<std::vector<ByteSegment>> byte_segments(ExpressionLowering &state, const ast::Bitstring &binary) {
    std::vector<ByteSegment> segments;
    unsigned bytes = 0;
    for (const auto &segment : binary.segments) {
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
    unsigned offset = 0;
    for (const auto &segment : segments) {
        store_segment(state, segment, builder.CreateConstGEP1_64(builder.getInt8Ty(), data, offset));
        offset += segment.width / 8;
    }
    auto *made = builder.CreateOr(builder.CreatePtrToInt(top, state.word),
                                  llvm::ConstantInt::get(state.word, abi::v1::boxed_tag));
    mark_term(state, made);
    return made;
}
} // namespace

llvm::Value *inline_binary(ExpressionLowering &state, const ast::Bitstring &binary,
                           const llvm::function_ref<llvm::Value *()> service) {
    if (state.word->getBitWidth() != 64) {
        return nullptr;
    }
    const auto segments = byte_segments(state, binary);
    if (!segments) {
        return nullptr;
    }
    unsigned bytes = 0;
    for (const auto &segment : *segments) {
        bytes += segment.width / 8;
    }
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const auto room = heap_room(state, llvm::ConstantInt::get(state.word, 2 + (bytes + 7) / 8));
    auto *direct = llvm::BasicBlock::Create(context, "binary.inline", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "binary.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "binary.join", &state.entry);
    builder.CreateCondBr(builder.CreateAnd(all_small(state, *segments), room.fits), direct, fallback);
    builder.SetInsertPoint(direct);
    auto *made = write_binary(state, *segments, room.top, bytes);
    heap_commit(state, room);
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    auto *serviced = service();
    auto *fallback_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {direct, made}, {fallback_end, serviced});
}
} // namespace clause::codegen
