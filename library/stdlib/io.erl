-module(io).
-export([
    fread/2, fread/3,
    get_line/1, get_line/2,
    get_chars/2, get_chars/3,
    request/1, request/2,
    requests/1, requests/2
]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/library.md#runtime-module-replacements).
-export([
    columns/0,
    columns/1,
    format/1,
    format/2,
    format/3,
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
get_line(Io, Prompt) ->
    request(Io, {get_line, unicode, Prompt}).

% Write Prompt, then read up to Count characters of standard input; eof at its end.
get_chars(Prompt, Count) -> get_chars(standard_io, Prompt, Count).

get_chars(standard_io, Prompt, Count) when is_integer(Count), Count >= 0 ->
    prompt(Prompt),
    stdin_request({get_chars, Count});
get_chars(Io, Prompt, Count) when is_integer(Count), Count >= 0 ->
    request(Io, {get_chars, unicode, Prompt, Count}).

% Write Prompt, then read the fields of Format (io_lib:fread/2) from the next lines of standard input or an I/O
% server, as OTP's get_until request (plan 21 step LIB4E).
fread(Prompt, Format) -> fread(standard_io, Prompt, Format).

fread(Io, Prompt, Format) -> request(Io, {get_until, unicode, Prompt, io_lib, fread, [Format]}).

prompt(Prompt) when is_atom(Prompt) -> io:put_chars(atom_to_list(Prompt));
prompt(Prompt) -> io:put_chars(Prompt).

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
request(standard_error, {requests, Requests}) ->
    requests(standard_error, Requests);
request(standard_error, Request) ->
    standard_error_request(io_request(Request));
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
% OTP's conversions of the old output and fread requests (plan 21 step LIB54A, for file_sorter's term format).
io_request({write, Term}) -> {put_chars, unicode, io_lib, write, [Term]};
io_request({format, Format, Args}) -> {put_chars, unicode, io_lib, format, [Format, Args]};
io_request({fwrite, Format, Args}) -> {put_chars, unicode, io_lib, fwrite, [Format, Args]};
io_request(nl) -> {put_chars, unicode, io_lib:nl()};
io_request({fread, Prompt, Format}) -> {get_until, unicode, Prompt, io_lib, fread, [Format]};
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

% The server's state: the port, the input buffered so far, whether the port reported eof, the requests waiting, and
% the options of OTP's noshell standard_io (plan 21 step LIB63).
start(Starter) ->
    try register(clause_stdin, self()) of
        true ->
            Starter ! {self(), started},
            serve(#{
                port => open_port({fd, 0, 1}, [binary, eof, in]),
                buffer => <<>>,
                ended => false,
                pending => [],
                binary => false,
                encoding => unicode,
                echo => false,
                log => none
            })
    catch
        error:badarg -> Starter ! {self(), started}
    end.

% Serve requests in order from the input buffered so far.
serve(State) ->
    wait(answer(State)).

% Pending requests are {From, Ref, ReplyTag, Request}; ReplyTag is stdin_reply or io_reply.
wait(#{port := Port, buffer := Buffer, pending := Pending} = State) ->
    receive
        {Port, {data, Data}} ->
            serve(State#{buffer := <<Buffer/binary, Data/binary>>});
        {Port, eof} ->
            serve(State#{ended := true});
        {stdin_request, From, Ref, Request} ->
            serve(State#{pending := Pending ++ [{From, Ref, stdin_reply, Request}]});
        {io_request, From, Ref, Request} ->
            serve(State#{pending := Pending ++ [{From, Ref, io_reply, accept(Request)}]})
    end.

% Answer the oldest requests the buffer can; the rest wait. A get_until request that consumed the buffer but needs
% more input stays first with its new continuation, and gets eof at once when input already ended.
answer(#{pending := []} = State) ->
    State;
answer(#{pending := [{From, Ref, Tag, Request} | Rest] = Pending} = State) ->
    case take(Request, State) of
        wait ->
            State;
        {more, Continued, NewState} ->
            answer(NewState#{pending := [{From, Ref, Tag, Continued} | Rest]});
        {Reply, NewState} ->
            From ! {Tag, Ref, Reply},
            answer(NewState#{pending := Rest})
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
    {get_until, M, F, Args, [], Prompt};
accept({put_chars, _, Chars}) ->
    {put_chars, Chars};
accept({put_chars, _, M, F, A}) ->
    {put_chars, {M, F, A}};
accept(Request) when
    Request =:= getopts;
    element(1, Request) =:= setopts;
    element(1, Request) =:= get_geometry;
    element(1, Request) =:= get_password;
    element(1, Request) =:= put_ansi
->
    Request;
accept(Request) ->
    {unknown, Request}.

% The reply to one request from the buffer and the options, or wait for more input.
take({put_chars, Chars}, State) ->
    {put(Chars), State};
take({unknown, Request}, State) ->
    {{error, {request, Request}}, State};
take(getopts, State) ->
    {stdin_options(State), State};
take({setopts, Options}, State) ->
    set_stdin_options(Options, State);
take({get_geometry, What}, State) ->
    {geometry(stdout, What), State};
% Without a shell OTP's standard_io cannot hide what is typed (noshell).
take({get_password, _}, State) ->
    {{error, enotsup}, State};
take({put_ansi, Options, Ansi}, State) ->
    {put_ansi(Options, Ansi), State};
take({get_until, M, F, Args, Cont, Prompt}, State) ->
    until({M, F, Args, Prompt}, Cont, State);
take(_, #{buffer := <<>>, ended := true} = State) ->
    {eof, State};
take(get_line, #{buffer := Buffer, ended := Ended} = State) ->
    case line_size(Buffer, 0) of
        none when Ended -> split(byte_size(Buffer), State);
        none -> wait;
        Size -> split_line(Size, State)
    end;
take({get_chars, Count}, State) ->
    case chars_size(Count, State) of
        wait -> wait;
        Size -> split(Size, State)
    end.

% The bytes holding the next Count characters of the buffer, all of them once input ended, or wait.
chars_size(Count, #{buffer := Buffer, ended := Ended}) ->
    {Chars, _} = decode(Buffer),
    case length(Chars) >= Count of
        true -> byte_size(unicode:characters_to_binary(lists:sublist(Chars, Count)));
        false when Ended -> byte_size(Buffer);
        false -> wait
    end.

% Input bytes (UTF-8, as OTP reads the terminal) as the reply data: characters, or a binary in the device encoding
% in binary mode.
input(Bytes, #{binary := true, encoding := latin1}) ->
    case unicode:characters_to_binary(element(1, decode(Bytes)), unicode, latin1) of
        Latin1 when is_binary(Latin1) -> Latin1;
        _ -> Bytes
    end;
input(Bytes, #{binary := true}) ->
    Bytes;
input(Bytes, _) ->
    element(1, decode(Bytes)).

% The characters of UTF-8 bytes and an incomplete character at their end; bytes that are no UTF-8 count as latin1.
decode(Bytes) ->
    case unicode:characters_to_list(Bytes) of
        Chars when is_list(Chars) -> {Chars, <<>>};
        {incomplete, Chars, Rest} -> {Chars, Rest};
        {error, Chars, Rest} -> {Chars ++ binary_to_list(Rest), <<>>}
    end.

% Feed the next line of the buffer, or eof once input ended, to M:F; its rest characters go back in front of the
% buffer. A function asking for more input gets the prompt written again before the next line, as OTP's group reads
% one line per prompt.
until({M, F, Args, Prompt}, Cont, #{buffer := Buffer, ended := Ended} = State) ->
    case next_line(Buffer, Ended) of
        wait ->
            wait;
        {Line, After} ->
            {Data, Tail} =
                case Line of
                    <<>> -> {eof, <<>>};
                    _ -> decode(Line)
                end,
            try apply(M, F, [Cont, Data | Args]) of
                {done, Result, Rest} ->
                    Unused = rest_bytes(Rest),
                    echo(binary:part(Line, 0, max(0, byte_size(Line) - byte_size(Unused) - byte_size(Tail))), State),
                    {until_result(Result, State), State#{buffer := <<Unused/binary, Tail/binary, After/binary>>}};
                {more, NewCont} ->
                    echo(binary:part(Line, 0, byte_size(Line) - byte_size(Tail)), State),
                    prompt(Prompt),
                    {more, {get_until, M, F, Args, NewCont, Prompt}, State#{buffer := <<Tail/binary, After/binary>>}}
            catch
                _:_ -> {{error, F}, State#{buffer := <<>>}}
            end
    end.

% The first line of the buffer with its newline (CRLF read as LF) and the bytes after it; the rest once input ended
% (<<>> for eof).
next_line(Buffer, Ended) ->
    case line_size(Buffer, 0) of
        none when Ended ->
            {Buffer, <<>>};
        none ->
            wait;
        Size ->
            {Line, After} = split_binary(Buffer, Size),
            {lf_end(Line), After}
    end.

% A list result in binary mode is a binary, as OTP's group casts it.
until_result(Result, #{binary := true, encoding := latin1}) when is_list(Result) -> list_to_binary(Result);
until_result(Result, #{binary := true}) when is_list(Result) -> unicode:characters_to_binary(Result);
until_result(Result, _) -> Result.

% Bytes of the characters a get_until function left over; eof leaves none.
rest_bytes(Rest) when is_list(Rest); is_binary(Rest) -> unicode:characters_to_binary(Rest);
rest_bytes(_) -> <<>>.

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

% A virtual terminal sequence of io_ansi:fwrite/4, written as standard output's terminal renders it.
put_ansi(Options, Ansi) ->
    Module = io_ansi,
    try Module:format([Ansi], [], [{reset, false} | Options]) of
        Chars -> put(Chars)
    catch
        _:_ -> {error, {put_ansi, unicode, Ansi}}
    end.

split(Size, #{buffer := Buffer} = State) ->
    <<Taken:Size/binary, Rest/binary>> = Buffer,
    echo(Taken, State),
    {input(Taken, State), State#{buffer := Rest}}.

% A line of the buffer, its CRLF end read as LF.
split_line(Size, #{buffer := Buffer} = State) ->
    <<Taken:Size/binary, Rest/binary>> = Buffer,
    echo(Taken, State),
    {input(lf_end(Taken), State), State#{buffer := Rest}}.

% A CRLF line end as LF, as OTP's standard input reads lines (plan 21 step LIB74).
lf_end(Line) ->
    Size = byte_size(Line) - 2,
    case Line of
        <<Text:Size/binary, $\r, $\n>> -> <<Text/binary, $\n>>;
        _ -> Line
    end.

% With option echo, consumed input is written to standard output, as OTP's noshell standard_io does.
echo(Bytes, #{echo := true}) -> io:put_chars(Bytes);
echo(_, _) -> ok.

% The size of the first line of Buffer with its newline, or none.
line_size(Buffer, At) ->
    case Buffer of
        <<_:At/binary, $\n, _/binary>> -> At + 1;
        <<_:At/binary, _, _/binary>> -> line_size(Buffer, At + 1);
        _ -> none
    end.

% --- Options and terminal geometry (plan 21 step LIB63) ---

% OTP's noshell standard_io options, with whether the standard streams are terminals.
stdin_options(#{echo := Echo, log := Log, binary := Binary, encoding := Encoding}) ->
    Stdout = clause_io:isatty(stdout),
    [
        {expand_fun, false},
        {echo, Echo},
        {line_history, false},
        {log, Log},
        {binary, Binary},
        {encoding, Encoding},
        {terminal, Stdout},
        {stdout, Stdout},
        {stderr, clause_io:isatty(stderr)},
        {stdin, clause_io:isatty(stdin)}
    ].

% OTP's checks without a shell: binary, encoding, echo and log; the first occurrence of an option counts.
set_stdin_options(Options0, State) when is_list(Options0) ->
    Options = unfold_options(Options0),
    case lists:all(fun stdin_option/1, Options) of
        true ->
            #{binary := Binary, encoding := Encoding, echo := Echo, log := Log} = State,
            {ok, State#{
                binary := proplists:get_value(binary, Options, Binary),
                encoding := unicode_name(proplists:get_value(encoding, Options, Encoding)),
                echo := proplists:get_value(echo, Options, Echo),
                log := proplists:get_value(log, Options, Log)
            }};
        false ->
            {{error, enotsup}, State}
    end;
set_stdin_options(_, State) ->
    {{error, enotsup}, State}.

stdin_option({binary, Flag}) -> is_boolean(Flag);
stdin_option({encoding, Encoding}) -> lists:member(Encoding, [unicode, utf8, latin1]);
stdin_option({echo, Flag}) -> is_boolean(Flag);
stdin_option({log, Log}) -> lists:member(Log, [none, output, input, all]);
stdin_option(_) -> false.

% binary, list, latin1 and unicode as the options they stand for.
unfold_options(Options) ->
    Expanded = [
        case Option of
            latin1 -> {encoding, latin1};
            unicode -> {encoding, unicode};
            _ -> Option
        end
     || Option <- Options
    ],
    proplists:unfold(proplists:substitute_negations([{list, binary}], Expanded)).

unicode_name(utf8) -> unicode;
unicode_name(Encoding) -> Encoding.

% The columns or rows of a standard stream's terminal window, or {error, enotsup}.
geometry(Stream, What) ->
    case clause_io:window_size(Stream) of
        {Columns, _} when What =:= columns -> Columns;
        {_, Rows} when What =:= rows -> Rows;
        _ -> {error, enotsup}
    end.

% standard_error answers as OTP's standard_error server, with its options in a persistent term.
standard_error_request({put_chars, _, Chars}) ->
    error_output(fun() -> io:put_chars(standard_error, Chars) end);
standard_error_request({put_chars, _, M, F, A}) ->
    error_output(fun() -> io:put_chars(standard_error, apply(M, F, A)) end);
standard_error_request(getopts) ->
    #{encoding := Encoding, onlcr := Onlcr, log := Log} = error_options(),
    [{encoding, Encoding}, {onlcr, Onlcr}, {log, Log}, {terminal, Onlcr}];
standard_error_request({setopts, Options}) when is_list(Options) ->
    Expanded = [error_option(Option) || Option <- Options],
    case lists:all(fun valid_error_option/1, Expanded) of
        true ->
            persistent_term:put({?MODULE, standard_error}, maps:merge(error_options(), maps:from_list(Expanded))),
            ok;
        false ->
            {error, enotsup}
    end;
standard_error_request({get_geometry, What}) ->
    geometry(stderr, What);
standard_error_request(Request) ->
    {error, {request, Request}}.

error_output(Write) ->
    try Write() of
        ok -> ok
    catch
        _:_ -> {error, put_chars}
    end.

error_options() ->
    persistent_term:get({?MODULE, standard_error}, #{
        encoding => unicode, onlcr => clause_io:isatty(stderr), log => none
    }).

error_option(Encoding) when Encoding =:= latin1; Encoding =:= unicode -> {encoding, Encoding};
error_option(utf8) -> {encoding, unicode};
error_option({encoding, utf8}) -> {encoding, unicode};
error_option(Option) -> Option.

valid_error_option({encoding, Encoding}) -> Encoding =:= unicode orelse Encoding =:= latin1;
valid_error_option({onlcr, Flag}) -> is_boolean(Flag);
valid_error_option({log, Log}) -> lists:member(Log, [none, output, input, all]);
valid_error_option(_) -> false.

% --- Erlang terms, tokens and forms (plan 21 step LIB33) ---

% get_until requests scanning with erl_scan:tokens/3,4 from StartLocation (1 by default), parsed as a term, as
% expressions or as a form. Their results carry the location after the form, but read/1,2 drop it.

read(Prompt) -> read(standard_io, Prompt).

read(Io, Prompt) ->
    case request(Io, {get_until, unicode, Prompt, erl_scan, tokens, [1]}) of
        {ok, Tokens, _} -> erl_parse:parse_term(Tokens);
        {error, Error, _} -> {error, Error};
        {eof, _} -> eof;
        Other -> Other
    end.

read(Io, Prompt, Start) -> read(Io, Prompt, Start, []).

read(Io, Prompt, Start, Options) ->
    parsed(
        request(Io, {get_until, unicode, Prompt, erl_scan, tokens, [Start, Options]}),
        fun erl_parse:parse_term/1
    ).

scan_erl_exprs(Prompt) -> scan_erl_exprs(standard_io, Prompt, 1).

scan_erl_exprs(Io, Prompt) -> scan_erl_exprs(Io, Prompt, 1).

scan_erl_exprs(Io, Prompt, Start) ->
    request(Io, {get_until, unicode, Prompt, erl_scan, tokens, [Start]}).

scan_erl_exprs(Io, Prompt, Start, Options) ->
    request(Io, {get_until, unicode, Prompt, erl_scan, tokens, [Start, Options]}).

scan_erl_form(Prompt) -> scan_erl_exprs(standard_io, Prompt, 1).

scan_erl_form(Io, Prompt) -> scan_erl_exprs(Io, Prompt, 1).

scan_erl_form(Io, Prompt, Start) -> scan_erl_exprs(Io, Prompt, Start).

scan_erl_form(Io, Prompt, Start, Options) -> scan_erl_exprs(Io, Prompt, Start, Options).

parse_erl_exprs(Prompt) -> parse_erl_exprs(standard_io, Prompt, 1).

parse_erl_exprs(Io, Prompt) -> parse_erl_exprs(Io, Prompt, 1).

parse_erl_exprs(Io, Prompt, Start) ->
    parsed(scan_erl_exprs(Io, Prompt, Start), fun erl_parse:parse_exprs/1).

parse_erl_exprs(Io, Prompt, Start, Options) ->
    parsed(scan_erl_exprs(Io, Prompt, Start, Options), fun erl_parse:parse_exprs/1).

parse_erl_form(Prompt) -> parse_erl_form(standard_io, Prompt, 1).

parse_erl_form(Io, Prompt) -> parse_erl_form(Io, Prompt, 1).

parse_erl_form(Io, Prompt, Start) ->
    parsed(scan_erl_exprs(Io, Prompt, Start), fun erl_parse:parse_form/1).

parse_erl_form(Io, Prompt, Start, Options) ->
    parsed(scan_erl_exprs(Io, Prompt, Start, Options), fun erl_parse:parse_form/1).

% The parse of scanned tokens with the location after them; scan errors, eof and failed requests as they are.
parsed({ok, Tokens, End}, Parse) ->
    case Parse(Tokens) of
        {ok, Parsed} -> {ok, Parsed, End};
        {error, Error} -> {error, Error, End}
    end;
parsed(Other, _Parse) ->
    Other.

format(_) -> clause_builtin_placeholder.
format(_, _) -> clause_builtin_placeholder.
format(_, _, _) -> clause_builtin_placeholder.
fwrite(_) -> clause_builtin_placeholder.
fwrite(_, _) -> clause_builtin_placeholder.
fwrite(_, _, _) -> clause_builtin_placeholder.
nl() -> clause_builtin_placeholder.
nl(_) -> clause_builtin_placeholder.
%% Clause has no +pc flag: printable characters are Latin-1 (plan 21 step LIB4C).
printable_range() -> latin1.
put_chars(_) -> clause_builtin_placeholder.
put_chars(_, _) -> clause_builtin_placeholder.

%% The I/O options and terminal size of a device (plan 21 step LIB63): standard_io and user are the standard input
%% server, standard_error answers here, others are asked by the I/O protocol.
getopts() -> getopts(standard_io).

getopts(Io) -> request(Io, getopts).

setopts(Options) -> setopts(standard_io, Options).

setopts(Io, Options) -> request(Io, {setopts, Options}).

columns() -> columns(standard_io).

columns(Io) -> positive(request(Io, {get_geometry, columns})).

rows() -> rows(standard_io).

rows(Io) -> positive(request(Io, {get_geometry, rows})).

positive(Size) when is_integer(Size), Size > 0 -> {ok, Size};
positive(_) -> {error, enotsup}.

%% Without a shell, OTP's standard_io cannot read a password: {error, enotsup}.
get_password() -> get_password(user).

get_password(Io) -> request(Io, {get_password, unicode}).

%% Term as io_lib:write/1 writes it (plan 21 step LIB63).
write(Term) -> write(standard_io, Term).

write(Io, Term) -> io:put_chars(Io, io_lib:write(Term)).
