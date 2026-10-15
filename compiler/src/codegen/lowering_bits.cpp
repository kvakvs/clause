#include "../semantic/binary_options.hpp"
#include "../semantic/capabilities.hpp"
#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <charconv>
#include <clause/abi/bits.hpp>
#include <clause/abi/term.hpp>
#include <cstdint>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <optional>
#include <utility>

namespace clause::codegen {
namespace {
using Op = abi::v1::BitOperation;

// Metadata and cursors fit both target small-integer ranges; no raw host term words enter IR.
llvm::Value *integer(ExpressionLowering &state, const unsigned value) {
    return llvm::ConstantInt::get(state.word, *abi::v1::IntegerEncoding<32>::encode(value));
}

// Resolve native endian from the LLVM data layout, including foreign object emission.
llvm::Value *descriptor(ExpressionLowering &state, const semantic::BinaryOptions &options, const bool empty = false) {
    const bool little = options.native ? state.entry.getParent()->getDataLayout().isLittleEndian() : options.little;
    const auto flags = (empty ? abi::v1::bit_empty : 0) | (little ? abi::v1::bit_little : 0) |
                       (options.signed_value ? abi::v1::bit_signed : 0) | (options.all ? abi::v1::bit_all : 0);
    return integer(state, (options.unit << 8) | flags | static_cast<unsigned>(options.type));
}

// Root every borrowed argument and both success-only outputs through the shared runtime scratch buffer.
BitLowering service(ExpressionLowering &state, Op operation, const std::span<llvm::Value *const> values) {
    auto &builder = state.builder;
    auto &module = *state.entry.getParent();
    auto *arguments = builder.CreateGEP(state.word, state.roots->buffer,
                                        llvm::ConstantInt::get(state.word, state.roots->next), "binary.arguments");
    for (auto *value : values) {
        root_value(state, value);
    }
    auto *slot = root_slot(state);
    auto *next = root_slot(state);
    auto callee = module.getOrInsertFunction(
        services::symbol<services::Bits>(module.getTargetTriple()),
        llvm::FunctionType::get(
            builder.getInt8Ty(),
            {builder.getPtrTy(), builder.getInt8Ty(), builder.getPtrTy(), state.word, builder.getPtrTy()}, false));
    auto *outcome = builder.CreateCall(callee,
                                       {state.entry.getArg(0), builder.getInt8(static_cast<std::uint8_t>(operation)),
                                        arguments, llvm::ConstantInt::get(state.word, values.size()), slot},
                                       "binary.outcome");
    auto *value = checked_value(state, {outcome, slot}, bad_argument_exit(state));
    return {value,
            builder.CreateAlignedLoad(state.word, next, llvm::Align(state.word->getBitWidth() / 8), "binary.cursor")};
}

// Expand literal strings as scalar segment values without constructing an unrelated list value.
void segment_values(ExpressionLowering &state, const ast::BinarySegment &segment, std::vector<llvm::Value *> &values) {
    const auto &syntax = *state.module.syntax;
    const auto options = semantic::binary_options(segment);
    auto *metadata = descriptor(state, options);
    auto *size = segment.size ? state.values.at(&syntax.expression(*segment.size)) : integer(state, options.size);
    const auto &value = syntax.expression(semantic::ungroup(syntax, segment.value)).value;
    if (const auto *string = std::get_if<ast::StringLiteral>(&value)) {
        if (string->value.empty()) {
            values.insert(values.end(), {descriptor(state, options, true), size, integer(state, 0)});
        }
        for (const auto character : string->value) {
            values.insert(values.end(), {metadata, size, integer(state, character)});
        }
    } else {
        values.insert(values.end(), {metadata, size, state.values.at(&syntax.expression(segment.value))});
    }
}

// OTP coerces integer literals to binary64 before comparing an extracted float, without width rounding.
llvm::Value *expected_float(ExpressionLowering &state, const semantic::MatchLiteral &literal) {
    if (const auto *real = std::get_if<ast::FloatLiteral>(&literal)) {
        return lower_float(state, real->value);
    }
    auto *value = lower_integer(state, std::get<ast::IntegerLiteral>(literal).value.decimal);
    return lower_operation(state, abi::v1::ImmediateOperation::to_float, value);
}

// Compare the finite extracted value with the original literal after OTP's integer-to-float coercion.
void literal_constraint(ExpressionLowering &state, llvm::Value *actual, llvm::Value *expected,
                        llvm::BasicBlock *mismatch) {
    auto *success = llvm::BasicBlock::Create(state.entry.getContext(), "binary.literal", &state.entry);
    state.builder.CreateCondBr(lower_exact(state, actual, expected), success, mismatch);
    state.builder.SetInsertPoint(success);
}

// The bits service lowering of one binary pattern node (defined below).
BitLowering service_pattern(ExpressionLowering &state, const semantic::MatchNode &node,
                            std::span<llvm::Value *const> values, llvm::BasicBlock *mismatch);

// Small-integer words of a tagged small (encode) and of a tagged cursor (decode).
llvm::Value *encode_small(ExpressionLowering &state, llvm::Value *payload) {
    auto &builder = state.builder;
    return builder.CreateOr(builder.CreateShl(payload, llvm::ConstantInt::get(state.word, abi::v1::small_integer_bits)),
                            llvm::ConstantInt::get(state.word, abi::v1::small_integer_tag));
}

llvm::Value *decode_small(ExpressionLowering &state, llvm::Value *value) {
    return state.builder.CreateAShr(value, llvm::ConstantInt::get(state.word, abi::v1::small_integer_bits));
}

llvm::Value *compare(ExpressionLowering &state, llvm::CmpInst::Predicate predicate, llvm::Value *left,
                     llvm::Value *right) {
    return state.builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, predicate, left, right));
}

