#include "work_list.hpp"
#include <algorithm>
#include <iterator>
#include <map>

namespace clause::runtime::builtins::dets {
namespace {
using Items = std::vector<Item>;

// The commands of one key, oldest first.
struct KeyWork {
    Term key;
    std::vector<Command> commands;
};

bool item_less(const Item &left, const Item &right) {
    const auto order = Terms::cmp(left.term, right.term);
    return order != 0 ? order < 0 : left.seq < right.seq;
}

// dets_utils:msort/1 of {Term, Seq, _} items.
Items msorted(Items items) {
    std::ranges::stable_sort(items, item_less);
    return items;
}

// lists:keysort(2, Items): by sequence number.
Items by_seq(Items items) {
    std::ranges::stable_sort(items, {}, &Item::seq);
    return items;
}

std::vector<Term> terms_of(const Items &items) {
    std::vector<Term> terms;
    for (const auto &item : items) {
        terms.push_back(item.term);
    }
    return terms;
}

Item item_of(const Command &command) {
    return {.term = command.object,
            .seq = command.seq,
            .source = command.kind == Command::Kind::insert ? Item::Source::insert : Item::Source::delete_object,
            .bytes = {}};
}

// dets_utils:family/1: the commands grouped per key in key order, each key's by sequence number.
std::vector<KeyWork> family(std::vector<Command> commands) {
    std::ranges::stable_sort(
        commands, [](const Command &left, const Command &right) { return Terms::cmp(left.key, right.key) < 0; });
    std::vector<KeyWork> works;
    for (auto &command : commands) {
        if (works.empty() || !Terms::same(works.back().key, command.key)) {
            works.push_back({.key = command.key, .commands = {}});
        }
        works.back().commands.push_back(command);
    }
    for (auto &work : works) {
        std::ranges::stable_sort(work.commands, {}, &Command::seq);
    }
    return works;
}

// dets_v9:bag_object/6: the last of the commands on one term from `at` decides whether it is in the bag.
void bag_object(const Items &commands, std::size_t &at, Items &out, std::int64_t &objects) {
    const auto term = commands[at].term;
    std::optional<Item> current;
    for (; at < commands.size() && Terms::same(commands[at].term, term); ++at) {
        current = commands[at].source == Item::Source::insert ? std::optional(commands[at]) : std::nullopt;
    }
    if (current) {
        out.push_back(*current);
        ++objects;
    }
}

// dets_v9:dup_object/6: the commands on one term after `at` add copies or delete them all.
void dup_object(const Items &commands, std::size_t &at, const Term &term, Items queue, Items &out,
                std::int64_t &objects) {
    for (; at < commands.size() && Terms::same(commands[at].term, term); ++at) {
        if (commands[at].source == Item::Source::insert) {
            queue.push_back(commands[at]);
            ++objects;
        } else {
            objects -= static_cast<std::int64_t>(queue.size());
            queue.clear();
        }
    }
    std::ranges::move(queue, std::back_inserter(out));
}

// The stored copies of `term` from `kept` on (dets_v9:old_dup_object/6).
Items copies(const Items &old, std::size_t &kept, const Term &term) {
    Items queue;
    for (; kept < old.size() && Terms::same(old[kept].term, term); ++kept) {
        queue.push_back(old[kept]);
    }
    return queue;
}

// A key's sorted commands merged into its sorted objects: dets_v9:eval_bag/4 and eval_dupbag/4. The new objects
// come in no order; eval_end/6 sorts them by sequence number.
class Merge final {
  public:
    // The sorted commands and stored objects of a key.
    struct Sides {
        const Items &commands;
        const Items &stored;
    };

    Merge(const Sides &sides, std::int64_t &objects)
        : commands_(sides.commands), old_(sides.stored), objects_(objects) {}

    Items bag() {
        while (!done()) {
            if (!old_first()) {
                bag_step();
            }
        }
        return std::move(out_);
    }

    Items duplicate_bag() {
        while (!done()) {
            if (!old_first()) {
                duplicate_bag_step();
            }
        }
        return std::move(out_);
    }

  private:
    bool done() const { return at_ == commands_.size() && kept_ == old_.size(); }

    // The order of the next command's term to the next stored object's; -1 when none is stored.
    int order() const { return kept_ < old_.size() ? Terms::cmp(commands_[at_].term, old_[kept_].term) : -1; }

    // A stored object goes first when no command or one on a later term comes; whether it went.
    bool old_first() {
        if (kept_ < old_.size() && (at_ == commands_.size() || order() > 0)) {
            out_.push_back(old_[kept_++]);
            return true;
        }
        return false;
    }

