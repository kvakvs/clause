#include <clause/runtime/interpreted_modules.hpp>
#include <mutex>

// The registry of interpreted modules (plan 21 LIB77B).
namespace clause::runtime {
void InterpretedModules::add(Word module, Module entry, const void *route) {
    const std::unique_lock lock(mutex_);
    modules_.insert_or_assign(module, std::move(entry));
    route_ = route;
    any_.store(true, std::memory_order_release);
}

bool InterpretedModules::remove(Word module) {
    const std::unique_lock lock(mutex_);
    const auto removed = modules_.erase(module) != 0;
    any_.store(!modules_.empty(), std::memory_order_release);
    return removed;
}

const void *InterpretedModules::route(Word module) const noexcept {
    if (!any_.load(std::memory_order_acquire)) {
        return nullptr;
    }
    const std::shared_lock lock(mutex_);
    return modules_.contains(module) ? route_ : nullptr;
}

bool InterpretedModules::exported(Word module, const Export &function) const noexcept {
    if (!any_.load(std::memory_order_acquire)) {
        return false;
    }
    const std::shared_lock lock(mutex_);
    const auto found = modules_.find(module);
    return found != modules_.end() && found->second.exports.contains(function);
}

std::optional<std::u32string> InterpretedModules::file(Word module) const {
    if (!any_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    const std::shared_lock lock(mutex_);
    const auto found = modules_.find(module);
    return found == modules_.end() ? std::nullopt : std::optional{found->second.file};
}

std::vector<std::pair<Word, std::u32string>> InterpretedModules::modules() const {
    std::vector<std::pair<Word, std::u32string>> listed;
    if (!any_.load(std::memory_order_acquire)) {
        return listed;
    }
    const std::shared_lock lock(mutex_);
    for (const auto &[module, entry] : modules_) {
        listed.emplace_back(module, entry.file);
    }
    return listed;
}
} // namespace clause::runtime