// The size of a segment known at compile time: its default, or a size written as an integer literal.
std::optional<std::uint64_t> literal_size(const ExpressionLowering &state, const semantic::MatchNode &node,
                                          const semantic::BinaryOptions &options) {
    if (!node.key) {
        return options.size;
    }
    const auto &syntax = *state.module.syntax;
    const auto *integer =
        std::get_if<ast::IntegerLiteral>(&syntax.expression(semantic::ungroup(syntax, *node.key)).value);
    std::uint64_t size = 0;
    if (!integer) {
        return std::nullopt;
    }
    const auto &digits = integer->value.decimal;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), size);
    return parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() ? std::optional{size} : std::nullopt;
}

// The width of an integer segment the matcher reads inline (OPT1.4): a constant number of whole bytes that fits a
// small integer unsigned, at most 7 bytes; none otherwise.
std::optional<unsigned> inline_width(const ExpressionLowering &state, const semantic::MatchNode &node) {
    if (node.literal || !node.bits) {
        return std::nullopt;
    }
    const auto &options = *node.bits;
    const auto size = literal_size(state, node, options);
    if (!size || *size > 64) {
        return std::nullopt;
    }
    const auto width = static_cast<unsigned>(*size) * options.unit;
    if (options.type != abi::v1::BitType::integer || options.all || width == 0 || width % 8 != 0 || width > 56) {
        return std::nullopt;
    }
    return width;
}

// binary_start inline: a bitstring matches with its cursor at 0 (OPT1.4).
BitLowering start(ExpressionLowering &state, llvm::Value *input, llvm::BasicBlock *mismatch) {
    auto *matched = llvm::BasicBlock::Create(state.entry.getContext(), "binary.started", &state.entry);
    state.builder.CreateCondBr(inline_bitstring_test(state, input), matched, mismatch);
    state.builder.SetInsertPoint(matched);
    return {input, encode_small(state, llvm::ConstantInt::get(state.word, 0))};
}

// binary_finish inline: the cursor is at the end of the bitstring (OPT1.4).
BitLowering finish(ExpressionLowering &state, llvm::Value *input, llvm::Value *cursor, llvm::BasicBlock *mismatch) {
    const auto view = inline_binary_view(state, input);
    auto *matched = llvm::BasicBlock::Create(state.entry.getContext(), "binary.finished", &state.entry);
    state.builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_EQ, cursor, encode_small(state, view.bits)), matched,
                               mismatch);
    state.builder.SetInsertPoint(matched);
    return {input, cursor};
}

