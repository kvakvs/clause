#include "lowering_state.hpp"
#include <clause/abi/term.hpp>
#include <optional>

// Small-map lookups: generated code reads and copies a classic map cell (a header, then sorted key and value words)
// for atom and small-integer keys, whose term equality is word equality. Map trees, other keys, errors and a full
// heap block keep the map service as their slow tail.
namespace clause::codegen {
namespace {
using abi::v1::HeaderKind;

// Fields of an update copied inline at most; a larger update scans the cell once per field, so it goes to the service.
constexpr std::size_t INLINE_UPDATE_FIELDS = 8;

struct ClassicMap {
    // The cell a classic map word points at, and the count of key and value words after its header.
    llvm::Value *cell;
    llvm::Value *words;
};

llvm::Value *word(ExpressionLowering &state, const std::uint64_t value) {
    return llvm::ConstantInt::get(state.word, value);
}

llvm::Value *compare(ExpressionLowering &state, const llvm::CmpInst::Predicate predicate, llvm::Value *left,
                     llvm::Value *right) {
    return state.builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, predicate, left, right));
}

// i1: the bits of `mask` in the word are `tag`.
llvm::Value *tagged(ExpressionLowering &state, llvm::Value *value, const std::uint64_t mask, const std::uint64_t tag) {
    return compare(state, llvm::CmpInst::ICMP_EQ, state.builder.CreateAnd(value, word(state, mask)), word(state, tag));
}

// i1: a key a classic cell can be searched for by word equality: an atom or a small integer.
llvm::Value *word_key(ExpressionLowering &state, llvm::Value *key) {
    return state.builder.CreateOr(
        tagged(state, key, abi::v1::small_integer_tag, abi::v1::small_integer_tag),
        tagged(state, key, (std::uint64_t{1} << abi::v1::atom_tag_bits) - 1, abi::v1::atom_tag));
}

llvm::BasicBlock *block(ExpressionLowering &state, const char *name) {
    return llvm::BasicBlock::Create(state.entry.getContext(), name, &state.entry);
}

// The kind bits of a header word.
llvm::Value *header_kind(ExpressionLowering &state, llvm::Value *header) {
    return state.builder.CreateAnd(state.builder.CreateLShr(header, word(state, abi::v1::header_kind_shift)),
                                   word(state, abi::v1::header_kind_mask));
}

// Continue where `map` is a classic map cell, else branch to `other`.
ClassicMap classic_map(ExpressionLowering &state, llvm::Value *map, llvm::BasicBlock *other) {
    auto &builder = state.builder;
    auto *header_block = block(state, "map.header");
    auto *classic = block(state, "map.classic");
    builder.CreateCondBr(tagged(state, map, abi::v1::primary_mask, abi::v1::boxed_tag), header_block, other);
    builder.SetInsertPoint(header_block);
    auto *cell = cell_address(state, map, abi::v1::boxed_tag);
    auto *header = builder.CreateAlignedLoad(state.word, cell, llvm::Align(state.word->getBitWidth() / 8), "map.cell");
    builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_EQ, header_kind(state, header),
                                 word(state, static_cast<std::uint64_t>(HeaderKind::map))),
                         classic, other);
    builder.SetInsertPoint(classic);
    return {cell, builder.CreateLShr(header, word(state, abi::v1::header_count_shift))};
}

