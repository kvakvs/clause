%% Clause's prettypr (plan 21 step LIB77E3a, docs/libraries/syntax_tools.md#prettypr): John Hughes' pretty printer
%% with the paragraph, empty, floating and null-text additions of OTP's prettypr. Documents are OTP's records. The
%% constructors are plain record building here; best/3, the rewriting that chooses a layout, and the layout to text are
%% runtime builtins (runtime/src/builtins/prettypr.cpp, clause_prettypr), as are text widths and text_par/2's words.
-module(prettypr).
-export([
    above/2,
    beside/2,
    best/3,
    break/1,
    empty/0,
    floating/1,
    floating/3,
    follow/2,
    follow/3,
    format/1,
    format/2,
    format/3,
    nest/2,
    null_text/1,
    par/1,
    par/2,
    sep/1,
    text/1,
    text_par/1,
    text_par/2
]).
-include("prettypr.types.hrl").

%% Texts keep their width in front of the characters: [Width | Characters].
text(S) -> #text{s = [clause_prettypr:width(S) | S]}.

null_text(S) -> #text{s = [0 | S]}.

text_par(S) -> text_par(S, 0).

text_par(S, 0) -> par(words(S));
text_par(S, N) when N > 0 -> nest(N, par(words(S), -N));
text_par(S, N) when N < 0 -> par(words(S), -N).

words(S) -> [text(Word) || Word <- clause_prettypr:words(S)].

empty() -> null.

break(D) -> above(D, empty()).

nest(0, D) -> D;
nest(N, D) -> #nest{n = N, d = D}.

beside(D1, D2) -> #beside{d1 = D1, d2 = D2}.

above(D1, D2) -> #above{d1 = D1, d2 = D2}.

sep(Ds) -> #sep{ds = Ds}.

par(Ds) -> par(Ds, 0).

par(Ds, N) when is_integer(N) -> #sep{ds = Ds, i = N, p = true}.

follow(D1, D2) -> follow(D1, D2, 0).

follow(D1, D2, N) when is_integer(N) -> beside(par([D1, text("")], N), D2).

floating(D) -> floating(D, 0, 0).

floating(D, H, V) when is_integer(H), is_integer(V) -> #float{d = D, h = H, v = V}.

format(D) -> format(D, 80).

format(D, W) -> format(D, W, 65).

%% The text of the best layout within paper width W and ribbon width R; no_layout is thrown when none exists.
format(D, W, R) ->
    case prettypr:best(D, W, R) of
        empty -> throw(no_layout);
        L -> clause_prettypr:layout(L)
    end.

%% best(D, W, R): the layout document (texts, nests, aboves and null) of the best layout, or empty.
best(_, _, _) -> clause_builtin_placeholder.