// An integer segment read inline (OPT1.4): at a byte-aligned position with `width` bits left, its bytes are one
// unaligned load, swapped into the segment byte order and extended to a small integer; a position with too few
// bits left mismatches, as the service does; an unaligned one takes the service.
BitLowering extract(ExpressionLowering &state, const semantic::MatchNode &node, std::span<llvm::Value *const> values,
                    const unsigned width, llvm::BasicBlock *mismatch) {
    auto &builder = state.builder;
    const auto &options = *node.bits;
    auto *input = values[node.input];
    auto *cursor = values[node.index];
    const auto view = inline_binary_view(state, input);
    auto *position = decode_small(state, cursor);
    auto *absolute = builder.CreateAdd(view.offset, position);
    auto *room = compare(state, llvm::CmpInst::ICMP_ULE,
                         builder.CreateAdd(position, llvm::ConstantInt::get(state.word, width)), view.bits);
    auto *aligned =
        compare(state, llvm::CmpInst::ICMP_EQ, builder.CreateAnd(absolute, llvm::ConstantInt::get(state.word, 7)),
                llvm::ConstantInt::get(state.word, 0));
    auto *checked = llvm::BasicBlock::Create(state.entry.getContext(), "binary.room", &state.entry);
    builder.CreateCondBr(room, checked, mismatch);
    builder.SetInsertPoint(checked);
    auto *read = llvm::BasicBlock::Create(state.entry.getContext(), "binary.read", &state.entry);
    auto *unaligned = llvm::BasicBlock::Create(state.entry.getContext(), "binary.unaligned", &state.entry);
    auto *join = llvm::BasicBlock::Create(state.entry.getContext(), "binary.read.join", &state.entry);
    builder.CreateCondBr(aligned, read, unaligned);
    builder.SetInsertPoint(read);
    auto *bytes = builder.CreateIntToPtr(
        builder.CreateAdd(view.data, builder.CreateLShr(absolute, llvm::ConstantInt::get(state.word, 3))),
        builder.getPtrTy(), "proven.cell.bytes");
    llvm::Value *raw = builder.CreateAlignedLoad(builder.getIntNTy(width), bytes, llvm::Align(1), "binary.bytes");
    const auto host_little = state.entry.getParent()->getDataLayout().isLittleEndian();
    const bool little = options.native ? host_little : options.little;
    if (width > 8 && little != host_little) {
        // bswap needs an even number of bytes: swap the word and shift the bytes back down.
        auto *swapped = builder.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, builder.CreateZExt(raw, state.word));
        raw = builder.CreateTrunc(builder.CreateLShr(swapped, state.word->getBitWidth() - width), raw->getType());
    }
    auto *payload = options.signed_value ? builder.CreateSExt(raw, state.word) : builder.CreateZExt(raw, state.word);
    auto *value = encode_small(state, payload);
    auto *next = encode_small(state, builder.CreateAdd(position, llvm::ConstantInt::get(state.word, width)));
    auto *read_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(unaligned);
    const auto serviced = service_pattern(state, node, values, mismatch);
    auto *unaligned_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return {join_values(state, state.word, {read_end, value}, {unaligned_end, serviced.value}),
            join_values(state, state.word, {read_end, next}, {unaligned_end, serviced.cursor})};
}

// Whether a node extracts the rest of a binary (`Rest/binary` or `Rest/bits`) (OPT1.4B).
bool rest_extraction(const semantic::MatchNode &node) {
    return node.operation == semantic::MatchOperation::binary_extract && node.bits && !node.key && !node.literal &&
           node.bits->type == abi::v1::BitType::binary && node.bits->all;
}

// The rest of a binary as an inline sub-binary of its original (OPT1.4B): offset, length and the original word; a
// binary rest that is not whole bytes mismatches. Without heap room the service extracts it.
BitLowering rest(ExpressionLowering &state, const semantic::MatchNode &node, std::span<llvm::Value *const> values,
                 const unsigned unit, llvm::BasicBlock *mismatch) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const llvm::Align align(state.word->getBitWidth() / 8);
    const auto view = inline_binary_view(state, values[node.input]);
    auto *position = decode_small(state, values[node.index]);
    auto *left = builder.CreateSub(view.bits, position);
    if (unit != 1) {
        auto *whole = llvm::BasicBlock::Create(context, "binary.rest.whole", &state.entry);
        builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_EQ,
                                     builder.CreateURem(left, llvm::ConstantInt::get(state.word, unit)),
                                     llvm::ConstantInt::get(state.word, 0)),
                             whole, mismatch);
        builder.SetInsertPoint(whole);
    }
    const auto room = heap_room(state, llvm::ConstantInt::get(state.word, 4));
    auto *direct = llvm::BasicBlock::Create(context, "binary.rest", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "binary.rest.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "binary.rest.join", &state.entry);
    builder.CreateCondBr(room.fits, direct, fallback);
    builder.SetInsertPoint(direct);
    const auto at = [&](unsigned index) { return builder.CreateConstGEP1_64(state.word, room.top, index); };
    builder.CreateAlignedStore(
        llvm::ConstantInt::get(state.word, (std::uint64_t{3} << abi::v1::header_count_shift) |
                                               (static_cast<std::uint64_t>(abi::v1::HeaderKind::sub_binary)
                                                << abi::v1::header_kind_shift)),
        at(0), align);
    // The offset counts from the original's own first bit: the view's offset past the original's, plus the cursor.
    builder.CreateAlignedStore(builder.CreateAdd(builder.CreateSub(view.offset, view.base), position),
                               at(abi::v1::sub_offset_word), align);
    builder.CreateAlignedStore(left, at(abi::v1::sub_bits_word), align);
    builder.CreateAlignedStore(view.original, at(abi::v1::sub_original_word), align);
    heap_commit(state, room);
    auto *made = builder.CreateOr(builder.CreatePtrToInt(room.top, state.word),
                                  llvm::ConstantInt::get(state.word, abi::v1::boxed_tag));
    mark_term(state, made);
    auto *end = encode_small(state, view.bits);
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    const auto serviced = service_pattern(state, node, values, mismatch);
    auto *fallback_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return {join_values(state, state.word, {direct, made}, {fallback_end, serviced.value}),
            join_values(state, state.word, {direct, end}, {fallback_end, serviced.cursor})};
}

