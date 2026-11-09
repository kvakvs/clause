%% OTP's ram_file as libraries call it (plan 21 step MIM7, docs/libraries/library.md#undocumented-modules): a file in
%% memory is file's (file:open/2 with ram), and its functions are file's. compress, uuencode and the other driver-only
%% functions are declined.
-module(ram_file).
-export([
    close/1,
    datasync/1,
    get_size/1,
    open/2,
    position/2,
    pread/2, pread/3,
    pwrite/2, pwrite/3,
    read/2,
    sync/1,
    truncate/1,
    write/2
]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([
    advise/4,
    allocate/3,
    copy/3,
    get_file/1,
    ipread_s32bu_p32bu/3
]).

close(File) -> file:close(File).

datasync(File) -> file:datasync(File).

% The size of a file in memory, its position kept: einval once closed, function_clause for another
% file (as OTP's).
get_size(File) when is_pid(File) ->
    Ref = monitor(process, File),
    File ! {file_request, self(), Ref, get_size},
    receive
        {file_reply, Ref, {ok, Size}} ->
            demonitor(Ref, [flush]),
            {ok, Size};
        {file_reply, Ref, _} ->
            demonitor(Ref, [flush]),
            erlang:error(function_clause, [File]);
        {'DOWN', Ref, process, File, _} ->
            {error, einval}
    end;
get_size(File) ->
    erlang:error(function_clause, [File]).

open(Data, Modes) -> file:open(Data, [ram | Modes]).

position(File, Location) -> file:position(File, Location).

pread(File, LocationNumbers) -> file:pread(File, LocationNumbers).

pread(File, At, Size) -> file:pread(File, At, Size).

pwrite(File, LocationBytes) -> file:pwrite(File, LocationBytes).

pwrite(File, At, Data) -> file:pwrite(File, At, Data).

read(File, Size) -> file:read(File, Size).

sync(File) -> file:sync(File).

truncate(File) -> file:truncate(File).

write(File, Data) -> file:write(File, Data).

advise(_, _, _, _) -> erlang:error({not_implemented, advise, 4}).
allocate(_, _, _) -> erlang:error({not_implemented, allocate, 3}).
copy(_, _, _) -> erlang:error({not_implemented, copy, 3}).
get_file(_) -> erlang:error({not_implemented, get_file, 1}).
ipread_s32bu_p32bu(_, _, _) -> erlang:error({not_implemented, ipread_s32bu_p32bu, 3}).
