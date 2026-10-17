-module(io).
-export([
    get_line/1, get_line/2, get_chars/2, get_chars/3, request/1, request/2, requests/1, requests/2
]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/library.md#runtime-module-replacements).
-export([
    columns/0,
    columns/1,
    format/1,
    format/2,
    format/3,
    fread/2,
    fread/3,
    fwrite/1,
    fwrite/2,
    fwrite/3,
    get_password/0,
    get_password/1,
    getopts/0,
    getopts/1,
    nl/0,
    nl/1,
    parse_erl_exprs/1,
    parse_erl_exprs/2,
    parse_erl_exprs/3,
    parse_erl_exprs/4,
    parse_erl_form/1,
    parse_erl_form/2,
    parse_erl_form/3,
    parse_erl_form/4,
    printable_range/0,
    put_chars/1,
    put_chars/2,
    read/1,
    read/2,
    read/3,
    read/4,
    rows/0,
    rows/1,
    scan_erl_exprs/1,
    scan_erl_exprs/2,
    scan_erl_exprs/3,
    scan_erl_exprs/4,
    scan_erl_form/1,
    scan_erl_form/2,
    scan_erl_form/3,
    scan_erl_form/4,
    setopts/1,
    setopts/2,
    write/1,
    write/2
]).
-include("io.types.hrl").

% Input functions of the project-owned io module (docs/library.md, docs/ports.md#standard-io-and-files); io:format/1,2
% and io:put_chars/1 are builtins of the runtime. Standard input is read by one server process, registered as
% clause_stdin, which owns an {fd, 0, 1} port. It and every open file's server also answer OTP's I/O protocol
% ({io_request, From, ReplyAs, Request}), which io:request/1,2 and io:requests/1,2 send.

% Write Prompt, then read a line of standard input with its newline; the rest without one at the end, then eof.
get_line(Prompt) -> get_line(standard_io, Prompt).

get_line(standard_io, Prompt) ->
    prompt(Prompt),
    stdin_request(get_line);
get_line(File, Prompt) when is_pid(File) ->
    prompt(Prompt),
    file_request(File, get_line).

% Write Prompt, then read up to Count characters of standard input; eof at its end.
get_chars(Prompt, Count) -> get_chars(standard_io, Prompt, Count).

get_chars(standard_io, Prompt, Count) when is_integer(Count), Count >= 0 ->
    prompt(Prompt),
    stdin_request({get_chars, Count});
get_chars(File, Prompt, Count) when is_pid(File), is_integer(Count), Count >= 0 ->
    prompt(Prompt),
    case file:read(File, Count) of
        {ok, Data} -> Data;
        Other -> Other
    end.

prompt(Prompt) when is_atom(Prompt) -> io:put_chars(atom_to_list(Prompt));
prompt(Prompt) -> io:put_chars(Prompt).

% A request to an open file's server, answered with the data or eof.
file_request(File, Request) ->
    Ref = monitor(process, File),
    File ! {file_request, self(), Ref, Request},
    receive
        {file_reply, Ref, Reply} ->
            demonitor(Ref, [flush]),
            Reply;
        {'DOWN', Ref, process, File, _} ->
            {error, terminated}
    end.

% Ask the standard input server, starting it on first use.
stdin_request(Request) ->
    Server = server(),
    Ref = monitor(process, Server),
    Server ! {stdin_request, self(), Ref, Request},
    receive
        {stdin_reply, Ref, Reply} ->
            demonitor(Ref, [flush]),
            Reply;
        {'DOWN', Ref, process, Server, _} ->
            {error, terminated}
    end.

% --- OTP's I/O protocol (step 41ZC) ---

% Send one I/O request, in OTP's request format, to an I/O server: standard_io and user name the standard input
% server, other names are looked up as registered processes. {requests, List} runs its requests in order.
request(Request) -> request(standard_io, Request).

request(standard_io, Request) ->
    request(server(), Request);
request(user, Request) ->
    request(server(), Request);
request(Pid, _) when Pid =:= self() -> {error, calling_self};
request(Pid, {requests, Requests}) when is_pid(Pid) -> requests(Pid, Requests);
request(Pid, Request) when is_pid(Pid) -> execute(Pid, io_request(Request));
request(Name, Request) when is_atom(Name) ->
    case whereis(Name) of
        undefined -> {error, arguments};
        Pid -> request(Pid, Request)
    end.

% Run I/O requests in order, stopping at the first {error, _} reply; the last reply.
requests(Requests) -> requests(standard_io, Requests).

requests(Io, [Request]) ->
    request(Io, Request);
requests(Io, [Request | Rest]) ->
    case request(Io, Request) of
        {error, _} = Error -> Error;
        _ -> requests(Io, Rest)
    end;
requests(_, []) ->
    ok.

execute(Pid, Request) ->
    Ref = monitor(process, Pid),
    Pid ! {io_request, self(), Ref, Request},
    receive
        {io_reply, Ref, Reply} ->
            demonitor(Ref, [flush]),
            Reply;
        {'DOWN', Ref, process, Pid, _} ->
            {error, terminated}
    end.

% Requests without an encoding are latin1 requests, as OTP's I/O servers read them.
io_request({put_chars, Chars}) -> {put_chars, latin1, Chars};
io_request({put_chars, M, F, A}) -> {put_chars, latin1, M, F, A};
io_request({get_chars, Prompt, Count}) -> {get_chars, latin1, Prompt, Count};
io_request({get_line, Prompt}) -> {get_line, latin1, Prompt};
io_request({get_until, Prompt, M, F, Args}) -> {get_until, latin1, Prompt, M, F, Args};
io_request(Request) -> Request.

% --- The standard input server ---

% The standard input server; two first users race to register it, the loser's server stops.
server() ->
    case whereis(clause_stdin) of
        undefined ->
            Self = self(),
            Pid = spawn(fun() -> start(Self) end),
            receive
                {Pid, started} -> server()
            end;
        Pid ->
            Pid
    end.

start(Starter) ->
    try register(clause_stdin, self()) of
        true ->
            Starter ! {self(), started},
            serve(open_port({fd, 0, 1}, [binary, eof, in]), <<>>, false, [])
    catch
        error:badarg -> Starter ! {self(), started}
    end.

% Serve requests in order from the input buffered so far; Ended once the port reported eof.
serve(Port, Buffer, Ended, Pending) ->
    case answer(Pending, Buffer, Ended) of
        {[], NewBuffer} -> wait(Port, NewBuffer, Ended, []);
        {Waiting, NewBuffer} -> wait(Port, NewBuffer, Ended, Waiting)
    end.

% Pending requests are {From, Ref, ReplyTag, Request}; ReplyTag is stdin_reply or io_reply.
wait(Port, Buffer, Ended, Pending) ->
    receive
        {Port, {data, Data}} ->
            serve(Port, <<Buffer/binary, Data/binary>>, Ended, Pending);
        {Port, eof} ->
            serve(Port, Buffer, true, Pending);
        {stdin_request, From, Ref, Request} ->
            serve(Port, Buffer, Ended, Pending ++ [{From, Ref, stdin_reply, Request}]);
        {io_request, From, Ref, Request} ->
            serve(Port, Buffer, Ended, Pending ++ [{From, Ref, io_reply, accept(Request)}])
    end.

% Answer the oldest requests the buffer can; the rest wait. A get_until request that consumed the buffer but needs
% more input stays first with its new continuation, and gets eof at once when input already ended.
answer([], Buffer, _) ->
    {[], Buffer};
answer([{From, Ref, Tag, Request} | Rest] = Pending, Buffer, Ended) ->
    case take(Request, Buffer, Ended) of
        wait ->
            {Pending, Buffer};
        {more, Continued, NewBuffer} ->
            answer([{From, Ref, Tag, Continued} | Rest], NewBuffer, Ended);
        {Reply, NewBuffer} ->
            From ! {Tag, Ref, Reply},
            answer(Rest, NewBuffer, Ended)
    end.

% An I/O protocol request as the server's own request: input requests write their prompt now, once.
accept({get_line, _, Prompt}) ->
    prompt(Prompt),
    get_line;
accept({get_chars, _, Prompt, Count}) when is_integer(Count), Count >= 0 ->
    prompt(Prompt),
    {get_chars, Count};
accept({get_until, _, Prompt, M, F, Args}) ->
    prompt(Prompt),
    {get_until, M, F, Args, []};
accept({put_chars, _, Chars}) ->
    {put_chars, Chars};
accept({put_chars, _, M, F, A}) ->
    {put_chars, {M, F, A}};
accept(Request) ->
    {unknown, Request}.

% The reply to one request from the buffer, or wait for more input.
take({put_chars, Chars}, Buffer, _) ->
    {put(Chars), Buffer};
take({unknown, Request}, Buffer, _) ->
    {{error, {request, Request}}, Buffer};
take({get_until, M, F, Args, Cont}, Buffer, Ended) ->
    until(M, F, Args, Cont, Buffer, Ended);
take(_, <<>>, true) ->
    {eof, <<>>};
take(get_line, Buffer, Ended) ->
    case line_size(Buffer, 0) of
        none when Ended -> {binary_to_list(Buffer), <<>>};
        none -> wait;
        Size -> split(Buffer, Size)
    end;
take({get_chars, Count}, Buffer, Ended) when byte_size(Buffer) >= Count; Ended ->
    split(Buffer, min(Count, byte_size(Buffer)));
take({get_chars, _}, _, _) ->
    wait.

% Feed the whole buffer, or eof once input ended, to M:F; its rest characters go back in front of the buffer.
until(_, _, _, _, <<>>, false) ->
    wait;
until(M, F, Args, Cont, Buffer, Ended) ->
    Data =
        case Buffer of
            <<>> when Ended -> eof;
            _ -> binary_to_list(Buffer)
        end,
    try apply(M, F, [Cont, Data | Args]) of
        {done, Result, Rest} -> {Result, rest_bytes(Rest)};
        {more, NewCont} -> {more, {get_until, M, F, Args, NewCont}, <<>>}
    catch
        _:_ -> {{error, F}, <<>>}
    end.

% Bytes of the characters a get_until function left over; eof leaves none.
rest_bytes(Rest) when is_list(Rest); is_binary(Rest) ->
    try
        iolist_to_binary(Rest)
    catch
        error:badarg -> unicode:characters_to_binary(Rest)
    end;
rest_bytes(_) ->
    <<>>.

% Write a put_chars request's characters: chardata, or the result of M:F(A...).
put({M, F, A}) ->
    try apply(M, F, A) of
        Chars -> put(Chars)
    catch
        _:_ -> {error, F}
    end;
put(Chars) ->
    try io:put_chars(Chars) of
        ok -> ok
    catch
        error:badarg -> {error, put_chars}
    end.

split(Buffer, Size) ->
    <<Taken:Size/binary, Rest/binary>> = Buffer,
    {binary_to_list(Taken), Rest}.

% The size of the first line of Buffer with its newline, or none.
line_size(Buffer, At) ->
    case Buffer of
        <<_:At/binary, $\n, _/binary>> -> At + 1;
        <<_:At/binary, _, _/binary>> -> line_size(Buffer, At + 1);
        _ -> none
    end.

columns() -> erlang:error({not_implemented, columns, 0}).
columns(_) -> erlang:error({not_implemented, columns, 1}).
format(_) -> clause_builtin_placeholder.
format(_, _) -> clause_builtin_placeholder.
format(_, _, _) -> clause_builtin_placeholder.
fread(_, _) -> erlang:error({not_implemented, fread, 2}).
fread(_, _, _) -> erlang:error({not_implemented, fread, 3}).
fwrite(_) -> clause_builtin_placeholder.
fwrite(_, _) -> clause_builtin_placeholder.
fwrite(_, _, _) -> clause_builtin_placeholder.
get_password() -> erlang:error({not_implemented, get_password, 0}).
get_password(_) -> erlang:error({not_implemented, get_password, 1}).
getopts() -> erlang:error({not_implemented, getopts, 0}).
getopts(_) -> erlang:error({not_implemented, getopts, 1}).
nl() -> clause_builtin_placeholder.
nl(_) -> clause_builtin_placeholder.
parse_erl_exprs(_) -> erlang:error({not_implemented, parse_erl_exprs, 1}).
parse_erl_exprs(_, _) -> erlang:error({not_implemented, parse_erl_exprs, 2}).
parse_erl_exprs(_, _, _) -> erlang:error({not_implemented, parse_erl_exprs, 3}).
parse_erl_exprs(_, _, _, _) -> erlang:error({not_implemented, parse_erl_exprs, 4}).
parse_erl_form(_) -> erlang:error({not_implemented, parse_erl_form, 1}).
parse_erl_form(_, _) -> erlang:error({not_implemented, parse_erl_form, 2}).
parse_erl_form(_, _, _) -> erlang:error({not_implemented, parse_erl_form, 3}).
parse_erl_form(_, _, _, _) -> erlang:error({not_implemented, parse_erl_form, 4}).
%% Clause has no +pc flag: printable characters are Latin-1 (plan 21 step LIB4C).
printable_range() -> latin1.
put_chars(_) -> clause_builtin_placeholder.
put_chars(_, _) -> clause_builtin_placeholder.
read(_) -> erlang:error({not_implemented, read, 1}).
read(_, _) -> erlang:error({not_implemented, read, 2}).
read(_, _, _) -> erlang:error({not_implemented, read, 3}).
read(_, _, _, _) -> erlang:error({not_implemented, read, 4}).
rows() -> erlang:error({not_implemented, rows, 0}).
rows(_) -> erlang:error({not_implemented, rows, 1}).
scan_erl_exprs(_) -> erlang:error({not_implemented, scan_erl_exprs, 1}).
scan_erl_exprs(_, _) -> erlang:error({not_implemented, scan_erl_exprs, 2}).
scan_erl_exprs(_, _, _) -> erlang:error({not_implemented, scan_erl_exprs, 3}).
scan_erl_exprs(_, _, _, _) -> erlang:error({not_implemented, scan_erl_exprs, 4}).
scan_erl_form(_) -> erlang:error({not_implemented, scan_erl_form, 1}).
scan_erl_form(_, _) -> erlang:error({not_implemented, scan_erl_form, 2}).
scan_erl_form(_, _, _) -> erlang:error({not_implemented, scan_erl_form, 3}).
scan_erl_form(_, _, _, _) -> erlang:error({not_implemented, scan_erl_form, 4}).
setopts(_) -> erlang:error({not_implemented, setopts, 1}).
setopts(_, _) -> erlang:error({not_implemented, setopts, 2}).
write(_) -> erlang:error({not_implemented, write, 1}).
write(_, _) -> erlang:error({not_implemented, write, 2}).
