#pragma once
#include "file.hpp"

// The file header of a dets file (plan 21 step LIB53): read and checked as dets_v9:read_file_header/2 and
// check_file_header/2 do, and written as file_header/3,4 write it, with the MD5 of its digested part (version 9(c))
// and the base of the buddy system (version 9(d)).
namespace clause::runtime::builtins::dets {
// The fields of a file's header and the facts read with it.
struct FileHeader {
    std::uint32_t freelist = 0;
    // The base of the buddy system as stored: 0 means BASE.
    std::uint32_t stored_base = 0;
    std::uint32_t cookie = 0;
    std::uint32_t closed_properly = 0;
    std::uint32_t type = 0;
    std::uint32_t version = 0;
    std::uint32_t m = 0;
    std::uint32_t next = 0;
    std::uint32_t keypos = 0;
    std::uint32_t no_objects = 0;
    std::uint32_t no_keys = 0;
    std::uint32_t min_slots = 0;
    std::uint32_t max_slots = 0;
    std::uint32_t hash_method = 0;
    std::uint32_t n = 0;
    Collections collections;
    bool has_md5 = false;
    bool md5_matches = false;
    // The file size the trailer records plus the stored base, and the real size.
    std::uint64_t trailer = 0;
    std::uint64_t eof = 0;

    std::uint32_t base() const { return stored_base == 0 ? BASE : stored_base; }
};

// The header of a version 9 file; throws tooshort, format_8_no_longer_supported or not_a_dets_file.
FileHeader read_file_header(const File &file);

// What check_file_header/2 finds: ok, not_closed, or the reason the file cannot be used.
struct HeaderCheck {
    std::string status;
    Head head;
};

HeaderCheck check_file_header(const FileHeader &header);

// The header bytes of `head` with the free lists at `freelists` and the closed flag; the counters are the head's.
Bytes file_header(const Head &head, std::uint32_t freelists, std::uint32_t closed);
// The same with every counter zero, as a new file is started.
Bytes initial_file_header(const Head &head);

// The table type of a type code; none for a code that names none.
std::optional<TableType> type_of_code(std::uint32_t code);
} // namespace clause::runtime::builtins::dets
