#include "header.hpp"
#include "../erlang/md5.hpp"
#include <algorithm>

namespace clause::runtime::builtins::dets {
namespace {
// The first and last buddy size counted in the header.
constexpr int FIRST_COUNTED = 4;
constexpr int LAST_COUNTED = MAX_BUDDY - 1;
// The offsets of the counters, the MD5 and the base within the header.
constexpr std::size_t COUNTERS_AT = 56;
constexpr std::size_t MD5_AT = COUNTERS_AT + COUNTER_BYTES;
constexpr std::size_t BASE_AT = MD5_AT + MD5_BYTES;
// The digested part: from the type code to the end of the counters.
constexpr std::size_t DIGEST_FROM = 12;

std::array<std::byte, MD5_BYTES> digest(std::span<const std::byte> bytes) {
    Md5 md5;
    md5.update(bytes);
    return md5.finish();
}

// The non-zero counters of a header, or none for a version 9(a) file that has objects but no counters.
Collections read_counters(std::span<const std::byte> bytes, std::uint32_t no_objects) {
    std::map<int, std::int64_t> counted;
    for (int size = FIRST_COUNTED; size <= LAST_COUNTED; ++size) {
        const auto count = read_u32(bytes, COUNTERS_AT + 4 * static_cast<std::size_t>(size - FIRST_COUNTED));
        if (count != 0) {
            counted.emplace(size, count);
        }
    }
    if (counted.empty() && no_objects > 0) {
        return std::nullopt;
    }
    return counted;
}

void read_fields(std::span<const std::byte> bytes, FileHeader &header) {
    std::array<std::uint32_t *, 14> fields{
        &header.freelist,  &header.cookie,    &header.closed_properly, &header.type,       &header.version,
        &header.m,         &header.next,      &header.keypos,          &header.no_objects, &header.no_keys,
        &header.min_slots, &header.max_slots, &header.hash_method,     &header.n};
    for (std::size_t index = 0; index < fields.size(); ++index) {
        *fields[index] = read_u32(bytes, 4 * index);
    }
    header.stored_base = read_u32(bytes, BASE_AT);
}

// The reason a header that reads well cannot be used, or empty.
std::string header_fault(const FileHeader &header) {
    if (header.cookie != MAGIC) {
        return "not_a_dets_file";
    }
    if (!type_of_code(header.type)) {
        return "invalid_type_code";
    }
    if (header.version != FORMAT_VERSION) {
        return "bad_version";
    }
    if (header.has_md5 && !header.md5_matches) {
        return "not_a_dets_file";
    }
    if (header.trailer != header.eof) {
        return "not_closed";
    }
    if (header.hash_method > 1) {
        return "bad_hash_bif";
    }
    return {};
}

Head head_of(const FileHeader &header) {
    Head head;
    head.m = header.m;
    head.m2 = header.m * 2;
    head.next = header.next;
    head.n = header.n;
    head.no_objects = header.no_objects;
    head.no_keys = header.no_keys;
    head.max_object_size = header.collections ? std::optional<int>(max_object_size(*header.collections)) : std::nullopt;
    head.type = type_of_code(header.type).value_or(TableType::set);
    head.keypos = header.keypos;
    head.min_slots = header.min_slots;
    head.max_slots = header.max_slots;
    head.hash = header.hash_method == 0 ? HashMethod::phash : HashMethod::phash2;
    head.has_md5 = header.has_md5;
    head.collections = header.collections;
    head.base = header.base();
    head.freelists_p = header.freelist;
    return head;
}

// The number of collections of 2^size bytes; 0 when none are counted.
std::uint32_t counter(const Head &head, int size) {
    if (!head.collections) {
        return 0;
    }
    const auto found = head.collections->find(size);
    return found == head.collections->end() ? 0 : static_cast<std::uint32_t>(found->second);
}

Bytes encode_header(const Head &head, std::uint32_t freelists, std::uint32_t closed, bool counted) {
    Bytes out;
    for (const auto word : {freelists, MAGIC, closed, static_cast<std::uint32_t>(head.type), FORMAT_VERSION, head.m,
                            head.next, head.keypos, head.no_objects, head.no_keys, head.min_slots, head.max_slots,
                            static_cast<std::uint32_t>(head.hash), head.n}) {
        append_u32(out, word);
    }
    for (int size = FIRST_COUNTED; size <= LAST_COUNTED; ++size) {
        append_u32(out, counted ? counter(head, size) : 0);
    }
    const auto md5 = head.has_md5 ? digest(std::span(out).subspan(DIGEST_FROM)) : std::array<std::byte, MD5_BYTES>{};
    append(out, md5);
    append_u32(out, head.base == BASE ? 0 : head.base);
    out.resize(HEAD_END);
    return out;
}
} // namespace

std::optional<TableType> type_of_code(std::uint32_t code) {
    if (code >= 1 && code <= 3) {
        return static_cast<TableType>(code);
    }
    return std::nullopt;
}

FileHeader read_file_header(const File &file) {
    const auto version = file.pread(VERSION_POS, 4);
    if (version.size() < 4) {
        fail(Failure::Shape::named, "tooshort", file.name());
    }
    if (read_u32(version, 0) <= 8) {
        fail(Failure::Shape::named, "format_8_no_longer_supported", file.name());
    }
    if (read_u32(version, 0) != FORMAT_VERSION) {
        fail(Failure::Shape::named, "not_a_dets_file", file.name());
    }
    const auto bytes = file.pread(0, HEAD_BYTES);
    if (bytes.size() < HEAD_BYTES) {
        fail(Failure::Shape::named, "tooshort", file.name());
    }
    FileHeader header;
    read_fields(bytes, header);
    header.collections = read_counters(bytes, header.no_objects);
    header.has_md5 = std::ranges::any_of(std::span(bytes).subspan(MD5_AT, MD5_BYTES),
                                         [](std::byte value) { return value != std::byte{0}; });
    const auto computed = digest(std::span(bytes).subspan(DIGEST_FROM, MD5_AT - DIGEST_FROM));
    header.md5_matches = std::ranges::equal(computed, std::span(bytes).subspan(MD5_AT, MD5_BYTES));
    header.eof = file.size();
    const auto trailer = file.pread(header.eof - 4, 4);
    header.trailer = static_cast<std::uint64_t>(read_u32(trailer, 0)) + header.stored_base;
    return header;
}

HeaderCheck check_file_header(const FileHeader &header) {
    auto fault = header_fault(header);
    if (fault.empty()) {
        if (header.closed_properly == CLOSED_PROPERLY) {
            fault = "ok";
        } else {
            fault = header.closed_properly == NOT_PROPERLY_CLOSED ? "not_closed" : "not_a_dets_file";
        }
    }
    return {.status = std::move(fault), .head = head_of(header)};
}

Bytes file_header(const Head &head, std::uint32_t freelists, std::uint32_t closed) {
    return encode_header(head, freelists, closed, true);
}

Bytes initial_file_header(const Head &head) { return encode_header(head, 0, NOT_PROPERLY_CLOSED, false); }
} // namespace clause::runtime::builtins::dets
