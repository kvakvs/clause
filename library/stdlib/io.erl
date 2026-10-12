-module(io).
-export([get_line/1, get_line/2, get_chars/2, get_chars/3]).
%% OTP 29 exports Clause does not implement yet: stubs raising error({not_implemented, F, A}),
%% or catalog builtins the runtime puts in their place (docs/library.md#runtime-module-replacements).
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
    request/1,
    request/2,
    requests/1,
    requests/2,
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

% Input functions of the project-owned io module (docs/library.md, docs/ports.md#standard-io-and-files); io:format/1,2
% and io:put_chars/1 are builtins of the runtime. Standard input is read by one server process, registered as
% clause_stdin, which owns an {fd, 0, 1} port.

% Write Prompt, then read a line of standard input with its newline; the rest without one at the end, then eof.
get_line(Prompt) -> get_line(standard_io, Prompt).

get_line(standard_io, Prompt) ->
    prompt(Prompt),
    request(get_line);
get_line(File, Prompt) when is_pid(File) ->
    prompt(Prompt),
    file_request(File, get_line).

% Write Prompt, then read up to Count characters of standard input; eof at its end.
get_chars(Prompt, Count) -> get_chars(standard_io, Prompt, Count).

get_chars(standard_io, Prompt, Count) when is_integer(Count), Count >= 0 ->
    prompt(Prompt),
    request({get_chars, Count});
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
request(Request) ->
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

wait(Port, Buffer, Ended, Pending) ->
    receive
        {Port, {data, Data}} ->
            serve(Port, <<Buffer/binary, Data/binary>>, Ended, Pending);
        {Port, eof} ->
            serve(Port, Buffer, true, Pending);
        {stdin_request, From, Ref, Request} ->
            serve(Port, Buffer, Ended, Pending ++ [{From, Ref, Request}])
    end.

% Answer the oldest requests the buffer can; the rest wait.
answer([], Buffer, _) ->
    {[], Buffer};
answer([{From, Ref, Request} | Rest] = Pending, Buffer, Ended) ->
    case take(Request, Buffer, Ended) of
        wait ->
            {Pending, Buffer};
        {Reply, NewBuffer} ->
            From ! {stdin_reply, Ref, Reply},
            answer(Rest, NewBuffer, Ended)
    end.

% The reply to one request from the buffer, or wait for more input.
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
format(_) -> erlang:error({not_implemented, format, 1}).
format(_, _) -> erlang:error({not_implemented, format, 2}).
format(_, _, _) -> erlang:error({not_implemented, format, 3}).
fread(_, _) -> erlang:error({not_implemented, fread, 2}).
fread(_, _, _) -> erlang:error({not_implemented, fread, 3}).
fwrite(_) -> erlang:error({not_implemented, fwrite, 1}).
fwrite(_, _) -> erlang:error({not_implemented, fwrite, 2}).
fwrite(_, _, _) -> erlang:error({not_implemented, fwrite, 3}).
get_password() -> erlang:error({not_implemented, get_password, 0}).
get_password(_) -> erlang:error({not_implemented, get_password, 1}).
getopts() -> erlang:error({not_implemented, getopts, 0}).
getopts(_) -> erlang:error({not_implemented, getopts, 1}).
nl() -> erlang:error({not_implemented, nl, 0}).
nl(_) -> erlang:error({not_implemented, nl, 1}).
parse_erl_exprs(_) -> erlang:error({not_implemented, parse_erl_exprs, 1}).
parse_erl_exprs(_, _) -> erlang:error({not_implemented, parse_erl_exprs, 2}).
parse_erl_exprs(_, _, _) -> erlang:error({not_implemented, parse_erl_exprs, 3}).
parse_erl_exprs(_, _, _, _) -> erlang:error({not_implemented, parse_erl_exprs, 4}).
parse_erl_form(_) -> erlang:error({not_implemented, parse_erl_form, 1}).
parse_erl_form(_, _) -> erlang:error({not_implemented, parse_erl_form, 2}).
parse_erl_form(_, _, _) -> erlang:error({not_implemented, parse_erl_form, 3}).
parse_erl_form(_, _, _, _) -> erlang:error({not_implemented, parse_erl_form, 4}).
printable_range() -> erlang:error({not_implemented, printable_range, 0}).
put_chars(_) -> erlang:error({not_implemented, put_chars, 1}).
put_chars(_, _) -> erlang:error({not_implemented, put_chars, 2}).
read(_) -> erlang:error({not_implemented, read, 1}).
read(_, _) -> erlang:error({not_implemented, read, 2}).
read(_, _, _) -> erlang:error({not_implemented, read, 3}).
read(_, _, _, _) -> erlang:error({not_implemented, read, 4}).
request(_, _) -> erlang:error({not_implemented, request, 2}).
requests(_) -> erlang:error({not_implemented, requests, 1}).
requests(_, _) -> erlang:error({not_implemented, requests, 2}).
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
