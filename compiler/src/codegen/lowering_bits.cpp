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

// Whether the target stores the low byte of a word first.
bool host_little(const ExpressionLowering &state) { return state.entry.getParent()->getDataLayout().isLittleEndian(); }

// Whether a segment holds its low byte first, native endian resolved from the LLVM data layout, including foreign
// object emission.
bool little_segment(const ExpressionLowering &state, const semantic::BinaryOptions &options) {
    return options.native ? host_little(state) : options.little;
}

llvm::Value *descriptor(ExpressionLowering &state, const semantic::BinaryOptions &options, const bool empty = false) {
    const bool little = little_segment(state, options);
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

// Whether an integer segment of `width` bits is read inline: with one more byte its bytes fit a word (so it fits a
// small integer unsigned: 56 bits on 64-bit targets, 24 on 32-bit ones); a width of whole bytes may be little-endian,
// any other one is big-endian (OPT1.4D).
bool inline_integer(const ExpressionLowering &state, const semantic::BinaryOptions &options, const unsigned width) {
    if (options.type != abi::v1::BitType::integer || options.all || width == 0 ||
        width > state.word->getBitWidth() - 8) {
        return false;
    }
    return width % 8 == 0 || !little_segment(state, options);
}

// The width of an integer segment the matcher reads inline (OPT1.4), a constant; none otherwise.
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
    return inline_integer(state, options, width) ? std::optional{width} : std::nullopt;
}

// Whether a pattern node reads the parameter of the binary loop variant being lowered, whose view is read once.
bool loop_parameter(const ExpressionLowering &state, const semantic::MatchNode &node) {
    const auto *loop = state.binary_loop;
    return loop && loop->in_variant && loop->view && node.input == loop->reads.parameter;
}

// The view of a pattern node's input binary: the loop parameter's view, or one resolved here.
BinaryView view_of(ExpressionLowering &state, const semantic::MatchNode &node, llvm::Value *input) {
    return loop_parameter(state, node) ? *state.binary_loop->view : inline_binary_view(state, input);
}

// binary_start inline: a bitstring matches with its cursor at 0 (OPT1.4); a loop parameter is always one.
BitLowering start(ExpressionLowering &state, const semantic::MatchNode &node, llvm::Value *input,
                  llvm::BasicBlock *mismatch) {
    if (loop_parameter(state, node)) {
        return {input, encode_small(state, llvm::ConstantInt::get(state.word, 0))};
    }
    auto *matched = llvm::BasicBlock::Create(state.entry.getContext(), "binary.started", &state.entry);
    state.builder.CreateCondBr(inline_bitstring_test(state, input), matched, mismatch);
    state.builder.SetInsertPoint(matched);
    return {input, encode_small(state, llvm::ConstantInt::get(state.word, 0))};
}

// binary_finish inline: the cursor is at the end of the bitstring (OPT1.4).
BitLowering finish(ExpressionLowering &state, const semantic::MatchNode &node, llvm::Value *input, llvm::Value *cursor,
                   llvm::BasicBlock *mismatch) {
    const auto view = view_of(state, node, input);
    auto *matched = llvm::BasicBlock::Create(state.entry.getContext(), "binary.finished", &state.entry);
    state.builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_EQ, cursor, encode_small(state, view.bits)), matched,
                               mismatch);
    state.builder.SetInsertPoint(matched);
    return {input, cursor};
}

// The address of the byte holding bit `absolute` of a view's data.
llvm::Value *byte_address(ExpressionLowering &state, const BinaryView &view, llvm::Value *absolute) {
    auto &builder = state.builder;
    return builder.CreateIntToPtr(
        builder.CreateAdd(view.data, builder.CreateLShr(absolute, llvm::ConstantInt::get(state.word, 3))),
        builder.getPtrTy(), "proven.cell.bytes");
}

// The low `width` bits of a word (whole bytes) with their bytes in reverse order: bswap reverses the whole word, so
// the bytes are shifted back down.
llvm::Value *swap_bytes(ExpressionLowering &state, llvm::Value *value, const unsigned width) {
    auto *swapped = state.builder.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, value);
    return state.builder.CreateLShr(swapped, state.word->getBitWidth() - width);
}

