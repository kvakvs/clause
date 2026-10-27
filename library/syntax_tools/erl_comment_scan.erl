%% Clause's erl_comment_scan (plan 21 LIB77E4b, docs/libraries/syntax_tools.md#erl_comment_scan): the comments of
%% Erlang source text. Scanning and joining lines are runtime builtins (runtime/src/builtins/comments/scan.cpp) with
%% clause_builtin_placeholder bodies (so file/1 calls them qualified); file/1 reads and decodes the file here, reporting
%% failures as OTP does.
-module(erl_comment_scan).
-export([file/1, join_lines/1, scan_lines/1, string/1]).
-compile(nowarn_deprecated_catch).
-include("erl_comment_scan.types.hrl").

file(Name) ->
    Name1 = filename(Name),
    case catch {ok, file:read_file(Name1)} of
        {ok, {ok, Bytes}} -> erl_comment_scan:string(decode(Bytes, Name1));
        {ok, {error, Reason}} -> failed(Name1, fun() -> exit({read, Reason}) end);
        {'EXIT', Reason} -> failed(Name1, fun() -> exit(Reason) end);
        Thrown -> failed(Name1, fun() -> throw(Thrown) end)
    end.

% The text of a file in its declared encoding, else in the default one and then Latin-1.
decode(Bytes, Name) ->
    Declared = epp:read_encoding_from_binary(Bytes),
    Encoding =
        case Declared of
            none -> epp:default_encoding();
            _ -> Declared
        end,
    case catch unicode:characters_to_list(Bytes, Encoding) of
        Text when is_list(Text) ->
            Text;
        Failure when Declared =:= none ->
            case catch unicode:characters_to_list(Bytes, latin1) of
                Text when is_list(Text) -> Text;
                _ -> failed(Name, fun() -> exit(Failure) end)
            end;
        Failure ->
            failed(Name, fun() -> exit(Failure) end)
    end.

failed(Name, Raise) ->
    report_error("error reading file `~ts'.", [Name]),
    Raise().

% A file name as a flat character list; a report and exit(error) for anything else.
filename([C | T]) when is_integer(C), C > 0 -> [C | filename(T)];
filename([]) ->
    [];
filename(Name) ->
    report_error("bad filename: `~tP'.", [Name, 25]),
    exit(error).

report_error(Format, Args) -> error_logger:error_msg(lists:concat([?MODULE, ": ", Format, "\n"]), Args).

% Native join_lines/1.
join_lines(_) -> clause_builtin_placeholder.

% Native scan_lines/1.
scan_lines(_) -> clause_builtin_placeholder.

% Native string/1.
string(_) -> clause_builtin_placeholder.
