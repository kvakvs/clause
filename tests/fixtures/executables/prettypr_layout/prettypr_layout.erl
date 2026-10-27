%% prettypr (plan 21 LIB77E3a): texts, nests, besides, aboves, seps and pars, follows, floats, breaks and empty
%% documents laid out at several paper and ribbon widths; best/3's layout documents printed whole (OTP's records and
%% joined strings), long paragraphs, tabs, null texts and the errors of malformed documents.
-module(prettypr_layout).
-export([main/1]).

-import(prettypr, [above/2, beside/2, break/1, empty/0, floating/1, floating/3, follow/2, follow/3, nest/2]).
-import(prettypr, [null_text/1, par/1, par/2, sep/1, text/1, text_par/1, text_par/2]).

main(_) ->
    basics(),
    seps(),
    paragraphs(),
    code(),
    floats(),
    layouts(),
    errors(),
    io:format("prettypr_layout: ok~n").

basics() ->
    show(text, prettypr:format(text("hello"))),
    show(beside, prettypr:format(beside(text("ab"), text("cd")))),
    show(above, prettypr:format(above(text("ab"), text("cd")))),
    show(nest, prettypr:format(above(text("head"), nest(4, above(text("one"), text("two")))))),
    show(nest_beside, prettypr:format(beside(text("key: "), above(text("first"), nest(2, text("second")))))),
    show(negative_nest, prettypr:format(nest(4, above(text("a"), nest(-2, text("b")))))),
    show(break, prettypr:format(beside(break(text("x")), text("y")))),
    show(empty, prettypr:format(empty())),
    show(empty_beside, prettypr:format(beside(empty(), text("z")))),
    show(empty_above, prettypr:format(above(text("a"), above(empty(), text("b"))))),
    show(tabs, prettypr:format(beside(text("a\tb"), above(text("c"), text("d"))))),
    show(null_text, prettypr:format(par([null_text("[[["), text("x"), text("y")], 2), 5)),
    show(deep_text, prettypr:format(text(["ab", [$c, "de"], []]))),
    show(empty_text, prettypr:format(above(text(""), above(nest(3, text("")), text("end"))))).

seps() ->
    Words = [text(W) || W <- ["alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta"]],
    [show({sep, W}, prettypr:format(sep(Words), W)) || W <- [80, 30, 10]],
    [show({sep_nested, W}, prettypr:format(beside(text("f("), beside(sep(Words), text(")"))), W)) || W <- [80, 25]],
    [show({par, W}, prettypr:format(par(Words), W)) || W <- [80, 30, 12]],
    [show({par_indent, W}, prettypr:format(par(Words, 4), W)) || W <- [30, 12]],
    show(sep_one, prettypr:format(sep([text("only")]))),
    show(sep_with_null, prettypr:format(sep([text("a"), empty(), text("b")]), 5)),
    show(par_with_null, prettypr:format(par([text("a"), empty(), text("b"), text("c")]), 5)),
    show(follow, prettypr:format(follow(text("label:"), sep(Words)), 30)),
    show(follow_indent, prettypr:format(follow(text("label:"), sep(Words), 6), 30)),
    show(sep_of_aboves, prettypr:format(sep([above(text("x"), text("y")), text("z")]), 40)),
    show(par_of_aboves, prettypr:format(par([text("p"), above(text("x"), text("y")), text("z")]), 40)),
    show(nested_seps, prettypr:format(sep([sep([text("aa"), text("bb")]), sep([text("cc"), text("dd")])]), 6)).

paragraphs() ->
    Lorem =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore\n"
        "et dolore magna aliqua.\tUt enim ad minim veniam,  quis nostrud exercitation ullamco laboris.",
    [show({text_par, W}, prettypr:format(text_par(Lorem), W)) || W <- [80, 40, 20]],
    show(text_par_indent, prettypr:format(text_par(Lorem, 4), 30)),
    show(text_par_hanging, prettypr:format(text_par(Lorem, -4), 30)),
    show(ribbon, prettypr:format(nest(20, text_par(Lorem)), 80, 20)),
    Long = lists:append(lists:duplicate(300, "word another longerword x ")),
    Text = prettypr:format(text_par(Long), 60),
    Lines = string:split(Text, "\n", all),
    show(long_paragraph, {length(Text), length(Lines), hd(Lines), lists:last(Lines)}).

