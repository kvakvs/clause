#include "table.hpp"
#include <utility>

// ETS tables (plan 21 step LIB10A): parts, slots, sizes, fixation and deletion.
namespace clause::runtime::detail::ets {
namespace {
// Words ets:info/2 memory counts for a table and for each object besides the object's own words: the table record
// and part headers, and an index entry per object (Clause's own figures, docs/differences.md).
constexpr std::size_t TABLE_WORDS = 160;
constexpr std::size_t PART_WORDS = 8;
constexpr std::size_t OBJECT_WORDS = 6;

// How a table type stores an object whose key it holds.
Insertion insertion(TableType type) noexcept {
    switch (type) {
    case TableType::bag:
        return Insertion::unique;
    case TableType::duplicate_bag:
        return Insertion::duplicate;
    default:
        return Insertion::replace;
    }
}

std::size_t parts(const TableOptions &options) noexcept {
    const auto hashed = options.type != TableType::ordered_set;
    return hashed && options.write_concurrency != WriteConcurrency::off ? Table::CONCURRENT_PARTS : 1;
}
} // namespace

Table::Table(const TableIdentity &identity, const TableOptions &options)
    : number_(identity.number), options_(options), name_(identity.name), owner_(identity.owner),
      protection_(options.protection), heir_(0), part_count_(parts(options)),
      parts_(std::make_unique<Part[]>(part_count_)) {
    for (std::size_t at = 0; at < part_count_; ++at) {
        parts_[at].index = fresh_index();
    }
}

std::unique_ptr<Index> Table::fresh_index() const {
    return hashed() ? hash_index(options_.keypos, insertion(options_.type)) : tree_index(options_.keypos);
}

Reached Table::slot(std::size_t index, std::vector<Word> &objects) const {
    return visit_slot(index, [&objects](std::span<const Word> found) { objects.assign(found.begin(), found.end()); });
}

std::optional<std::size_t> Table::size() const {
    std::size_t total = 0;
    if (!read_parts([&total](const Index &index) { total += index.size(); })) {
        return std::nullopt;
    }
    return total;
}

std::optional<std::size_t> Table::words() const {
    std::size_t total = TABLE_WORDS + (part_count_ * PART_WORDS);
    if (!read_parts([&total](const Index &index) { total += index.words() + (index.size() * OBJECT_WORDS); })) {
        return std::nullopt;
    }
    return total;
}

std::optional<bool> Table::fix(Word pid, FixTime time) {
    const std::unique_lock table(mutex_);
    if (deleted_) {
        return std::nullopt;
    }
    if (fixers_.empty()) {
        fixed_monotonic_ = time.monotonic_nanos;
        fixed_system_ = time.system_micros;
        for (std::size_t at = 0; at < part_count_; ++at) {
            parts_[at].index->fix();
        }
    }
    return ++fixers_[pid] == 1;
}

std::optional<bool> Table::unfix(Word pid) {
    const std::unique_lock table(mutex_);
    if (deleted_) {
        return std::nullopt;
    }
    const auto found = fixers_.find(pid);
    if (found == fixers_.end()) {
        return false;
    }
    const auto last = --found->second == 0;
    if (last) {
        fixers_.erase(found);
        if (fixers_.empty()) {
            for (std::size_t at = 0; at < part_count_; ++at) {
                parts_[at].index->unfix();
            }
        }
    }
    return last;
}

void Table::unfix_all(Word pid) {
    const std::unique_lock table(mutex_);
    if (deleted_ || fixers_.erase(pid) == 0 || !fixers_.empty()) {
        return;
    }
    for (std::size_t at = 0; at < part_count_; ++at) {
        parts_[at].index->unfix();
    }
}

std::optional<Fixation> Table::fixation() const {
    const std::shared_lock table(mutex_);
    if (deleted_ || fixers_.empty()) {
        return std::nullopt;
    }
    Fixation fixation{.monotonic_nanos = fixed_monotonic_, .system_micros = fixed_system_, .holders = {}};
    // OTP lists the fixing processes from the highest pid down.
    for (auto at = fixers_.rbegin(); at != fixers_.rend(); ++at) {
        fixation.holders.emplace_back(at->first, at->second);
    }
    return fixation;
}

std::optional<bool> Table::fixed() const {
    const std::shared_lock table(mutex_);
    if (deleted_) {
        return std::nullopt;
    }
    return !fixers_.empty();
}

Table::Contents Table::destroy() {
    Contents contents;
    const std::unique_lock table(mutex_);
    deleted_ = true;
    fixers_.clear();
    for (std::size_t at = 0; at < part_count_; ++at) {
        contents.push_back(std::move(parts_[at].index));
    }
    return contents;
}

std::optional<Table::Contents> Table::empty() {
    Contents contents;
    const std::unique_lock table(mutex_);
    if (deleted_) {
        return std::nullopt;
    }
    for (std::size_t at = 0; at < part_count_; ++at) {
        if (fixers_.empty() || !hashed()) {
            contents.push_back(std::exchange(parts_[at].index, fresh_index()));
        } else {
            parts_[at].index->clear();
        }
    }
    return contents;
}

void Table::set_heir(Word heir, std::shared_ptr<const DetachedTerm> data) noexcept {
    heir_data_ = std::move(data);
    heir_.store(heir, std::memory_order_release);
}
} // namespace clause::runtime::detail::ets
