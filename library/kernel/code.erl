%% Clause's replacement for OTP's code (docs/library.md#runtime-module-replacements). The queries about loaded
%% modules are runtime builtins answering for the linked program without a code server (plan step 41Z,
%% docs/builtins.md#code-module). The code path and its directory queries are Erlang over a persistent term (plan 21
%% step LIB61); loading answers as OTP does for object code it cannot load, since every module is linked (D01).
-module(code).
-export([
    add_path/1,
    add_path/2,
    add_patha/1,
    add_patha/2,
    add_paths/1,
    add_paths/2,
    add_pathsa/1,
    add_pathsa/2,
    add_pathsz/1,
    add_pathsz/2,
    add_pathz/1,
    add_pathz/2,
    all_available/0,
    all_loaded/0,
    atomic_load/1,
    clash/0,
    clear_cache/0,
    compiler_dir/0,
    coverage_support/0,
    del_path/1,
    del_paths/1,
    delete/1,
    ensure_loaded/1,
    ensure_modules_loaded/1,
    finish_loading/1,
    get_chunk/2,
    get_coverage/2,
    get_coverage_mode/0,
    get_coverage_mode/1,
    get_debug_info/1,
    get_doc/1,
    get_doc/2,
    get_mode/0,
    get_object_code/1,
    get_path/0,
    is_loaded/1,
    is_sticky/1,
    lib_dir/0,
    lib_dir/1,
    lib_dir/2,
    load_abs/1,
    load_abs/2,
    load_binary/3,
    load_file/1,
    modified_modules/0,
    module_md5/1,
    module_status/0,
    module_status/1,
    objfile_extension/0,
    prepare_loading/1,
    priv_dir/1,
    purge/1,
    replace_path/2,
    replace_path/3,
    reset_coverage/1,
    root_dir/0,
    set_coverage_mode/1,
    set_path/1,
    set_path/2,
    soft_purge/1,
    start_link/0,
    stick_dir/1,
    stick_mod/1,
    stop/0,
    unstick_dir/1,
    unstick_mod/1,
    where_is_file/1,
    where_is_file/2,
    which/1
]).
-include("code.types.hrl").

-define(PATH_KEY, {code, path}).
-define(PREPARED, '$prepared$').
-define(is_cache(Cache), (Cache =:= cache orelse Cache =:= nocache)).

%% The code path (plan 21 step LIB61): a persistent term, built on first use as OTP's interactive code server builds
%% it: the ebin directories of ERL_LIBS, then the newest version of each application in lib_dir(), then ".".
get_path() -> path().

path() ->
    case persistent_term:get(?PATH_KEY, undefined) of
        undefined ->
            Path = initial_path(),
            persistent_term:put(?PATH_KEY, Path),
            Path;
        Path ->
            Path
    end.

initial_path() ->
    user_lib_paths() ++ bundle_paths(code:lib_dir()) ++ ["."].

% The ebin directories of the applications in the ERL_LIBS directories.
user_lib_paths() ->
    case os:getenv("ERL_LIBS") of
        false ->
            [];
        Dirs ->
            Separator =
                case os:type() of
                    {win32, _} -> $;;
                    _ -> $:
                end,
            [
                Ebin
             || Dir <- string:split(Dirs, [Separator], all),
                Ebin <- bundle_paths(Dir),
                filename:basename(Ebin) =:= "ebin"
            ]
    end.

% The code directory of the newest version of each application in Dir: its ebin or, without one, itself.
bundle_paths(Dir) ->
    case file:list_dir(Dir) of
        {ok, Names} ->
            lists:reverse([
                Code
             || Name <- newest_bundles(Names), Code <- bundle_code(filename:append(Dir, Name))
            ]);
        {error, _} ->
            []
    end.

bundle_code(Bundle) ->
    Ebin = filename:append(Bundle, "ebin"),
    case {filelib:is_dir(Ebin), filelib:is_dir(Bundle)} of
        {true, _} -> [Ebin];
        {false, true} -> [Bundle];
        {false, false} -> []
    end.

% One name per application, the highest numeric version of Name-Vsn, in application order.
newest_bundles(Names) ->
    Sorted = lists:reverse(lists:sort([bundle(Name) || Name <- Names])),
    Chosen = lists:foldl(
        fun({App, _, _} = Bundle, Acc) ->
            case lists:keymember(App, 1, Acc) of
                true -> Acc;
                false -> [Bundle | Acc]
            end
        end,
        [],
        Sorted
    ),
    [Name || {_, _, Name} <- Chosen].

% {App, Version, Name} of a directory name App-Vsn with a numeric version, {Name, [0], Name} otherwise.
bundle(Name) ->
    case string:lexemes(Name, "-") of
        [_, _ | _] = Parts ->
            App = lists:join($-, lists:droplast(Parts)),
            case numeric_version(lists:last(Parts)) of
                {ok, Version} -> {lists:flatten(App), Version, Name};
                error -> {Name, [0], Name}
            end;
        _ ->
            {Name, [0], Name}
    end.

numeric_version(Text) ->
    Parts = string:lexemes(Text, "."),
    case lists:all(fun(Part) -> lists:all(fun(C) -> C >= $0 andalso C =< $9 end, Part) end, Parts) of
        true -> {ok, [list_to_integer(Part) || Part <- Parts]};
        false -> error
    end.

%% The application name of a code path directory .../Name[-Vsn][/ebin], as OTP's code server names it.
path_name(Dir) ->
    case lists:reverse(filename:split(Dir)) of
        ["ebin", Name | _] -> before_hyphen(Name);
        [Name | _] -> before_hyphen(Name);
        [] -> ""
    end.

before_hyphen(Name) ->
    lists:takewhile(fun(C) -> C =/= $- end, Name).

% The name lib_dir/1 knows a path directory by: "." and a lone ebin are named after their absolute directory.
lookup_name(Dir) ->
    case path_name(Dir) of
        Name when Name =/= "ebin", Name =/= "." -> Name;
        _ -> path_name(filename:absname(Dir))
    end.

% The application directory of a code path directory: without its trailing ebin.
application_dir(Dir) ->
    case filename:split(Dir) of
        [_, _, _ | _] = Parts ->
            case lists:last(Parts) of
                "ebin" -> filename:join(lists:droplast(Parts));
                _ -> filename:join(Parts)
            end;
        [_, "ebin"] ->
            application_dir(filename:absname(Dir));
        ["ebin"] ->
            application_dir(filename:absname(Dir));
        Parts ->
            filename:join(Parts)
    end.

% The directory of application Name: the first path directory it names, or the root's erts.
named_dir(Name) ->
    case [Dir || Dir <- path(), lookup_name(Dir) =:= Name] of
        [Dir | _] ->
            {ok, application_dir(Dir)};
        [] when Name =:= "erts" ->
            Erts = filename:join(code:root_dir(), "erts"),
            case filelib:is_dir(Erts) of
                true -> {ok, Erts};
                false -> error
            end;
        [] ->
            error
    end.

%% The installation root is the executable's directory: a Clause program has no OTP installation.
root_dir() -> filename:join([filename:dirname(init:script_name())]).

lib_dir() -> filename:append(code:root_dir(), "lib").

lib_dir(App) when is_atom(App); is_list(App) ->
    case catch named_dir(to_list(App)) of
        {ok, Dir} -> Dir;
        _ -> {error, bad_name}
    end.

lib_dir(App, SubDir) when is_atom(App), is_atom(SubDir) ->
    sub_dir(App, SubDir).

priv_dir(App) when is_atom(App); is_list(App) ->
    sub_dir(App, priv).

sub_dir(App, SubDir) ->
    case catch named_dir(to_list(App)) of
        {ok, Dir} -> filename:join(Dir, atom_to_list(SubDir));
        _ -> {error, bad_name}
    end.

compiler_dir() ->
    case named_dir("compiler") of
        {ok, Dir} -> Dir;
        error -> ""
    end.

to_list(Name) when is_atom(Name) -> atom_to_list(Name);
to_list(Name) -> Name.

objfile_extension() -> init:objfile_extension().

get_mode() -> interactive.

%% No directory listing is cached: clearing the cache has nothing to do.
clear_cache() -> ok.

%% Path changes (plan 21 step LIB61), with OTP's normalization, checks and answers.
set_path(PathList) -> set_path(PathList, nocache).

set_path(PathList, Cache) when is_list(PathList), ?is_cache(Cache) ->
    case normalize_paths(PathList) of
        {ok, Dirs} ->
            case lists:all(fun filelib:is_dir/1, Dirs) of
                true -> store_path(Dirs);
                false -> {error, bad_directory}
            end;
        {error, _} ->
            {error, bad_directory}
    end.

store_path(Dirs) when is_list(Dirs) ->
    persistent_term:put(?PATH_KEY, Dirs),
    true.

% Each directory as a normalized string: atoms as their names, flat strings joined; {error, Valid} when some are not.
normalize_paths(Dirs) ->
    Normalized = [normalize_path(Dir) || Dir <- Dirs],
    Valid = [Dir || {ok, Dir} <- Normalized],
    case length(Valid) =:= length(Dirs) of
        true -> {ok, Valid};
        false -> {error, Valid}
    end.

normalize_path(Dir) when is_atom(Dir) ->
    {ok, atom_to_list(Dir)};
normalize_path(Dir) when is_list(Dir) ->
    case lists:all(fun is_integer/1, Dir) of
        true -> {ok, filename:join([Dir])};
        false -> error
    end;
normalize_path(_) ->
    error.

add_path(Dir) -> add_pathz(Dir, nocache).

add_path(Dir, Cache) when is_list(Dir), ?is_cache(Cache) -> add_pathz(Dir, Cache).

add_pathz(Dir) -> add_pathz(Dir, nocache).

add_pathz(Dir, Cache) when is_list(Dir), ?is_cache(Cache) ->
    {_, [Normalized]} = normalize_paths([Dir]),
    add_dir(last, Normalized).

add_patha(Dir) -> add_patha(Dir, nocache).

add_patha(Dir, Cache) when is_list(Dir), ?is_cache(Cache) ->
    {_, [Normalized]} = normalize_paths([Dir]),
    add_dir(first, Normalized).

add_paths(Dirs) -> add_pathsz(Dirs, nocache).

add_paths(Dirs, Cache) when is_list(Dirs), ?is_cache(Cache) -> add_pathsz(Dirs, Cache).

add_pathsz(Dirs) -> add_pathsz(Dirs, nocache).

%% Each valid directory added in turn; invalid ones are skipped.
add_pathsz(Dirs, Cache) when is_list(Dirs), ?is_cache(Cache) ->
    {_, Normalized} = normalize_paths(Dirs),
    lists:foreach(fun(Dir) -> add_dir(last, Dir) end, Normalized).

add_pathsa(Dirs) -> add_pathsa(Dirs, nocache).

add_pathsa(Dirs, Cache) when is_list(Dirs), ?is_cache(Cache) ->
    {_, Normalized} = normalize_paths(Dirs),
    lists:foreach(fun(Dir) -> add_dir(first, Dir) end, Normalized).

% Add a directory first (moving it there when present) or last (kept in place when present).
add_dir(Where, Dir) ->
    case filelib:is_dir(Dir) of
        false ->
            {error, bad_directory};
        true ->
            Path = path(),
            case Where of
                first -> store_path([Dir | lists:delete(Dir, Path)]);
                last -> store_path(append_new(Path, Dir))
            end
    end.

append_new(Path, Dir) ->
    case lists:member(Dir, Path) of
        true -> Path;
        false -> Path ++ [Dir]
    end.

%% Delete the first directory named .../Name[-Vsn][/ebin], or the directory Name itself.
del_path(Name) when is_list(Name); is_atom(Name) ->
    case catch filename:join([to_list(Name)]) of
        {'EXIT', _} ->
            {error, bad_name};
        Normalized ->
            case delete_named(Normalized, path(), []) of
                {ok, Path} -> store_path(Path);
                false -> false
            end
    end.

delete_named(Name, [Dir | Rest], Before) ->
    case path_name(Dir) =:= Name orelse Dir =:= Name of
        true -> {ok, lists:reverse(Before, Rest)};
        false -> delete_named(Name, Rest, [Dir | Before])
    end;
delete_named(_, [], _) ->
    false.

del_paths(Names) when is_list(Names) ->
    lists:foreach(fun del_path_entry/1, Names).

del_path_entry(Name) when is_list(Name); is_atom(Name) -> del_path(Name);
del_path_entry(_) -> false.

replace_path(Name, Dir) -> replace_path(Name, Dir, nocache).

%% Replace the first directory named .../Name[-Vsn][/ebin] by Dir, which must have that name; add it last otherwise.
replace_path(Name, Dir, Cache) when
    (is_atom(Name) orelse is_list(Name)), (is_atom(Dir) orelse is_list(Dir)), ?is_cache(Cache)
->
    case catch replacement(to_list(Name), Dir) of
        {ok, App, NewDir} -> store_path(replace_named(App, NewDir, path(), []));
        {error, _} = Error -> Error;
        _ -> {error, {badarg, [Name, Dir]}}
    end.

replacement(App, Dir) ->
    NewDir = filename:join([to_list(Dir)]),
    case path_name(Dir) =:= App of
        false ->
            {error, bad_name};
        true ->
            case filelib:is_dir(NewDir) of
                true -> {ok, App, NewDir};
                false -> {error, bad_directory}
            end
    end.

replace_named(App, NewDir, [Dir | Rest], Before) ->
    case path_name(Dir) =:= App of
        true -> lists:reverse(Before, [NewDir | Rest]);
        false -> replace_named(App, NewDir, Rest, [Dir | Before])
    end;
replace_named(_, NewDir, [], Before) ->
    lists:reverse(Before, [NewDir]).

%% The first File found in a code path directory, or non_existing.
where_is_file(File) when is_list(File) ->
    where_is_file(path(), File).

where_is_file([], _) ->
    non_existing;
where_is_file([{Dir, Files} | Rest], File) ->
    case lists:member(File, Files) of
        true -> filename:append(Dir, File);
        false -> where_is_file(Rest, File)
    end;
where_is_file([Dir | Rest], File) ->
    Full = filename:append(Dir, File),
    case file:read_file_info(Full) of
        {ok, _} -> Full;
        {error, _} -> where_is_file(Rest, File)
    end.

%% Print each object file that hides another of the same name later in the path, and their count.
clash() ->
    Files = lists:append([object_files(Dir) || Dir <- path()]),
    Count = length(clashes(Files)),
    io:format("** Found ~w name clashes in code paths ~n", [Count]).

object_files(Dir) ->
    case file:list_dir(Dir) of
        {ok, Names} ->
            Extension = objfile_extension(),
            [{Dir, Name} || Name <- Names, lists:suffix(Extension, Name)];
        {error, _} ->
            io:format("** Bad path can't read ~ts~n", [Dir]),
            []
    end.

clashes([{Dir, File} | Rest]) ->
    case lists:keyfind(File, 2, Rest) of
        false ->
            clashes(Rest);
        {Hidden, File} ->
            io:format("** ~ts hides ~ts~n", [filename:join(Dir, File), filename:join(Hidden, File)]),
            [clash | clashes(Rest)]
    end;
clashes([]) ->
    [].

%% The loaded modules and the object files of the path that are not, as {Name, File, Loaded}, sorted by name.
all_available() ->
    Loaded = lists:sort([{atom_to_list(M), File, true} || {M, File} <- code:all_loaded()]),
    Found = lists:foldl(fun available_files/2, #{}, path()),
    Files = lists:sort([{filename:rootname(Name), filename:append(Dir, Name), false} || Name := Dir <- Found]),
    lists:ukeymerge(1, Loaded, Files).

available_files(Dir, Found) ->
    case file:list_dir(Dir) of
        {ok, Names} ->
            Extension = objfile_extension(),
            lists:foldl(
                fun(Name, Acc) ->
                    case filename:extension(Name) =:= Extension andalso not is_map_key(Name, Acc) of
                        true -> Acc#{Name => Dir};
                        false -> Acc
                    end
                end,
                Found,
                Names
            );
        {error, _} ->
            Found
    end.

%% Sticky directories: a directory that can be listed is accepted; no module of the executable is ever reloaded.
stick_dir(Dir) when is_list(Dir) -> listable(Dir).

unstick_dir(Dir) when is_list(Dir) -> listable(Dir).

listable(Dir) ->
    case file:list_dir(Dir) of
        {ok, _} -> ok;
        {error, _} -> error
    end.

stick_mod(Module) when is_atom(Module) -> true.

unstick_mod(Module) when is_atom(Module) -> true.

%% The object code of Module from the first path directory that has it, or error.
get_object_code(Module) when is_atom(Module) ->
    Name = atom_to_list(Module),
    case erl_prim_loader:is_basename(Name) of
        true -> object_code(Module, path(), Name ++ objfile_extension());
        false -> error
    end.

object_code(Module, [Dir | Rest], File) ->
    Full = filename:append(Dir, File),
    case file:read_file(Full) of
        {ok, Binary} -> {Module, Binary, filename:absname(Full)};
        {error, _} -> object_code(Module, Rest, File)
    end;
object_code(_, [], _) ->
    error.

%% Loading (D01): every module is linked into the executable. Object code that is found cannot be loaded and gives
%% erlang:prepare_loading/2's error, missing code nofile, as in OTP.
load_file(Module) when is_atom(Module) ->
    case get_object_code(Module) of
        error -> {error, nofile};
        {Module, Binary, _} -> load_module(Module, Binary)
    end.

load_abs(File) when is_list(File); is_atom(File) ->
    load_abs(File, list_to_atom(filename:basename(File))).

load_abs(File, Module) when (is_list(File) orelse is_atom(File)), is_atom(Module) ->
    case is_file_name(File) of
        true ->
            case file:read_file(filename:absname(lists:concat([File, objfile_extension()]))) of
                {ok, Binary} -> load_module(Module, Binary);
                {error, _} -> {error, nofile}
            end;
        false ->
            {error, badarg}
    end.

load_binary(Module, File, Binary) when
    is_atom(Module), (is_list(File) orelse is_atom(File)), is_binary(Binary)
->
    case is_file_name(File) of
        true -> load_module(Module, Binary);
        false -> {error, badarg}
    end.

load_module(Module, Binary) ->
    {error, _} = erlang:prepare_loading(Module, Binary).

is_file_name(File) when is_atom(File) -> true;
is_file_name(File) -> lists:all(fun is_integer/1, File).

%% The modules of an executable have no old code to purge and cannot be deleted.
delete(Module) when is_atom(Module) ->
    try erlang:delete_module(Module) of
        true -> true;
        _ -> false
    catch
        _:_ -> false
    end.

purge(Module) when is_atom(Module) -> false.

soft_purge(Module) when is_atom(Module) -> true.

modified_modules() -> [Module || {Module, modified} <- code:module_status()].

atomic_load(Modules) ->
    case prepare(Modules) of
        {ok, []} -> ok;
        {error, _} = Error -> Error;
        badarg -> erlang:error(function_clause, [Modules])
    end.

prepare_loading(Modules) ->
    case prepare(Modules) of
        {ok, Prepared} -> {ok, {?PREPARED, Prepared}};
        {error, _} = Error -> Error;
        badarg -> erlang:error(function_clause, [Modules])
    end.

%% Only an empty list of modules is ever prepared.
finish_loading({?PREPARED, []}) -> ok;
finish_loading(Prepared) -> erlang:error(function_clause, [Prepared]).

% {ok, []} for no modules, the error of each one otherwise, duplicates first; badarg for an item that is no module.
prepare(Modules) when is_list(Modules) ->
    case lists:all(fun loadable/1, Modules) of
        true -> prepare_unique(Modules);
        false -> badarg
    end;
prepare(_) ->
    badarg.

loadable({Module, File, Binary}) -> is_atom(Module) andalso is_list(File) andalso is_binary(Binary);
loadable(Module) -> is_atom(Module).

prepare_unique(Modules) ->
    Names = lists:sort([module_of(Item) || Item <- Modules]),
    case lists:usort([{Name, duplicated} || [Name, Name | _] <- tails(Names)]) of
        [] -> prepare_each(Modules);
        Duplicated -> {error, lists:reverse(Duplicated)}
    end.

tails([_ | Rest] = List) -> [List | tails(Rest)];
tails([]) -> [].

module_of({Module, _, _}) -> Module;
module_of(Module) -> Module.

prepare_each([]) ->
    {ok, []};
prepare_each(Modules) ->
    Binaries = [{Module, prepare_error(Module, Binary)} || {Module, _, Binary} <- Modules],
    Files = [{Module, prepare_file(Module)} || Module <- Modules, is_atom(Module)],
    {error, Binaries ++ Files}.

prepare_file(Module) ->
    case get_object_code(Module) of
        {Module, Binary, _} -> prepare_error(Module, Binary);
        error -> nofile
    end.

prepare_error(Module, Binary) ->
    {error, Reason} = erlang:prepare_loading(Module, Binary),
    Reason.

%% Documentation chunks are not embedded in executables (plan 21 step LIB61).
get_doc(Module) when is_atom(Module) ->
    case code:which(Module) of
        non_existing -> {error, non_existing};
        _ -> {error, missing}
    end.

get_doc(Module, #{sources := [_ | _]}) -> get_doc(Module);
get_doc(_, #{sources := []}) -> {error, missing}.

%% Linked code has no debug information table.
get_debug_info(Module) when is_atom(Module) ->
    case code:is_loaded(Module) of
        false -> erlang:error(badarg, [Module]);
        _ -> none
    end;
get_debug_info(Module) ->
    erlang:error(badarg, [Module]).

%% Native coverage is not supported: OTP's answers for such a runtime system.
coverage_support() -> false.

get_coverage(Level, Module) -> erlang:error(badarg, [Level, Module]).

get_coverage_mode() -> erlang:error(badarg, []).

get_coverage_mode(Module) -> erlang:error(badarg, [Module]).

set_coverage_mode(Mode) -> erlang:error(badarg, [Mode]).

reset_coverage(Module) -> erlang:error(badarg, [Module]).

all_loaded() -> clause_builtin_placeholder.
ensure_loaded(_) -> clause_builtin_placeholder.
ensure_modules_loaded(_) -> clause_builtin_placeholder.
get_chunk(_, _) -> erlang:error({not_implemented, get_chunk, 2}).
is_loaded(_) -> clause_builtin_placeholder.
is_sticky(_) -> clause_builtin_placeholder.
module_md5(_) -> erlang:error({not_implemented, module_md5, 1}).
module_status() -> clause_builtin_placeholder.
module_status(_) -> clause_builtin_placeholder.
start_link() -> erlang:error({not_implemented, start_link, 0}).
stop() -> erlang:error({not_implemented, stop, 0}).
which(_) -> clause_builtin_placeholder.
