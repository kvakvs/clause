#include "literals.hpp"
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace clause::codegen {
namespace {
// The cells of one module's literal area while its functions are lowered.
struct Pool {
    // Stands for the area until finish_literals defines it; every literal word is an offset into it.
    llvm::GlobalVariable *placeholder = nullptr;
    // The area's words in order, and the offset (in words) of every distinct cell.
    std::vector<llvm::Constant *> words;
    std::map<std::pair<std::vector<llvm::Constant *>, unsigned>, std::size_t> cells;
};

// The pools of the modules being lowered, under one lock: modules may be lowered on several threads.
struct Pools {
    std::mutex mutex;
    std::map<const llvm::Module *, Pool> pools;
};

Pools &all_pools() {
    static Pools pools;
    return pools;
}

llvm::Constant *tagged_word(const Pool &pool, llvm::IntegerType *word, std::size_t offset, unsigned tag) {
    auto &context = word->getContext();
    auto *bytes = llvm::ConstantInt::get(word, offset * (word->getBitWidth() / 8) + tag);
    auto *address = llvm::ConstantExpr::getGetElementPtr(llvm::Type::getInt8Ty(context), pool.placeholder, bytes);
    return llvm::ConstantExpr::getPtrToInt(address, word);
}
} // namespace

llvm::Constant *literal_cell(llvm::Module &output, llvm::IntegerType *word, std::span<llvm::Constant *const> words,
                             unsigned tag) {
    auto &registry = all_pools();
    const std::scoped_lock lock(registry.mutex);
    auto &pool = registry.pools[&output];
    if (!pool.placeholder) {
        pool.placeholder = new llvm::GlobalVariable(output, llvm::ArrayType::get(word, 0), true,
                                                    llvm::GlobalValue::PrivateLinkage, nullptr, "literals.pending");
    }
    auto key = std::make_pair(std::vector<llvm::Constant *>(words.begin(), words.end()), tag);
    const auto [found, added] = pool.cells.try_emplace(std::move(key), pool.words.size());
    if (added) {
        pool.words.insert(pool.words.end(), words.begin(), words.end());
    }
    return tagged_word(pool, word, found->second, tag);
}

llvm::Constant *literal_address(llvm::Constant *word, unsigned tag) {
    auto *expression = llvm::dyn_cast<llvm::ConstantExpr>(word);
    if (!expression || expression->getOpcode() != llvm::Instruction::PtrToInt) {
        return nullptr;
    }
    auto *tagged = expression->getOperand(0);
    auto *offset = llvm::ConstantInt::getSigned(llvm::cast<llvm::IntegerType>(word->getType()), -static_cast<int>(tag));
    return llvm::ConstantExpr::getGetElementPtr(llvm::Type::getInt8Ty(word->getContext()), tagged, offset);
}

void finish_literals(llvm::Module &output, llvm::IntegerType *word, const std::string &prefix) {
    Pool pool;
    {
        auto &registry = all_pools();
        const std::scoped_lock lock(registry.mutex);
        const auto found = registry.pools.find(&output);
        if (found == registry.pools.end()) {
            return;
        }
        pool = std::move(found->second);
        registry.pools.erase(found);
    }
    auto *type = llvm::ArrayType::get(word, pool.words.size());
    auto *area = new llvm::GlobalVariable(output, type, true, llvm::GlobalValue::PrivateLinkage,
                                          llvm::ConstantArray::get(type, pool.words), prefix + ".literals");
    area->setAlignment(llvm::Align(word->getBitWidth() / 8));
    pool.placeholder->replaceAllUsesWith(area);
    pool.placeholder->eraseFromParent();
    // The descriptor's last two fields name the area (abi::v1::ModuleDescriptor::literals, literal_words).
    auto *descriptor = output.getNamedGlobal(prefix + ".descriptor");
    auto *fields = llvm::cast<llvm::ConstantStruct>(descriptor->getInitializer());
    std::vector<llvm::Constant *> operands;
    operands.reserve(fields->getNumOperands());
    for (unsigned i = 0; i < fields->getNumOperands(); ++i) {
        operands.push_back(fields->getAggregateElement(i));
    }
    operands[operands.size() - 2] = area;
    operands.back() = llvm::ConstantInt::get(word, pool.words.size());
    descriptor->setInitializer(llvm::ConstantStruct::get(fields->getType(), operands));
}
} // namespace clause::codegen