// A segment of whole bytes at a byte-aligned bit `absolute` (OPT1.4A): one load in target order, swapped into the
// segment's order.
llvm::Value *aligned_value(ExpressionLowering &state, const BinaryView &view, llvm::Value *absolute,
                           const unsigned width, const bool little) {
    auto &builder = state.builder;
    auto *raw =
        builder.CreateZExt(builder.CreateAlignedLoad(builder.getIntNTy(width), byte_address(state, view, absolute),
                                                     llvm::Align(1), "binary.bytes"),
                           state.word);
    return width > 8 && little != host_little(state) ? swap_bytes(state, raw, width) : raw;
}

// A segment at any bit `absolute` (unaligned reads, OPT1.4D): the whole bytes from its first byte, first byte most
// significant, then the byte holding its last bit make one word that shifts down to the field. Only bytes the
// field touches are read; a field within the first bytes reads its last byte twice and shifts that copy out.
llvm::Value *unaligned_value(ExpressionLowering &state, const BinaryView &view, llvm::Value *absolute,
                             const unsigned width, const bool little) {
    auto &builder = state.builder;
    const auto constant = [&](std::uint64_t value) { return llvm::ConstantInt::get(state.word, value); };
    const auto bytes = (width + 7) / 8 * 8;
    auto *head =
        builder.CreateZExt(builder.CreateAlignedLoad(builder.getIntNTy(bytes), byte_address(state, view, absolute),
                                                     llvm::Align(1), "binary.head"),
                           state.word);
    if (bytes > 8 && host_little(state)) {
        head = swap_bytes(state, head, bytes);
    }
    auto *last = byte_address(state, view, builder.CreateAdd(absolute, constant(width - 1)));
    auto *tail = builder.CreateZExt(builder.CreateAlignedLoad(builder.getInt8Ty(), last, llvm::Align(1), "binary.tail"),
                                    state.word);
    auto *joined = builder.CreateOr(builder.CreateShl(head, constant(8)), tail);
    auto *shift = builder.CreateSub(constant(bytes + 8 - width), builder.CreateAnd(absolute, constant(7)));
    auto *field = builder.CreateAnd(builder.CreateLShr(joined, shift), constant((std::uint64_t{1} << width) - 1));
    return little && width > 8 ? swap_bytes(state, field, width) : field;
}

// An integer segment read inline (OPT1.4): with `width` bits left, a byte-aligned segment of whole bytes is one load
// and any other is an unaligned read (OPT1.4D), extended to a small integer; a position with too few bits left
// mismatches, as the service does.
BitLowering extract(ExpressionLowering &state, const semantic::MatchNode &node, std::span<llvm::Value *const> values,
                    const unsigned width, llvm::BasicBlock *mismatch) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const auto &options = *node.bits;
    const bool little = little_segment(state, options);
    const auto view = view_of(state, node, values[node.input]);
    auto *position = decode_small(state, values[node.index]);
    auto *absolute = builder.CreateAdd(view.offset, position);
    auto *next = builder.CreateAdd(position, llvm::ConstantInt::get(state.word, width));
    auto *checked = llvm::BasicBlock::Create(context, "binary.room", &state.entry);
    builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_ULE, next, view.bits), checked, mismatch);
    builder.SetInsertPoint(checked);
    llvm::Value *payload = nullptr;
    if (width % 8 != 0) {
        payload = unaligned_value(state, view, absolute, width, little);
    } else {
        auto *read = llvm::BasicBlock::Create(context, "binary.read", &state.entry);
        auto *unaligned = llvm::BasicBlock::Create(context, "binary.unaligned", &state.entry);
        auto *join = llvm::BasicBlock::Create(context, "binary.read.join", &state.entry);
        builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_EQ,
                                     builder.CreateAnd(absolute, llvm::ConstantInt::get(state.word, 7)),
                                     llvm::ConstantInt::get(state.word, 0)),
                             read, unaligned);
        builder.SetInsertPoint(read);
        auto *aligned = aligned_value(state, view, absolute, width, little);
        builder.CreateBr(join);
        builder.SetInsertPoint(unaligned);
        auto *shifted = unaligned_value(state, view, absolute, width, little);
        builder.CreateBr(join);
        builder.SetInsertPoint(join);
        payload = join_values(state, state.word, {read, aligned}, {unaligned, shifted});
    }
    if (options.signed_value) {
        auto *spare = llvm::ConstantInt::get(state.word, state.word->getBitWidth() - width);
        payload = builder.CreateAShr(builder.CreateShl(payload, spare), spare);
    }
    return {encode_small(state, payload), encode_small(state, next)};
}

