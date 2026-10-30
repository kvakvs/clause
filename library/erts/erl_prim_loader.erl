%% Clause's replacement for OTP's erl_prim_loader (docs/libraries/library.md#runtime-module-replacements), generated from
%% erl_prim_loader.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). An executable has no boot loader or archives (plan step 41P): the file
%% queries answer over Clause's file module with OTP's bare error; loading .beam modules waits for step RT1.
-module(erl_prim_loader).
-export([
    get_cwd/0,
    get_cwd/1,
    get_file/1,
    get_modules/2,
    get_modules/3,
    get_path/0,
    is_basename/1,
    list_dir/1,
    prim_get_cwd/2,
    prim_init/0,
    prim_list_dir/2,
    prim_read_file/2,
    prim_read_file_info/3,
    purge_archive_cache/0,
    read_file/1,
    read_file_info/1,
    read_link_info/1,
    set_path/1,
    set_primary_archive/4,
    start/0
]).

get_modules(_, _) -> erlang:error({not_implemented, get_modules, 2}).
get_modules(_, _, _) -> erlang:error({not_implemented, get_modules, 3}).
prim_get_cwd(_, _) -> erlang:error({not_implemented, prim_get_cwd, 2}).
prim_init() -> erlang:error({not_implemented, prim_init, 0}).
prim_list_dir(_, _) -> erlang:error({not_implemented, prim_list_dir, 2}).
prim_read_file(_, _) -> erlang:error({not_implemented, prim_read_file, 2}).
prim_read_file_info(_, _, _) -> erlang:error({not_implemented, prim_read_file_info, 3}).
set_primary_archive(_, _, _, _) -> erlang:error({not_implemented, set_primary_archive, 4}).
start() -> erlang:error({not_implemented, start, 0}).

get_cwd() -> result(file:get_cwd()).

get_cwd(Drive) -> result(file:get_cwd(Drive)).

list_dir(Dir) -> result(file:list_dir(Dir)).

read_file(File) -> result(file:read_file(File)).

read_file_info(File) -> result(file:read_file_info(File)).

read_link_info(File) -> result(file:read_link_info(File)).

%% {ok, Value} as is, error for any failure.
result({ok, Value}) -> {ok, Value};
result(_) -> error.

%% {ok, Binary, FullName}: a base name is looked up in the loader path, another name read as given.
get_file(File) when is_atom(File) ->
    get_file(atom_to_list(File));
get_file(File) ->
    case is_basename(File) of
        true -> get_file_in(File, path());
        false -> get_file_at(File)
    end.

get_file_in(File, [Dir | Dirs]) ->
    case get_file_at(lists:flatten([Dir, "/", File])) of
        error -> get_file_in(File, Dirs);
        Found -> Found
    end;
get_file_in(_, []) ->
    error.

get_file_at(Name) ->
    case file:read_file(Name) of
        {ok, Binary} -> {ok, Binary, Name};
        _ -> error
    end.

%% The loader path: ["."] until set_path/1 sets it.
get_path() -> {ok, path()}.

set_path(Paths) when is_list(Paths) -> persistent_term:put({?MODULE, path}, Paths).

path() -> persistent_term:get({?MODULE, path}, ["."]).

%% Whether a name has no directory part: no slash, and on Windows no backslash or drive.
is_basename(File) ->
    case deep_member($/, File) of
        true ->
            false;
        false ->
            case os:type() of
                {win32, _} ->
                    case File of
                        [_, $: | _] -> false;
                        _ -> not deep_member($\\, File)
                    end;
                _ ->
                    true
            end
    end.

deep_member(C, [C | _]) -> true;
deep_member(C, [L | T]) when is_list(L) -> deep_member(C, L) orelse deep_member(C, T);
deep_member(C, [A | T]) when is_atom(A) -> deep_member(C, atom_to_list(A)) orelse deep_member(C, T);
deep_member(C, [_ | T]) -> deep_member(C, T);
deep_member(_, []) -> false.

%% No archives exist.
purge_archive_cache() -> ok.
