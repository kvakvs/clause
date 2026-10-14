-module(clause_zlib).
-export([
    open/0,
    close/1,
    set_controller/2,
    deflate_init/2,
    inflate_init/2,
    deflate/3,
    inflate/2,
    deflate_end/1,
    inflate_end/1,
    deflate_reset/1,
    inflate_reset/1,
    deflate_params/2,
    deflate_set_dictionary/2,
    inflate_set_dictionary/2,
    inflate_get_dictionary/1
]).

% Clause's zlib streams (plan step 41Q, docs/builtins.md#zlib-module): runtime builtins over the pinned zlib, which
% replace these bodies at registration. library/erts/zlib.erl answers OTP's zlib functions over them.

open() -> erlang:error(undef).
close(_) -> erlang:error(undef).
set_controller(_, _) -> erlang:error(undef).
deflate_init(_, _) -> erlang:error(undef).
inflate_init(_, _) -> erlang:error(undef).
deflate(_, _, _) -> erlang:error(undef).
inflate(_, _) -> erlang:error(undef).
deflate_end(_) -> erlang:error(undef).
inflate_end(_) -> erlang:error(undef).
deflate_reset(_) -> erlang:error(undef).
inflate_reset(_) -> erlang:error(undef).
deflate_params(_, _) -> erlang:error(undef).
deflate_set_dictionary(_, _) -> erlang:error(undef).
inflate_set_dictionary(_, _) -> erlang:error(undef).
inflate_get_dictionary(_) -> erlang:error(undef).
