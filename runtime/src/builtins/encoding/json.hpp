#pragma once
#include "../support.hpp"
#include <cstddef>

// Shared by the json builtins (plan 21 step LIB5E/LIB5F, docs/builtins.md#encodings).
namespace clause::runtime::builtins::encoding {
// A piece of iodata and its size in bytes, as iolist_size/1 counts it.
struct Piece {
    Term term;
    std::size_t size;
};

// encode_binary/1 (all = false) or encode_binary_escape_all/1 of a binary: OTP's iodata and its size.
Piece escaped(ProcessContext &context, const Term &binary, bool all);
} // namespace clause::runtime::builtins::encoding
