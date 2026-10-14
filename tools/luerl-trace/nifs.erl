-module(nifs).
-export([main/1]).
%% nifs:main([ModulesFile, OutFile]): M:F/A of OTP-source functions whose body is only erlang:nif_error.
main([In, Out]) ->
    {ok, Bin} = file:read_file(In),
    Mods = [list_to_atom(M) || M <- string:tokens(binary_to_list(Bin), [$\r, $\n])],
    Lines = lists:append([nif_funs(M) || M <- Mods]),
    file:write_file(Out, [[L, $\n] || L <- Lines]),
    halt().

nif_funs(M) ->
    case filelib:wildcard("C:/Program Files/Erlang OTP/lib/*/src/" ++ atom_to_list(M) ++ ".erl") of
        [File | _] ->
            Inc = [filename:dirname(File), filename:join(filename:dirname(File), "../include")],
            case epp:parse_file(File, [{includes, Inc}]) of
                {ok, Forms} ->
                    [io_lib:format("~w:~w/~w", [M, F, A]) || {function, _, F, A, Cs} <- Forms, lists:all(fun nif_clause/1, Cs)];
                _ -> []
            end;
        [] -> []
    end.

nif_clause({clause, _, _, _, [{call, _, {remote, _, {atom, _, erlang}, {atom, _, nif_error}}, _}]}) -> true;
nif_clause(_) -> false.
