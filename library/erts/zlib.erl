%% Clause's replacement for OTP's zlib (docs/library.md#runtime-module-replacements), generated from
%% zlib.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}).
-module(zlib).
-export([
    close/1,
    compress/1,
    deflate/2,
    deflate/3,
    deflateEnd/1,
    deflateInit/1,
    deflateInit/2,
    deflateInit/6,
    deflateParams/3,
    deflateReset/1,
    deflateSetDictionary/2,
    gunzip/1,
    gzip/1,
    inflate/2,
    inflate/3,
    inflateEnd/1,
    inflateGetDictionary/1,
    inflateInit/1,
    inflateInit/2,
    inflateInit/3,
    inflateReset/1,
    inflateSetDictionary/2,
    on_load/0,
    open/0,
    safeInflate/2,
    set_controlling_process/2,
    uncompress/1,
    unzip/1,
    zip/1
]).
-include("zlib.types.hrl").

close(_) -> erlang:error({not_implemented, close, 1}).
compress(_) -> erlang:error({not_implemented, compress, 1}).
deflate(_, _) -> erlang:error({not_implemented, deflate, 2}).
deflate(_, _, _) -> erlang:error({not_implemented, deflate, 3}).
deflateEnd(_) -> erlang:error({not_implemented, deflateEnd, 1}).
deflateInit(_) -> erlang:error({not_implemented, deflateInit, 1}).
deflateInit(_, _) -> erlang:error({not_implemented, deflateInit, 2}).
deflateInit(_, _, _, _, _, _) -> erlang:error({not_implemented, deflateInit, 6}).
deflateParams(_, _, _) -> erlang:error({not_implemented, deflateParams, 3}).
deflateReset(_) -> erlang:error({not_implemented, deflateReset, 1}).
deflateSetDictionary(_, _) -> erlang:error({not_implemented, deflateSetDictionary, 2}).
gunzip(_) -> erlang:error({not_implemented, gunzip, 1}).
gzip(_) -> erlang:error({not_implemented, gzip, 1}).
inflate(_, _) -> erlang:error({not_implemented, inflate, 2}).
inflate(_, _, _) -> erlang:error({not_implemented, inflate, 3}).
inflateEnd(_) -> erlang:error({not_implemented, inflateEnd, 1}).
inflateGetDictionary(_) -> erlang:error({not_implemented, inflateGetDictionary, 1}).
inflateInit(_) -> erlang:error({not_implemented, inflateInit, 1}).
inflateInit(_, _) -> erlang:error({not_implemented, inflateInit, 2}).
inflateInit(_, _, _) -> erlang:error({not_implemented, inflateInit, 3}).
inflateReset(_) -> erlang:error({not_implemented, inflateReset, 1}).
inflateSetDictionary(_, _) -> erlang:error({not_implemented, inflateSetDictionary, 2}).
on_load() -> erlang:error({not_implemented, on_load, 0}).
open() -> erlang:error({not_implemented, open, 0}).
safeInflate(_, _) -> erlang:error({not_implemented, safeInflate, 2}).
set_controlling_process(_, _) -> erlang:error({not_implemented, set_controlling_process, 2}).
uncompress(_) -> erlang:error({not_implemented, uncompress, 1}).
unzip(_) -> erlang:error({not_implemented, unzip, 1}).
zip(_) -> erlang:error({not_implemented, zip, 1}).
