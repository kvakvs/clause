%% Clause's replacement for OTP's prim_eval (docs/library.md#runtime-module-replacements), generated from
%% prim_eval.terms by tests/compiler/library/library_modules.py; edited by hand from here on.
-module(prim_eval).
-export([
    'receive'/2
]).

%% A receive whose clauses are a fun, as erl_eval runs them (plan step 41S, docs/builtins.md#prim-eval): the first
%% message for which Fun returns anything but nomatch is taken and that answer returned; timeout once Timeout
%% expires. The scan uses the steps of a compiled receive (clause_receive builtins over CLAUSE_receive_v1).
'receive'(Fun, Timeout) when is_function(Fun, 1) ->
    scan(Fun, Timeout);
'receive'(Fun, Timeout) ->
    erlang:error(badarg, [Fun, Timeout]).

scan(Fun, Timeout) ->
    case clause_receive:peek() of
        {message, Message} ->
            case matched(Fun, Message) of
                nomatch ->
                    clause_receive:skip(),
                    scan(Fun, Timeout);
                Answer ->
                    clause_receive:take(),
                    Answer
            end;
        none ->
            case clause_receive:wait(Timeout) of
                true ->
                    scan(Fun, Timeout);
                false ->
                    clause_receive:restart(),
                    timeout
            end
    end.

%% Fun's answer for a message; an exception leaves the mailbox for the next receive to scan from its oldest message.
matched(Fun, Message) ->
    try
        Fun(Message)
    catch
        Class:Reason:Stack ->
            clause_receive:restart(),
            erlang:raise(Class, Reason, Stack)
    end.
