#include "reduction_budget.hpp"
#include "alias_tags.hpp"
#include <clause/abi/frames.hpp>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/SSAUpdater.h>
#include <set>
#include <vector>

namespace clause::codegen {
namespace {
using abi::v1::StateField;

// The reductions word of one body's context and the accesses promoted to SSA values.
class Budget {
  public:
    Budget(llvm::Function &body, llvm::IntegerType &word) : context_(body.getArg(0)), word_(word) {}

    // Record every load and store of the word, found through the context's uses (the field's address reads the
    // context as its pointer operand) and the address's uses; use lists avoid operand accessors, which trip
    // clang-analyzer in LLVM headers.
    void find_accesses(const llvm::DataLayout &layout) {
        for (const auto &use : context_->uses()) {
            auto *address = llvm::dyn_cast<llvm::GetElementPtrInst>(use.getUser());
            if (address && use.getOperandNo() == 0 && field(*address, layout)) {
                accesses(*address);
            }
        }
    }

    // Record an existing load or store of the word.
    void promote(llvm::Instruction &access) { accesses_.push_back(&access); }

    // Make the word's value in memory current before `call`, and take it from memory again after it when it returns.
    void around(llvm::CallInst &call) {
        llvm::IRBuilder<> builder(&call);
        auto *current = load(builder);
        promote(*current);
        auto *written = store(builder, current);
        promote(*written);
        kept_.insert(written);
        if (!call.isMustTailCall()) {
            builder.SetInsertPoint(call.getNextNode());
            define(builder);
        }
    }

    // Take the word's value from memory where the builder is: an unpromoted load whose value a promoted store
    // defines.
    void define(llvm::IRBuilder<> &builder) { promote(*store(builder, load(builder))); }

    // Replace the promoted accesses by SSA values; only the stores before calls stay.
    void run() {
        llvm::SSAUpdater updater;
        Promoter(accesses_, updater, kept_).run(accesses_);
    }

  private:
    // Whether a constant address of the context is the reductions word, decoded through LLVM's offset folding.
    [[nodiscard]] bool field(llvm::GetElementPtrInst &address, const llvm::DataLayout &layout) const {
        llvm::APInt offset(layout.getPointerSizeInBits(), 0);
        return address.getSourceElementType() == &word_ && address.accumulateConstantOffset(layout, offset) &&
               offset == static_cast<std::uint64_t>(StateField::reductions) * (word_.getBitWidth() / 8);
    }

    // The loads through `address` and the stores into it (its use as their pointer operand).
    void accesses(llvm::GetElementPtrInst &address) {
        for (const auto &use : address.uses()) {
            auto *user = llvm::cast<llvm::Instruction>(use.getUser());
            const bool load = llvm::isa<llvm::LoadInst>(user) && use.getOperandNo() == 0;
            const bool store = llvm::isa<llvm::StoreInst>(user) && use.getOperandNo() == 1;
            if (load || store) {
                promote(*user);
            }
        }
    }

    // LLVM's promoter of one memory word, keeping the stores the services read.
    class Promoter final : public llvm::LoadAndStorePromoter {
      public:
        Promoter(const llvm::SmallVector<llvm::Instruction *> &accesses, llvm::SSAUpdater &updater,
                 const std::set<const llvm::Instruction *> &kept)
            : LoadAndStorePromoter({accesses.begin(), accesses.end()}, updater, "reductions"), kept_(kept) {}

        [[nodiscard]] bool shouldDelete(llvm::Instruction *instruction) const override {
            return !kept_.contains(instruction);
        }

      private:
        const std::set<const llvm::Instruction *> &kept_;
    };

    // A load and a store of the word, tagged as process state like the frame lowering's accesses.
    llvm::LoadInst *load(llvm::IRBuilder<> &builder) const {
        return llvm::cast<llvm::LoadInst>(tag_memory(
            builder.CreateAlignedLoad(&word_, address(builder), llvm::Align(word_.getBitWidth() / 8), "reductions"),
            MemoryKind::state));
    }

    llvm::StoreInst *store(llvm::IRBuilder<> &builder, llvm::Value *value) const {
        return llvm::cast<llvm::StoreInst>(
            tag_memory(builder.CreateAlignedStore(value, address(builder), llvm::Align(word_.getBitWidth() / 8)),
                       MemoryKind::state));
    }

    llvm::Value *address(llvm::IRBuilder<> &builder) const {
        return builder.CreateConstInBoundsGEP1_64(&word_, context_, static_cast<std::uint64_t>(StateField::reductions));
    }

    // The body's context, whose FrameState starts it, and the target word.
    llvm::Value *context_;
    llvm::IntegerType &word_;
    // Every load and store of the word to promote, and the stores before calls among them, which stay.
    llvm::SmallVector<llvm::Instruction *> accesses_;
    std::set<const llvm::Instruction *> kept_;
};
} // namespace

void keep_reductions_in_register(llvm::Function &body, llvm::BasicBlock &entry, llvm::IntegerType &word) {
    Budget budget(body, word);
    budget.find_accesses(body.getParent()->getDataLayout());
    std::vector<llvm::CallInst *> calls;
    for (auto &instruction : llvm::instructions(body)) {
        auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction);
        if (call && !llvm::isa<llvm::IntrinsicInst>(call)) {
            calls.push_back(call);
        }
    }
    for (auto *call : calls) {
        budget.around(*call);
    }
    llvm::IRBuilder<> builder(entry.getTerminator());
    budget.define(builder);
    budget.run();
}
} // namespace clause::codegen