    void bag_step() {
        if (order() == 0) {
            ++kept_;
            --objects_;
        } else if (commands_[at_].source == Item::Source::delete_object) {
            ++at_;
            return;
        }
        bag_object(commands_, at_, out_, objects_);
    }

    void duplicate_bag_step() {
        const auto ordered = order();
        if (ordered < 0 && commands_[at_].source == Item::Source::delete_object) {
            ++at_;
        } else if (ordered == 0) {
            const auto term = commands_[at_].term;
            dup_object(commands_, at_, term, copies(old_, kept_, term), out_, objects_);
        } else {
            const auto first = commands_[at_++];
            ++objects_;
            dup_object(commands_, at_, first.term, {first}, out_, objects_);
        }
    }

    const Items &commands_;
    const Items &old_;
    std::int64_t &objects_;
    std::size_t at_ = 0;
    std::size_t kept_ = 0;
    Items out_;
};

// The new bytes of one key: unchanged, or its objects' bytes (none when it has no objects) and object delta.
struct KeyResult {
    bool changed = false;
    Bytes bytes;
    std::int64_t objects = 0;
};

// dets_v9:eval_key1/9 and eval_end/6 for one key's commands.
class KeyEval final {
  public:
    KeyEval(const Terms &terms, TableType type, std::vector<Found> &found, const Items &original)
        : terms_(terms), type_(type), found_(found), original_(original) {}

    KeyResult run(const std::vector<Command> &commands, Items old) {
        for (std::size_t at = 0; at < commands.size(); ++at) {
            const auto &command = commands[at];
            if (command.kind == Command::Kind::lookup) {
                auto now = by_seq(merged(old));
                found_.push_back({.asker = command.object, .objects = terms_of(now)});
                if (at + 1 == commands.size()) {
                    return end(now);
                }
                old = msorted(std::move(now));
            } else if (command.kind == Command::Kind::delete_key) {
                objects_ -= static_cast<std::int64_t>(old.size());
                old.clear();
                pending_.clear();
            } else if (type_ == TableType::set) {
                set_step(command, old);
            } else {
                pending_.push_back(item_of(command));
            }
        }
        return end(merged(old));
    }

  private:
    void set_step(const Command &command, Items &old) {
        const bool present = old.size() == 1 && Terms::same(old.front().term, command.object);
        if (command.kind == Command::Kind::insert && !present) {
            objects_ += 1 - static_cast<std::int64_t>(old.size());
            old = {item_of(command)};
        } else if (command.kind == Command::Kind::delete_object && present) {
            old.clear();
            --objects_;
        }
    }

    // dets_v9:eval_comms/4: the pending bag commands merged into the objects.
    Items merged(const Items &old) {
        if (type_ == TableType::set) {
            return old;
        }
        const auto commands = msorted(std::exchange(pending_, {}));
        Merge merge({.commands = commands, .stored = old}, objects_);
        return type_ == TableType::bag ? merge.bag() : merge.duplicate_bag();
    }

    KeyResult end(const Items &now) {
        const auto sorted = by_seq(now);
        const bool same = sorted.size() == original_.size() &&
                          std::ranges::equal(sorted, original_, [](const Item &left, const Item &right) {
                              return Terms::same(left.term, right.term);
                          });
        if (same) {
            return {};
        }
        KeyResult result{.changed = true, .bytes = {}, .objects = objects_};
        if (sorted.empty()) {
            return result;
        }
        for (const auto &item : sorted) {
            append_item(result.bytes, item);
        }
        if (type_ != TableType::set) {
            Bytes framed;
            append_u32(framed, static_cast<std::uint32_t>(result.bytes.size() + 4));
            append(framed, result.bytes);
            result.bytes = std::move(framed);
        }
        return result;
    }

    void append_item(Bytes &out, const Item &item) const {
        if (item.source == Item::Source::stored) {
            append(out, item.bytes);
            return;
        }
        const auto encoded = terms_.encode(item.term);
        append_u32(out, static_cast<std::uint32_t>(encoded.size() + 4));
        append(out, encoded);
    }

    const Terms &terms_;
    TableType type_;
    std::vector<Found> &found_;
    // The key's objects as stored, in stored order.
    const Items &original_;
    // The bag commands not yet merged, and the change of the number of objects.
    Items pending_;
    std::int64_t objects_ = 0;
};

// The new collection of a slot: its keys' bytes, the changes of objects and keys, and whether anything changed.
struct SlotResult {
    Bytes bytes;
    std::int64_t objects = 0;
    std::int64_t keys = 0;
    bool changed = false;
};

// One slot's work merged with its stored keys: dets_v9:eval_slot/9 and eval_key/9.
class SlotEval final {
  public:
    SlotEval(const Terms &terms, TableType type, std::vector<Found> &found)
        : terms_(terms), type_(type), found_(found) {}