// A 4-word sub-binary cell at `top` of `bits` bits from `offset` of `original` (OPT1.4B); its boxed word.
llvm::Value *sub_cell(ExpressionLowering &state, const HeapRoom &room, const LoopRest &rest) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    const auto at = [&](unsigned index) { return builder.CreateConstGEP1_64(state.word, room.top, index); };
    builder.CreateAlignedStore(
        llvm::ConstantInt::get(state.word, (std::uint64_t{3} << abi::v1::header_count_shift) |
                                               (static_cast<std::uint64_t>(abi::v1::HeaderKind::sub_binary)
                                                << abi::v1::header_kind_shift)),
        at(0), align);
    builder.CreateAlignedStore(rest.offset, at(abi::v1::sub_offset_word), align);
    builder.CreateAlignedStore(rest.bits, at(abi::v1::sub_bits_word), align);
    builder.CreateAlignedStore(rest.original, at(abi::v1::sub_original_word), align);
    heap_commit(state, room);
    auto *made = builder.CreateOr(builder.CreatePtrToInt(room.top, state.word),
                                  llvm::ConstantInt::get(state.word, abi::v1::boxed_tag));
    mark_term(state, made);
    return made;
}

// A rest of a binary loop's parameter (OPT1.4C): the original-relative offset (the view's offset past the
// original's, plus the cursor), the bits left and what the service needs to make it.
LoopRest loop_rest(ExpressionLowering &state, llvm::Value *fresh, const BinaryView &view, llvm::Value *input,
                   llvm::Value *cursor) {
    auto &builder = state.builder;
    auto *position = decode_small(state, cursor);
    return {fresh,
            builder.CreateAdd(builder.CreateSub(view.offset, view.base), position),
            builder.CreateSub(view.bits, position),
            view.original,
            input,
            cursor};
}

// The rest of the parameter of a binary loop's variant (OPT1.4C): no cell is made; the value is the parameter's own
// sub-binary cell, and the tail call passing it on first stores the rest's offset and length into that cell.
BitLowering context_rest(ExpressionLowering &state, BinaryLoop &loop, const LoopRest &rest, const BinaryView &view) {
    auto *value = state.builder.CreateFreeze(rest.input, "binary.context");
    mark_term(state, value);
    loop.rests.emplace(value, rest);
    return {value, encode_small(state, view.bits)};
}

