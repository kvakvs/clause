-module(clause_io).
-export([isatty/1, window_size/1, put_chars/2, leader/0, start_user/0]).

% Clause's terminal queries (plan 21 step LIB63, docs/builtins.md#io-module): runtime builtins in
% runtime/src/builtins/terminal.cpp, which replace these bodies at registration. io's getopts/0,1, columns/0,1,
% rows/0,1 and io_ansi:enabled/0,1 answer over them. A stream is stdin, stdout or stderr.

%% Characters of io:format/3, io:put_chars/2 and io:nl/1 for an I/O server, as an I/O protocol request: the server
%% writes them in its own encoding (plan 21 step LIB63).
put_chars(Server, Utf8) -> io:request(Server, {put_chars, unicode, Utf8}).

isatty(_) -> clause_builtin_placeholder.
window_size(_) -> clause_builtin_placeholder.

%% Where the caller's standard_io requests go: user while its group leader is this node's standard I/O, else the
%% leader's pid (plan 30 step DIST7c); a builtin in runtime/src/builtins/io.cpp.
leader() -> clause_builtin_placeholder.

%% This node's standard I/O as an I/O server registered as user, as OTP's user, started with distribution so that
%% processes of other nodes led from here have a group leader to answer them (plan 30 step DIST7c).
start_user() ->
    case whereis(user) of
        undefined ->
            Pid = spawn(fun serve_user/0),
            try
                register(user, Pid),
                ok
            catch
                error:badarg ->
                    exit(Pid, kill),
                    start_user()
            end;
        _ ->
            ok
    end.

%% Output is written at once in the order it comes; other requests wait for standard input in processes of their own.
serve_user() ->
    receive
        {io_request, From, ReplyAs, Request} when element(1, Request) =:= put_chars ->
            From ! {io_reply, ReplyAs, io:request(user, Request)};
        {io_request, From, ReplyAs, Request} ->
            spawn(fun() -> From ! {io_reply, ReplyAs, io:request(user, Request)} end);
        _ ->
            ok
    end,
    serve_user().