    SlotResult run(const std::vector<KeyWork> &works, const std::vector<StoredKey> &stored) {
        SlotResult result;
        std::size_t kept = 0;
        for (const auto &work : works) {
            for (; kept < stored.size() && Terms::cmp(stored[kept].key, work.key) < 0; ++kept) {
                append(result.bytes, stored[kept].bytes);
            }
            const bool found = kept < stored.size() && Terms::same(stored[kept].key, work.key);
            eval_key(work, found ? &stored[kept++] : nullptr, result);
        }
        for (; kept < stored.size(); ++kept) {
            append(result.bytes, stored[kept].bytes);
        }
        return result;
    }

  private:
    void eval_key(const KeyWork &work, const StoredKey *stored, SlotResult &result) {
        Items original;
        if (stored != nullptr) {
            original.push_back(stored->first);
            std::ranges::move(Table::other_objects(terms_, stored->rest), std::back_inserter(original));
        }
        if (work.commands.size() == 1 && work.commands.front().kind == Command::Kind::lookup) {
            found_.push_back({.asker = work.commands.front().object, .objects = terms_of(by_seq(original))});
            if (stored != nullptr) {
                append(result.bytes, stored->bytes);
            }
            return;
        }
        const auto had = !original.empty();
        const auto key = KeyEval(terms_, type_, found_, original).run(work.commands, msorted(original));
        if (!key.changed) {
            if (had) {
                append(result.bytes, stored->bytes);
            }
            return;
        }
        changed_key(key, had, result);
    }

    // A key whose objects changed: its new bytes, and the key counted when it came or went.
    static void changed_key(const KeyResult &key, bool had, SlotResult &result) {
        append(result.bytes, key.bytes);
        result.objects += key.objects;
        if (!had && !key.bytes.empty()) {
            ++result.keys;
        } else if (had && key.bytes.empty()) {
            --result.keys;
        }
        result.changed = true;
    }

    const Terms &terms_;
    TableType type_;
    std::vector<Found> &found_;
};

// A slot whose collection is read with others: its pointer and size, its slot position and its work.
struct Queued {
    std::uint32_t pointer;
    std::uint32_t size;
    std::uint32_t slot_position;
    const std::vector<KeyWork> *works;
};

// dets_v9:read_buckets/11 and update_no_keys/4 over the slots a flush touches, highest first.
class Buckets final {
  public:
    Buckets(Table &table, const Terms &terms) : table_(table), terms_(terms) {}

    std::vector<Found> run(const std::map<std::uint32_t, std::vector<KeyWork>> &slots) {
        std::vector<std::pair<std::uint64_t, std::uint32_t>> positions;
        std::vector<std::pair<std::uint32_t, const std::vector<KeyWork> *>> order;
        for (auto it = slots.rbegin(); it != slots.rend(); ++it) {
            const auto position = table_.slot_position(it->first);
            positions.emplace_back(position, SLOT_BYTES);
            order.emplace_back(position, &it->second);
        }
        const auto pointers = table_.pread_all(positions);
        for (std::size_t at = 0; at < order.size();) {
            at += visit(order[at].first, *order[at].second, pointers[at]) ? 1 : 0;
        }
        flush();
        finish();
        std::ranges::reverse(found_);
        return std::move(found_);
    }

  private:
    // Evaluate or queue one slot; false when the queue had to be flushed first.
    bool visit(std::uint32_t position, const std::vector<KeyWork> &works, const Bytes &pointer) {
        const auto size = read_u32(pointer, 0);
        const auto address = pointer.size() >= SLOT_BYTES ? read_u32(pointer, 4) : 0;
        if (address == 0) {
            evaluate(works, position, 0, 0, {});
            return true;
        }
        if (so_far_ + size < MAX_COLLECTION || queue_.empty()) {
            queue_.push_back({.pointer = address, .size = size, .slot_position = position, .works = &works});
            so_far_ += size;
            return true;
        }
        flush();
        return false;
    }

    void evaluate(const std::vector<KeyWork> &works, std::uint32_t position, std::uint32_t pointer,
                  std::uint32_t old_size, const std::vector<StoredKey> &stored) {
        auto slot = SlotEval(terms_, table_.head().type, found_).run(works, stored);
        auto changes = table_.updated({.collection = pointer, .size = old_size, .slot = position}, slot.bytes,
                                      slot.changed, slot.objects == 0 && slot.keys == 0);
        std::ranges::move(changes.pointers.rbegin(), changes.pointers.rend(), std::back_inserter(writes_));
        std::ranges::move(changes.collections.rbegin(), changes.collections.rend(), std::back_inserter(writes_));
        objects_ += slot.objects;
        keys_ += slot.keys;
    }

