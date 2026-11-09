%% XPath 1.0 expressions as OTP's xmerl parses them (plan 21 step MIM3b, docs/libraries/xmerl.md#xmerl_xpath): the
%% same parse trees, which xmerl_xpath evaluates and its not_implemented errors show; a syntax error as OTP's parser's
%% {badmatch, {error, {1, xmerl_xpath_parse, ["syntax error before: ", [Token]]}}}.
-module(clause_xmerl_xpath).
-export([parse/1]).

parse(Text) ->
    case expr(tokens(Text, start)) of
        {Tree, ['$end']} -> Tree;
        {_, [Token | _]} -> syntax_error(Token)
    end.

syntax_error(Token) ->
    erlang:error({badmatch, {error, {1, xmerl_xpath_parse, ["syntax error before: ", [token_text(Token)]]}}}).

token_text({_, Value}) when is_atom(Value) -> io_lib:write(Value);
token_text({_, Value}) -> io_lib:write(Value);
token_text(Token) -> io_lib:write(Token).

%% --- Tokens ---

% Tokens of Text. Prev says whether an operator may come next (XPath 1.0's disambiguation of '*' and of names
% such as and, or, div and mod).
tokens([C | T], Prev) when C =:= $\s; C =:= $\t; C =:= $\n; C =:= $\r ->
    tokens(T, Prev);
tokens([], _) ->
    ['$end'];
tokens("//" ++ T, _) ->
    ['//' | tokens(T, operator)];
tokens("::" ++ T, _) ->
    ['::' | tokens(T, operator)];
tokens("!=" ++ T, _) ->
    ['!=' | tokens(T, operator)];
tokens("<=" ++ T, _) ->
    ['<=' | tokens(T, operator)];
tokens(">=" ++ T, _) ->
    ['>=' | tokens(T, operator)];
tokens(".." ++ T, _) ->
    ['..' | tokens(T, value)];
tokens([$. | [D | _] = T], _) when D >= $0, D =< $9 -> number([$0, $. | T], []);
tokens("." ++ T, _) ->
    ['.' | tokens(T, value)];
tokens("*" ++ T, value) ->
    ['mul' | tokens(T, operator)];
tokens("*" ++ T, _) ->
    [{wildcard} | tokens(T, value)];
tokens([C | T], _) when C =:= $"; C =:= $' ->
    {Literal, [C | T1]} = lists:splitwith(fun(D) -> D =/= C end, T),
    [{literal, Literal} | tokens(T1, value)];
tokens([D | _] = T, _) when D >= $0, D =< $9 ->
    number(T, []);
tokens("$" ++ T, _) ->
    {Name, T1} = name(T),
    [{var, list_to_atom(Name)} | tokens(T1, value)];
tokens([C | T], _) when
    C =:= $(;
    C =:= $[;
    C =:= $,;
    C =:= $@;
    C =:= $/;
    C =:= $|;
    C =:= $+;
    C =:= $-;
    C =:= $=;
    C =:= $<;
    C =:= $>
->
    [list_to_atom([C]) | tokens(T, operator)];
tokens([C | T], _) when C =:= $); C =:= $] ->
    [list_to_atom([C]) | tokens(T, value)];
tokens(T, Prev) ->
    case name(T) of
        {[], _} -> [{invalid, T}];
        {Name, T1} -> name_token(Name, T1, Prev)
    end.

number(T, _) ->
    {Digits, T1} = lists:splitwith(fun(C) -> (C >= $0 andalso C =< $9) orelse C =:= $. end, T),
    Value =
        case lists:member($., Digits) of
            true -> list_to_float(float_text(Digits));
            false -> list_to_integer(Digits)
        end,
    [{number, Value} | tokens(T1, value)].

float_text(Digits) ->
    case lists:last(Digits) of
        $. -> Digits ++ "0";
        _ -> Digits
    end.

% A name: an operator name after a value, an axis before '::', a node type or function before '(', a wildcard
% prefix, or a name test.
name_token(Name, T, value) when Name =:= "and"; Name =:= "or"; Name =:= "div"; Name =:= "mod" ->
    [list_to_atom(Name) | tokens(T, operator)];
