%% An -on_load function named as a tuple, which only makes atoms exist, as jwerl's conveniece_keys/0 does.
-module(ol_keys).
-export([keys/0]).
-on_load({known, 0}).

known() ->
    _ = [iss, sub, aud],
    ok.

keys() -> [iss, sub].
