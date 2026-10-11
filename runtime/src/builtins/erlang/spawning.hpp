#pragma once
#include "../../scheduler/executor.hpp"

// Options shared by monitor/3 (processes.cpp) and the spawn monitors of spawn_opt and spawn_request (spawning.cpp),
// plan step 41G2.
namespace clause::runtime::builtins {
// The options of monitor/3 or {monitor, Options}: {alias, explicit_unalias | demonitor | reply_demonitor} and
// {tag, Tag}; badarg for an improper list or any other option.
detail::MonitorOptions monitor_options(const Term &list);
} // namespace clause::runtime::builtins
