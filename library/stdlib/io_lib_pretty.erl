%% OTP's io_lib_pretty as libraries call it (plan 21 step MIM7, docs/libraries/library.md#undocumented-modules):
%% print/1-6 over Clause's pretty printer (clause_io_lib:pretty/2), a record_print_fun asked about each record-like
%% tuple of the term. The text is OTP's; the deep list holding it is Clause's.
-module(io_lib_pretty).
-export([print/1, print/2, print/3, print/4, print/5, print/6]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([
    intermediate/7,
    print_bin/2,
    write/1,
    write/2
]).

print(Term) -> print(Term, []).

% Options, or else a record_print_fun.
print(Term, Options) when is_list(Options) ->
    clause_io_lib:pretty(Term, options(Term, Options));
print(Term, RecDefFun) ->
    print(Term, [{record_print_fun, RecDefFun}]).

print(Term, Depth, RecDefFun) -> print(Term, [{depth, Depth}, {record_print_fun, RecDefFun}]).

print(Term, Column, LineLength, Depth) ->
    print(Term, [{column, Column}, {line_length, LineLength}, {depth, Depth}]).

print(Term, Column, LineLength, Depth, RecDefFun) ->
    print(Term, Column, LineLength, Depth, -1, RecDefFun).

print(Term, Column, LineLength, Depth, LineMaxChars, RecDefFun) ->
    Options = [{column, Column}, {line_length, LineLength}, {depth, Depth}, {line_max_chars, LineMaxChars}],
    print(Term, [{record_print_fun, RecDefFun} | Options]).

% The options clause_io_lib:pretty/2 takes, the records a record_print_fun knows among them; others ignored.
options(Term, Options) ->
    Known = [column, line_length, depth, line_max_chars, strings, encoding],
    Records =
        case lists:keyfind(record_print_fun, 1, Options) of
            {_, Fun} -> [{records, records(Fun, Term)}];
            false -> []
        end,
    [Option || {Name, _} = Option <- Options, lists:member(Name, Known)] ++ Records.

% The {Name, Fields} a record_print_fun answers for the record-like tuples of Term.
records(Fun, Term) ->
    lists:usort([
        {Name, Fields}
     || {Name, Size} <- lists:usort(shapes(Term, [])),
        Fields <- [Fun(Name, Size)],
        is_list(Fields),
        length(Fields) =:= Size
    ]).

shapes(Term, Acc) when is_tuple(Term), tuple_size(Term) > 0, is_atom(element(1, Term)) ->
    [{element(1, Term), tuple_size(Term) - 1} | lists:foldl(fun shapes/2, Acc, tl(tuple_to_list(Term)))];
shapes(Term, Acc) when is_tuple(Term) ->
    lists:foldl(fun shapes/2, Acc, tuple_to_list(Term));
shapes([Head | Tail], Acc) ->
    shapes(Tail, shapes(Head, Acc));
shapes(Term, Acc) when is_map(Term) ->
    maps:fold(fun(K, V, A) -> shapes(V, shapes(K, A)) end, Acc, Term);
shapes(_, Acc) ->
    Acc.

intermediate(_, _, _, _, _, _, _) -> erlang:error({not_implemented, intermediate, 7}).
print_bin(_, _) -> erlang:error({not_implemented, print_bin, 2}).
write(_) -> erlang:error({not_implemented, write, 1}).
write(_, _) -> erlang:error({not_implemented, write, 2}).
