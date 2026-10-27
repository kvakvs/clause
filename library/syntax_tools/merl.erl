%% Clause's merl (plan 21 LIB77E5, docs/libraries/syntax_tools.md#merl): metaprogramming with syntax trees. Quoting
%% text (scanning and the parse retries), printing, switch/2 (its actions are funs) and compiling are Erlang here;
%% templates, substitution, renaming and matching are runtime builtins (runtime/src/builtins/merl/) with
%% clause_builtin_placeholder bodies, so the Erlang functions call them qualified.
-module(merl).
-export([
    alpha/2,
    compile/1,
    compile/2,
    compile_and_load/1,
    compile_and_load/2,
    match/2,
    meta_template/1,
    print/1,
    qquote/2,
    qquote/3,
    quote/1,
    quote/2,
    show/1,
    subst/2,
    switch/2,
    template/1,
    template_vars/1,
    term/1,
    tree/1,
    tsubst/2,
    var/1
]).

%% Compiling.

compile(Code) -> compile(Code, []).

compile(Code, Options) when not is_list(Code) ->
    case kind(Code) of
        form_list -> compile(erl_syntax:form_list_elements(Code));
        _ -> compile([Code], Options)
    end;
compile(Code, Options) when is_list(Options) ->
    compile:noenv_forms(erl_syntax:revert_forms(Code), [verbose, report_errors, report_warnings, binary | Options]).

compile_and_load(Code) -> compile_and_load(Code, []).

compile_and_load(Code, Options) ->
    case compile(Code, Options) of
        {ok, Module, Binary} ->
            _ = code:load_binary(Module, "", Binary),
            {ok, Binary};
        Other ->
            Other
    end.

%% Small constructors.

var(Name) -> erl_syntax:variable(Name).

term(Term) -> erl_syntax:abstract(Term).

%% Printing.

print(Trees) when is_list(Trees) -> lists:foreach(fun print/1, Trees);
print(Tree) ->
    io:put_chars(erl_prettypr:format(merl:tree(Tree))),
    io:nl().

show(Trees) when is_list(Trees) -> lists:foreach(fun show/1, Trees);
show(Tree) ->
    io:put_chars(outline(merl:tree(Tree), 0)),
    io:nl().

% A node's type and limited text on one line at indentation Indent, then its subtree groups separated by "+".
outline(Tree, Indent) ->
    Text = lists:flatten([atom_to_list(kind(Tree)), ": ", erl_prettypr:format(erl_syntax_lib:limit(Tree, 3))]),
    Groups = [Group || Group <- groups(Tree), Group =/= []],
    [lists:duplicate(Indent, $\s), one_line(Text, 79 - Indent), $\n, outline_groups(Groups, Indent + 2)].

outline_groups([Group], Indent) ->
    [outline(T, Indent) || T <- Group];
outline_groups([Group | Groups], Indent) ->
    [[outline(T, Indent) || T <- Group], lists:duplicate(Indent, $\s), "+\n" | outline_groups(Groups, Indent)];
outline_groups([], _) ->
    [].

% Text on one line within Width columns: white space runs as one space, control characters dropped, and dots
% for what is cut.
one_line([C | Cs], N) when C =:= $\n; C =:= $\r; C =:= $\v; C =:= $\t -> one_line([$\s | Cs], N);
one_line([$\s, $\s | Cs], N) -> one_line([$\s | Cs], N);
one_line([C | Cs], N) when C < 32 -> one_line(Cs, N);
one_line([C | Cs], N) when N > 3 -> [C | one_line(Cs, N - 1)];
one_line([_, _, _, _ | _], 3) -> "...";
one_line(Cs, 3) -> Cs;
one_line([_, _, _ | _], 2) -> "..";
one_line(Cs, 2) -> Cs;
one_line([_, _ | _], 1) -> ".";
one_line(Cs, 1) -> Cs;
one_line(_, _) -> [].

