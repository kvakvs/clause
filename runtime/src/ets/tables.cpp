#include "tables.hpp"
#include <algorithm>
#include <mutex>
#include <utility>

// The ETS table registry (plan 21 steps LIB10A, LIB10B): names, ownership, heirs and the end of owners.
namespace clause::runtime::detail::ets {
bool Tables::add(const std::shared_ptr<Table> &table) {
    const std::unique_lock lock(mutex_);
    if (table->options().named && !names_.try_emplace(table->name(), table).second) {
        return false;
    }
    tables_.emplace(table->number(), table);
    holdings_[table->owner()].owned.push_back(table->number());
    return true;
}

std::shared_ptr<Table> Tables::find(std::uint64_t number) const {
    const std::shared_lock lock(mutex_);
    const auto found = tables_.find(number);
    return found == tables_.end() ? nullptr : found->second;
}

std::shared_ptr<Table> Tables::named(Word name) const {
    const std::shared_lock lock(mutex_);
    const auto found = names_.find(name);
    return found == names_.end() ? nullptr : found->second;
}

std::vector<std::shared_ptr<Table>> Tables::all() const {
    const std::shared_lock lock(mutex_);
    std::vector<std::shared_ptr<Table>> tables;
    tables.reserve(tables_.size());
    for (const auto &[number, table] : tables_) {
        tables.push_back(table);
    }
    return tables;
}

bool Tables::remove(const Table &table) {
    const std::unique_lock lock(mutex_);
    if (tables_.erase(table.number()) == 0) {
        return false;
    }
    if (table.options().named) {
        names_.erase(table.name());
    }
    forget(table.owner(), &Holdings::owned, table.number());
    return true;
}

bool Tables::rename(Table &table, Word name) {
    const std::unique_lock lock(mutex_);
    if (table.options().named) {
        if (name == table.name()) {
            return true;
        }
        const auto found = names_.find(table.name());
        if (!names_.try_emplace(name, found->second).second) {
            return false;
        }
        names_.erase(found);
    }
    table.rename(name);
    return true;
}

void Tables::give(Table &table, Word owner) {
    const std::unique_lock lock(mutex_);
    forget(table.owner(), &Holdings::owned, table.number());
    table.set_owner(owner);
    holdings_[owner].owned.push_back(table.number());
}

void Tables::set_heir(Table &table, Word heir, std::shared_ptr<const DetachedTerm> data) {
    const std::unique_lock lock(mutex_);
    table.set_heir(heir, std::move(data));
}

std::shared_ptr<const DetachedTerm> Tables::heir_data(const Table &table) const {
    const std::shared_lock lock(mutex_);
    return table.heir_data();
}

bool Tables::fix(Table &table, Word pid, FixTime time) {
    const std::unique_lock lock(mutex_);
    const auto first = table.fix(pid, time);
    if (first && *first) {
        holdings_[pid].fixed.push_back(table.number());
    }
    return first.has_value();
}

bool Tables::unfix(Table &table, Word pid) {
    const std::unique_lock lock(mutex_);
    const auto last = table.unfix(pid);
    if (last && *last) {
        forget(pid, &Holdings::fixed, table.number());
    }
    return last.has_value();
}

bool Tables::holds(Word pid) const {
    const std::shared_lock lock(mutex_);
    return holdings_.contains(pid);
}

Tables::Released Tables::release(Word pid, const std::function<bool(Word)> &alive) {
    Released released;
    const std::unique_lock lock(mutex_);
    auto node = holdings_.extract(pid);
    if (node.empty()) {
        return released;
    }
    for (const auto number : node.mapped().fixed) {
        if (const auto found = tables_.find(number); found != tables_.end()) {
            found->second->unfix_all(pid);
        }
    }
    for (const auto number : node.mapped().owned) {
        release_owned(pid, alive, number, released);
    }
    return released;
}

void Tables::release_owned(Word pid, const std::function<bool(Word)> &alive, std::uint64_t number, Released &released) {
    const auto found = tables_.find(number);
    if (found == tables_.end()) {
        return;
    }
    auto table = found->second;
    const auto heir = table->heir();
    if (heir != 0 && heir != pid && alive(heir)) {
        table->set_owner(heir);
        holdings_[heir].owned.push_back(number);
        auto data = table->heir_data();
        released.transfers.push_back({.table = std::move(table), .heir = heir, .data = std::move(data)});
        return;
    }
    tables_.erase(found);
    if (table->options().named) {
        names_.erase(table->name());
    }
    released.dropped.push_back(std::move(table));
}

void Tables::forget(Word pid, std::vector<std::uint64_t> Holdings::*list, std::uint64_t number) {
    const auto found = holdings_.find(pid);
    if (found == holdings_.end()) {
        return;
    }
    auto &numbers = found->second.*list;
    if (const auto at = std::ranges::find(numbers, number); at != numbers.end()) {
        numbers.erase(at);
    }
    if (found->second.owned.empty() && found->second.fixed.empty()) {
        holdings_.erase(found);
    }
}
} // namespace clause::runtime::detail::ets
