#include "../../match_spec/program.hpp"
#include "../../terms/identities.hpp"
#include "../typed.hpp"
#include "bulk.hpp"
#include "open.hpp"
#include "scan.hpp"
#include "work_list.hpp"
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <terms.hpp>

// The builtins of Clause's private clause_dets module (plan 21 step LIB53, docs/builtins.md#dets): a table process
// of library/stdlib/dets.erl holds a handle to its open file and calls these for every operation on the file; the
// write cache, users, fixes and timers stay in the process. Failures of the file come back as {error, Reason} in
// OTP's shapes.
namespace clause::runtime::builtins::dets {
namespace {
// The buffer of a handle: a magic number and the table it owns.
struct Holder {
    std::uint64_t magic;
    Table *table;
};

constexpr std::uint64_t HOLDER_MAGIC = 0x646574735f746162; // "dets_tab"

using HandleArgument = Named<struct HandleRole>;
using TermArgument = Named<struct TermRole>;
using OptionArgument = Named<struct OptionRole>;

void finalize(std::span<std::byte> bytes) noexcept {
    Holder holder{};
    std::memcpy(&holder, bytes.data(), sizeof(holder));
    delete holder.table;
}

Table &table_of(const HandleArgument &handle) {
    const auto buffer = detail::HandleAccess::buffer(handle.term);
    if (!buffer || (*buffer)->size() != sizeof(Holder)) {
        bad_argument();
    }
    Holder holder{};
    std::memcpy(&holder, (*buffer)->data(), sizeof(holder));
    if (holder.magic != HOLDER_MAGIC || holder.table == nullptr) {
        bad_argument();
    }
    return *holder.table;
}

Term handle_of(ProcessContext &context, Table table) {
    const Holder holder{.magic = HOLDER_MAGIC, .table = new Table(std::move(table))};
    std::array<std::byte, sizeof(Holder)> bytes{};
    std::memcpy(bytes.data(), &holder, sizeof(holder));
    return need(detail::HandleAccess::make(context.heap(), bytes, finalize));
}

Term integer(ProcessContext &context, std::int64_t value) { return need(TermFactory(context).integer(value)); }

Term tuple(ProcessContext &context, std::span<const Term> elements) {
    return need(TermFactory(context).tuple(elements));
}

Term list(ProcessContext &context, std::span<const Term> elements) { return need(TermFactory(context).list(elements)); }

Term binary(ProcessContext &context, std::span<const std::byte> bytes) {
    return need(TermFactory(context).binary(bytes));
}

Term ok(ProcessContext &context) { return atom(context, "ok"_atom); }

void append_utf8(std::string &out, std::uint32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6U)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12U)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18U)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
    }
}

// A file name as dets keeps it, a list of characters, in UTF-8; badarg for anything else.
std::string name_of(const Term &name) {
    std::string out;
    auto rest = name;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto code = small(rest.head_unchecked().word());
        if (!code || *code < 0 || *code > 0x10FFFF) {
            bad_argument();
        }
        append_utf8(out, static_cast<std::uint32_t>(*code));
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return out;
}

// The bytes of the UTF-8 sequence a lead byte starts.
std::size_t sequence_length(unsigned char lead) {
    if (lead < 0x80) {
        return 1;
    }
    if (lead < 0xE0) {
        return 2;
    }
    return lead < 0xF0 ? 3 : 4;
}

// The code point of the UTF-8 sequence at `at`, and the bytes it took.
std::pair<std::uint32_t, std::size_t> code_at(const std::string &name, std::size_t at) {
    const auto lead = static_cast<unsigned char>(name[at]);
    const auto length = sequence_length(lead);
    std::uint32_t code = length == 1 ? lead : lead & (0x7FU >> length);
    for (std::size_t index = 1; index < length && at + index < name.size(); ++index) {
        code = (code << 6U) | (static_cast<unsigned char>(name[at + index]) & 0x3FU);
    }
    return {code, length};
}

