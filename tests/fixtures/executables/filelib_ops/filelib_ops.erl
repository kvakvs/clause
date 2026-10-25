-module(filelib_ops).
-export([main/1]).

%% filelib (plan 21 step LIB51): wildcards (every pattern form, escapes, "**",
%% "..", alternatives, character sets, Cwd and module forms, bad patterns),
%% file queries, ensure_dir/ensure_path, fold_files/5,6, find_file/2,3,
%% find_source/1,2,3 and safe_relative_path/2. A directory tree is made in the
%% working directory and removed again.
main([]) ->
    Dir = "filelib_ops.dir",
    _ = remove(Dir),
    make_tree(Dir),
    wildcards(Dir),
    queries(Dir),
    ensured(Dir),
    folded(Dir),
    found(Dir),
    safe(),
    ok = remove(Dir),
    io:format("filelib_ops: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, Value]).

make_tree(Dir) ->
    Files = [
        "a.erl",
        "b.hrl",
        "c.txt",
        "[x].txt",
        "@at",
        ".hidden",
        "sub/x.erl",
        "sub/deep/y.erl",
        "sub/deep/z.beam",
        "ebin/m.beam",
        "ebin/o.beam",
        "src/m.erl",
        "src/n/o.erl",
        "lib/p/ebin/q.beam",
        "lib/p/src/q.erl"
    ],
    [ok = write(filename:join(Dir, F)) || F <- Files],
    ok = file:make_dir(filename:join(Dir, "empty")).

write(Name) ->
    ok = filelib:ensure_dir(Name),
    file:write_file(Name, Name).

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

wildcards(Dir) ->
    Patterns = [
        "*",
        "*.erl",
        "*.{erl,hrl}",
        "?.erl",
        "[a-b]*",
        "[b-a]*",
        "[]x]*",
        "[[]x].txt",
        "\\[x\\].txt",
        "[x].txt",
        "\\@*",
        "@*",
        ".*",
        "**/*.erl",
        "**/*.{beam,hrl}",
        "sub/**",
        "**",
        "sub/../*.erl",
        "sub/*/y.erl",
        "*/deep/*",
        "s*/d*/*.beam",
        "{a,b}*",
        "{a.erl,s*}",
        "a.erl",
        "sub",
        "sub/",
        "missing",
        "missing/*",
        "*/missing",
        "*.erl\\",
        "[",
        "a**",
        "lib/*/ebin/*.beam",
        "empty/*",
        "a.erl/*",
        "{a,{b,c}}",
        "{abc",
        "*.{erl,hrl"
    ],
    [show(P, outcome(fun() -> filelib:wildcard(P, Dir) end)) || P <- Patterns],
    show(relative, filelib:wildcard(Dir ++ "/sub/*.erl")),
    show(dot_relative, filelib:wildcard("./" ++ Dir ++ "/*.hrl")),
    show(parent_cwd, filelib:wildcard("*.erl", Dir ++ "/sub/..")),
    show(module, filelib:wildcard(Dir ++ "/*.erl", erl_prim_loader)),
    show(module_cwd, filelib:wildcard("*.txt", Dir, file)),
    Absolute = filename:absname(Dir),
    Found = filelib:wildcard(Absolute ++ "/sub/*/*.erl"),
    show(absolute, [
        lists:prefix(Absolute, F) andalso lists:nthtail(length(Absolute), F)
     || F <- Found
    ]),
    show(absolute_literal, filelib:wildcard(Absolute ++ "/a.erl") =:= [Absolute ++ "/a.erl"]),
    show(bad_arguments, [
        outcome(fun() -> filelib:wildcard(<<"*">>) end),
        outcome(fun() -> filelib:wildcard("*", 1) end),
        outcome(fun() -> filelib:wildcard("") end)
    ]).

queries(Dir) ->
    A = filename:join(Dir, "a.erl"),
    Sub = filename:join(Dir, "sub"),
    Missing = filename:join(Dir, "missing"),
    show(is_dir, [filelib:is_dir(N) || N <- [A, Sub, Missing]]),
    show(is_file, [filelib:is_file(N) || N <- [A, Sub, Missing]]),
    show(is_regular, [filelib:is_regular(N) || N <- [A, Sub, Missing]]),
    show(module_queries, [
        filelib:is_dir(Sub, erl_prim_loader),
        filelib:is_file(A, file),
        filelib:is_regular(Sub, erl_prim_loader),
        filelib:file_size(A, erl_prim_loader),
        filelib:last_modified(Missing, file)
    ]),
    show(file_size, [filelib:file_size(A), filelib:file_size(Missing)]),
    show(last_modified, [date_time(filelib:last_modified(A)), filelib:last_modified(Missing)]),
    show(binary_names, [filelib:is_dir(list_to_binary(Sub)), filelib:file_size(list_to_binary(A))]).

date_time({{Y, M, D}, {H, Mi, S}}) ->
    lists:all(fun erlang:is_integer/1, [Y, M, D, H, Mi, S]);
date_time(_) ->
    false.

ensured(Dir) ->
    show(ensure, [
        filelib:ensure_dir(filename:join(Dir, "new/deeper/file.txt")),
        filelib:is_dir(filename:join(Dir, "new/deeper")),
        filelib:is_dir(filename:join(Dir, "new/deeper/file.txt")),
        filelib:ensure_dir(filename:join(Dir, "new/deeper/file.txt")),
        filelib:ensure_path(filename:join(Dir, "p/q/r")),
        filelib:is_dir(filename:join(Dir, "p/q/r")),
        filelib:ensure_path(filename:join(Dir, "p/q/r")),
        filelib:ensure_dir("/"),
        filelib:ensure_path("/"),
        filelib:ensure_dir("plain_name"),
        element(1, filelib:ensure_path(filename:join(Dir, "a.erl/x")))
    ]).

folded(Dir) ->
    Collect = fun(F, Acc) -> [F | Acc] end,
    show(fold_recursive, lists:sort(filelib:fold_files(Dir, "\\.erl$", true, Collect, []))),
    show(fold_flat, lists:sort(filelib:fold_files(Dir, "^[ab]", false, Collect, []))),
    show(
        fold_module,
        lists:sort(filelib:fold_files(Dir ++ "/sub", "", true, Collect, [], erl_prim_loader))
    ),
    show(
        fold_missing, filelib:fold_files(filename:join(Dir, "missing"), ".*", true, Collect, none)
    ),
    show(fold_count, filelib:fold_files(Dir, ".", true, fun(_, N) -> N + 1 end, 0)),
    show(fold_bad_regexp, outcome(fun() -> filelib:fold_files(Dir, "(", true, Collect, []) end)).

found(Dir) ->
    Ebin = filename:join(Dir, "ebin"),
    show(find_file, [
        filelib:find_file("m.erl", Ebin),
        filelib:find_file("m.erl", Ebin, [{"ebin", "src"}]),
        filelib:find_file("o.erl", Ebin, [{"ebin", "src/*"}]),
        filelib:find_file("o.erl", Ebin, [{"ebin", "src"}]),
        filelib:find_file("m.beam", Ebin, [{"", ""}]),
        filelib:find_file("m.erl", Ebin, [{"nope", "src"}, {"ebin", "src"}]),
        filelib:find_file("m.erl", Ebin, [{".beam", ".erl", []}])
    ]),
    show(find_source, [
        filelib:find_source(filename:join(Ebin, "m.beam")),
        filelib:find_source("o.beam", Ebin),
        filelib:find_source("q.beam", filename:join(Dir, "lib/p/ebin")),
        filelib:find_source("m.beam", Ebin, [{".beam", ".erl", [{"ebin", "src"}]}]),
        filelib:find_source("m.beam", Ebin, [{".beam", ".hrl", [{"ebin", "src"}]}]),
        filelib:find_source("m.beam", Ebin, [{"ebin", "src"}]),
        filelib:find_source(filename:join(Dir, "a.beam")),
        filelib:find_source(filename:join(Dir, "src/m.erl"))
    ]).

safe() ->
    Paths = [
        "dir/sub_dir/..",
        "dir/..",
        "dir/../..",
        "/abs/path",
        "a/./b",
        "./a/b/",
        "",
        ".",
        "..",
        "a/../../b",
        <<"x/../y">>,
        <<"x/./..">>,
        "a//b"
    ],
    show(safe_relative_path, [filelib:safe_relative_path(P, ".") || P <- Paths]),
    show(safe_empty_cwd, filelib:safe_relative_path("a/b", "")).

remove(Dir) ->
    case file:del_dir_r(Dir) of
        {error, enoent} -> ok;
        Other -> Other
    end.
