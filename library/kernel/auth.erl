%% Clause's auth (plan 21 step LIB25, docs/library.md): the deprecated cookie API as an OTP node without distribution
%% answers it: there is no cookie, and setting one raises distribution_not_started.
-module(auth).
-export([
    cookie/0,
    cookie/1,
    get_cookie/0,
    get_cookie/1,
    is_auth/1,
    node_cookie/1,
    node_cookie/2,
    print/3,
    set_cookie/1,
    set_cookie/2,
    sync_cookie/0,
    start_link/0,
    init/1,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    terminate/2,
    code_change/3
]).

is_auth(_) -> no.

cookie() -> get_cookie().

cookie([Cookie]) -> set_cookie(Cookie);
cookie(Cookie) -> set_cookie(Cookie).

node_cookie([Node, Cookie]) -> node_cookie(Node, Cookie).

node_cookie(Node, Cookie) ->
    set_cookie(Node, Cookie),
    is_auth(Node).

get_cookie() -> nocookie.

get_cookie(_) -> nocookie.

set_cookie(Cookie) when is_atom(Cookie) -> erlang:error(distribution_not_started).

set_cookie(Node, Cookie) when is_atom(Node), is_atom(Cookie) -> erlang:error(distribution_not_started).

%% The auth server is not running.
sync_cookie() -> exit({noproc, {gen_server, call, [auth, sync_cookie, infinity]}}).

print(_, _, _) -> ok.

%% The auth server belongs to distribution.
start_link() -> erlang:error({not_implemented, start_link, 0}).
init(_) -> erlang:error({not_implemented, init, 1}).
handle_call(_, _, _) -> erlang:error({not_implemented, handle_call, 3}).
handle_cast(_, _) -> erlang:error({not_implemented, handle_cast, 2}).
handle_info(_, _) -> erlang:error({not_implemented, handle_info, 2}).
terminate(_, _) -> erlang:error({not_implemented, terminate, 2}).
code_change(_, _, _) -> erlang:error({not_implemented, code_change, 3}).