// The list of characters of a UTF-8 name.
Term name_term(ProcessContext &context, const std::string &name) {
    std::vector<Term> codes;
    for (std::size_t at = 0; at < name.size();) {
        const auto [code, length] = code_at(name, at);
        codes.push_back(integer(context, code));
        at += length;
    }
    return list(context, codes);
}

// {error, Reason} of a failure, in OTP's shapes.
Term error_term(ProcessContext &context, const Failure &failure) {
    const auto file = name_term(context, failure.file);
    Term reason;
    switch (failure.shape) {
    case Failure::Shape::named:
        reason = tuple(context, std::array{atom(context, failure.reason), file});
        break;
    case Failure::Shape::file_error:
        reason = tuple(context, std::array{atom(context, "file_error"_atom), file, atom(context, failure.reason)});
        break;
    case Failure::Shape::bad_object:
        reason = tuple(context, std::array{tuple(context, std::array{atom(context, "bad_object"_atom),
                                                                     atom(context, failure.reason)}),
                                           file});
        break;
    case Failure::Shape::bare:
        reason = atom(context, failure.reason);
        break;
    }
    return tuple(context, std::array{atom(context, "error"_atom), reason});
}

// Run an operation on the file, its failure as {error, Reason}.
template <typename Operation> Term guarded(ProcessContext &context, Operation operation) {
    try {
        return operation();
    } catch (const Failure &failure) {
        return error_term(context, failure);
    }
}

std::string spelling(const Term &term) {
    if (!term.is_atom()) {
        bad_argument();
    }
    return std::string(term.atom_spelling_unchecked());
}

TableType type_named(const std::string &name) {
    if (name == "bag") {
        return TableType::bag;
    }
    return name == "duplicate_bag" ? TableType::duplicate_bag : TableType::set;
}

std::optional<std::uint32_t> slots_option(const Term &term) {
    const auto value = small(term.word());
    return value ? std::optional(static_cast<std::uint32_t>(*value)) : std::nullopt;
}

// The arguments of #open_args{file, type, keypos, repair, min_no_slots, max_no_slots, ram_file, ..., access}.
OpenArgs open_args(const Term &record) {
    if (!record.is_tuple() || record.tuple_size_unchecked() < 11) {
        bad_argument();
    }
    const auto field = [&](std::size_t index) { return record.tuple_element_unchecked(index); };
    const auto repair = spelling(field(4));
    return {.file = name_of(field(1)),
            .type = type_named(spelling(field(2))),
            .keypos = static_cast<std::uint32_t>(small(field(3).word()).value_or(1)),
            .repair = repair == "force" ? OpenArgs::Repair::force
                                        : (repair == "false" ? OpenArgs::Repair::no : OpenArgs::Repair::yes),
            .min_slots = slots_option(field(5)),
            .max_slots = slots_option(field(6)),
            .ram = spelling(field(7)) == "true",
            .read_only = spelling(field(10)) == "read"};
}

// The tooshort of an open is reported as not_a_dets_file, as dets:do_open_file/4 does.
Term open_error(ProcessContext &context, Failure failure) {
    if (failure.shape == Failure::Shape::named && failure.reason == "tooshort") {
        failure.reason = "not_a_dets_file";
    }
    return error_term(context, failure);
}

// open(#open_args{}): {ok, Handle, Created} or {error, Reason}.
Term open(ProcessContext &context, const OptionArgument &args) {
    const auto parsed = open_args(args.term);
    try {
        bool created = false;
        auto table = open_table(Terms(context), parsed, created);
        return tuple(context, std::array{ok(context), handle_of(context, std::move(table)),
                                         need(TermFactory(context).boolean(created))});
    } catch (const Failure &failure) {
        return open_error(context, failure);
    }
}

// open_file(Name): {ok, Handle} of an existing file, or {error, Reason}.
Term open_file(ProcessContext &context, const TermArgument &name) {
    const auto file = name_of(name.term);
    try {
        auto table = open_existing(Terms(context), file);
        return tuple(context, std::array{ok(context), handle_of(context, std::move(table))});
    } catch (const Failure &failure) {
        return open_error(context, failure);
    }
}