    // Read the queued collections in file order, evaluate them and write everything so far.
    void flush() {
        if (queue_.empty()) {
            return;
        }
        std::ranges::stable_sort(queue_, {}, &Queued::pointer);
        std::vector<std::pair<std::uint64_t, std::uint32_t>> reads;
        reads.reserve(queue_.size());
        for (const auto &queued : queue_) {
            reads.emplace_back(queued.pointer, queued.size);
        }
        const auto collections = table_.pread_all(reads);
        try {
            for (std::size_t at = 0; at < queue_.size(); ++at) {
                const auto &queued = queue_[at];
                const auto stored = table_.per_key(terms_, collections[at]);
                evaluate(*queued.works, queued.slot_position, queued.pointer,
                         static_cast<std::uint32_t>(collections[at].size()), stored);
            }
        } catch (const BadObject &) {
            bad_object("read_buckets", table_.name());
        }
        table_.apply(std::exchange(writes_, {}));
        queue_.clear();
        so_far_ = 0;
    }

    // Count the objects and keys, rewriting the header when the keys cross a segment boundary; write the rest.
    void finish() {
        auto &head = table_.head();
        if (objects_ != 0 || keys_ != 0) {
            const auto old_keys = head.no_keys;
            head.no_keys = static_cast<std::uint32_t>(head.no_keys + keys_);
            head.no_objects = static_cast<std::uint32_t>(head.no_objects + objects_);
            if (head.no_keys <= head.max_slots && old_keys / SEGMENT_SLOTS != head.no_keys / SEGMENT_SLOTS) {
                writes_.emplace_back(0, file_header(head, 0, NOT_PROPERLY_CLOSED));
            }
        }
        table_.apply(std::exchange(writes_, {}));
    }

    Table &table_;
    const Terms &terms_;
    std::vector<Found> found_;
    Writes writes_;
    std::vector<Queued> queue_;
    std::uint64_t so_far_ = 0;
    std::int64_t objects_ = 0;
    std::int64_t keys_ = 0;
};

// The objects of one key looked up alone: dets_v9:eval_work_list/2's first clause.
std::vector<Found> lookup_one(Table &table, const Terms &terms, const KeyWork &work) {
    const auto collection = table.read_slot(table.slot_position(table.slot_of(terms, work.key)), "eval_work_list");
    std::vector<Term> objects;
    try {
        for (const auto &stored :
             collection.bytes.empty() ? std::vector<StoredKey>{} : table.per_key(terms, collection.bytes)) {
            if (Terms::same(stored.key, work.key)) {
                objects.push_back(stored.first.term);
                std::ranges::move(terms_of(Table::other_objects(terms, stored.rest)), std::back_inserter(objects));
                break;
            }
        }
    } catch (const BadObject &) {
        bad_object("eval_work_list", table.name());
    }
    return {{.asker = work.commands.front().object, .objects = std::move(objects)}};
}
} // namespace

std::vector<Found> write_cache(Table &table, const Terms &terms, std::vector<Command> commands, std::uint64_t inserts) {
    auto works = family(std::move(commands));
    table.may_grow(terms, std::min<std::uint64_t>(inserts, works.size()), false);
    if (works.size() == 1 && works.front().commands.size() == 1 &&
        works.front().commands.front().kind == Command::Kind::lookup) {
        return lookup_one(table, terms, works.front());
    }
    std::map<std::uint32_t, std::vector<KeyWork>> slots;
    for (auto &work : works) {
        slots[table.slot_of(terms, work.key)].push_back(std::move(work));
    }
    return Buckets(table, terms).run(slots);
}
} // namespace clause::runtime::builtins::dets

namespace clause::runtime::builtins::dets {
Bytes inserted_keys(const Terms &terms, TableType type, std::vector<Command> inserts, Counts &counts) {
    Bytes bytes;
    std::vector<Found> unused;
    const Items none;
    for (const auto &work : family(std::move(inserts))) {
        const auto key = KeyEval(terms, type, unused, none).run(work.commands, {});
        append(bytes, key.bytes);
        counts.objects += static_cast<std::uint64_t>(key.objects);
        counts.keys += key.bytes.empty() ? 0 : 1;
    }
    return bytes;
}
} // namespace clause::runtime::builtins::dets
