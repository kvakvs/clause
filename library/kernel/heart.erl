%% Clause's heart (plan 21, docs/libraries/library.md#runtime-module-replacements): a Clause program has no heart
%% port program, so it answers as an OTP node started without -heart, where the heart process is not registered and
%% every documented call raises badarg. Starting heart is declined.
-module(heart).
-export([
    clear_callback/0,
    clear_cmd/0,
    cycle/0,
    get_callback/0,
    get_cmd/0,
    get_options/0,
    init/2,
    set_callback/2,
    set_cmd/1,
    set_options/1,
    start/0
]).

% The answer of a request to the heart process, which is never running.
clear_callback() -> no_heart().
clear_cmd() -> no_heart().
get_callback() -> no_heart().
get_cmd() -> no_heart().
get_options() -> no_heart().
set_callback(_, _) -> no_heart().
set_cmd(_) -> no_heart().
set_options(_) -> no_heart().

no_heart() -> erlang:error(badarg).

cycle() -> erlang:error({not_implemented, cycle, 0}).
init(_, _) -> erlang:error({not_implemented, init, 2}).
start() -> erlang:error({not_implemented, start, 0}).