Term collections_term(ProcessContext &context, const Head &head) {
    if (!head.collections) {
        return atom(context, "undefined"_atom);
    }
    std::vector<Term> counts;
    for (const auto &[size, count] : *head.collections) {
        if (count != 0) {
            counts.push_back(tuple(context, std::array{integer(context, size), integer(context, count)}));
        }
    }
    return list(context, counts);
}

// info(H): {Type, Keypos, NoObjects, NoKeys, FileSize, Min, Next, Max, HashMethod, M, N, Collections}.
Term info(ProcessContext &context, const HandleArgument &handle) {
    auto &table = table_of(handle);
    return guarded(context, [&] {
        const auto &head = table.head();
        static constexpr std::array<std::string_view, 4> TYPES{"", "set", "bag", "duplicate_bag"};
        const std::array fields{atom(context, TYPES[static_cast<std::size_t>(head.type)]),
                                integer(context, head.keypos),
                                integer(context, head.no_objects),
                                integer(context, head.no_keys),
                                integer(context, static_cast<std::int64_t>(table.file().size())),
                                integer(context, head.min_slots),
                                integer(context, head.next),
                                integer(context, head.max_slots),
                                atom(context, head.hash == HashMethod::phash2 ? "phash2"_atom : "phash"_atom),
                                integer(context, head.m),
                                integer(context, head.n),
                                collections_term(context, head)};
        return tuple(context, fields);
    });
}

// The command of an {Op, Term} cache entry: insert, delete_object or {lookup, Asker}.
Command::Kind kind_named(const std::string &name) {
    if (name == "insert") {
        return Command::Kind::insert;
    }
    return name == "delete_object" ? Command::Kind::delete_object : Command::Kind::lookup;
}

// One {Key, {Seq, Op}} entry of the write cache.
Command command_of(const Term &entry) {
    if (!entry.is_tuple() || entry.tuple_size_unchecked() != 2) {
        bad_argument();
    }
    const auto action = entry.tuple_element_unchecked(1);
    if (!action.is_tuple() || action.tuple_size_unchecked() != 2) {
        bad_argument();
    }
    Command command{.key = entry.tuple_element_unchecked(0),
                    .seq = small(action.tuple_element_unchecked(0).word()).value_or(0),
                    .kind = Command::Kind::delete_key,
                    .object = {}};
    const auto op = action.tuple_element_unchecked(1);
    if (op.is_tuple() && op.tuple_size_unchecked() == 2) {
        command.kind = kind_named(spelling(op.tuple_element_unchecked(0)));
        command.object = op.tuple_element_unchecked(1);
    }
    return command;
}

// write_cache(H, Cache, Inserts): {ok, [{Asker, Objects}]} of the cache flushed, or {error, Reason}.
Term write_cache_builtin(ProcessContext &context, const HandleArgument &handle, const ListArgument &cache,
                         std::int64_t inserts) {
    auto &table = table_of(handle);
    std::vector<Command> commands;
    commands.reserve(cache.elements.size());
    for (const auto &entry : cache.elements) {
        commands.push_back(command_of(entry));
    }
    return guarded(context, [&] {
        const Terms terms(context);
        std::vector<Term> found;
        for (const auto &result : write_cache(table, terms, std::move(commands), static_cast<std::uint64_t>(inserts))) {
            found.push_back(tuple(context, std::array{result.asker, list(context, result.objects)}));
        }
        return tuple(context, std::array{ok(context), list(context, found)});
    });
}

Term mark_dirty(ProcessContext &context, const HandleArgument &handle) {
    auto &table = table_of(handle);
    return guarded(context, [&] {
        table.mark_dirty();
        return ok(context);
    });
}

Term save(ProcessContext &context, const HandleArgument &handle, const TermArgument &sync) {
    auto &table = table_of(handle);
    return guarded(context, [&] {
        table.save(spelling(sync.term) == "true");
        return ok(context);
    });
}

Term close(ProcessContext &context, const HandleArgument &handle) {
    table_of(handle).file().close();
    return ok(context);
}

