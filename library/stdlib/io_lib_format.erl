%% OTP's io_lib_format as libraries call it (plan 21 step MIM7, docs/libraries/library.md#undocumented-modules): the
%% shortest text of a float. io_lib's documented functions format; the rest is declined.
-module(io_lib_format).
-export([fwrite_g/1]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([
    build/1,
    build/2,
    build_bin/1,
    build_bin/2,
    fwrite/2,
    fwrite/3,
    fwrite_bin/2,
    fwrite_bin/3,
    indentation/2,
    scan/2,
    unscan/1
]).

fwrite_g(Float) -> float_to_list(Float, [short]).

build(_) -> erlang:error({not_implemented, build, 1}).
build(_, _) -> erlang:error({not_implemented, build, 2}).
build_bin(_) -> erlang:error({not_implemented, build_bin, 1}).
build_bin(_, _) -> erlang:error({not_implemented, build_bin, 2}).
fwrite(_, _) -> erlang:error({not_implemented, fwrite, 2}).
fwrite(_, _, _) -> erlang:error({not_implemented, fwrite, 3}).
fwrite_bin(_, _) -> erlang:error({not_implemented, fwrite_bin, 2}).
fwrite_bin(_, _, _) -> erlang:error({not_implemented, fwrite_bin, 3}).
indentation(_, _) -> erlang:error({not_implemented, indentation, 2}).
scan(_, _) -> erlang:error({not_implemented, scan, 2}).
unscan(_) -> erlang:error({not_implemented, unscan, 1}).