name_token(Name, T, _) ->
    case {skip(T), lists:suffix(":", Name)} of
        {"::" ++ _, _} ->
            [{axis, list_to_atom(underscored(Name))} | tokens(T, operator)];
        {"(" ++ _, _} when
            Name =:= "comment"; Name =:= "text"; Name =:= "node"; Name =:= "processing-instruction"
        ->
            [{node_type, list_to_atom(Name)} | tokens(T, operator)];
        {"(" ++ _, _} ->
            [{function, list_to_atom(Name)} | tokens(T, operator)];
        {_, true} when hd(T) =:= $* ->
            [{prefix_test, lists:droplast(Name)} | tokens(tl(T), value)];
        _ ->
            [{name, Name} | tokens(T, value)]
    end.

underscored(Name) ->
    [
        case C of
            $- -> $_;
            _ -> C
        end
     || C <- Name
    ].

% A name's characters; one ':' joins a prefix to a local name, or ends a prefix before '*'.
name(T) ->
    {Prefix, T1} = lists:splitwith(fun name_char/1, T),
    case T1 of
        [$:, C | _] when Prefix =/= [], C =/= $: ->
            {Local, T2} = lists:splitwith(fun name_char/1, tl(T1)),
            {Prefix ++ ":" ++ Local, T2};
        _ ->
            {Prefix, T1}
    end.

name_char(C) ->
    (C >= $a andalso C =< $z) orelse (C >= $A andalso C =< $Z) orelse (C >= $0 andalso C =< $9) orelse
        C =:= $_ orelse C =:= $- orelse C =:= $. orelse C >= 16#80.

skip([C | T]) when C =:= $\s; C =:= $\t; C =:= $\n; C =:= $\r -> skip(T);
skip(T) -> T.

%% --- Expressions, lowest precedence first ---

expr(Ts) -> binary(Ts, ['or'], fun and_expr/1, fun(Op, L, R) -> {bool, Op, L, R} end).

and_expr(Ts) -> binary(Ts, ['and'], fun equality/1, fun(Op, L, R) -> {bool, Op, L, R} end).

equality(Ts) -> binary(Ts, ['=', '!='], fun relational/1, fun(Op, L, R) -> {comp, Op, L, R} end).

relational(Ts) -> binary(Ts, ['<', '>', '<=', '>='], fun additive/1, fun(Op, L, R) -> {comp, Op, L, R} end).

additive(Ts) -> binary(Ts, ['+', '-'], fun multiplicative/1, fun(Op, L, R) -> {arith, Op, L, R} end).

multiplicative(Ts) -> binary(Ts, ['mul', 'div', 'mod'], fun unary/1, fun arith/3).

arith('mul', L, R) -> {arith, '*', L, R};
arith(Op, L, R) -> {arith, Op, L, R}.

% Left-associative operators Ops between operands Next parses.
binary(Ts, Ops, Next, Make) ->
    {Left, Ts1} = Next(Ts),
    binary_rest(Left, Ts1, Ops, Next, Make).

binary_rest(Left, [Op | Ts], Ops, Next, Make) ->
    case lists:member(Op, Ops) of
        true ->
            {Right, Ts1} = Next(Ts),
            binary_rest(Make(Op, Left, Right), Ts1, Ops, Next, Make);
        false ->
            {Left, [Op | Ts]}
    end.

unary(['-' | Ts]) ->
    {Expr, Ts1} = unary(Ts),
    {{negative, Expr}, Ts1};
unary(Ts) ->
    union(Ts).

union(Ts) ->
    {Left, Ts1} = path_expr(Ts),
    union_rest(Left, Ts1).

union_rest(Left, ['|' | Ts]) ->
    {Right, Ts1} = path_expr(Ts),
    union_rest({path, union, {Left, Right}}, Ts1);
union_rest(Left, Ts) ->
    {Left, Ts}.

%% --- Paths ---

path_expr(['/' | Ts]) ->
    case step_start(Ts) of
        true ->
            {Steps, Ts1} = relative(Ts),
            {{path, abs, Steps}, Ts1};
        false ->
            {{path, abs, '/'}, Ts}
    end;
path_expr(['//' | Ts]) ->
    {Steps, Ts1} = relative(Ts, descendant_or_self()),
    {{path, abs, Steps}, Ts1};
path_expr(Ts) ->
    case step_start(Ts) of
        true ->
            {Steps, Ts1} = relative(Ts),
            {{path, rel, Steps}, Ts1};
        false ->
            filter(Ts)
    end.

