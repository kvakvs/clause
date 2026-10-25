%% Clause's replacement for OTP's edlin (docs/library.md#interactive-shell), generated from edlin.terms by
%% tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). Only keymap/0 is documented.
-module(edlin).
-export([
    current_chars/1,
    current_line/1,
    edit_line/2,
    edit_line1/2,
    erase_inp/1,
    erase_line/0,
    init/0,
    init/1,
    keymap/0,
    length_after/1,
    length_before/1,
    over_word/3,
    prompt/1,
    redraw_line/1,
    start/1,
    start/2
]).

current_chars(_) -> erlang:error({not_implemented, current_chars, 1}).
current_line(_) -> erlang:error({not_implemented, current_line, 1}).
edit_line(_, _) -> erlang:error({not_implemented, edit_line, 2}).
edit_line1(_, _) -> erlang:error({not_implemented, edit_line1, 2}).
erase_inp(_) -> erlang:error({not_implemented, erase_inp, 1}).
erase_line() -> erlang:error({not_implemented, erase_line, 0}).
init() -> erlang:error({not_implemented, init, 0}).
init(_) -> erlang:error({not_implemented, init, 1}).
%% The keymap of the calling line editor process (plan 21 step LIB74): undefined outside one, as in a shell's
%% evaluator. Clause's shell reads lines as the terminal delivers them, so no process edits lines with a keymap.
keymap() -> get(key_map).
length_after(_) -> erlang:error({not_implemented, length_after, 1}).
length_before(_) -> erlang:error({not_implemented, length_before, 1}).
over_word(_, _, _) -> erlang:error({not_implemented, over_word, 3}).
prompt(_) -> erlang:error({not_implemented, prompt, 1}).
redraw_line(_) -> erlang:error({not_implemented, redraw_line, 1}).
start(_) -> erlang:error({not_implemented, start, 1}).
start(_, _) -> erlang:error({not_implemented, start, 2}).
