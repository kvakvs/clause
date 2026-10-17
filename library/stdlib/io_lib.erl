% Clause's io_lib (plan 21 step LIB4C, docs/builtins.md#io-lib): the writers, checks, collect functions and
% format engine are runtime builtins with clause_builtin_placeholder bodies, building OTP's deep lists; get_until
% (which applies a callback) and format_prompt are Erlang; print/1,4, write/3 and fread/2,3 are stubs raising
% error({not_implemented, F, A}) (docs/library.md#runtime-module-replacements).
-module(io_lib).
-export([
    bformat/2,
    bformat/3,
    bfwrite/2,
    bfwrite/3,
    build_text/1,
    build_text/2,
    bwrite/2,
    bwrite_string/3,
    char_list/1,
    chars_length/1,
    collect_chars/1,
    collect_chars/3,
    collect_chars/4,
    collect_chars_eager/4,
    collect_line/1,
    collect_line/3,
    collect_line/4,
    collect_line_no_eol/4,
    deep_char_list/1,
    deep_latin1_char_list/1,
    deep_unicode_char_list/1,
    format/2,
    format/3,
    format_prompt/1,
    format_prompt/2,
    fread/2,
    fread/3,
    fwrite/2,
    fwrite/3,
    get_until/1,
    get_until/3,
    get_until/4,
    indentation/2,
    latin1_char_list/1,
    limit_term/2,
    nl/0,
    print/1,
    print/4,
    printable_latin1_list/1,
    printable_list/1,
    printable_unicode_list/1,
    quote_atom/2,
    scan_format/2,
    unscan_format/1,
    write/1,
    write/2,
    write/3,
    write/5,
    write_atom/1,
    write_atom_as_latin1/1,
    write_bin/5,
    write_binary/3,
    write_binary_bin/4,
    write_char/1,
    write_char_as_latin1/1,
    write_latin1_char/1,
    write_latin1_string/1,
    write_latin1_string/2,
    write_string/1,
    write_string/2,
    write_string_as_latin1/1,
    write_string_as_latin1/2,
    write_string_bin/3,
    write_unicode_char/1,
    write_unicode_string/1
]).

-include("io_lib.types.hrl").

bformat(_, _) -> clause_builtin_placeholder.
bformat(_, _, _) -> clause_builtin_placeholder.
bfwrite(_, _) -> clause_builtin_placeholder.
bfwrite(_, _, _) -> clause_builtin_placeholder.
build_text(_) -> clause_builtin_placeholder.
build_text(_, _) -> clause_builtin_placeholder.
bwrite(_, _) -> clause_builtin_placeholder.
bwrite_string(_, _, _) -> clause_builtin_placeholder.
char_list(_) -> clause_builtin_placeholder.
chars_length(_) -> clause_builtin_placeholder.
collect_chars(_) -> clause_builtin_placeholder.
collect_chars(_, _, _) -> clause_builtin_placeholder.
collect_chars(_, _, _, _) -> clause_builtin_placeholder.
collect_chars_eager(_, _, _, _) -> clause_builtin_placeholder.
collect_line(_) -> clause_builtin_placeholder.
collect_line(_, _, _) -> clause_builtin_placeholder.
collect_line(_, _, _, _) -> clause_builtin_placeholder.
collect_line_no_eol(_, _, _, _) -> clause_builtin_placeholder.
deep_char_list(_) -> clause_builtin_placeholder.
deep_latin1_char_list(_) -> clause_builtin_placeholder.
deep_unicode_char_list(_) -> clause_builtin_placeholder.
format(_, _) -> clause_builtin_placeholder.
format(_, _, _) -> clause_builtin_placeholder.
fread(_, _) -> erlang:error({not_implemented, fread, 2}).
fread(_, _, _) -> erlang:error({not_implemented, fread, 3}).
fwrite(_, _) -> clause_builtin_placeholder.
fwrite(_, _, _) -> clause_builtin_placeholder.
indentation(_, _) -> clause_builtin_placeholder.
latin1_char_list(_) -> clause_builtin_placeholder.
limit_term(_, _) -> clause_builtin_placeholder.
nl() -> clause_builtin_placeholder.
print(_) -> erlang:error({not_implemented, print, 1}).
print(_, _, _, _) -> erlang:error({not_implemented, print, 4}).
printable_latin1_list(_) -> clause_builtin_placeholder.
printable_list(_) -> clause_builtin_placeholder.
printable_unicode_list(_) -> clause_builtin_placeholder.
quote_atom(_, _) -> clause_builtin_placeholder.
scan_format(_, _) -> clause_builtin_placeholder.
unscan_format(_) -> clause_builtin_placeholder.
write(_) -> clause_builtin_placeholder.
write(_, _) -> clause_builtin_placeholder.
write(_, _, _, _, _) -> clause_builtin_placeholder.
write_atom(_) -> clause_builtin_placeholder.
write_atom_as_latin1(_) -> clause_builtin_placeholder.
write_bin(_, _, _, _, _) -> clause_builtin_placeholder.
write_binary(_, _, _) -> clause_builtin_placeholder.
write_binary_bin(_, _, _, _) -> clause_builtin_placeholder.
write_char(_) -> clause_builtin_placeholder.
write_char_as_latin1(_) -> clause_builtin_placeholder.
write_latin1_char(_) -> clause_builtin_placeholder.
write_latin1_string(_) -> clause_builtin_placeholder.
write_latin1_string(_, _) -> clause_builtin_placeholder.
write_string(_) -> clause_builtin_placeholder.
write_string(_, _) -> clause_builtin_placeholder.
write_string_as_latin1(_) -> clause_builtin_placeholder.
write_string_as_latin1(_, _) -> clause_builtin_placeholder.
write_string_bin(_, _, _) -> clause_builtin_placeholder.
write_unicode_char(_) -> clause_builtin_placeholder.
write_unicode_string(_) -> clause_builtin_placeholder.

%% write/3: write/2, or print/4 on one line of 80 when the flag is true.
write(Term, Depth, false) -> io_lib:write(Term, Depth);
write(Term, Depth, true) -> io_lib:print(Term, 1, 80, Depth).

%% format_prompt/1,2: the prompt text of an I/O request, "???" when it cannot be formatted.
format_prompt(Prompt) -> format_prompt(Prompt, latin1).

format_prompt({format, Format, Args}, _Encoding) ->
    prompt_text(Format, Args);
format_prompt(Prompt, Encoding) when is_list(Prompt); is_atom(Prompt); is_binary(Prompt) ->
    prompt_text(modified(Encoding, $s), [Prompt]);
format_prompt(Prompt, Encoding) ->
    prompt_text(modified(Encoding, $p), [Prompt]).

prompt_text(Format, Args) ->
    try
        io_lib:format(Format, Args)
    catch
        _:_ -> "???"
    end.

%% ~s or ~p, with t unless the encoding is latin1.
modified(latin1, Control) -> [$~, Control];
modified(_, Control) -> [$~, $t, Control].

%% get_until/1,3,4: a get_until request's state for the I/O protocol. The first call notes whether the data is a
%% binary; Mod:Func(Cont, Chars | Extra) answers {done, Result, Rest}, cast to a binary for binary data, or
%% {more, Cont}.
get_until(_State) -> 0.

get_until(State, Data, MFA) -> get_until(State, Data, latin1, MFA).

get_until(start, Data, Encoding, MFA) ->
    get_until({is_binary(Data), []}, Data, Encoding, MFA);
get_until({Binary, Cont}, Data, Encoding, {Mod, Func, Extra}) ->
    case apply(Mod, Func, [Cont, as_chars(Data, Encoding) | Extra]) of
        {done, Result, Rest} -> {stop, as_result(Binary, Result, Encoding), Rest};
        {more, Next} -> {Binary, Next}
    end.

as_chars(Data, unicode) when is_binary(Data) -> unicode:characters_to_list(Data, utf8);
as_chars(Data, _) when is_binary(Data) -> binary_to_list(Data);
as_chars(Data, _) -> Data.

as_result(true, Result, unicode) when is_list(Result) ->
    unicode:characters_to_binary(Result, unicode, unicode);
as_result(true, Result, _) when is_list(Result) -> iolist_to_binary(Result);
as_result(_, Result, _) ->
    Result.
