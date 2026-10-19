#include "scan.hpp"
#include <variant>

// The file-order scans of dets tables (plan 21 step LIB53): dets:init_scan/2, scan/2 and scan_read/8 with
// dets_v9:scan_objs/8, and dets_v9:read_bchunks/2.
namespace clause::runtime::builtins::dets {
namespace {
// The continuation at the end of a file's areas.
ScanState ended() {
    ScanState state;
    state.eof = true;
    return state;
}

// Collections of more than this are only read when the file holds collections that big.
void check_size(const Table &table, std::uint64_t size, const char *where) {
    if (size > MAX_COLLECTION && table.max_collection() < size) {
        bad_object(where, table.name());
    }
}

// More bytes are needed from `from`, at least `size`; or the scan stops at `from` with enough objects.
struct More {
    std::uint64_t from;
    std::uint32_t size;
};

struct Stop {
    std::uint64_t from;
};

// The parts of `bytes` framed as <<Size:32, Part/binary>>, each Size counting its own four bytes.
std::vector<std::span<const std::byte>> framed(std::span<const std::byte> bytes) {
    std::vector<std::span<const std::byte>> parts;
    for (std::size_t at = 0; at < bytes.size();) {
        const auto size = read_u32(bytes, at);
        if (size < 4 || at + size > bytes.size()) {
            throw BadObject{};
        }
        parts.push_back(bytes.subspan(at + 4, size - 4));
        at += size;
    }
    return parts;
}

// The objects of one collection's keys, appended in stored order; their number.
std::int64_t collection_objects(std::span<const std::byte> keys, TableType type, std::vector<Bytes> &out) {
    std::int64_t count = 0;
    const auto take = [&](std::span<const std::byte> objects) {
        for (const auto object : framed(objects)) {
            out.emplace_back(object.begin(), object.end());
            ++count;
        }
    };
    if (type == TableType::set) {
        take(keys);
        return count;
    }
    for (const auto key : framed(keys)) {
        take(key);
    }
    return count;
}

// A position of the scan: `skip` bytes past `from`, the position its bytes are counted from.
struct Position {
    std::uint64_t from;
    std::uint64_t skip;
};

using Step = std::variant<Position, More, Stop>;

class Scanner final {
  public:
    Scanner(const Table &table, std::optional<std::uint64_t> count)
        : table_(table), objects_left_(count ? -static_cast<std::int64_t>(*count) - 1 : 0) {}

    Chunk run(ScanState state) {
        if (!state.areas_left) {
            return {.objects = {}, .next = std::move(state)};
        }
        buffer_ = std::move(state.leftover);
        buffer_at_ = state.from;
        to_ = state.to;
        areas_ = std::move(state.areas);
        auto step = walk({.from = state.from, .skip = 0});
        while (const auto *more = std::get_if<More>(&step)) {
            check_size(table_, more->size, "scan");
            if (objects_left_ >= static_cast<std::int64_t>(CHUNK_SIZE)) {
                return done(more->from, {});
            }
            if (!read(*more)) {
                return {.objects = std::move(objects_), .next = ended()};
            }
            step = walk({.from = buffer_at_, .skip = 0});
        }
        return stopped(std::get<Stop>(step).from);
    }

  private:
    Chunk done(std::uint64_t from, Bytes leftover) {
        return {.objects = std::move(objects_),
                .next = {.eof = false,
                         .leftover = std::move(leftover),
                         .areas_left = true,
                         .from = static_cast<std::uint32_t>(from),
                         .to = to_,
                         .areas = std::move(areas_)}};
    }

    Chunk stopped(std::uint64_t from) {
        Bytes leftover;
        if (from - buffer_at_ < buffer_.size()) {
            leftover.assign(buffer_.begin() + static_cast<std::ptrdiff_t>(from - buffer_at_), buffer_.end());
        }
        const auto more = table_.allocator().next_allocated(static_cast<std::uint32_t>(from));
        if (leftover.empty() && areas_.empty() && !more) {
            return {.objects = std::move(objects_), .next = ended()};
        }
        return done(from, std::move(leftover));
    }

