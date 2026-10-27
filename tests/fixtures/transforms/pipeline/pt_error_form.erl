-module(pt_error_form).
-export([parse_transform/2]).

%% Adds an erl_lint error form before eof, which the loader formats with erl_lint:format_error/1 (plan 21 LIB77D).
parse_transform(Forms, _Options) ->
    {Body, [Eof]} = lists:split(length(Forms) - 1, Forms),
    Body ++ [{error, {{5, 1}, erl_lint, {unbound_var, 'Missing'}}}, Eof].
