-module(io_requests).
-export([main/1, until_dot/2, until_dot/3, fail/2, words/0]).

% OTP's I/O protocol through io:request/1,2 and io:requests/1,2 (step 41ZC): get_until with a continuation function
% on an open file in list and binary mode and on standard input, the characters it leaves over read again by the
% next request, get_line, get_chars and put_chars requests, old request forms, unknown requests and a failing
% get_until function. Files are created in the working directory and removed again.

show(Label, Value) -> io:format("~s ~p~n", [Label, Value]).

% A get_until function: the characters up to and including the next dot, or what is left at eof.
until_dot(Cont, Chars) -> until_dot(Cont, Chars, dot).

until_dot(Cont, eof, _) ->
    {done, {eof, lists:reverse(Cont)}, eof};
until_dot(Cont, Chars, Tag) ->
    case lists:splitwith(fun(C) -> C =/= $. end, Chars) of
        {Before, [$. | After]} -> {done, {Tag, lists:reverse(Cont, Before ++ ".")}, After};
        {All, []} -> {more, lists:reverse(All, Cont)}
    end.

fail(_, _) -> erlang:error(failed).

words() -> ["put", $\s, <<"by mfa">>, $\n].

main(["file"]) ->
    file:delete("r1.txt"),
    ok = file:write_file("r1.txt", "one. two.\nthree\nfour. tail"),
    {ok, F} = file:open("r1.txt", [read]),
    show("get_until", io:request(F, {get_until, latin1, '', ?MODULE, until_dot, [dot]})),
    show("get_line", io:request(F, {get_line, latin1, ''})),
    show(
        "get_until across lines",
        io:request(F, {get_until, unicode, '', ?MODULE, until_dot, [line]})
    ),
    show("get_chars", io:request(F, {get_chars, latin1, '', 2})),
    show("old get_until", io:request(F, {get_until, '', ?MODULE, until_dot, []})),
    show("get_until at eof", io:request(F, {get_until, latin1, '', ?MODULE, until_dot, [dot]})),
    show("get_line at eof", io:request(F, {get_line, latin1, ''})),
    show("unknown", io:request(F, {no_such_request, 1})),
    show("requests", io:requests(F, [{get_line, latin1, ''}, {get_chars, latin1, '', 1}])),
    file:close(F),
    {ok, B} = file:open("r1.txt", [read, binary]),
    show("binary get_until", io:request(B, {get_until, latin1, '', ?MODULE, until_dot, [dot]})),
    show("binary get_line", io:request(B, {get_line, latin1, ''})),
    file:close(B),
    {ok, W} = file:open("r1.txt", [write]),
    show("put_chars", io:request(W, {put_chars, unicode, "written\n"})),
    show("put_chars mfa", io:request(W, {put_chars, latin1, ?MODULE, words, []})),
    show("old put_chars", io:request(W, {put_chars, <<"old\n">>})),
    show(
        "requests stop at error",
        io:requests(W, [{put_chars, latin1, "a\n"}, {bad}, {put_chars, latin1, "b\n"}])
    ),
    file:close(W),
    show("written", file:read_file("r1.txt")),
    failing(),
    file:delete("r1.txt"),
    io:format("io_requests: ok~n");
main(["stdin"]) ->
    show("get_until", io:request(standard_io, {get_until, latin1, '', ?MODULE, until_dot, [dot]})),
    show("get_line", io:request({get_line, latin1, ''})),
    show("get_until again", io:request(user, {get_until, latin1, '', ?MODULE, until_dot, [dot]})),
    show("io:get_line", io:get_line("")),
    show("get_chars", io:request(standard_io, {get_chars, latin1, '', 3})),
    show(
        "get_until at eof",
        io:request(standard_io, {get_until, latin1, '', ?MODULE, until_dot, [dot]})
    ),
    show("put_chars", io:request(standard_io, {put_chars, latin1, "out\n"})),
    show("unregistered", io:request(no_such_server, {get_line, latin1, ''})),
    show("calling self", io:request(self(), {get_line, latin1, ''})),
    io:format("io_requests: ok~n").

% A failing get_until function: the file server replies {error, Function} and exits with the failure; the process
% that opened the file lives on.
failing() ->
    {ok, F} = file:open("r1.txt", [read]),
    Ref = monitor(process, F),
    show("failing get_until", io:request(F, {get_until, latin1, '', ?MODULE, fail, []})),
    receive
        {'DOWN', Ref, process, F, Reason} -> show("file server exit", element(1, Reason))
    end,
    show("read after exit", file:read(F, 1)).
