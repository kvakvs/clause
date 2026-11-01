#pragma once
#include "../support.hpp"
#include "../typed.hpp"

// Spawning on other nodes (plan 30 step DIST5b2, docs/distribution.md#spawning): spawn requests sent as OTP's
// SPAWN_REQUEST, the blocking spawns continuing in clause_dist_spawn:spawn/5, and the SPAWN_REQUEST and SPAWN_REPLY a
// connection's input handler puts.
namespace clause::runtime::builtins {
// The call a spawn on another node makes: Module and Function atoms and the argument list.
struct RemoteSpawnCall {
    Term module;
    Term function;
    Term arguments;
};

// spawn_request/2..5 naming another node: SPAWN_REQUEST on its connection, or at once the error reply the options
// ask for (badopt, noconnection, notsup); the request id. Badarg for an improper argument list.
Term request_remote(ProcessContext &context, const Term &node, const RemoteSpawnCall &call,
                    const ListArgument *options);
// spawn/2,4, spawn_link/2,4, spawn_monitor/2,4 and spawn_opt/3,5 naming another node: continue as
// clause_dist_spawn:spawn(Node, Module, Function, Args, Options), which waits for the reply.
Word spawn_remote(ProcessContext &context, const Term &node, const RemoteSpawnCall &call, const Term &options);
// The call of spawn(Node, Fun) on another node: erlang:apply(Fun, []).
RemoteSpawnCall fun_call(ProcessContext &context, const Term &fun);
// Serve another node's SPAWN_REQUEST {29, ReqId, From, GroupLeader, {M, F, Arity}, Options} with its argument
// list, or answer it with an error SPAWN_REPLY; take its SPAWN_REPLY {31, ReqId, To, Flags, Result}.
void take_spawn_request(ProcessContext &context, const Term &control, Word node, const Term &arguments);
void take_spawn_reply(ProcessContext &context, Word node, const Term &control);
} // namespace clause::runtime::builtins
