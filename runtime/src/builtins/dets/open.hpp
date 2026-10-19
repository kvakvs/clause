#pragma once
#include "table.hpp"

// Opening dets files (plan 21 step LIB53): dets:fopen2/2 for open_file/1 and fopen3/2 for open_file/2, with the
// repairs a file not properly closed (or forced) gets.
namespace clause::runtime::builtins::dets {
struct OpenArgs {
    enum class Repair : std::uint8_t { no, yes, force };
    std::string file;
    TableType type = TableType::set;
    std::uint32_t keypos = 1;
    Repair repair = Repair::yes;
    // none: default.
    std::optional<std::uint32_t> min_slots;
    std::optional<std::uint32_t> max_slots;
    bool ram = false;
    bool read_only = false;
};

// open_file/2: an existing file checked (and repaired or compacted as asked), or a new one; throws Failure.
// `created` tells whether the file is new (OTP's new_dirty).
Table open_table(const Terms &terms, const OpenArgs &args, bool &created);
// open_file/1: an existing file only, repaired when it was not properly closed.
Table open_existing(const Terms &terms, const std::string &file);
// Whether a file is a dets file: true, false, or throws its file error.
bool is_dets_file(const std::string &file);
} // namespace clause::runtime::builtins::dets