// The objects of slot `slot`, or '$end_of_table' beyond the table.
Term slot(ProcessContext &context, const HandleArgument &handle, std::int64_t slot) {
    auto &table = table_of(handle);
    return guarded(context, [&] {
        const auto objects = table.slot_objects(Terms(context), static_cast<std::uint32_t>(slot));
        return objects ? list(context, *objects) : atom(context, "$end_of_table"_atom);
    });
}

// The key of the first object from slot `from` on.
Term key_from(ProcessContext &context, Table &table, std::uint32_t from) {
    const Terms terms(context);
    for (auto at = from;; ++at) {
        const auto objects = table.slot_objects(terms, at);
        if (!objects) {
            return atom(context, "$end_of_table"_atom);
        }
        if (!objects->empty()) {
            return Terms::element(objects->front(), table.head().keypos);
        }
    }
}

Term first(ProcessContext &context, const HandleArgument &handle) {
    auto &table = table_of(handle);
    return guarded(context, [&] { return tuple(context, std::array{ok(context), key_from(context, table, 0)}); });
}

// next(H, Key): the key after Key in slot order (dets:fnext/3).
Term next(ProcessContext &context, const HandleArgument &handle, const TermArgument &key) {
    auto &table = table_of(handle);
    return guarded(context, [&] {
        const Terms terms(context);
        const auto at = table.slot_of(terms, key.term);
        const auto objects = table.slot_objects(terms, at);
        if (!objects) {
            return tuple(context, std::array{ok(context), atom(context, "$end_of_table"_atom)});
        }
        const auto keypos = table.head().keypos;
        const auto found = std::ranges::find_if(
            *objects, [&](const Term &object) { return Terms::cmp(Terms::element(object, keypos), key.term) == 0; });
        const auto beyond = std::find_if(found, objects->end(), [&](const Term &object) {
            return Terms::cmp(Terms::element(object, keypos), key.term) != 0;
        });
        const auto result = found != objects->end() && beyond != objects->end() ? Terms::element(*beyond, keypos)
                                                                                : key_from(context, table, at + 1);
        return tuple(context, std::array{ok(context), result});
    });
}

// may_grow(H): {ok, Again} after growing by a segment or up to the number of keys.
Term may_grow(ProcessContext &context, const HandleArgument &handle) {
    auto &table = table_of(handle);
    return guarded(context, [&] {
        const auto again = table.may_grow(Terms(context), 0, true);
        return tuple(context, std::array{ok(context), need(TermFactory(context).boolean(again))});
    });
}

Term fix(ProcessContext &context, const HandleArgument &handle, const TermArgument &on) {
    auto &table = table_of(handle);
    if (spelling(on.term) == "true") {
        table.fix();
    } else {
        table.unfix();
    }
    return ok(context);
}

// Start the file again, empty: dets:do_delete_all_objects/1 and the restart of init_table.
void reinitiate(Table &table, std::uint32_t min_slots, bool init_segments) {
    const auto head = table.head();
    auto file = std::move(table.file());
    file.truncate(0);
    table = Table::initiate(
        std::move(file),
        {.type = head.type, .keypos = head.keypos, .min_slots = min_slots, .max_slots = head.max_slots}, init_segments);
}

Term delete_all(ProcessContext &context, const HandleArgument &handle) {
    auto &table = table_of(handle);
    return guarded(context, [&] {
        reinitiate(table, table.head().min_slots, true);
        return ok(context);
    });
}

// The bin and alloc fields of a #dets_cont{}.
struct Continuation {
    Term bin;
    Term alloc;
};

ScanState state_of(const Continuation &continuation) {
    const auto &[bin, alloc] = continuation;
    ScanState state;
    state.eof = bin.is_atom();
    if (bin.is_binary()) {
        state.leftover = need(bin.binary_bytes());
    }
    if (!alloc.is_tuple() || alloc.tuple_size_unchecked() != 3) {
        return state;
    }
    state.areas_left = true;
    state.from = static_cast<std::uint32_t>(small(alloc.tuple_element_unchecked(0).word()).value_or(0));
    state.to = static_cast<std::uint32_t>(small(alloc.tuple_element_unchecked(1).word()).value_or(0));
    const auto areas = need(alloc.tuple_element_unchecked(2).binary_bytes());
    for (std::size_t at = 0; at + 8 <= areas.size(); at += 8) {
        state.areas.push_back({.from = read_u32(areas, at), .to = read_u32(areas, at + 4)});
    }
    return state;
}

