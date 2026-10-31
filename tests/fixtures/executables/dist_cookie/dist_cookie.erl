%% Cookies as OTP's auth keeps them (plan 30 step DIST4b1c): none without distribution, the home directory's
%% .erlang.cookie created or read when distribution starts, --setcookie and --nocookie, cookies set for this node and
%% for other nodes, and the deprecated auth API. Each node runs in a fresh home directory.
-module(dist_cookie).
-export([main/1]).
-compile(nowarn_deprecated_function).

main(["created" = Mode]) ->
    off(),
    started(Mode),
    File = cookie_file(),
    {ok, Text} = file:read_file(File),
    show("cookie from file", {
        atom_to_binary(erlang:get_cookie()) =:= Text,
        byte_size(Text),
        lists:all(fun(C) -> C >= $A andalso C =< $Z end, binary_to_list(Text))
    }),
    show("file writable", writable(File)),
    show("other node", erlang:get_cookie(other@host) =:= erlang:get_cookie()),
    show("auth", auth:get_cookie() =:= erlang:get_cookie()),
    setting(),
    ok = net_kernel:stop(),
    show("stopped", [erlang:get_cookie(), raised(fun() -> erlang:set_cookie(c) end)]),
    started(Mode),
    show("again from file", atom_to_binary(erlang:get_cookie()) =:= Text),
    done();
main(["existing" = Mode]) ->
    File = cookie_file(),
    ok = file:write_file(File, "  Existing_Cookie\n"),
    ok = file:change_mode(File, 8#400),
    started(Mode),
    show("cookie", erlang:get_cookie()),
    done();
main(["given" = Mode]) ->
    started(Mode),
    show("cookie", erlang:get_cookie()),
    show("file", filelib:is_regular(cookie_file())),
    done();
main(["none" = Mode]) ->
    started(Mode),
    show("cookie", [erlang:get_cookie(), erlang:get_cookie(other@host)]),
    show("set", erlang:set_cookie(later)),
    show("cookie after set", erlang:get_cookie()),
    show("file", filelib:is_regular(cookie_file())),
    done().

%% Without distribution there is no cookie, and none can be set.
off() ->
    show("off", [
        erlang:get_cookie(),
        erlang:get_cookie(x@y),
        raised(fun() -> erlang:set_cookie(c) end),
        raised(fun() -> erlang:set_cookie(x@y, c) end),
        raised(fun() -> erlang:set_cookie(node(), c) end),
        auth:get_cookie(),
        auth:cookie(),
        raised(fun() -> auth:set_cookie(c) end),
        auth:is_auth(x@y)
    ]),
    show("bad arguments", [
        raised(fun() -> erlang:get_cookie("x") end),
        raised(fun() -> erlang:set_cookie("c") end),
        raised(fun() -> erlang:set_cookie(1, c) end)
    ]),
    show("file before", filelib:is_regular(cookie_file())).

%% Cookies set for this node and for other nodes, also through auth's deprecated functions.
setting() ->
    show("set", erlang:set_cookie(newc)),
    show("cookie after set", erlang:get_cookie()),
    show("set other", erlang:set_cookie(other@host, nodec)),
    show("other cookies", [erlang:get_cookie(other@host), erlang:get_cookie(third@host)]),
    show("set this node", erlang:set_cookie(node(), selfc)),
    show("this node's cookies", [erlang:get_cookie(), erlang:get_cookie(node()), erlang:get_cookie(third@host)]),
    show("auth functions", [
        auth:set_cookie(viaauth),
        auth:get_cookie(),
        auth:cookie(),
        auth:cookie([c2]),
        erlang:get_cookie(),
        auth:node_cookie(n@h, nc),
        auth:node_cookie([n2@h, nc2]),
        auth:get_cookie(n@h),
        auth:get_cookie(n2@h)
    ]),
    show("bad arguments", [
        raised(fun() -> erlang:set_cookie("s") end),
        raised(fun() -> erlang:set_cookie(x@y, "s") end),
        raised(fun() -> erlang:get_cookie(1) end)
    ]),
    show("auth server", [is_pid(whereis(auth)), auth:sync_cookie()]).

% Each node of the run registers a name of its own with the shared epmd.
started(Mode) ->
    {ok, _} = net_kernel:start([list_to_atom("cookie_" ++ Mode), shortnames]),
    ok.

done() -> io:format("dist_cookie: ok~n").

cookie_file() ->
    {ok, [[Home]]} = init:get_argument(home),
    filename:join(Home, ".erlang.cookie").

% Whether anyone may write the file: OTP leaves it readable by its owner alone.
writable(File) ->
    {ok, Info} = file:read_file_info(File),
    element(8, Info) band 8#222 =/= 0.

raised(Fun) ->
    try Fun() of
        Result -> {returned, Result}
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Label, Term) -> io:format("~s: ~p~n", [Label, Term]).