// The index of `key`'s value word in a classic cell, continuing where it was found; `miss` where the cell lacks it.
llvm::Value *find_key(ExpressionLowering &state, const ClassicMap &map, llvm::Value *key, llvm::BasicBlock *miss) {
    auto &builder = state.builder;
    auto *before = builder.GetInsertBlock();
    auto *loop = block(state, "map.scan");
    auto *step = block(state, "map.step");
    auto *next_block = block(state, "map.next");
    auto *found = block(state, "map.found");
    builder.CreateBr(loop);
    builder.SetInsertPoint(loop);
    // Key words sit at the odd indexes past the header.
    auto *index = loop_placeholder(state, "map.index");
    builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_ULT, index, map.words), step, miss);
    builder.SetInsertPoint(step);
    auto *stored = builder.CreateAlignedLoad(state.word, builder.CreateGEP(state.word, map.cell, index),
                                             llvm::Align(state.word->getBitWidth() / 8), "map.key");
    builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_EQ, stored, key), found, next_block);
    builder.SetInsertPoint(next_block);
    auto *next = builder.CreateAdd(index, word(state, 2));
    builder.CreateBr(loop);
    auto *at = close_loop(index, loop, {before, word(state, 1)}, {next_block, next});
    builder.SetInsertPoint(found);
    return builder.CreateAdd(at, word(state, 1));
}

struct Lookup {
    // A classic cell holding the key and the index of its value word, where the builder continues; the block every
    // other map or key goes to, without an insert point.
    ClassicMap map;
    llvm::Value *index;
    llvm::BasicBlock *slow;
};

// Search a classic cell for an atom or small-integer key; `miss` where the cell lacks it (the slow block if null).
std::optional<Lookup> lookup(ExpressionLowering &state, const MapKey operands, llvm::BasicBlock *miss) {
    auto *map = operands.map;
    auto *key = operands.key;
    // An integer constant is an immediate: no pointer is formed for it.
    if (llvm::isa<llvm::ConstantInt>(map)) {
        return std::nullopt;
    }
    auto &builder = state.builder;
    auto *slow = block(state, "map.slow");
    auto *keyed = block(state, "map.keyed");
    builder.CreateCondBr(word_key(state, key), keyed, slow);
    builder.SetInsertPoint(keyed);
    const auto classic = classic_map(state, map, slow);
    auto *index = find_key(state, classic, key, miss ? miss : slow);
    return Lookup{classic, index, slow};
}

// Finish an inline form: `fast` ends the current block, `service` runs in `slow`; the joined value.
llvm::Value *with_service(ExpressionLowering &state, llvm::Value *fast, llvm::BasicBlock *slow,
                          llvm::function_ref<llvm::Value *()> service) {
    auto &builder = state.builder;
    auto *join = block(state, "map.join");
    auto *fast_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(slow);
    auto *serviced = service();
    auto *slow_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {fast_end, fast}, {slow_end, serviced});
}

// Copy a classic cell to free heap words and store `values`' new field values into the copy; `slow` where a key is
// missing or the heap block has no room.
llvm::Value *copy_updated(ExpressionLowering &state, const ClassicMap &map, std::span<llvm::Value *const> values,
                          llvm::BasicBlock *slow) {
    auto &builder = state.builder;
    const auto bytes = state.word->getBitWidth() / 8;
    const llvm::Align align(bytes);
    std::vector<llvm::Value *> indexes;
    for (std::size_t field = 1; field < values.size(); field += 3) {
        indexes.push_back(find_key(state, map, values[field], slow));
    }
    auto *words = builder.CreateAdd(map.words, word(state, 1));
    const auto room = heap_room(state, words);
    auto *copy = block(state, "map.copy");
    builder.CreateCondBr(room.fits, copy, slow);
    builder.SetInsertPoint(copy);
    builder.CreateMemCpy(room.top, align, map.cell, align, builder.CreateMul(words, word(state, bytes)));
    for (std::size_t i = 0; i < indexes.size(); ++i) {
        builder.CreateAlignedStore(values[2 + 3 * i], builder.CreateGEP(state.word, room.top, indexes[i]), align);
    }
    heap_commit(state, room);
    auto *built = builder.CreateOr(builder.CreatePtrToInt(room.top, state.word), word(state, abi::v1::boxed_tag));
    mark_term(state, built);
    return built;
}
} // namespace

