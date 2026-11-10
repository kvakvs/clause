%% An -on_load function making the module's state before any call, as MongooseIM's mongoose_mam_id does.
-module(ol_counter).
-export([next/0]).
-on_load(load/0).
-vsn(1).

load() ->
    persistent_term:put(?MODULE, {atomics:new(1, []), self()}),
    ok.

next() ->
    {Counter, _} = persistent_term:get(?MODULE),
    atomics:add_get(Counter, 1, 1).
