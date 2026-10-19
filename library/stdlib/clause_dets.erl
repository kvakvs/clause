%% The dets file engine (plan 21 step LIB53, docs/builtins.md#dets): runtime builtins over an open dets file held
%% by a handle, called by the table processes of library/stdlib/dets.erl. Failures of the file are {error, Reason}
%% in OTP's shapes.
-module(clause_dets).
-export([
    bchunk/2,
    bchunk_add/2,
    bchunk_end/2,
    bchunk_init/1,
    bchunk_start/2,
    bulk_abort/1,
    bulk_add/2,
    bulk_end/1,
    bulk_start/3,
    close/1,
    delete_all/1,
    first/1,
    fix/2,
    info/1,
    is_dets_file/1,
    mark_dirty/1,
    may_grow/1,
    next/2,
    open/1,
    open_file/1,
    save/2,
    scan/6,
    scan_init/1,
    slot/2,
    truncate/1,
    where/2,
    write_cache/3
]).

bchunk(_, _) -> clause_builtin_placeholder.
bchunk_add(_, _) -> clause_builtin_placeholder.
bchunk_end(_, _) -> clause_builtin_placeholder.
bchunk_init(_) -> clause_builtin_placeholder.
bchunk_start(_, _) -> clause_builtin_placeholder.
bulk_abort(_) -> clause_builtin_placeholder.
bulk_add(_, _) -> clause_builtin_placeholder.
bulk_end(_) -> clause_builtin_placeholder.
bulk_start(_, _, _) -> clause_builtin_placeholder.
close(_) -> clause_builtin_placeholder.
delete_all(_) -> clause_builtin_placeholder.
first(_) -> clause_builtin_placeholder.
fix(_, _) -> clause_builtin_placeholder.
info(_) -> clause_builtin_placeholder.
is_dets_file(_) -> clause_builtin_placeholder.
mark_dirty(_) -> clause_builtin_placeholder.
may_grow(_) -> clause_builtin_placeholder.
next(_, _) -> clause_builtin_placeholder.
open(_) -> clause_builtin_placeholder.
open_file(_) -> clause_builtin_placeholder.
save(_, _) -> clause_builtin_placeholder.
scan(_, _, _, _, _, _) -> clause_builtin_placeholder.
scan_init(_) -> clause_builtin_placeholder.
slot(_, _) -> clause_builtin_placeholder.
truncate(_) -> clause_builtin_placeholder.
where(_, _) -> clause_builtin_placeholder.
write_cache(_, _, _) -> clause_builtin_placeholder.
