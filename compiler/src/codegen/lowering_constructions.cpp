#include "lowering_state.hpp"
#include <algorithm>
#include <clause/abi/equality.hpp>
#include <clause/abi/term.hpp>
#include <llvm/Transforms/Utils/Local.h>
#include <llvm/Transforms/Utils/SSAUpdater.h>
#include <set>
#include <vector>

// Construct/deconstruct fusion and allocation sinking (OPT1.13, docs/specialization.md#constructions): a match of a
// tuple or cons cell built in the same function, often by an inlined leaf or fun, reads the words it was built of
// instead of the cell, and a cell whose word nothing reads any more is not built at all.
namespace clause::codegen {
namespace {
// How many joins deep a word is followed to the constructions it joins.
constexpr std::size_t JOIN_DEPTH = 4;

// Whether a word is an immediate: a constant that points at no cell, or an atom read from the module's atoms.
bool immediate(const ExpressionLowering &state, const llvm::Value *word) {
    if (const auto *constant = llvm::dyn_cast<llvm::ConstantInt>(word)) {
        const auto tag = constant->getZExtValue() & abi::v1::primary_mask;
        return tag != abi::v1::boxed_tag && tag != abi::v1::list_tag;
    }
    return state.atom_literals.contains(word);
}

// Whether every path's word is a construction or an immediate, following at most `depth` joins.
bool known(const ExpressionLowering &state, const llvm::Value *word, const std::size_t depth) {
    if (state.constructions.contains(word) || immediate(state, word)) {
        return true;
    }
    const auto join = state.joins.find(word);
    return join != state.joins.end() && depth > 0 &&
           std::ranges::all_of(join->second.incoming,
                               [&](const BlockValue &path) { return known(state, path.second, depth - 1); });
}

// Collect the shapes of the constructions a known word was built or joined from: (tuple, words).
void shapes(const ExpressionLowering &state, const llvm::Value *word, std::set<std::pair<bool, std::size_t>> &found) {
    if (const auto built = state.constructions.find(word); built != state.constructions.end()) {
        found.emplace(built->second.tuple, built->second.words.size());
    } else if (const auto join = state.joins.find(word); join != state.joins.end()) {
        for (const auto &path : join->second.incoming) {
            shapes(state, path.second, found);
        }
    }
}

// The answers of the paths joined at `block`, joined there; created PHIs are added to `phis`.
llvm::Value *join_answers(llvm::BasicBlock *block, llvm::Type *type, const std::vector<BlockValue> &answers,
                          llvm::SmallVectorImpl<llvm::PHINode *> &phis) {
    llvm::SSAUpdater updater(&phis);
    updater.Initialize(type, "fused");
    for (const auto &[from, answer] : answers) {
        updater.AddAvailableValue(from, answer);
    }
    // SSA formation inspects successors, so a block still being lowered gets a stand-in terminator meanwhile.
    llvm::Instruction *boundary = nullptr;
    if (!block->hasTerminator()) {
        boundary = new llvm::UnreachableInst(block->getContext(), block);
    }
    auto *value = updater.GetValueInMiddleOfBlock(block);
    if (boundary) {
        boundary->eraseFromParent();
    }
    return value;
}

// What `answer` gives for each path's known word (its construction, null for an immediate, and the word): that value,
// or for a joined word the answers joined at its block (where every path's answer is available); PHIs it creates are
// added to `phis`.
template <typename Answer>
llvm::Value *resolve(ExpressionLowering &state, llvm::Value *word, llvm::Type *type, const Answer &answer,
                     llvm::SmallVectorImpl<llvm::PHINode *> &phis) {
    if (const auto built = state.constructions.find(word); built != state.constructions.end()) {
        return answer(&built->second, word);
    }
    const auto join = state.joins.find(word);
    if (join == state.joins.end()) {
        return answer(nullptr, word);
    }
    // Copied: resolving the paths may record further joins.
    const auto incoming = join->second.incoming;
    auto *block = join->second.block;
    std::vector<BlockValue> answers;
    answers.reserve(incoming.size());
    for (const auto &[from, value] : incoming) {
        answers.emplace_back(from, resolve(state, value, type, answer, phis));
    }
    return join_answers(block, type, answers, phis);
}

// Whether nothing reads `word` but PHIs that nothing else reads; collects those PHIs (allocation sinking).
bool unread(llvm::Value *word, std::set<llvm::PHINode *> &phis) {
    return std::ranges::all_of(word->users(), [&](llvm::User *user) {
        auto *phi = llvm::dyn_cast<llvm::PHINode>(user);
        return phi && (!phis.insert(phi).second || unread(phi, phis));
    });
}

// Drop the cell whose allocation `branch` starts when nothing reads its word: the branch goes straight to the join.
bool drop_cell(llvm::Instruction *branch) {
    // The cell's word is the marked PHI of the block both of its paths join.
    auto *join = branch->getSuccessor(0)->getSingleSuccessor();
    const auto candidates = join->phis();
    const auto marked =
        llvm::find_if(candidates, [](const llvm::PHINode &phi) { return phi.getMetadata(CELL_METADATA) != nullptr; });
    if (marked == candidates.end()) {
        return false;
    }
    auto *word = &*marked;
    std::set<llvm::PHINode *> phis;
    if (!unread(word, phis)) {
        return false;
    }
    // The PHIs read only each other: once none has a reader, each goes with its operands.
    for (auto *phi : phis) {
        phi->replaceAllUsesWith(llvm::PoisonValue::get(phi->getType()));
    }
    for (auto *phi : phis) {
        phi->eraseFromParent();
    }
    word->eraseFromParent();
    llvm::IRBuilder<>(branch).CreateBr(join);
    branch->eraseFromParent();
    return true;
}

// Drop every unread cell once; true when one was dropped, which may leave the cells it held unread too.
bool drop_round(llvm::Function &function) {
    std::vector<llvm::Instruction *> branches;
    for (auto &block : function) {
        auto *branch = block.getTerminatorOrNull();
        if (branch && branch->getMetadata(CELL_METADATA)) {
            branches.push_back(branch);
        }
    }
    bool dropped = false;
    for (auto *branch : branches) {
        dropped = drop_cell(branch) || dropped;
    }
    if (dropped) {
        llvm::removeUnreachableBlocks(function);
    }
    return dropped;
}
} // namespace

void mark_cell(llvm::Instruction *branch, llvm::Value *word) {
    auto *mark = llvm::MDNode::get(branch->getContext(), {});
    branch->setMetadata(CELL_METADATA, mark);
    llvm::cast<llvm::Instruction>(word)->setMetadata(CELL_METADATA, mark);
}

void record_construction(ExpressionLowering &state, llvm::Value *word, const bool tuple,
                         const std::span<llvm::Value *const> words) {
    // Only optimized code (O2, where leaves have a budget) knows constructions. A binary loop knows its rests by their
    // words, so its matches keep reading copies of them from cells; {} is an immediate.
    if (state.leaf_budget && !state.binary_loop && !words.empty()) {
        state.constructions.insert_or_assign(word, Construction{tuple, {words.begin(), words.end()}});
    }
}

void record_join(ExpressionLowering &state, llvm::Value *word, llvm::BasicBlock *block,
                 std::vector<BlockValue> incoming) {
    if (std::ranges::none_of(incoming, [&](const BlockValue &path) { return path.second == word; })) {
        state.joins.insert_or_assign(word, Join{block, std::move(incoming)});
    }
}

llvm::Value *constructed_shape(ExpressionLowering &state, llvm::Value *word, const std::optional<std::size_t> arity) {
    // {} is an immediate word, which its own comparison tests.
    if (arity == 0 || !known(state, word, JOIN_DEPTH)) {
        return nullptr;
    }
    auto *type = state.builder.getInt1Ty();
    llvm::SmallVector<llvm::PHINode *, 2> phis;
    return resolve(
        state, word, type,
        [&](const Construction *built, llvm::Value *) -> llvm::Value * {
            const bool holds = built && (arity ? built->tuple && built->words.size() == *arity : !built->tuple);
            return llvm::ConstantInt::getBool(type, holds);
        },
        phis);
}

llvm::Value *constructed_word(ExpressionLowering &state, llvm::Value *word, const bool tuple, const std::size_t index) {
    if (!known(state, word, JOIN_DEPTH)) {
        return nullptr;
    }
    // Only paths whose word has the shape read it; the others pass [].
    auto *other = llvm::ConstantInt::get(state.word, abi::v1::empty_list);
    llvm::SmallVector<llvm::PHINode *, 2> phis;
    auto *value = resolve(
        state, word, state.word,
        [&](const Construction *built, llvm::Value *) -> llvm::Value * {
            return built && built->tuple == tuple && index < built->words.size() ? built->words[index] : other;
        },
        phis);
    // The read words are terms wherever they cross a call.
    for (auto *phi : phis) {
        mark_term(state, phi);
    }
    return value;
}

llvm::Value *constructed_immediate(ExpressionLowering &state, llvm::Value *word) {
    if (!known(state, word, JOIN_DEPTH)) {
        return nullptr;
    }
    // A boxed word of address 0 points at no cell and equals no immediate.
    auto *cell = llvm::ConstantInt::get(state.word, abi::v1::boxed_tag);
    llvm::SmallVector<llvm::PHINode *, 2> phis;
    return resolve(
        state, word, state.word,
        [&](const Construction *built, llvm::Value *immediate) { return built ? cell : immediate; }, phis);
}

llvm::Value *rebuilt(ExpressionLowering &state, llvm::Value *word) {
    std::set<std::pair<bool, std::size_t>> found;
    if (known(state, word, JOIN_DEPTH)) {
        shapes(state, word, found);
    }
    if (found.empty()) {
        return word;
    }
    auto &context = state.entry.getContext();
    auto *join = llvm::BasicBlock::Create(context, "rebuilt.join", &state.entry);
    std::vector<BlockValue> incoming;
    for (const auto &[tuple, size] : found) {
        auto *cell = llvm::BasicBlock::Create(context, "rebuilt.cell", &state.entry);
        auto *next = llvm::BasicBlock::Create(context, "rebuilt.next", &state.entry);
        state.builder.CreateCondBr(constructed_shape(state, word, tuple ? std::optional{size} : std::nullopt), cell,
                                   next);
        state.builder.SetInsertPoint(cell);
        std::vector<llvm::Value *> words;
        words.reserve(size);
        for (std::size_t index = 0; index < size; ++index) {
            words.push_back(constructed_word(state, word, tuple, index));
        }
        auto *value = tuple ? lower_tuple(state, words) : lower_list(state, words);
        incoming.emplace_back(state.builder.GetInsertBlock(), value);
        state.builder.CreateBr(join);
        state.builder.SetInsertPoint(next);
    }
    // No shape held: the path's word was an immediate.
    incoming.emplace_back(state.builder.GetInsertBlock(), constructed_immediate(state, word));
    state.builder.CreateBr(join);
    return join_inlined(state, join, incoming);
}

void drop_unused_cells(llvm::Function &function) {
    while (drop_round(function)) {
    }
}
} // namespace clause::codegen
