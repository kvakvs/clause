#!/usr/bin/env escript
%% Observes the host OTP's unicode_util for every code point (plan step 41O1, docs/builtins.md#unicode-util) and
%% writes one line per code point whose answers differ from the defaults, for tools/unicode/generate.py:
%%   CP ccc=N canon=CP:CCC,... compat=Tag:CP:CCC,... cat=Group/Sub case=U;L;T;F flags=wide,ws,ids,idc,nlps
%% A case value is CP or CP+CP+... for a list. Usage: escript observe.escript OUT
-mode(compile).

main([Out]) ->
    {ok, Fd} = file:open(Out, [write, raw, {delayed_write, 1 bsl 20, 1000}]),
    {Major, Minor} = unicode_util:spec_version(),
    ok = file:write(Fd, io_lib:format("spec ~w.~w~n", [Major, Minor])),
    ok = file:write(Fd, io_lib:format("pattern_whitespace ~w~n", [unicode_util:pattern_whitespace()])),
    observe(Fd, 0),
    ok = file:close(Fd).

observe(_, 16#110000) ->
    ok;
observe(Fd, CP) ->
    case fields(CP) of
        [] -> ok;
        Fields -> ok = file:write(Fd, [integer_to_list(CP), [[$\s | F] || F <- Fields], $\n])
    end,
    observe(Fd, CP + 1).

%% The answers of CP that differ from an unassigned code point's.
fields(CP) ->
    #{ccc := CCC, canon := Canon, compat := Compat, category := {Group, Sub}} = unicode_util:lookup(CP),
    Case = unicode_util:get_case(CP),
    Flags = [
        Name
     || {Name, true} <- [
            {"wide", unicode_util:is_wide(CP)},
            {"ws", unicode_util:is_whitespace(CP)},
            {"ids", unicode_util:is_other_id_start(CP)},
            {"idc", unicode_util:is_other_id_continue(CP)},
            {"nlps", not unicode_util:is_letter_not_pattern_syntax(CP)}
        ]
    ],
    lists:append([
        [io_lib:format("ccc=~w", [CCC]) || CCC =/= 0],
        ["canon=" ++ pairs(Canon) || Canon =/= []],
        [compat(Compat) || Compat =/= []],
        [io_lib:format("cat=~w/~w", [Group, Sub]) || {Group, Sub} =/= {other, not_assigned}],
        [cases(Case) || Case =/= #{upper => CP, lower => CP, title => CP, fold => CP}],
        ["flags=" ++ lists:join($,, Flags) || Flags =/= []]
    ]).

pairs(Pairs) -> lists:join($,, [io_lib:format("~w:~w", [P, C]) || {C, P} <- Pairs]).

compat({Tag, Pairs}) -> io_lib:format("compat=~w:", [Tag]) ++ pairs(Pairs).

cases(#{upper := U, lower := L, title := T, fold := F}) ->
    "case=" ++ lists:join($;, [value(V) || V <- [U, L, T, F]]).

value(V) when is_integer(V) -> integer_to_list(V);
value(Vs) -> lists:join($+, [integer_to_list(V) || V <- Vs]).
