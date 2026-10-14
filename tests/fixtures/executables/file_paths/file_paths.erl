-module(file_paths).
-export([main/1]).

%% Directories, links and copies of Clause's file module and the
%% erl_prim_loader queries over it (plan step 41P). Answers that differ
%% between platforms (absolute paths, symbolic links needing a privilege on
%% Windows) are checked for shape.
main([]) ->
    Base = "file_paths_work",
    _ = file:del_dir_r(Base),
    ok = file:make_dir(Base),
    {ok, Start} = file:get_cwd(),
    ok = file:set_cwd(Base),
    {ok, Inside} = file:get_cwd(),
    io:format("~p~n", [
        [lists:suffix("/" ++ Base, Inside), lists:prefix(Start, Inside), absolute(Inside)]
    ]),
    io:format("~p~n", [files()]),
    io:format("~p~n", [links()]),
    io:format("~p~n", [loader()]),
    ok = file:set_cwd(Start),
    io:format("~p~n", [[file:del_dir_r(Base), file:read_file_info(Base)]]),
    io:format("file_paths: ok~n").

files() ->
    ok = file:write_file("a.txt", <<"hello">>),
    ok = file:make_dir("d"),
    ok = file:write_file("d/b.txt", <<"x">>),
    [
        file:copy("a.txt", "c.txt"),
        file:read_file("c.txt"),
        file:copy("a.txt", "e.txt", 3),
        file:read_file("e.txt"),
        file:copy({"a.txt", [read]}, {"f.txt", [append]}),
        file:copy("a.txt", {"f.txt", [append]}),
        file:read_file("f.txt"),
        file:copy("nope", "g.txt"),
        file:copy("a.txt", "nope_dir/g.txt"),
        file:copy("a.txt", "x", -1),
        lists:sort(element(2, file:list_dir_all("."))),
        file:native_name_encoding(),
        element(3, element(2, file:raw_read_file_info("a.txt"))),
        file:read_file("a.txt", [raw]),
        file:delete("e.txt", [raw]),
        file:read_file("a.txt/x"),
        file:read_file("d"),
        file:open("d", [write]),
        file:write_file("d", <<"x">>),
        file:del_dir("a.txt"),
        file:del_dir("nope"),
        file:rename("nope", "x"),
        file:set_cwd("nope"),
        file:del_dir_r("nope"),
        file:del_dir_r("d"),
        file:read_file_info("d")
    ].

links() ->
    Symlink = file:make_symlink("a.txt", "s.txt"),
    [
        file:make_link("a.txt", "h.txt"),
        file:read_file("h.txt"),
        file:make_link("a.txt", "h.txt"),
        file:make_link("nope", "h2.txt"),
        file:read_link("a.txt"),
        file:read_link("nope"),
        file:read_link_all("a.txt"),
        element(3, element(2, file:read_link_info("a.txt"))),
        element(3, element(2, file:read_link_info("a.txt", [{time, posix}]))),
        file:read_link_info("nope"),
        lists:member(Symlink, [ok, {error, eperm}]),
        symlink(Symlink)
    ].

%% A symbolic link's own type and target where the platform let the program make one.
symlink(ok) ->
    {ok, Info} = file:read_link_info("s.txt"),
    Answers = [element(3, Info), file:read_link("s.txt"), file:read_file("s.txt")],
    ok = file:delete("s.txt"),
    Answers;
symlink(_) ->
    [symlink, {ok, "a.txt"}, {ok, <<"hello">>}].

loader() ->
    {ok, Cwd} = file:get_cwd(),
    [
        erl_prim_loader:read_file("a.txt"),
        erl_prim_loader:read_file("nope"),
        lists:sort(element(2, erl_prim_loader:list_dir("."))),
        erl_prim_loader:list_dir("nope"),
        element(3, element(2, erl_prim_loader:read_file_info("a.txt"))),
        erl_prim_loader:read_file_info("nope"),
        erl_prim_loader:read_link_info("nope"),
        erl_prim_loader:get_cwd() =:= {ok, Cwd},
        erl_prim_loader:get_file("nope"),
        erl_prim_loader:get_file("nope/a.txt"),
        erl_prim_loader:is_basename("a.txt"),
        erl_prim_loader:is_basename("d/a.txt"),
        erl_prim_loader:is_basename(["d", ["/a"]]),
        outcome(fun() -> erl_prim_loader:is_basename('a.txt') end),
        erl_prim_loader:purge_archive_cache()
    ].

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

%% Whether a path is absolute with forward slashes, as OTP spells the current directory.
absolute([$/ | _]) -> true;
absolute([_, $:, $/ | _]) -> true;
absolute(_) -> false.
