-module(code_paths).
-moduledoc "code's path functions, directory queries and loading answers (plan 21 LIB61).".
-export([main/1]).

main(_) ->
    make_tree(),
    Old = code:get_path(),
    queries(),
    paths(Old),
    lookups(),
    changes(),
    loading(),
    show(restored, code:set_path(Old)),
    io:format("code_paths: ok~n").

% Application directories below t/: two versions of a, b with priv, c without ebin, x.beam twice.
make_tree() ->
    [
        ok = filelib:ensure_path(Dir)
     || Dir <- ["t/a-1.0/ebin", "t/a-2.0/ebin", "t/b/ebin", "t/b/priv", "t/c", "t/d-3/ebin"]
    ],
    ok = file:write_file("t/b/ebin/x.beam", <<"not object code">>),
    ok = file:write_file("t/a-2.0/ebin/x.beam", <<"not object code">>),
    ok = file:write_file("t/b/ebin/b.app", <<"{application, b, []}.\n">>).

queries() ->
    show(get_mode, code:get_mode()),
    show(objfile_extension, code:objfile_extension()),
    show(root_dir, is_list(code:root_dir())),
    show(lib_dir, code:lib_dir() =:= filename:join(code:root_dir(), "lib")),
    show(clear_cache, code:clear_cache()),
    show(path_strings, lists:all(fun is_list/1, code:get_path())).

% Only the t/ directories of the path, in path order.
ours() -> [Dir || "t/" ++ _ = Dir <- code:get_path()].

paths(Old) ->
    show(set_path, code:set_path(["t/b/ebin", "t/a-2.0/ebin" | Old])),
    show(ours, ours()),
    show(set_path_missing, code:set_path(["t/none" | Old])),
    show(set_path_bad, code:set_path([id(1) | Old])),
    show(set_path_atom_dir, code:set_path([id('t/c') | Old])),
    show(ours, ours()),
    show(set_path_not_list, catch_error(fun() -> code:set_path(id(x)) end)),
    show(set_path_bad_cache, catch_error(fun() -> code:set_path(Old, id(sometimes)) end)),
    show(add_patha, code:add_patha("t/a-1.0/ebin")),
    show(add_pathz, code:add_pathz("t/b/ebin/")),
    show(add_path, code:add_path("t/d-3/ebin", cache)),
    show(add_path_missing, code:add_path("t/none")),
    show(ours, ours()),
    show(add_patha_again, code:add_patha("t/d-3/ebin")),
    show(add_pathz_again, code:add_pathz("t/a-1.0/ebin")),
    show(ours, ours()),
    show(add_path_atom, catch_error(fun() -> code:add_path(id('t/b')) end)),
    show(add_pathsa, code:add_pathsa(["t/b", "t/none", "t/a-2.0"])),
    show(ours, ours()),
    show(add_pathsz, code:add_pathsz(["t/a-2.0/ebin", "t/b/priv"])),
    show(ours, ours()),
    show(add_paths, code:add_paths(["t/c/"], nocache)).

lookups() ->
    show(lib_dir_b, code:lib_dir(b)),
    show(lib_dir_a, code:lib_dir("a")),
    show(lib_dir_d, code:lib_dir(d)),
    show(lib_dir_missing, code:lib_dir(no_such_application)),
    show(lib_dir_bad, catch_error(fun() -> code:lib_dir(id(1)) end)),
    show(priv_dir_b, code:priv_dir(b)),
    show(priv_dir_missing, code:priv_dir("no_such_application")),
    show(lib_dir_sub, apply(code, lib_dir, [b, ebin])),
    show(where_is_file, code:where_is_file("x.beam")),
    show(where_is_app, code:where_is_file("b.app")),
    show(where_is_missing, code:where_is_file("none.beam")),
    show(where_is_file_bad, catch_error(fun() -> code:where_is_file(id(x)) end)),
    show(available, [Item || {_, "t/" ++ _, _} = Item <- code:all_available()]),
    {x, Binary, File} = code:get_object_code(x),
    show(
        object_code,
        {Binary, filename:pathtype(File),
            lists:last(filename:split(filename:dirname(filename:dirname(File))))}
    ),
    show(object_code_missing, code:get_object_code(no_such_module)),
    show(compiler_dir, is_list(code:compiler_dir())),
    show(stick_dir, code:stick_dir("t/b/ebin")),
    show(stick_dir_missing, code:stick_dir("t/none")),
    show(unstick_dir, code:unstick_dir("t/b/ebin")),
    code:clash().

changes() ->
    show(del_path_name, code:del_path(a)),
    show(ours, ours()),
    show(del_path_dir, code:del_path("t/b/priv")),
    show(del_path_missing, code:del_path(no_such_application)),
    show(del_path_bad, catch_error(fun() -> code:del_path(id(1)) end)),
    show(ours, ours()),
    show(replace_path, code:replace_path(a, "t/a-1.0/ebin")),
    show(ours, ours()),
    show(replace_path_name, code:replace_path(a, "t/b/ebin")),
    show(replace_path_missing, code:replace_path(a, "t/none/a-9/ebin")),
    show(replace_path_last, code:replace_path("d", "t/d-3/ebin")),
    show(ours, ours()),
    show(del_paths, code:del_paths([b, "t/c", d])),
    show(ours, ours()).

loading() ->
    show(load_file, code:load_file(x)),
    show(load_file_missing, code:load_file(no_such_module)),
    show(load_file_bad, catch_error(fun() -> code:load_file(id("x")) end)),
    show(load_abs, code:load_abs("t/b/ebin/x")),
    show(load_abs_missing, code:load_abs("t/none")),
    show(load_binary, code:load_binary(y, "y.beam", <<"junk">>)),
    show(load_binary_name, code:load_binary(y, id([y, z]), <<"junk">>)),
    show(load_binary_bad, catch_error(fun() -> code:load_binary(y, "y.beam", id(junk)) end)),
    show(atomic_load, sorted(code:atomic_load([x, no_such_module, {y, "y.beam", <<"junk">>}]))),
    show(atomic_load_duplicated, code:atomic_load([x, {x, "x.beam", <<>>}])),
    show(atomic_load_empty, code:atomic_load([])),
    show(atomic_load_bad, catch_error(fun() -> code:atomic_load(id([1])) end)),
    show(prepare_loading, sorted(code:prepare_loading([no_such_module, x]))),
    {ok, Prepared} = code:prepare_loading([]),
    show(finish_loading, code:finish_loading(Prepared)),
    show(finish_loading_bad, catch_error(fun() -> code:finish_loading(id(x)) end)),
    show(delete, code:delete(no_such_module)),
    show(purge, code:purge(no_such_module)),
    show(soft_purge, code:soft_purge(no_such_module)),
    show(modified_modules, code:modified_modules()),
    show(get_doc_missing, code:get_doc(no_such_module)),
    show(get_debug_info_missing, catch_error(fun() -> code:get_debug_info(no_such_module) end)),
    show(get_debug_info_bad, catch_error(fun() -> code:get_debug_info(id("lists")) end)).

sorted({error, Errors}) -> {error, lists:sort(Errors)};
sorted(Other) -> Other.

show(Label, Value) -> io:format("~p ~p~n", [Label, Value]).

catch_error(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

id(X) -> X.
