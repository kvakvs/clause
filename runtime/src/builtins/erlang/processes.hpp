#pragma once
#include "../typed.hpp"

// Process builtins the option-taking variants share (plan 21 step LIB9A: send/3, send_nosuspend/2,3, link/2,
// exit/3, exit_signal/3 and the spawns naming a node reuse them).
namespace clause::runtime::builtins {
// Dest ! Msg: Msg, after delivering it to the process Dest names.
TermResult<Term> send_message(ProcessContext &context, const Term &destination, const Term &message);
// Whether a send destination is {Name, Node} naming another node, or another node's pid or reference: the message
// goes on that node's connection (plan 30 step DIST4b3a). Another node's port is badarg.
bool remote_destination(const Term &destination);
// link(Pid): true once linked; error:noproc for an ended process unless the caller traps exits.
TermResult<Term> link_process(ProcessContext &context, const Term &pid);
// exit(Dest, Reason) (`self_normal`: Dest the caller with reason normal ends it) or exit_signal(Dest, Reason).
TermResult<Term> send_exit(ProcessContext &context, const Term &destination, const Term &reason, bool self_normal);
} // namespace clause::runtime::builtins