    // Read at least the bytes wanted of the allocated areas from where more are wanted; false at the end of the
    // file's areas.
    bool read(const More &more) {
        const auto length = std::max(CHUNK_SIZE, more.size);
        const auto areas = table_.allocator().allocated(static_cast<std::uint32_t>(more.from), length);
        if (areas.empty()) {
            return false;
        }
        buffer_ = table_.pread(areas.front().from, length);
        if (buffer_.empty()) {
            fail(Failure::Shape::named, "premature_eof", table_.name());
        }
        buffer_at_ = areas.front().from;
        to_ = areas.front().to;
        areas_.assign(areas.begin() + 1, areas.end());
        return true;
    }

    // dets_v9:scan_skip/8, bin2bins/9 and slot_end/9 from a position.
    std::variant<More, Stop> walk(const Position &start) {
        Step step = start;
        while (const auto *position = std::get_if<Position>(&step)) {
            step = advance(*position);
        }
        if (const auto *more = std::get_if<More>(&step)) {
            return *more;
        }
        return std::get<Stop>(step);
    }

    Step advance(const Position &position) {
        const auto at = position.from + position.skip;
        if (at >= to_) {
            return area_end(position);
        }
        const auto offset = at - buffer_at_;
        if (at < buffer_at_ || offset + 12 > buffer_.size()) {
            return More{.from = at, .size = 0};
        }
        const auto size = read_u32(buffer_, offset);
        const auto status = read_u32(buffer_, offset + STATUS_POS);
        if (status != ACTIVE && status != FREE) {
            // A segment or segment array part.
            return Position{.from = at + 12, .skip = SEGMENT_BYTES - 12};
        }
        if (offset + size > buffer_.size()) {
            return More{.from = at, .size = size};
        }
        return collection(at);
    }

    // The end of an allocated area: on to the next one, or read more.
    Step area_end(const Position &position) {
        const auto at = position.from + position.skip;
        if (at > to_ || areas_.empty()) {
            return More{.from = at, .size = 0};
        }
        const auto skip = areas_.front().from - position.from;
        to_ = areas_.front().to;
        areas_.pop_front();
        return Position{.from = position.from, .skip = skip};
    }

    // A whole collection at `at`: its objects taken, the scan stopped when enough objects were wanted.
    Step collection(std::uint64_t at) {
        const auto size = read_u32(buffer_, at - buffer_at_);
        const auto count = objects_in(at - buffer_at_, size);
        const Position next{.from = at + 12, .skip = power(log2(size)) - 12};
        if (objects_left_ >= 0) {
            objects_left_ += size;
            return next;
        }
        if (objects_left_ + count >= -1) {
            return Stop{.from = next.from + next.skip};
        }
        objects_left_ += count;
        return next;
    }

    std::int64_t objects_in(std::uint64_t offset, std::uint32_t size) {
        if (size < 12) {
            bad_object("scan", table_.name());
        }
        try {
            return collection_objects(std::span(buffer_).subspan(offset + OBJECT_HEADER, size - OBJECT_HEADER),
                                      table_.head().type, objects_);
        } catch (const BadObject &) {
            bad_object("scan", table_.name());
        }
    }

    const Table &table_;
    // Bytes scanned (counting up from 0), or objects still wanted (counting up from -N-1).
    std::int64_t objects_left_;
    Bytes buffer_;
    std::uint64_t buffer_at_ = 0;
    std::uint32_t to_ = 0;
    std::deque<Area> areas_;
    std::vector<Bytes> objects_;
};

// dets_v9:read_bchunks/5 and bchunks/7.
class BlockReader final {
  public:
    BlockReader(const Table &table, const Terms &terms) : table_(table), terms_(terms) {}

    Blocks run(ScanState state) {
        from_ = state.from;
        to_ = state.to;
        areas_ = std::move(state.areas);
        std::uint32_t wanted = 0;
        for (;;) {
            if (bytes_ + wanted >= 4 * static_cast<std::uint64_t>(CHUNK_SIZE) && !blocks_.empty()) {
                return {.blocks = std::move(blocks_),
                        .next = {.eof = false,
                                 .leftover = {},
                                 .areas_left = true,
                                 .from = from_,
                                 .to = to_,
                                 .areas = std::move(areas_)}};
            }
            if (!fill(wanted)) {
                fail(Failure::Shape::named, "premature_eof", table_.name());
            }
            const auto parsed = parse();
            if (!parsed) {
                return {.blocks = std::move(blocks_), .next = ended()};
            }
            wanted = *parsed;
        }
    }

