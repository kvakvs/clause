#pragma once
#include "ets.hpp"
#include <optional>

// The options of ets:new/2 and ets:setopts/2 (plan 21 steps LIB10A, LIB10B).
namespace clause::runtime::builtins::ets {
// A heir named by an option: its pid and the data its 'ETS-TRANSFER' message carries; a heir set without data
// ({heir, Pid}) inherits without a message.
struct Heir {
    Word pid;
    std::optional<Term> data;
};

// The options of ets:new/2: the table's, and its heir, if any.
struct NewOptions {
    detail::ets::TableOptions table;
    std::optional<Heir> heir;
};

// Read ets:new/2's option list; badarg for anything else.
NewOptions new_options(const Term &options);

// A heir option of setopts/2, {heir, none}, {heir, Pid} or {heir, Pid, Data}: the new heir, none to clear it.
struct HeirChange {
    std::optional<Heir> heir;
};

// Read a {heir, ...} tuple of two or more elements; none when malformed.
std::optional<HeirChange> heir_change(const Term &option);
} // namespace clause::runtime::builtins::ets