// {Bin, Alloc} of a continuation, as #dets_cont{} holds them.
std::array<Term, 2> state_terms(ProcessContext &context, const ScanState &state) {
    const auto empty = binary(context, {});
    if (state.eof) {
        return {atom(context, "eof"_atom), empty};
    }
    if (!state.areas_left) {
        return {binary(context, state.leftover), empty};
    }
    Bytes areas;
    for (const auto &[from, to] : state.areas) {
        append_u32(areas, from);
        append_u32(areas, to);
    }
    const std::array alloc{integer(context, state.from), integer(context, state.to), binary(context, areas)};
    return {binary(context, state.leftover), tuple(context, alloc)};
}

Term scan_init(ProcessContext &context, const HandleArgument &handle) {
    return tuple(context, state_terms(context, init_scan(table_of(handle))));
}

// What a scan gives back: the match specification's results, the objects, or the objects it maps to true; and
// the bad object a decoding failure is reported as.
enum class ScanMode : std::uint8_t { select, objects, matching };

ScanMode mode_named(const std::string &name) {
    if (name == "select") {
        return ScanMode::select;
    }
    return name == "objects" ? ScanMode::objects : ScanMode::matching;
}

const char *bad_object_of(ScanMode mode) {
    if (mode == ScanMode::select) {
        return "chunk_match";
    }
    return mode == ScanMode::objects ? "do_trav_bins" : "fmatch_delete";
}

void collect(ProcessContext &context, const Term &object, ScanMode mode, const Term &program, std::vector<Term> &out) {
    if (program.is_atom()) {
        out.push_back(object);
        return;
    }
    const auto compiled = detail::match_spec::program_of(program.tuple_element_unchecked(1));
    if (!compiled) {
        bad_argument();
    }
    const auto result = detail::match_spec::run(context, *compiled, object.word(), detail::match_spec::Source::heap);
    if (!result) {
        return;
    }
    if (mode == ScanMode::select) {
        out.push_back(*result);
    } else if (result->is_atom() && result->atom_spelling_unchecked() == "true") {
        out.push_back(object);
    }
}

// scan(H, Bin, Alloc, NoObjs, MatchProgram, Mode): {Results, Scanned, Bin, Alloc} of the next chunk (Scanned
// objects were read), or {error, Reason}.
Term scan_builtin(ProcessContext &context, const HandleArgument &handle, const TermArgument &bin,
                  const TermArgument &alloc, const TermArgument &count, const TermArgument &program,
                  const TermArgument &mode) {
    auto &table = table_of(handle);
    const auto wanted = small(count.term.word());
    const auto how = mode_named(spelling(mode.term));
    return guarded(context, [&] {
        auto chunk = scan(table, state_of({.bin = bin.term, .alloc = alloc.term}),
                          wanted ? std::optional(static_cast<std::uint64_t>(*wanted)) : std::nullopt);
        const Terms terms(context);
        std::vector<Term> results;
        try {
            for (const auto &object : chunk.objects) {
                collect(context, terms.decode(object), how, program.term, results);
            }
        } catch (const BadObject &) {
            bad_object(bad_object_of(how), table.name());
        }
        const auto next = state_terms(context, chunk.next);
        const auto scanned = integer(context, static_cast<std::int64_t>(chunk.objects.size()));
        return tuple(context, std::array{list(context, results), scanned, next[0], next[1]});
    });
}

Term bchunk_init(ProcessContext &context, const HandleArgument &handle) {
    return tuple(context, state_terms(context, init_bchunk(table_of(handle))));
}