  private:
    // Read at least `wanted` bytes at the current position; false at a premature end of file.
    bool fill(std::uint32_t wanted) {
        const std::uint32_t size = std::max(2 * CHUNK_SIZE, wanted);
        check_size(table_, size, "bad_object");
        buffer_ = table_.pread(from_, size);
        if (buffer_.empty()) {
            return false;
        }
        if (buffer_.size() < wanted) {
            if (to_ - from_ != wanted || !areas_.empty()) {
                return false;
            }
            buffer_.resize(wanted);
        }
        at_ = 0;
        return true;
    }

    // Take the buffer's blocks; the bytes the next read needs, or none when every area is read.
    std::optional<std::uint32_t> parse() {
        for (;;) {
            if (from_ != to_) {
                if (const auto wanted = step()) {
                    return *wanted;
                }
                continue;
            }
            if (areas_.empty()) {
                return std::nullopt;
            }
            if (!next_area()) {
                return 0;
            }
        }
    }

    // Move on to the next area; false when it starts past the buffer.
    bool next_area() {
        const auto area = areas_.front();
        areas_.pop_front();
        const auto skip = area.from - from_;
        from_ = area.from;
        to_ = area.to;
        if (buffer_.size() - at_ < skip) {
            return false;
        }
        at_ += skip;
        return true;
    }

    // One block or segment at the current position; the bytes a read needs when the buffer ends first.
    std::optional<std::uint32_t> step() {
        const auto left = buffer_.size() - at_;
        if (left < 12) {
            return 0;
        }
        const auto size = read_u32(buffer_, at_);
        const auto status = read_u32(buffer_, at_ + STATUS_POS);
        if (status != ACTIVE && status != FREE) {
            if (left < SEGMENT_BYTES) {
                return SEGMENT_BYTES;
            }
            advance(SEGMENT_BYTES);
            return std::nullopt;
        }
        const auto log_size = size_class(size);
        const auto block = static_cast<std::uint32_t>(power(log_size - 1));
        if (left < block) {
            return block;
        }
        const auto bytes = std::span(buffer_).subspan(at_, block);
        blocks_.push_back({.log_size = log_size, .slot = slot_of(bytes), .bytes = Bytes(bytes.begin(), bytes.end())});
        bytes_ += block;
        advance(block);
        return std::nullopt;
    }

    std::uint32_t slot_of(std::span<const std::byte> block) const {
        const auto first = table_.head().type == TableType::set ? 12U : 16U;
        try {
            const auto term = terms_.decode(block.subspan(std::min<std::size_t>(first, block.size())));
            return table_.slot_of(terms_, Terms::element(term, table_.head().keypos));
        } catch (const BadObject &) {
            bad_object("read_bchunks", table_.name());
        }
    }

    void advance(std::uint32_t count) {
        at_ += count;
        from_ += count;
    }

    const Table &table_;
    const Terms &terms_;
    std::uint32_t from_ = 0;
    std::uint32_t to_ = 0;
    std::deque<Area> areas_;
    Bytes buffer_;
    std::size_t at_ = 0;
    std::uint64_t bytes_ = 0;
    std::vector<Block> blocks_;
};
} // namespace

ScanState init_scan(const Table &table) {
    const auto &allocator = table.allocator();
    const auto first = allocator.next_allocated(allocator.base());
    if (!first) {
        return ended();
    }
    return {.eof = false, .leftover = {}, .areas_left = true, .from = first->from, .to = first->to, .areas = {}};
}

Chunk scan(const Table &table, ScanState state, std::optional<std::uint64_t> count) {
    return Scanner(table, count).run(std::move(state));
}

ScanState init_bchunk(const Table &table) {
    const auto areas = table.allocator().all_allocated();
    if (areas.empty()) {
        return ended();
    }
    ScanState state{.eof = false,
                    .leftover = {},
                    .areas_left = true,
                    .from = areas.front().from,
                    .to = areas.front().to,
                    .areas = {}};
    state.areas.assign(areas.begin() + 1, areas.end());
    return state;
}

Blocks read_bchunks(const Table &table, const Terms &terms, ScanState state) {
    return BlockReader(table, terms).run(std::move(state));
}
} // namespace clause::runtime::builtins::dets
