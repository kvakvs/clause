-module(edlin_expand).
-export([expand/1, expand/2]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/library.md#runtime-module-replacements).
-export([
    bif/1,
    expand/3,
    format_matches/2,
    get_exports/1,
    is_type/3,
    match_arguments1/3,
    number_matches/1,
    over_word/1,
    shell_default_or_bif/1
]).

% Completion of the shell's input (plan 21 step LIB74, docs/library.md#interactive-shell).

expand(_) -> erlang:error({not_implemented, expand, 1}).
expand(_, _) -> erlang:error({not_implemented, expand, 2}).
expand(_, _, _) -> erlang:error({not_implemented, expand, 3}).
format_matches(_, _) -> erlang:error({not_implemented, format_matches, 2}).
is_type(_, _, _) -> erlang:error({not_implemented, is_type, 3}).
match_arguments1(_, _, _) -> erlang:error({not_implemented, match_arguments1, 3}).
number_matches(_) -> erlang:error({not_implemented, number_matches, 1}).
over_word(_) -> erlang:error({not_implemented, over_word, 1}).

%% Where a local call of Fun goes: "shell_default", "erlang", "shell" (a shell command) or "user_defined".
shell_default_or_bif(Fun) when is_atom(Fun) ->
    case lists:keymember(Fun, 1, get_exports(shell_default)) of
        true -> "shell_default";
        false -> bif(Fun)
    end;
shell_default_or_bif(Fun) ->
    case erl_scan:string(Fun) of
        {ok, [{atom, _, Name}], _} -> shell_default_or_bif(Name);
        _ -> []
    end.

%% "erlang" for a function erlang exports, else as a shell command or a user's function.
bif(Fun) when is_atom(Fun) ->
    case lists:keymember(Fun, 1, get_exports(erlang)) of
        true ->
            "erlang";
        false ->
            case shell:local_func(Fun) of
                true -> "shell";
                false -> "user_defined"
            end
    end;
bif(Fun) ->
    case erl_scan:string(Fun) of
        {ok, [{atom, _, Name}], _} -> bif(Name);
        _ -> []
    end.

%% The exports of a loaded module, or of its object file.
get_exports(Mod) ->
    case erlang:module_loaded(Mod) of
        true ->
            Mod:module_info(exports);
        false ->
            case beam_lib:chunks(code:which(Mod), [exports]) of
                {ok, {Mod, [{exports, E}]}} -> E;
                _ -> []
            end
    end.