// The rest of a binary as an inline sub-binary of its original (OPT1.4B); a binary rest that is not whole bytes
// mismatches. Without heap room the service extracts it.
BitLowering rest(ExpressionLowering &state, const semantic::MatchNode &node, std::span<llvm::Value *const> values,
                 const unsigned unit, llvm::BasicBlock *mismatch) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const auto view = view_of(state, node, values[node.input]);
    auto parts = loop_rest(state, builder.getTrue(), view, values[node.input], values[node.index]);
    if (unit != 1) {
        auto *whole = llvm::BasicBlock::Create(context, "binary.rest.whole", &state.entry);
        builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_EQ,
                                     builder.CreateURem(parts.bits, llvm::ConstantInt::get(state.word, unit)),
                                     llvm::ConstantInt::get(state.word, 0)),
                             whole, mismatch);
        builder.SetInsertPoint(whole);
    }
    auto *loop = state.binary_loop && node.input == state.binary_loop->reads.parameter ? state.binary_loop : nullptr;
    if (loop && loop->in_variant) {
        return context_rest(state, *loop, parts, view);
    }
    const auto room = heap_room(state, llvm::ConstantInt::get(state.word, 4));
    auto *direct = llvm::BasicBlock::Create(context, "binary.rest", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "binary.rest.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "binary.rest.join", &state.entry);
    builder.CreateCondBr(room.fits, direct, fallback);
    builder.SetInsertPoint(direct);
    auto *made = sub_cell(state, room, parts);
    auto *end = encode_small(state, view.bits);
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    const auto serviced = service_pattern(state, node, values, mismatch);
    auto *fallback_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    auto *value = join_values(state, state.word, {direct, made}, {fallback_end, serviced.value});
    if (loop) {
        // Only a cell made here belongs to this loop alone; a serviced rest may be shared (OPT1.4C).
        parts.fresh =
            join_values(state, builder.getInt1Ty(), {direct, builder.getTrue()}, {fallback_end, builder.getFalse()});
        loop->rests.emplace(value, parts);
    }
    return {value, join_values(state, state.word, {direct, end}, {fallback_end, serviced.cursor})};
}

// The match nodes a binary pattern lowers inline (OPT1.4); none for the others, which use the bits service.
std::optional<BitLowering> inline_pattern(ExpressionLowering &state, const semantic::MatchNode &node,
                                          const std::span<llvm::Value *> values, llvm::BasicBlock *mismatch) {
    using semantic::MatchOperation;
    if (node.operation == MatchOperation::binary_start) {
        return start(state, node, values[node.input], mismatch);
    }
    if (node.operation == MatchOperation::binary_finish) {
        return finish(state, node, values[node.input], values[node.index], mismatch);
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

bool rest_extraction(const semantic::MatchNode &node) {
    return node.operation == semantic::MatchOperation::binary_extract && node.bits && !node.key && !node.literal &&
           node.bits->type == abi::v1::BitType::binary && node.bits->all;
}

namespace {
// A new sub-binary cell with a loop rest's bits, for a read that may keep it (escaping rests, OPT1.4E); without heap
// room the bits service extracts the rest again from the unchanged input.
llvm::Value *copied(ExpressionLowering &state, const LoopRest &rest) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const auto room = heap_room(state, llvm::ConstantInt::get(state.word, 4));
    auto *direct = llvm::BasicBlock::Create(context, "binary.copy", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "binary.copy.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "binary.copied", &state.entry);
    builder.CreateCondBr(room.fits, direct, fallback);
    builder.SetInsertPoint(direct);
    auto *made = sub_cell(state, room, rest);
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    const semantic::BinaryOptions bits{.type = abi::v1::BitType::binary, .unit = 1, .size = 0, .all = true};
    auto *serviced =
        service(state, Op::extract,
                std::array{rest.input, rest.cursor, descriptor(state, bits), integer(state, 0), integer(state, 0)})
            .value;
    auto *fallback_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {direct, made}, {fallback_end, serviced});
}
} // namespace

LoopRest whole_rest(ExpressionLowering &state, llvm::Value *cell, const BinaryView &view) {
    return loop_rest(state, state.builder.getTrue(), view, cell,
                     encode_small(state, llvm::ConstantInt::get(state.word, 0)));
}

llvm::Value *loop_copy(ExpressionLowering &state, const LoopRest &rest, llvm::Value *value) {
    if (const auto *always = llvm::dyn_cast<llvm::ConstantInt>(rest.fresh); always && always->isOne()) {
        return copied(state, rest);
    }
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    auto *copy = llvm::BasicBlock::Create(context, "binary.fresh", &state.entry);
    auto *keep = llvm::BasicBlock::Create(context, "binary.kept", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "binary.read", &state.entry);
    builder.CreateCondBr(rest.fresh, copy, keep);
    builder.SetInsertPoint(copy);
    auto *made = copied(state, rest);
    auto *copy_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(keep);
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {copy_end, made}, {keep, value});
}

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
