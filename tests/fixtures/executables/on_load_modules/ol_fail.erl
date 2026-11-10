%% An -on_load function that fails when the program's first argument asks it to: returning something else than ok, or
%% raising.
-module(ol_fail).
-export([ready/0]).
-on_load(check/0).

check() ->
    case init:get_plain_arguments() of
        ["return"] -> {error, refused};
        ["raise"] -> erlang:error(refused);
        _ -> ok
    end.

ready() -> true.
