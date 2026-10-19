#pragma once
#include "table.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

// The runtime-wide registry of ETS tables (plan 21 step LIB10A, docs/runtime.md#ets-tables): tables by identifier
// number and by name, and the tables each process owns or fixes, so its end deletes them, hands them to their heirs
// and releases its fixations. Lock order: the executor lock, then this registry's, then a table's; ETS builtins never
// take the executor lock while they hold either.
namespace clause::runtime::detail::ets {
class Tables final {
  public:
    // A table an ended owner passed to its heir: the ETS-TRANSFER message to send.
    struct Transfer {
        std::shared_ptr<Table> table;
        Word heir;
        std::shared_ptr<const DetachedTerm> data;
    };

    // What an ended process left: tables passed to heirs, and tables removed from the registry for the caller to
    // destroy.
    struct Released {
        std::vector<Transfer> transfers;
        std::vector<std::shared_ptr<Table>> dropped;
    };

    // Register a new table owned by its owner; false when it is named and its name is taken.
    bool add(const std::shared_ptr<Table> &table);
    // The live table of an identifier number or a name; null for none.
    std::shared_ptr<Table> find(std::uint64_t number) const;
    std::shared_ptr<Table> named(Word name) const;
    // Every table.
    std::vector<std::shared_ptr<Table>> all() const;
    // Remove a table being deleted; false when it is gone already. The caller destroys it.
    bool remove(const Table &table);
    // Give a table a new name; false when it is named and the name is taken by another table.
    bool rename(Table &table, Word name);

    // Make `owner` the owner of a table (give_away/3).
    void give(Table &table, Word owner);
    // Set or clear (heir 0) a table's heir.
    void set_heir(Table &table, Word heir, std::shared_ptr<const DetachedTerm> data);
    // The heir data of a table, read under the lock.
    std::shared_ptr<const DetachedTerm> heir_data(const Table &table) const;

    // A process fixes a table once more, or releases one fixation (safe_fixtable/2); false when deleted.
    bool fix(Table &table, Word pid, FixTime time);
    bool unfix(Table &table, Word pid);

    // Whether a process owns or fixes any table: the cheap check of every process end.
    bool holds(Word pid) const;
    // A process ended: release its fixations, pass each table it owned to a live heir (`alive`) or drop it.
    Released release(Word pid, const std::function<bool(Word)> &alive);

  private:
    // The tables a process owns and fixes, by identifier number.
    struct Holdings {
        std::vector<std::uint64_t> owned;
        std::vector<std::uint64_t> fixed;
    };

    // Remove a number from a process's list.
    void forget(Word pid, std::vector<std::uint64_t> Holdings::*list, std::uint64_t number);
    // Pass a table of an ended owner on, or drop it; the lock is held alone.
    void release_owned(Word pid, const std::function<bool(Word)> &alive, std::uint64_t number, Released &released);

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Table>> tables_;
    std::unordered_map<Word, std::shared_ptr<Table>> names_;
    std::unordered_map<Word, Holdings> holdings_;
};
} // namespace clause::runtime::detail::ets