step_start([{name, _} | _]) -> true;
step_start([{wildcard} | _]) -> true;
step_start([{prefix_test, _} | _]) -> true;
step_start([{node_type, _} | _]) -> true;
step_start([{axis, _} | _]) -> true;
step_start([T | _]) -> T =:= '@' orelse T =:= '.' orelse T =:= '..';
step_start(_) -> false.

descendant_or_self() ->
    {step, {descendant_or_self, {node_type, node}, []}}.

% Steps joined by '/' and '//', left-nested as OTP's {refine, Left, Step}; after '//' a descendant-or-self step.
relative(Ts) ->
    {Step, Ts1} = step(Ts),
    relative_rest(Step, Ts1).

relative(Ts, First) ->
    {Step, Ts1} = step(Ts),
    relative_rest({refine, First, Step}, Ts1).

relative_rest(Left, ['/' | Ts]) ->
    {Step, Ts1} = step(Ts),
    relative_rest({refine, Left, Step}, Ts1);
relative_rest(Left, ['//' | Ts]) ->
    {Step, Ts1} = step(Ts),
    relative_rest({refine, {refine, Left, descendant_or_self()}, Step}, Ts1);
relative_rest(Left, Ts) ->
    {Left, Ts}.

step(['.' | Ts]) ->
    {{step, {self, {node_type, node}, []}}, Ts};
step(['..' | Ts]) ->
    {{step, {parent, {node_type, node}, []}}, Ts};
step([{axis, Axis}, '::' | Ts]) ->
    axis_step(Axis, Ts);
step(['@' | Ts]) ->
    axis_step(attribute, Ts);
step(Ts) ->
    axis_step(child, Ts).

axis_step(Axis, Ts) ->
    {Test, Ts1} = node_test(Ts),
    {Preds, Ts2} = predicates(Ts1, []),
    {{step, {Axis, Test, Preds}}, Ts2}.

node_test([{wildcard} | Ts]) ->
    {{wildcard, wildcard}, Ts};
node_test([{prefix_test, Prefix} | Ts]) ->
    {{prefix_test, Prefix}, Ts};
node_test([{name, Name} | Ts]) ->
    {{name, qname(Name)}, Ts};
node_test([{node_type, 'processing-instruction'}, '(', {literal, Target}, ')' | Ts]) ->
    {{processing_instruction, Target}, Ts};
node_test([{node_type, Type}, '(', ')' | Ts]) ->
    {{node_type, Type}, Ts};
node_test([Token | _]) ->
    syntax_error(Token).

qname(Name) ->
    case string:split(Name, ":") of
        [Prefix, Local] -> {list_to_atom(Name), Prefix, Local};
        [Local] -> {list_to_atom(Name), [], Local}
    end.

predicates(['[' | Ts], Acc) ->
    case expr(Ts) of
        {Expr, [']' | Ts1]} -> predicates(Ts1, [{pred, Expr} | Acc]);
        {_, [Token | _]} -> syntax_error(Token)
    end;
predicates(Ts, Acc) ->
    {lists:reverse(Acc), Ts}.

% A primary expression with its predicates, each nesting the filter before it.
filter(Ts) ->
    {Primary, Ts1} = primary(Ts),
    {Preds, Ts2} = predicates(Ts1, []),
    {lists:foldl(fun(Pred, Acc) -> {path, filter, {Acc, Pred}} end, Primary, Preds), Ts2}.

primary([{var, Name} | Ts]) ->
    {{variable_reference, Name}, Ts};
primary([{literal, Literal} | Ts]) ->
    {{literal, Literal}, Ts};
primary([{number, Number} | Ts]) ->
    {{number, Number}, Ts};
primary(['(' | Ts]) ->
    case expr(Ts) of
        {Expr, [')' | Ts1]} -> {Expr, Ts1};
        {_, [Token | _]} -> syntax_error(Token)
    end;
primary([{function, Name}, '(' | Ts]) ->
    {Args, Ts1} = arguments(Ts),
    {{function_call, Name, Args}, Ts1};
primary([Token | _]) ->
    syntax_error(Token).

arguments([')' | Ts]) ->
    {[], Ts};
arguments(Ts) ->
    case expr(Ts) of
        {Arg, [',' | Ts1]} ->
            {Args, Ts2} = arguments(Ts1),
            {[Arg | Args], Ts2};
        {Arg, [')' | Ts1]} ->
            {[Arg], Ts1};
        {_, [Token | _]} ->
            syntax_error(Token)
    end.