// The match nodes a binary pattern lowers inline (OPT1.4); none for the others, which use the bits service.
std::optional<BitLowering> inline_pattern(ExpressionLowering &state, const semantic::MatchNode &node,
                                          const std::span<llvm::Value *> values, llvm::BasicBlock *mismatch) {
    using semantic::MatchOperation;
    if (state.word->getBitWidth() != 64) {
        return std::nullopt;
    }
    if (node.operation == MatchOperation::binary_start) {
        return start(state, values[node.input], mismatch);
    }
    if (node.operation == MatchOperation::binary_finish) {
        return finish(state, values[node.input], values[node.index], mismatch);
    }
    if (node.operation != MatchOperation::binary_extract || !node.bits) {
        return std::nullopt;
    }
    if (const auto width = inline_width(state, node)) {
        return extract(state, node, values, *width, mismatch);
    }
    if (rest_extraction(node)) {
        return rest(state, node, values, node.bits->unit, mismatch);
    }
    return std::nullopt;
}
} // namespace

llvm::Value *lower_bits_operation(ExpressionLowering &state, const Op operation,
                                  const std::span<llvm::Value *const> values) {
    return service(state, operation, values).value;
}

llvm::Value *lower_bits(ExpressionLowering &state, const ast::Bitstring &binary) {
    const auto build = [&] {
        std::vector<llvm::Value *> values;
        values.reserve(3 * binary.segments.size());
        for (const auto &segment : binary.segments) {
            segment_values(state, segment, values);
        }
        return service(state, Op::make, values).value;
    };
    if (auto *made = inline_binary(state, binary, build)) {
        return made;
    }
    return build();
}

BitLowering lower_bit_pattern(ExpressionLowering &state, const semantic::MatchNode &node,
                              const std::span<llvm::Value *> values, llvm::BasicBlock *mismatch) {
    if (auto lowered = inline_pattern(state, node, values, mismatch)) {
        return *lowered;
    }
    return service_pattern(state, node, values, mismatch);
}

namespace {
BitLowering service_pattern(ExpressionLowering &state, const semantic::MatchNode &node,
                            const std::span<llvm::Value *const> values, llvm::BasicBlock *mismatch) {
    auto *saved = std::exchange(state.rejection, mismatch);
    std::vector<llvm::Value *> arguments{values[node.input]};
    auto operation = Op::test;
    llvm::Value *expected = nullptr;
    if (node.operation != semantic::MatchOperation::binary_start) {
        arguments.push_back(values[node.index]);
        operation = Op::finish;
    }
    if (node.operation == semantic::MatchOperation::binary_extract && node.bits) {
        operation = Op::extract;
        const auto options = *node.bits;
        auto *size = node.key ? lower_body(state, *node.key) : integer(state, options.size);
        if (node.literal) {
            expected = expected_float(state, *node.literal);
        }
        arguments.insert(arguments.end(), {descriptor(state, options), size, integer(state, 0)});
    }
    const auto result = service(state, operation, arguments);
    if (expected) {
        literal_constraint(state, result.value, expected, mismatch);
    }
    state.rejection = saved;
    return result;
}
} // namespace

llvm::Value *lower_binary_part(ExpressionLowering &state, const std::span<llvm::Value *const> values) {
    if (values.size() == 3) {
        return service(state, Op::part, values).value;
    }
    auto *tuple = values[1];
    auto *rejection = bad_argument_exit(state);
    lower_inspection(state, abi::v1::ContainerInspection::tuple_shape, tuple, 2, rejection);
    auto *start = lower_inspection(state, abi::v1::ContainerInspection::tuple_element, tuple, 0, rejection);
    auto *length = lower_inspection(state, abi::v1::ContainerInspection::tuple_element, tuple, 1, rejection);
    return service(state, Op::part, std::array{values[0], start, length}).value;
}
} // namespace clause::codegen