// bchunk(H, Alloc): {Bin, Alloc, [{LogSize, Slot, Block}]} or {error, Reason}.
Term bchunk(ProcessContext &context, const HandleArgument &handle, const TermArgument &alloc) {
    auto &table = table_of(handle);
    return guarded(context, [&] {
        auto blocks = read_bchunks(table, Terms(context), state_of({.bin = binary(context, {}), .alloc = alloc.term}));
        std::vector<Term> data;
        data.reserve(blocks.blocks.size());
        for (const auto &block : blocks.blocks) {
            data.push_back(tuple(context, std::array{integer(context, block.log_size), integer(context, block.slot),
                                                     binary(context, block.bytes)}));
        }
        auto next = state_terms(context, blocks.next);
        if (blocks.next.eof) {
            next[1] = alloc.term;
        }
        return tuple(context, std::array{next[0], next[1], list(context, data)});
    });
}

// where(H, Object): {ok, Pointer} of the collection holding it, or false.
Term where(ProcessContext &context, const HandleArgument &handle, const TermArgument &object) {
    auto &table = table_of(handle);
    try {
        const Terms terms(context);
        const auto at = table.slot_of(terms, Terms::element(object.term, table.head().keypos));
        if (at >= table.head().next) {
            return atom(context, "false"_atom);
        }
        const auto collection = table.read_slot(table.slot_position(at), "where");
        const auto objects = table.slot_objects(terms, at);
        const bool member = objects && std::ranges::any_of(*objects, [&](const Term &stored) {
                                return Terms::same(stored, object.term);
                            });
        return member ? tuple(context, std::array{ok(context), integer(context, collection.pointer)})
                      : atom(context, "false"_atom);
    } catch (const Failure &) {
        return atom(context, "false"_atom);
    } catch (const BadObject &) {
        return atom(context, "false"_atom);
    }
}

// bulk_start(H, MinSlots, Keep): an empty file for init_table/3, kept as it is when Keep is true.
Term bulk_start(ProcessContext &context, const HandleArgument &handle, std::int64_t min_slots,
                const TermArgument &keep) {
    auto &table = table_of(handle);
    return guarded(context, [&] {
        table.pending().clear();
        if (spelling(keep.term) != "true") {
            const auto previous = table.head();
            reinitiate(table, static_cast<std::uint32_t>(min_slots), false);
            table.previous() = previous;
        }
        return ok(context);
    });
}

// bulk_add(H, Objects): ok, or {error, invalid_objects_list} for an object without a key.
Term bulk_add(ProcessContext &context, const HandleArgument &handle, const ListArgument &objects) {
    auto &table = table_of(handle);
    const Terms terms(context);
    auto &pending = table.pending();
    for (const auto &object : objects.elements) {
        if (!object.is_tuple() || object.tuple_size_unchecked() < table.head().keypos) {
            return error_term(context, {.shape = Failure::Shape::bare, .reason = "invalid_objects_list", .file = {}});
        }
        const auto key = object.tuple_element_unchecked(table.head().keypos - 1);
        pending.push_back({.slot = table.slot_of(terms, key),
                           .seq = static_cast<std::uint32_t>(pending.size()),
                           .bytes = terms.encode(object)});
    }
    return ok(context);
}

Term bulk_end(ProcessContext &context, const HandleArgument &handle) {
    auto &table = table_of(handle);
    return guarded(context, [&] {
        const Terms terms(context);
        const auto bulk = bulk_collections(table, terms, std::exchange(table.pending(), {}));
        write_bulk(table, bulk.collections, bulk.counts, false);
        table.previous().reset();
        return ok(context);
    });
}

// truncate(H): the file emptied for init_table/3 of bchunk data, the head kept until the data's parameters come.
Term truncate(ProcessContext &context, const HandleArgument &handle) {
    auto &table = table_of(handle);
    return guarded(context, [&] {
        table.file().truncate(0);
        table.previous() = table.head();
        table.blocks().clear();
        return ok(context);
    });
}