%% Quoting.

qquote(Text, Env) -> qquote(1, Text, Env).

qquote(StartPos, Text, Env) -> merl:subst(quote(StartPos, Text), Env).

quote(Text) -> quote(1, Text).

quote({Line, Column}, Text) when is_integer(Line), is_integer(Column) -> quote_at(Line, {Line, Column}, Text);
quote(Line, Text) when is_integer(Line) -> quote_at(Line, Line, Text).

% The trees of a text scanned from Location, with its comments attached by line.
quote_at(Line, Location, Text) ->
    Flat = flat_text(Text),
    {ok, Tokens, _} = erl_scan:string(Flat, Location),
    with_comments(Line, erl_comment_scan:string(Flat), parse(Tokens)).

flat_text([Line | _] = Lines) when is_list(Line) -> lists:foldr(fun(S, T) -> S ++ [$\n | T] end, "", Lines);
flat_text([Line | _] = Lines) when is_binary(Line) ->
    lists:foldr(fun(S, T) -> binary_to_list(S) ++ [$\n | T] end, "", Lines);
flat_text(Text) when is_binary(Text) -> binary_to_list(Text);
flat_text(Text) ->
    Text.

% Forms when the text has dots; else expressions, try handlers, fun clauses or case clauses, the first that parses.
parse(Tokens) ->
    case forms(Tokens, [], []) of
        {ok, Forms} -> [parsed(erl_parse:parse_form(Form)) || Form <- Forms];
        error -> parse_exprs(Tokens)
    end.

forms([{dot, _} = Dot | Tokens], Forms, Form) -> forms(Tokens, [lists:reverse(Form, [Dot]) | Forms], []);
forms([Token | Tokens], Forms, Form) -> forms(Tokens, Forms, [Token | Form]);
forms([], Forms, []) -> {ok, lists:reverse(Forms)};
forms([], [], _) -> error;
forms([], _, [Token | _]) -> fail("incomplete form after ~p", [Token]).

parsed({ok, Form}) -> Form;
parsed({error, Reason}) -> parse_error(Reason).

parse_exprs(Tokens) ->
    A = erl_anno:new(0),
    case erl_parse:parse_exprs(Tokens ++ [{dot, A}]) of
        {ok, Exprs} -> Exprs;
        {error, E} -> parse_clauses(Tokens ++ [{'end', A}, {dot, A}], [E])
    end.

parse_clauses(Tokens, Errors) ->
    A = erl_anno:new(0),
    case erl_parse:parse_exprs([{'try', A}, {atom, A, true}, {'catch', A} | Tokens]) of
        {ok, [{'try', _, _, _, _, _} = Try]} ->
            erl_syntax:try_expr_handlers(Try);
        {error, E1} ->
            case erl_parse:parse_exprs([{'fun', A} | Tokens]) of
                {ok, [{'fun', _, {clauses, Clauses}}]} ->
                    Clauses;
                {error, E2} ->
                    case erl_parse:parse_exprs([{'case', A}, {atom, A, true}, {'of', A} | Tokens]) of
                        {ok, [{'case', _, _, Clauses}]} -> Clauses;
                        {error, E3} -> parse_error(lists:last(lists:sort([E3, E2, E1 | Errors])))
                    end
            end
    end.

parse_error({L, M, R}) when is_atom(M), is_integer(L) -> fail("~w: ~ts", [L, M:format_error(R)]);
parse_error({{L, C}, M, R}) when is_atom(M), is_integer(L), is_integer(C) ->
    fail("~w:~w: ~ts", [L, C, M:format_error(R)]);
parse_error({_, M, R}) when is_atom(M) -> fail(M:format_error(R), []);
parse_error(R) ->
    fail("unknown parse error: ~tp", [R]).

fail(Format, Args) -> throw({error, lists:flatten(io_lib:format(Format, Args))}).

