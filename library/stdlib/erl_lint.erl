%% Clause's erl_lint (plan 21 step LIB34C, docs/builtins.md#erl-lint): the checks run in C++
%% (runtime/src/builtins/front_end/lint*.cpp) after OTP's erl_lint. This module looks up the behaviours' callbacks
%% (behaviour_info/1 of the named modules), formats the messages format_error/1 describes, and keeps the functions
%% that take funs or only fold options.
-module(erl_lint).
-export([
    bool_option/4,
    check_format_string/1,
    check_format_string/2,
    exprs/2,
    exprs_opt/3,
    format_error/1,
    is_guard_expr/1,
    is_guard_test/1,
    is_guard_test/2,
    is_guard_test/3,
    is_pattern_expr/1,
    module/1,
    module/2,
    module/3,
    used_vars/2,
    value_option/3,
    value_option/7
]).
-include("erl_lint.types.hrl").

module(Forms) ->
    lint(Forms, "nofile", compiler_options(Forms)).

module(Forms, FileName) ->
    lint(Forms, FileName, compiler_options(Forms)).

module(Forms, FileName, Options) ->
    lint(Forms, FileName, Options ++ compiler_options(Forms)).

compiler_options(Forms) ->
    lists:flatten([C || {attribute, _, compile, C} <- Forms]).

lint(Forms, File, Options) ->
    clause_erl_lint:module(Forms, File, Options, behaviours(Forms)).

%% Each behaviour attribute's module with its callbacks and optional callbacks: {ok, List} or error.
behaviours(Forms) ->
    Names = lists:usort([
        B
     || {attribute, _, Tag, B} <- Forms, Tag =:= behaviour orelse Tag =:= behavior
    ]),
    [{B, behaviour_info(B, callbacks), behaviour_info(B, optional_callbacks)} || B <- Names].

behaviour_info(Behaviour, Kind) ->
    try Behaviour:behaviour_info(Kind) of
        Value -> {ok, Value}
    catch
        _:_ -> error
    end.

format_error(Error) ->
    case clause_erl_lint:format_spec(Error) of
        {plain, Format, Args} -> io_lib:format(Format, Args);
        {nested, Format, Parts} -> io_lib:format(Format, [part(P) || P <- Parts]);
        Text -> unicode:characters_to_list(Text)
    end.

%% An argument of a message: a term, or the result of io_lib:format/2.
part({value, Term}) -> Term;
part({format, Format, Parts}) -> io_lib:format(Format, [part(P) || P <- Parts]).

exprs(_, _) -> clause_builtin_placeholder.

exprs_opt(_, _, _) -> clause_builtin_placeholder.

used_vars(_, _) -> clause_builtin_placeholder.

is_pattern_expr(_) -> clause_builtin_placeholder.

is_guard_test(_) -> clause_builtin_placeholder.

is_guard_test(Expression, Forms) ->
    is_guard_test(Expression, Forms, fun(_) -> false end).

%% is_guard_test(Expression, Forms, IsOverridden): the local calls IsOverridden overrides are never guard BIFs.
is_guard_test(Expression, Forms, IsOverridden) ->
    Overridden = [FA || FA <- clause_erl_lint:local_calls(Expression), IsOverridden(FA)],
    clause_erl_lint:is_guard_test(Expression, Forms, Overridden).

is_guard_expr(_) -> clause_builtin_placeholder.

check_format_string(_) -> clause_builtin_placeholder.

check_format_string(_, _) -> clause_builtin_placeholder.

bool_option(On, Off, Default, Options) ->
    lists:foldl(
        fun
            (Option, _) when Option =:= On -> true;
            (Option, _) when Option =:= Off -> false;
            (_, Value) -> Value
        end,
        Default,
        Options
    ).

value_option(Flag, Default, Options) ->
    lists:foldl(
        fun
            ({Option, Value}, _) when Option =:= Flag -> Value;
            (_, Value) -> Value
        end,
        Default,
        Options
    ).

value_option(Flag, Default, On, OnValue, Off, OffValue, Options) ->
    lists:foldl(
        fun
            ({Option, Value}, _) when Option =:= Flag -> Value;
            (Option, _) when Option =:= On -> OnValue;
            (Option, _) when Option =:= Off -> OffValue;
            (_, Value) -> Value
        end,
        Default,
        Options
    ).
