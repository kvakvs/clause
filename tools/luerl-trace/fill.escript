#!/usr/bin/env escript
%% fill.escript ClauseModuleFile OtpSourceFile F/A...
%% Replace Clause stubs F/A with OTP's definitions; OTP private helpers they need are
%% appended renamed 'otp_<name>', OTP exported functions that are Clause stubs are pulled too.
main([ClauseFile, OtpFile | Wanted]) ->
    {ok, ClauseForms} = epp:parse_file(ClauseFile, []),
    {ok, OtpForms} = epp:parse_file(OtpFile, [{includes, [filename:dirname(OtpFile), filename:join(filename:dirname(OtpFile), "../include")]}]),
    Stubs = stubs(ClauseForms),
    Defined = [{F, A} || {function, _, F, A, _} <- ClauseForms],
    OtpExports = lists:append([E || {attribute, _, export, E} <- OtpForms]),
    OtpFuns = maps:from_list([{{F, A}, Fn} || {function, _, F, A, _} = Fn <- OtpForms]),
    Start = [parse_fa(W) || W <- Wanted],
    Pulled = closure(Start, OtpFuns, OtpExports, Stubs, Defined, []),
    Rename = fun(F, A) ->
                     case lists:member({F, A}, OtpExports) orelse not maps:is_key({F, A}, OtpFuns) of
                         true -> F;
                         false -> list_to_atom("otp_" ++ atom_to_list(F))
                     end
             end,
    Text = [erl_pp:form(rename(maps:get(FA, OtpFuns), Rename)) || FA <- lists:reverse(Pulled)],
    {ok, Bin} = file:read_file(ClauseFile),
    Lines = string:split(binary_to_list(Bin), "\n", all),
    Replaced = [FA || FA <- Pulled, lists:member(FA, Stubs)],
    Kept = [L || L <- Lines, not is_stub_line(L, Replaced)],
    ok = file:write_file(ClauseFile, [lists:join("\n", Kept), "\n%% Pulled from OTP for tracing.\n", Text]),
    io:format("pulled ~p~n", [lists:reverse(Pulled)]).

parse_fa(W) ->
    [F, A] = string:split(W, "/"),
    {list_to_atom(F), list_to_integer(A)}.

%% Functions whose single clause body is erlang:error({not_implemented, F, A}).
stubs(Forms) ->
    [{F, A} || {function, _, F, A, [{clause, _, _, [], [{call, _, {remote, _, {atom, _, erlang}, {atom, _, error}}, [{tuple, _, [{atom, _, not_implemented} | _]}]}]}]} <- Forms].

%% Pull wanted functions, then their local callees that are OTP-private or Clause stubs.
closure([], _, _, _, _, Acc) ->
    Acc;
closure([FA | Rest], Funs, Exports, Stubs, Defined, Acc) ->
    case lists:member(FA, Acc) orelse not maps:is_key(FA, Funs) of
        true ->
            closure(Rest, Funs, Exports, Stubs, Defined, Acc);
        false ->
            Calls = local_calls(maps:get(FA, Funs)),
            Need = [C || C <- Calls, maps:is_key(C, Funs),
                         not lists:member(C, Exports) orelse lists:member(C, Stubs) orelse not lists:member(C, Defined)],
            closure(Need ++ Rest, Funs, Exports, Stubs, Defined, [FA | Acc])
    end.

local_calls(Form) ->
    erl_syntax_lib:fold(
      fun(Node, Acc) ->
              case erl_syntax:revert(Node) of
                  {call, _, {atom, _, F}, Args} -> [{F, length(Args)} | Acc];
                  {'fun', _, {function, F, A}} when is_atom(F) -> [{F, A} | Acc];
                  _ -> Acc
              end
      end, [], Form).

rename({function, L, F, A, Clauses}, Rename) ->
    {function, L, Rename(F, A), A, [walk(C, Rename) || C <- Clauses]}.

walk({call, L, {atom, La, F}, Args}, Rename) ->
    {call, L, {atom, La, rename_local(F, length(Args), Rename)}, [walk(X, Rename) || X <- Args]};
walk({'fun', L, {function, F, A}}, Rename) when is_atom(F) ->
    {'fun', L, {function, rename_local(F, A, Rename), A}};
walk(T, Rename) when is_tuple(T) ->
    list_to_tuple([walk(X, Rename) || X <- tuple_to_list(T)]);
walk(L, Rename) when is_list(L) ->
    [walk(X, Rename) || X <- L];
walk(X, _) ->
    X.

rename_local(F, A, Rename) ->
    Rename(F, A).

is_stub_line(Line, Replaced) ->
    lists:any(fun({F, A}) ->
                      Needle = "erlang:error({not_implemented, " ++ atom_to_list(F) ++ ", " ++ integer_to_list(A) ++ "})",
                      string:find(Line, Needle) =/= nomatch andalso lists:prefix(atom_to_list(F) ++ "(", string:trim(Line, leading))
              end, Replaced).