% The comments of a quoted text as post- or precomments of the top-level trees on or after their lines, or as
% standalone comments after them; a single tree is returned as it is.
with_comments(_, [], [Tree]) -> Tree;
with_comments(_, [], Trees) -> Trees;
with_comments(Line, Comments, Trees) -> with_comments(Line, Comments, Trees, []).

with_comments(_, [], [], [Tree]) ->
    Tree;
with_comments(_, [], [Tree], []) ->
    Tree;
with_comments(_, [], Trees, Done) ->
    lists:reverse(Done, Trees);
with_comments(Start, Comments, [], Done) ->
    Standalone = [
        erl_syntax:set_pos(erl_syntax:comment(Indent, Text), erl_anno:new(Start + Line - 1))
     || {Line, _, Indent, Text} <- Comments
    ],
    with_comments(Start, [], [], Standalone ++ Done);
with_comments(Start, [{Line, _, Indent, Text} = Comment | Comments], [Tree | Trees], Done) ->
    CommentLine = Start + Line - 1,
    case erl_anno:line(erl_syntax:get_pos(Tree)) of
        At when At < CommentLine ->
            with_comments(Start, [Comment | Comments], Trees, [Tree | Done]);
        CommentLine ->
            Tree1 = erl_syntax:add_postcomments([erl_syntax:comment(Indent, Text)], Tree),
            with_comments(Start, Comments, [Tree1 | Trees], Done);
        _ ->
            Tree1 = erl_syntax:add_precomments([erl_syntax:comment(Indent, Text)], Tree),
            with_comments(Start, Comments, [Tree1 | Trees], Done)
    end.

%% Switching on patterns.

switch(Trees, [{Patterns, Actions} | Clauses]) when is_list(Actions) -> try_clause(Trees, Patterns, Actions, Clauses);
switch(Trees, [{Patterns, Action} | Clauses]) -> try_clause(Trees, Patterns, [Action], Clauses);
switch(Trees, [{Patterns, Guard, Action} | Clauses]) -> try_clause(Trees, Patterns, [{Guard, Action}], Clauses);
switch(_, [Default | _]) when is_function(Default, 0) -> Default();
switch(_, []) -> erlang:error(merl_switch_clause);
switch(_, _) -> erlang:error(merl_switch_badarg).

try_clause(Trees, Patterns, Actions, Clauses) ->
    case merl:match(Patterns, Trees) of
        {ok, Env} -> run_actions(Env, Actions, Trees, Clauses);
        error -> switch(Trees, Clauses)
    end.

run_actions(Env, [{Guard, Action} | Actions], Trees, Clauses) when is_function(Guard, 1), is_function(Action, 1) ->
    case Guard(Env) of
        true -> Action(Env);
        false -> run_actions(Env, Actions, Trees, Clauses)
    end;
run_actions(Env, [Action | _], _, _) when is_function(Action, 1) ->
    Action(Env);
run_actions(_, [], Trees, Clauses) ->
    switch(Trees, Clauses);
run_actions(_, _, _, _) ->
    erlang:error(merl_switch_badarg).

%% merl's view of node types and groups: [] is a list, {} a tuple with its (empty) group of elements.

kind(Tree) ->
    case erl_syntax:type(Tree) of
        nil -> list;
        Type -> Type
    end.

groups(Tree) -> clause_merl:subtrees(Tree).

% Native alpha/2.
alpha(_, _) -> clause_builtin_placeholder.

% Native match/2.
match(_, _) -> clause_builtin_placeholder.

% Native meta_template/1.
meta_template(_) -> clause_builtin_placeholder.

% Native subst/2.
subst(_, _) -> clause_builtin_placeholder.

% Native template/1.
template(_) -> clause_builtin_placeholder.

% Native template_vars/1.
template_vars(_) -> clause_builtin_placeholder.

% Native tree/1.
tree(_) -> clause_builtin_placeholder.

% Native tsubst/2.
tsubst(_, _) -> clause_builtin_placeholder.
