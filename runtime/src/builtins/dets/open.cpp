#include "open.hpp"
#include "bulk.hpp"
#include <cstdlib>

namespace clause::runtime::builtins::dets {
namespace {
File open_source(const std::string &file, bool read_only, bool ram) {
    if (ram) {
        return File::memory(file, read_whole(file));
    }
    return File::open(file, read_only ? File::Mode::read : File::Mode::read_write);
}

// The slots a repair aims at, as dets:fsck/6 chooses them from the arguments and the file.
struct RepairSlots {
    std::uint32_t min;
    std::uint32_t estimate;
    std::uint32_t max;
};

// A repair is tried again with the number of keys when the estimate was more than five segments off.
bool estimate_off(const RepairSlots &slots, std::uint64_t keys) {
    const auto segments = no_segments(static_cast<std::int64_t>(keys));
    return std::abs(segments - no_segments(slots.estimate)) > 5 && segments <= no_segments(slots.max) &&
           segments >= no_segments(slots.min);
}

// One repair into `file`.TMP, renamed over the file when it succeeds: dets:fsck_try/5. False when the estimate of
// the number of slots was off; `keys` then has the number of keys found.
bool fsck_try(const Terms &terms, File &source, const FileHeader &header, const RepairSlots &slots,
              std::uint64_t &keys) {
    const auto name = source.name();
    const auto temporary = name + ".TMP";
    remove_file(temporary);
    const TableShape shape{.type = *type_of_code(header.type),
                           .keypos = header.keypos,
                           .min_slots = slots.estimate,
                           .max_slots = slots.max};
    auto target = Table::initiate(File::open(temporary, File::Mode::create), shape, true);
    auto bulk = bulk_collections(target, terms, repair_input(source, header, target, terms));
    if (estimate_off(slots, bulk.counts.keys)) {
        keys = bulk.counts.keys;
        target.file().close();
        remove_file(temporary);
        return false;
    }
    source.close();
    write_bulk(target, bulk.collections, bulk.counts, false);
    target.save(false);
    target.file().close();
    rename_file(temporary, name);
    return true;
}

// Repair a file not properly closed: dets:fsck/6.
void fsck(const Terms &terms, File source, const FileHeader &header, std::optional<std::uint32_t> min_slots,
          std::optional<std::uint32_t> max_slots) {
    RepairSlots slots{
        .min = min_slots.value_or(header.min_slots), .estimate = 0, .max = max_slots.value_or(header.max_slots)};
    slots.estimate = std::min(slots.max, std::max(slots.min, header.no_keys));
    std::uint64_t keys = 0;
    if (fsck_try(terms, source, header, slots, keys)) {
        return;
    }
    slots.estimate = static_cast<std::uint32_t>(keys);
    if (!fsck_try(terms, source, header, slots, keys)) {
        fail(Failure::Shape::named, "cannot_repair", source.name());
    }
}

// What fopen_existing_file/2 does with a file whose header was read.
enum class Action : std::uint8_t { final, compact, repair };

// A forced repair of a properly closed file keeping its slots is a compaction.
bool compactable(const OpenArgs &args, const FileHeader &header) {
    return !args.read_only && header.collections && (!args.min_slots || *args.min_slots == header.min_slots) &&
           (!args.max_slots || *args.max_slots == header.max_slots);
}

// A properly closed file is used as it is, or compacted or repaired when the repair is forced.
Action closed_action(const OpenArgs &args, const FileHeader &header) {
    if (args.repair != OpenArgs::Repair::force) {
        return Action::final;
    }
    if (compactable(args, header)) {
        return Action::compact;
    }
    if (args.read_only) {
        fail(Failure::Shape::named, "access_mode", args.file);
    }
    return Action::repair;
}

Action action_of(const OpenArgs &args, const FileHeader &header, const std::string &status) {
    if (status == "ok") {
        return closed_action(args, header);
    }
    if (status == "not_closed" && args.repair == OpenArgs::Repair::no) {
        fail(Failure::Shape::named, "needs_repair", args.file);
    }
    if (status == "not_closed" && !args.read_only) {
        return Action::repair;
    }
    fail(Failure::Shape::named, status, args.file);
}

// Copy a properly closed file's collections into a fresh file with the same slots, renamed over it: dets:compact/1.
// False when the copy failed; the file is then repaired.
bool compact(const Terms &terms, File source, const Head &head, FreeLists lists) {
    const auto name = source.name();
    const auto temporary = name + ".TMP";
    remove_file(temporary);
    try {
        auto read = Table::opened(std::move(source), head, std::move(lists), true);
        auto target = Table::copy_of(File::open(temporary, File::Mode::create), read.head());
        const auto blocks = compact_input(read, terms);
        write_bulk(target, blocks, {.objects = head.no_objects, .keys = head.no_keys}, true);
        target.save(false);
        target.file().close();
        read.file().close();
        rename_file(temporary, name);
        return true;
    } catch (const Failure &) {
        remove_file(temporary);
        return false;
    }
}

OpenArgs unforced(OpenArgs args) {
    args.repair = OpenArgs::Repair::no;
    return args;
}

Table create(const OpenArgs &args) {
    auto file = args.ram ? File::memory(args.file, {}) : File::open(args.file, File::Mode::create);
    try {
        const TableShape shape{.type = args.type,
                               .keypos = args.keypos,
                               .min_slots = args.min_slots.value_or(DEFAULT_MIN_SLOTS),
                               .max_slots = args.max_slots.value_or(DEFAULT_MAX_SLOTS)};
        return Table::initiate(std::move(file), shape, true);
    } catch (const Failure &) {
        if (!args.ram) {
            remove_file(args.file);
        }
        throw;
    }
}

Table open_existing_file(const Terms &terms, const OpenArgs &args) {
    auto source = open_source(args.file, args.read_only, args.ram);
    const auto header = read_file_header(source);
    const auto check = check_file_header(header);
    auto action = action_of(args, header, check.status);
    std::optional<FreeLists> lists;
    if (action != Action::repair) {
        lists = read_free_lists(source, check.head);
        action = lists ? action : Action::repair;
    }
    if (type_of_code(header.type) != args.type) {
        fail(Failure::Shape::named, "type_mismatch", args.file);
    }
    if (header.keypos != args.keypos) {
        fail(Failure::Shape::named, "keypos_mismatch", args.file);
    }
    if (action == Action::compact) {
        if (!compact(terms, std::move(source), check.head, std::move(*lists))) {
            fsck(terms, open_source(args.file, args.read_only, args.ram), header, args.min_slots, args.max_slots);
        }
        return open_existing_file(terms, unforced(args));
    }
    if (action == Action::repair) {
        fsck(terms, std::move(source), header, args.min_slots, args.max_slots);
        return open_existing_file(terms, unforced(args));
    }
    return Table::opened(std::move(source), check.head, std::move(*lists), args.read_only);
}
} // namespace

Table open_table(const Terms &terms, const OpenArgs &args, bool &created) {
    created = !file_exists(args.file);
    if (created && args.read_only) {
        fail(Failure::Shape::file_error, "enoent", args.file);
    }
    return created ? create(args) : open_existing_file(terms, args);
}

Table open_existing(const Terms &terms, const std::string &file) {
    if (!file_exists(file)) {
        fail(Failure::Shape::file_error, "enoent", file);
    }
    auto source = File::open(file, File::Mode::read_write);
    const auto header = read_file_header(source);
    const auto check = check_file_header(header);
    if (check.status != "ok" && check.status != "not_closed") {
        fail(Failure::Shape::named, check.status, file);
    }
    auto lists = check.status == "ok" ? read_free_lists(source, check.head) : std::nullopt;
    if (!lists) {
        fsck(terms, std::move(source), header, std::nullopt, std::nullopt);
        return open_existing(terms, file);
    }
    return Table::opened(std::move(source), check.head, std::move(*lists), false);
}

bool is_dets_file(const std::string &file) {
    try {
        const auto source = File::open(file, File::Mode::read);
        return read_file_header(source).cookie == MAGIC;
    } catch (const Failure &failure) {
        if (failure.reason == "tooshort" || failure.reason == "not_a_dets_file") {
            return false;
        }
        throw;
    }
}
} // namespace clause::runtime::builtins::dets
