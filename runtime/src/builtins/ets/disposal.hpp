#pragma once
#include "ets.hpp"

// The objects of a deleted or emptied ETS table, freed in portions by the process that deleted or emptied it (plan
// 21 step LIB10F, docs/runtime.md#ets-tables), so a large table does not hold its scheduler.
namespace clause::runtime::builtins::ets {
// Free the contents and give true, trapping between portions while objects are left.
Word dispose(ProcessContext &context, Table::Contents contents);
} // namespace clause::runtime::builtins::ets
