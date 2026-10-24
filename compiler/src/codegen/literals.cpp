#include "literals.hpp"
#include <llvm/IR/Constants.h>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace clause::codegen {
namespace {
// The literals of one module while its functions are lowered.
struct Pool {
    // Stands for the literal cache until finish_literals defines it.
    llvm::GlobalVariable *cache = nullptr;
    // The encoded literals in index order, and the index of each.
    std::vector<const std::string *> order;
    std::map<std::string, std::size_t> indices;
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

// Take the module's pool out of the registry; empty when its functions made no literal.
Pool take_pool(const llvm::Module &output) {
    auto &registry = all_pools();
    const std::scoped_lock lock(registry.mutex);
    const auto found = registry.pools.find(&output);
    if (found == registry.pools.end()) {
        return {};
    }
    auto pool = std::move(found->second);
    registry.pools.erase(found);
    return pool;
}

// The encoded literals back to back as a private constant byte array.
llvm::Constant *encoded_literals(llvm::Module &output, const Pool &pool, const std::string &prefix,
                                 std::size_t &bytes) {
    std::string all;
    for (const auto *encoded : pool.order) {
        all += *encoded;
    }
    bytes = all.size();
    auto *data = llvm::ConstantDataArray::getString(output.getContext(), all, false);
    return new llvm::GlobalVariable(output, data->getType(), true, llvm::GlobalValue::PrivateLinkage, data,
                                    prefix + ".literals");
}

// The writable literal cache, one zeroed word per literal, in place of its placeholder if any.
llvm::Constant *define_cache(llvm::Module &output, Pool &pool, llvm::IntegerType *word, const std::string &prefix) {
    auto *type = llvm::ArrayType::get(word, pool.order.size());
    auto *cache = new llvm::GlobalVariable(output, type, false, llvm::GlobalValue::InternalLinkage,
                                           llvm::ConstantAggregateZero::get(type), prefix + ".literal_cache");
    if (pool.cache) {
        pool.cache->replaceAllUsesWith(cache);
        pool.cache->eraseFromParent();
    }
    return cache;
}
} // namespace

std::size_t literal_index(llvm::Module &output, const std::string &encoded) {
    auto &registry = all_pools();
    const std::scoped_lock lock(registry.mutex);
    auto &pool = registry.pools[&output];
    const auto [found, added] = pool.indices.try_emplace(encoded, pool.order.size());
    if (added) {
        pool.order.push_back(&found->first);
    }
    return found->second;
}

llvm::GlobalVariable *literal_cache(llvm::Module &output, llvm::IntegerType *word) {
    auto &registry = all_pools();
    const std::scoped_lock lock(registry.mutex);
    auto &pool = registry.pools[&output];
    if (!pool.cache) {
        pool.cache = new llvm::GlobalVariable(output, llvm::ArrayType::get(word, 0), false,
                                              llvm::GlobalValue::InternalLinkage, nullptr, "literal_cache.pending");
    }
    return pool.cache;
}

void finish_literals(llvm::Module &output, llvm::IntegerType *word, const std::string &prefix) {
    auto pool = take_pool(output);
    if (pool.order.empty()) {
        if (pool.cache) {
            pool.cache->eraseFromParent();
        }
        return;
    }
    auto *descriptor = output.getNamedGlobal(prefix + ".descriptor");
    auto *fields = llvm::cast<llvm::ConstantStruct>(descriptor->getInitializer());
    std::vector<llvm::Constant *> operands;
    operands.reserve(fields->getNumOperands());
    for (unsigned i = 0; i < fields->getNumOperands(); ++i) {
        operands.push_back(fields->getAggregateElement(i));
    }
    // The encoded literals come after the atom cache, the literal cache last (abi::v1::ModuleDescriptor field order).
    constexpr unsigned LITERALS = 13;
    constexpr unsigned LITERAL_CACHE = 19;
    std::size_t bytes = 0;
    operands[LITERALS] = encoded_literals(output, pool, prefix, bytes);
    operands[LITERALS + 1] = llvm::ConstantInt::get(word, bytes);
    operands[LITERAL_CACHE] = define_cache(output, pool, word, prefix);
    operands[LITERAL_CACHE + 1] = llvm::ConstantInt::get(word, pool.order.size());
    descriptor->setInitializer(llvm::ConstantStruct::get(fields->getType(), operands));
}
} // namespace clause::codegen