std::optional<std::uint32_t> word_at(const Term &record, std::size_t index) {
    const auto value = small(record.tuple_element_unchecked(index).word());
    if (!value || *value < 0 || *value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

// The slot numbers of a head are whole segments.
bool whole_segments(const Head &head) {
    const std::array values{head.n, head.m, head.next, head.min_slots, head.max_slots};
    return std::ranges::all_of(values, [](std::uint32_t value) { return value % SEGMENT_SLOTS == 0; });
}

// dets_v9:hash_invars/5: the slots of a table in whole segments, the split point and sizes consistent.
bool hash_invariants(const Head &head) {
    const std::uint64_t next = head.next;
    const std::uint64_t m = head.m;
    const std::array ordered{head.n <= m,        head.n <= 2 * next,     m <= next,
                             next <= 2 * m,      head.min_slots <= next, next <= head.max_slots,
                             head.min_slots <= m};
    return whole_segments(head) && std::ranges::all_of(ordered, [](bool holds) { return holds; });
}

// The head of the table bchunk data came from, from its '$hash2' record; none when the record does not fit.
std::optional<Head> copied_head(const Head &current, const Term &record) {
    if (!record.is_tuple() || record.tuple_size_unchecked() != 15) {
        return std::nullopt;
    }
    std::array<std::optional<std::uint32_t>, 7> fields;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        fields[index] = word_at(record, 7 + index);
    }
    if (word_at(record, 1) != FORMAT_VERSION || std::ranges::any_of(fields, [](const auto &field) { return !field; }) ||
        *fields[5] < *fields[6]) {
        return std::nullopt;
    }
    Head head = current;
    head.n = *fields[0];
    head.m = *fields[1];
    head.m2 = 2 * head.m;
    head.next = *fields[2];
    head.min_slots = *fields[3];
    head.max_slots = *fields[4];
    head.no_objects = *fields[5];
    head.no_keys = *fields[6];
    return hash_invariants(head) ? std::optional(head) : std::nullopt;
}

// bchunk_start(H, Record): a fresh file with the slots of the '$hash2' record, or badarg.
Term bchunk_start(ProcessContext &context, const HandleArgument &handle, const TermArgument &record) {
    auto &table = table_of(handle);
    const auto head = copied_head(table.head(), record.term);
    if (!head) {
        return atom(context, "badarg"_atom);
    }
    return guarded(context, [&] {
        auto previous = std::move(table.previous());
        table = Table::copy_of(std::move(table.file()), *head);
        table.previous() = std::move(previous);
        return ok(context);
    });
}

// Whether a block of bchunk/2's data has the buddy size its log2 size says.
bool block_fits(std::optional<std::int64_t> log_size, std::size_t size) {
    if (!log_size || *log_size < 1 || *log_size > MAX_BUDDY) {
        return false;
    }
    return size >= 12 && size == power(static_cast<int>(*log_size) - 1);
}

// One {LogSize, Slot, Block} of bchunk/2's data, its status made ACTIVE; none for anything else.
std::optional<BulkCollection> data_block(const Term &item) {
    if (!item.is_tuple() || item.tuple_size_unchecked() != 3 || !item.tuple_element_unchecked(2).is_binary()) {
        return std::nullopt;
    }
    const auto slot = small(item.tuple_element_unchecked(1).word());
    auto block = need(item.tuple_element_unchecked(2).binary_bytes());
    if (!slot || *slot < 0 || !block_fits(small(item.tuple_element_unchecked(0).word()), block.size())) {
        return std::nullopt;
    }
    const auto status = read_u32(block, STATUS_POS);
    if (status != ACTIVE && status != FREE) {
        return std::nullopt;
    }
    put_u32(block, STATUS_POS, ACTIVE);
    return BulkCollection{.slot = static_cast<std::uint32_t>(*slot), .block = std::move(block)};
}

Term invalid_objects(ProcessContext &context) {
    return error_term(context, {.shape = Failure::Shape::bare, .reason = "invalid_objects_list", .file = {}});
}

// bchunk_add(H, Data): ok, or {error, invalid_objects_list}.
Term bchunk_add(ProcessContext &context, const HandleArgument &handle, const ListArgument &data) {
    auto &table = table_of(handle);
    for (const auto &item : data.elements) {
        auto block = data_block(item);
        if (!block) {
            return invalid_objects(context);
        }
        table.blocks().push_back(std::move(*block));
    }
    return ok(context);
}

// The number of collections per log2 size of a '$hash2' record's no_colls, without the zero counts.
std::optional<std::map<int, std::int64_t>> expected_counts(const ListArgument &counts) {
    std::map<int, std::int64_t> expected;
    for (const auto &count : counts.elements) {
        if (!count.is_tuple() || count.tuple_size_unchecked() != 2) {
            return std::nullopt;
        }
        expected[static_cast<int>(small(count.tuple_element_unchecked(0).word()).value_or(-1))] +=
            small(count.tuple_element_unchecked(1).word()).value_or(-1);
    }
    std::erase_if(expected, [](const auto &entry) { return entry.second == 0; });
    return expected;
}

// bchunk_end(H, NoColls): the blocks written when each size has as many as the parameters said.
Term bchunk_end(ProcessContext &context, const HandleArgument &handle, const ListArgument &counts) {
    auto &table = table_of(handle);
    std::map<int, std::int64_t> found;
    for (const auto &block : table.blocks()) {
        ++found[log2(block.block.size())];
    }
    if (expected_counts(counts) != found) {
        return invalid_objects(context);
    }
    return guarded(context, [&] {
        const auto head = table.head();
        write_bulk(table, std::exchange(table.blocks(), {}), {.objects = head.no_objects, .keys = head.no_keys}, true);
        table.previous().reset();
        return ok(context);
    });
}

// bulk_abort(H): a failed init_table leaves the head it started with, as OTP's corrupt/2 of the old head does.
Term bulk_abort(ProcessContext &context, const HandleArgument &handle) {
    auto &table = table_of(handle);
    table.pending().clear();
    table.blocks().clear();
    if (table.previous()) {
        table.head() = *std::exchange(table.previous(), std::nullopt);
    }
    return ok(context);
}

Term is_dets_file_builtin(ProcessContext &context, const TermArgument &name) {
    const auto file = name_of(name.term);
    return guarded(context, [&] { return need(TermFactory(context).boolean(is_dets_file(file))); });
}

constexpr std::array DETS_BUILTINS{
    typed_entry<open>("clause_dets", "open"),
    typed_entry<open_file>("clause_dets", "open_file"),
    typed_entry<info>("clause_dets", "info"),
    typed_entry<write_cache_builtin>("clause_dets", "write_cache"),
    typed_entry<mark_dirty>("clause_dets", "mark_dirty"),
    typed_entry<save>("clause_dets", "save"),
    typed_entry<close>("clause_dets", "close"),
    typed_entry<slot>("clause_dets", "slot"),
    typed_entry<first>("clause_dets", "first"),
    typed_entry<next>("clause_dets", "next"),
    typed_entry<may_grow>("clause_dets", "may_grow"),
    typed_entry<fix>("clause_dets", "fix"),
    typed_entry<delete_all>("clause_dets", "delete_all"),
    typed_entry<scan_init>("clause_dets", "scan_init"),
    typed_entry<scan_builtin>("clause_dets", "scan"),
    typed_entry<bchunk_init>("clause_dets", "bchunk_init"),
    typed_entry<bchunk>("clause_dets", "bchunk"),
    typed_entry<where>("clause_dets", "where"),
    typed_entry<bulk_start>("clause_dets", "bulk_start"),
    typed_entry<bulk_add>("clause_dets", "bulk_add"),
    typed_entry<bulk_end>("clause_dets", "bulk_end"),
    typed_entry<bulk_abort>("clause_dets", "bulk_abort"),
    typed_entry<truncate>("clause_dets", "truncate"),
    typed_entry<bchunk_start>("clause_dets", "bchunk_start"),
    typed_entry<bchunk_add>("clause_dets", "bchunk_add"),
    typed_entry<bchunk_end>("clause_dets", "bchunk_end"),
    typed_entry<is_dets_file_builtin>("clause_dets", "is_dets_file"),
};
} // namespace
} // namespace clause::runtime::builtins::dets

namespace clause::runtime {
std::span<const BuiltinEntry> dets_builtins() noexcept { return builtins::dets::DETS_BUILTINS; }
} // namespace clause::runtime
