-module(clause_io).
-export([isatty/1, window_size/1, put_chars/2]).

% Clause's terminal queries (plan 21 step LIB63, docs/builtins.md#io-module): runtime builtins in
% runtime/src/builtins/terminal.cpp, which replace these bodies at registration. io's getopts/0,1, columns/0,1,
% rows/0,1 and io_ansi:enabled/0,1 answer over them. A stream is stdin, stdout or stderr.

%% Characters of io:format/3, io:put_chars/2 and io:nl/1 for an I/O server, as an I/O protocol request: the server
%% writes them in its own encoding (plan 21 step LIB63).
put_chars(Server, Utf8) -> io:request(Server, {put_chars, unicode, Utf8}).

isatty(_) -> clause_builtin_placeholder.
window_size(_) -> clause_builtin_placeholder.