%% A function-shaped document, as erl_prettypr builds them.
code() ->
    Clause = fun(Head, Body) -> par([beside(text(Head), text(" ->")), nest(4, sep(Body))], 4) end,
    Body = [beside(text(S), text(",")) || S <- ["X = lists:map(fun f/1, Xs)", "Y = g(X)"]] ++ [text("{X, Y}")],
    Function = above(Clause("f(Xs)", Body), Clause("f(_)", [text("error")])),
    [show({code, W}, prettypr:format(Function, W)) || W <- [100, 40, 20]],
    Call = fun(Name, Args) -> beside(text(Name ++ "("), beside(par(Args), text(")"))) end,
    Nested = Call("outer", [Call("inner", [text("argument_one"), text("argument_two")]), text("last")]),
    [show({call, W}, prettypr:format(Nested, W)) || W <- [80, 30, 15]].

floats() ->
    A = floating(text("a"), 1, 0),
    B = floating(text("b"), 0, 0),
    C = floating(text("c"), 0, 1),
    D = floating(text("d"), 1, 1),
    show(float_plain, prettypr:format(beside(floating(text("x")), text("y")))),
    show(float_left, prettypr:format(beside(A, B))),
    show(float_right, prettypr:format(beside(B, A))),
    show(float_vertical, prettypr:format(beside(B, C))),
    show(float_above, prettypr:format(above(C, B))),
    show(float_above_swap, prettypr:format(above(B, C))),
    show(float_above_same, prettypr:format(above(A, B))),
    show(float_three, prettypr:format(beside(A, beside(B, text("rest"))))),
    show(float_three_up, prettypr:format(above(C, beside(B, text("rest"))))),
    show(float_three_above, prettypr:format(beside(D, above(B, nest(2, text("rest")))))),
    show(float_mixed, prettypr:format(above(beside(A, D), above(C, nest(3, beside(B, text("tail"))))))),
    show(float_in_sep, prettypr:format(sep([floating(text("s")), text("t")]), 10)),
    show(float_beside_sep, prettypr:format(beside(A, sep([text("u"), text("v")])))),
    show(float_above_sep, prettypr:format(above(C, sep([text("u"), text("v")])))),
    show(float_null, prettypr:format(beside(A, beside(empty(), text("w"))))),
    show(float_above_null, prettypr:format(above(C, above(empty(), text("w"))))).

layouts() ->
    Words = [text(W) || W <- ["one", "two", "three"]],
    show(best_text, prettypr:best(text("x"), 80, 65)),
    show(best_nested, prettypr:best(nest(2, beside(text("ab"), text("cd"))), 80, 65)),
    show(best_lines, prettypr:best(above(text("a"), nest(3, above(text("b"), text("c")))), 80, 65)),
    show(best_sep, prettypr:best(sep(Words), 8, 65)),
    show(best_par, prettypr:best(beside(text("k: "), par(Words)), 9, 65)),
    show(best_empty, prettypr:best(empty(), 80, 65)),
    show(best_joined, prettypr:best(beside(text("a"), beside(text(""), beside(text("b"), text("c")))), 80, 65)),
    show(best_narrow, prettypr:best(sep(Words), 1, 1)),
    show(best_ribbon, prettypr:best(par(Words), 80, 4)).

errors() ->
    show(bad_document, failure(fun() -> prettypr:format(not_a_document) end)),
    show(bad_text, failure(fun() -> prettypr:text(atom) end)),
    show(bad_nested, failure(fun() -> prettypr:format(beside(text("a"), {beside, x})) end)).

failure(Fun) ->
    try Fun() of
        Value -> {returned, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Tag, Text) when is_list(Text) -> io:format("~p:~n~ts~n", [Tag, Text]);
show(Tag, Value) -> io:format("~p: ~p~n", [Tag, Value]).