llvm::Value *inline_map_get(ExpressionLowering &state, const MapKey operands, llvm::BasicBlock *miss,
                            llvm::function_ref<llvm::Value *()> service) {
    const auto found = lookup(state, operands, miss);
    if (!found) {
        return service();
    }
    auto *value =
        state.builder.CreateAlignedLoad(state.word, state.builder.CreateGEP(state.word, found->map.cell, found->index),
                                        llvm::Align(state.word->getBitWidth() / 8), "map.value");
    mark_term(state, value);
    return with_service(state, value, found->slow, service);
}

llvm::Value *inline_map_contains(ExpressionLowering &state, const MapKey operands,
                                 llvm::function_ref<llvm::Value *()> service) {
    auto &builder = state.builder;
    auto *absent = block(state, "map.absent");
    const auto found = lookup(state, operands, absent);
    if (!found) {
        absent->eraseFromParent();
        return service();
    }
    auto *known = block(state, "map.known");
    auto *hit = builder.GetInsertBlock();
    builder.CreateBr(known);
    builder.SetInsertPoint(absent);
    builder.CreateBr(known);
    builder.SetInsertPoint(known);
    auto *truth = join_values(state, state.word, {hit, word(state, abi::v1::true_atom)},
                              {absent, word(state, abi::v1::false_atom)});
    return with_service(state, truth, found->slow, service);
}

llvm::Value *inline_map_size(ExpressionLowering &state, llvm::Value *map, llvm::function_ref<llvm::Value *()> service) {
    if (llvm::isa<llvm::ConstantInt>(map)) {
        return service();
    }
    auto *slow = block(state, "map.slow");
    const auto classic = classic_map(state, map, slow);
    // Entries are half the words; as a small integer, (words / 2) << tag bits, plus the tag.
    auto *size =
        state.builder.CreateOr(state.builder.CreateShl(classic.words, word(state, abi::v1::small_integer_bits - 1)),
                               word(state, abi::v1::small_integer_tag));
    return with_service(state, size, slow, service);
}

bool inline_map_shape(ExpressionLowering &state, llvm::Value *map, llvm::BasicBlock *mismatch) {
    auto &builder = state.builder;
    if (llvm::isa<llvm::ConstantInt>(map)) {
        return false;
    }
    auto *header_block = block(state, "map.header");
    auto *shaped = block(state, "map.shaped");
    builder.CreateCondBr(tagged(state, map, abi::v1::primary_mask, abi::v1::boxed_tag), header_block, mismatch);
    builder.SetInsertPoint(header_block);
    auto *header = builder.CreateAlignedLoad(state.word, cell_address(state, map, abi::v1::boxed_tag),
                                             llvm::Align(state.word->getBitWidth() / 8), "map.cell");
    auto *kind = header_kind(state, header);
    builder.CreateCondBr(builder.CreateOr(compare(state, llvm::CmpInst::ICMP_EQ, kind,
                                                  word(state, static_cast<std::uint64_t>(HeaderKind::map))),
                                          compare(state, llvm::CmpInst::ICMP_EQ, kind,
                                                  word(state, static_cast<std::uint64_t>(HeaderKind::tree_map)))),
                         shaped, mismatch);
    builder.SetInsertPoint(shaped);
    return true;
}

llvm::Value *inline_map_update(ExpressionLowering &state, std::span<llvm::Value *const> values,
                               llvm::function_ref<llvm::Value *()> service) {
    const auto fields = (values.size() - 1) / 3;
    if (fields == 0 || fields > INLINE_UPDATE_FIELDS || llvm::isa<llvm::ConstantInt>(values[0])) {
        return service();
    }
    auto &builder = state.builder;
    auto *slow = block(state, "map.slow");
    auto *keyed = block(state, "map.keyed");
    llvm::Value *keys = builder.getTrue();
    for (std::size_t field = 1; field < values.size(); field += 3) {
        keys = builder.CreateAnd(keys, word_key(state, values[field]));
    }
    builder.CreateCondBr(keys, keyed, slow);
    builder.SetInsertPoint(keyed);
    const auto classic = classic_map(state, values[0], slow);
    return with_service(state, copy_updated(state, classic, values, slow), slow, service);
}
} // namespace clause::codegen
