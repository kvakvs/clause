%% A compile-time evaluating transform, as parse_trans's ct_expand: pt_eval:term(Expr) and pt_eval:reported(Expr) as a
%% function body become Expr's value, so the transform calls the modules Expr calls. term/1 lets an evaluation error
%% escape; reported/1 returns it as the transform's error, as ct_expand does.
-module(pt_eval).
-export([parse_transform/2]).

parse_transform(Forms, _Options) ->
    case lists:mapfoldl(fun form/2, [], Forms) of
        {Result, []} -> Result;
        {_, Errors} -> {error, [{file(Forms), lists:reverse(Errors)}], []}
    end.

file([{attribute, _, file, {File, _}} | _]) -> File.

form({function, Anno, Name, Arity, Clauses0}, Errors0) ->
    {Clauses, Errors} = lists:mapfoldl(fun clause/2, Errors0, Clauses0),
    {{function, Anno, Name, Arity, Clauses}, Errors};
form(Form, Errors) ->
    {Form, Errors}.

clause({clause, Anno, Patterns, Guards, Body0}, Errors0) ->
    {Body, Errors} = lists:mapfoldl(fun expand/2, Errors0, Body0),
    {{clause, Anno, Patterns, Guards, Body}, Errors}.

expand({call, Anno, {remote, _, {atom, _, pt_eval}, {atom, _, term}}, [Expression]}, Errors) ->
    {value(Expression, Anno), Errors};
expand({call, Anno, {remote, _, {atom, _, pt_eval}, {atom, _, reported}}, [Expression]} = Call, Errors) ->
    try
        {value(Expression, Anno), Errors}
    catch
        error:Reason:Stack -> {Call, [{Anno, ?MODULE, {Reason, Stack}} | Errors]}
    end;
expand(Expression, Errors) ->
    {Expression, Errors}.

value(Expression, Anno) ->
    {value, Value, _} = erl_eval:expr(Expression, erl_eval:new_bindings()),
    erl_parse:abstract(Value, [{location, Anno}]).
