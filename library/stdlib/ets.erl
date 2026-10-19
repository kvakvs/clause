%% Clause's ets (plan 21 step LIB10, docs/builtins.md#ets): OTP 29's ETS tables over the runtime's tables
%% (docs/runtime.md#ets-tables). Table, read, write, walk and update functions are runtime builtins with
%% clause_builtin_placeholder bodies; the folds and the i/0..3 table viewers, which call funs or the io server,
%% are Erlang over them. Match specifications come with plan 21 step LIB11 and table files with LIB12.
-module(ets).
-export([
    all/0,
    delete/1,
    delete/2,
    delete_all_objects/1,
    delete_object/2,
    file2tab/1,
    file2tab/2,
    first/1,
    first_lookup/1,
    foldl/3,
    foldr/3,
    from_dets/2,
    fun2ms/1,
    give_away/3,
    i/0,
    i/1,
    i/2,
    i/3,
    info/1,
    info/2,
    init_table/2,
    insert/2,
    insert_new/2,
    internal_delete_all/2,
    internal_request_all/0,
    internal_select_delete/2,
    is_compiled_ms/1,
    last/1,
    last_lookup/1,
    lookup/2,
    lookup_element/3,
    lookup_element/4,
    match/1,
    match/2,
    match/3,
    match_delete/2,
    match_object/1,
    match_object/2,
    match_object/3,
    match_spec_compile/1,
    match_spec_run/2,
    match_spec_run_r/3,
    member/2,
    new/2,
    next/2,
    next_lookup/2,
    prev/2,
    prev_lookup/2,
    rename/2,
    repair_continuation/2,
    safe_fixtable/2,
    select/1,
    select/2,
    select/3,
    select_count/2,
    select_delete/2,
    select_replace/2,
    select_reverse/1,
    select_reverse/2,
    select_reverse/3,
    setopts/2,
    slot/2,
    tab2file/2,
    tab2file/3,
    tab2list/1,
    tabfile_info/1,
    table/1,
    table/2,
    take/2,
    test_ms/2,
    to_dets/2,
    update_counter/3,
    update_counter/4,
    update_element/3,
    update_element/4,
    whereis/1
]).
-include("ets.types.hrl").

all() -> clause_builtin_placeholder.
delete(_) -> clause_builtin_placeholder.
delete(_, _) -> clause_builtin_placeholder.
delete_all_objects(_) -> clause_builtin_placeholder.
delete_object(_, _) -> clause_builtin_placeholder.
file2tab(_) -> erlang:error({not_implemented, file2tab, 1}).
file2tab(_, _) -> erlang:error({not_implemented, file2tab, 2}).
first(_) -> clause_builtin_placeholder.
first_lookup(_) -> clause_builtin_placeholder.
from_dets(_, _) -> erlang:error({not_implemented, from_dets, 2}).
give_away(_, _, _) -> clause_builtin_placeholder.
info(_) -> clause_builtin_placeholder.
info(_, _) -> clause_builtin_placeholder.
init_table(_, _) -> erlang:error({not_implemented, init_table, 2}).
insert(_, _) -> clause_builtin_placeholder.
insert_new(_, _) -> clause_builtin_placeholder.
internal_delete_all(_, _) -> erlang:error({not_implemented, internal_delete_all, 2}).
internal_request_all() -> erlang:error({not_implemented, internal_request_all, 0}).
internal_select_delete(_, _) -> erlang:error({not_implemented, internal_select_delete, 2}).
is_compiled_ms(_) -> erlang:error({not_implemented, is_compiled_ms, 1}).
last(_) -> clause_builtin_placeholder.
last_lookup(_) -> clause_builtin_placeholder.
lookup(_, _) -> clause_builtin_placeholder.
lookup_element(_, _, _) -> clause_builtin_placeholder.
lookup_element(_, _, _, _) -> clause_builtin_placeholder.
match(_) -> erlang:error({not_implemented, match, 1}).
match(_, _) -> erlang:error({not_implemented, match, 2}).
match(_, _, _) -> erlang:error({not_implemented, match, 3}).
match_delete(_, _) -> erlang:error({not_implemented, match_delete, 2}).
match_object(_) -> erlang:error({not_implemented, match_object, 1}).
match_object(_, _) -> erlang:error({not_implemented, match_object, 2}).
match_object(_, _, _) -> erlang:error({not_implemented, match_object, 3}).
match_spec_compile(_) -> erlang:error({not_implemented, match_spec_compile, 1}).
match_spec_run(_, _) -> erlang:error({not_implemented, match_spec_run, 2}).
match_spec_run_r(_, _, _) -> erlang:error({not_implemented, match_spec_run_r, 3}).
member(_, _) -> clause_builtin_placeholder.
new(_, _) -> clause_builtin_placeholder.
next(_, _) -> clause_builtin_placeholder.
next_lookup(_, _) -> clause_builtin_placeholder.
prev(_, _) -> clause_builtin_placeholder.
prev_lookup(_, _) -> clause_builtin_placeholder.
rename(_, _) -> clause_builtin_placeholder.
repair_continuation(_, _) -> erlang:error({not_implemented, repair_continuation, 2}).
safe_fixtable(_, _) -> clause_builtin_placeholder.
select(_) -> erlang:error({not_implemented, select, 1}).
select(_, _) -> erlang:error({not_implemented, select, 2}).
select(_, _, _) -> erlang:error({not_implemented, select, 3}).
select_count(_, _) -> erlang:error({not_implemented, select_count, 2}).
select_delete(_, _) -> erlang:error({not_implemented, select_delete, 2}).
select_replace(_, _) -> erlang:error({not_implemented, select_replace, 2}).
select_reverse(_) -> erlang:error({not_implemented, select_reverse, 1}).
select_reverse(_, _) -> erlang:error({not_implemented, select_reverse, 2}).
select_reverse(_, _, _) -> erlang:error({not_implemented, select_reverse, 3}).
setopts(_, _) -> clause_builtin_placeholder.
slot(_, _) -> clause_builtin_placeholder.
tab2file(_, _) -> erlang:error({not_implemented, tab2file, 2}).
tab2file(_, _, _) -> erlang:error({not_implemented, tab2file, 3}).
tab2list(_) -> clause_builtin_placeholder.
tabfile_info(_) -> erlang:error({not_implemented, tabfile_info, 1}).
table(_) -> erlang:error({not_implemented, table, 1}).
table(_, _) -> erlang:error({not_implemented, table, 2}).
take(_, _) -> clause_builtin_placeholder.
test_ms(_, _) -> erlang:error({not_implemented, test_ms, 2}).
to_dets(_, _) -> erlang:error({not_implemented, to_dets, 2}).
update_counter(_, _, _) -> clause_builtin_placeholder.
update_counter(_, _, _, _) -> clause_builtin_placeholder.
update_element(_, _, _) -> clause_builtin_placeholder.
update_element(_, _, _, _) -> clause_builtin_placeholder.
whereis(_) -> clause_builtin_placeholder.

%% fun2ms(LiteralFun): without the ms_transform parse transform, only a fun of the shell's evaluator could be
%% translated at run time; Clause has no such funs yet, so every fun exits as a compiled fun does in OTP.
fun2ms(Fun) when is_function(Fun) ->
    exit(
        {badarg,
            {ets, fun2ms, [
                function,
                called,
                with,
                real,
                'fun',
                should,
                be,
                transformed,
                with,
                parse_transform,
                'or',
                called,
                with,
                a,
                'fun',
                generated,
                in,
                the,
                shell
            ]}}
    ).

%% foldl(Function, Acc0, Table): Function over every object, first to last, with the table fixed meanwhile.
foldl(Function, Acc, Table) ->
    Tid = table_id(Table),
    ets:safe_fixtable(Tid, true),
    try
        fold_forward(Function, Acc, Tid, ets:first_lookup(Tid))
    after
        ets:safe_fixtable(Tid, false)
    end.

fold_forward(_Function, Acc, _Tid, '$end_of_table') ->
    Acc;
fold_forward(Function, Acc, Tid, {Key, Objects}) ->
    fold_forward(Function, lists:foldl(Function, Acc, Objects), Tid, ets:next_lookup(Tid, Key)).

%% foldr(Function, Acc0, Table): Function over every object, last to first.
foldr(Function, Acc, Table) ->
    Tid = table_id(Table),
    ets:safe_fixtable(Tid, true),
    try
        fold_backward(Function, Acc, Tid, ets:last_lookup(Tid))
    after
        ets:safe_fixtable(Tid, false)
    end.

fold_backward(_Function, Acc, _Tid, '$end_of_table') ->
    Acc;
fold_backward(Function, Acc, Tid, {Key, Objects}) ->
    fold_backward(Function, lists:foldr(Function, Acc, Objects), Tid, ets:prev_lookup(Tid, Key)).

%% The identifier of a table argument, so a rename during a fold or a browse does not lose the table.
table_id(Table) when is_atom(Table) ->
    case ets:whereis(Table) of
        undefined -> erlang:error(badarg, [Table]);
        Tid -> Tid
    end;
table_id(Table) ->
    Table.

%% i(): one line per table, sorted: id, name, type, size, memory and owner (its registered name if any).
i() ->
    columns([id, name, type, size, mem, owner]),
    io:format(
        " -------------------------------------"
        "---------------------------------------\n"
    ),
    lists:foreach(fun table_line/1, lists:sort(ets:all())).

table_line(Table) ->
    try
        Owner = ets:info(Table, owner),
        columns([
            Table,
            ets:info(Table, name),
            ets:info(Table, type),
            ets:info(Table, size),
            ets:info(Table, memory),
            owner_name(Owner)
        ])
    catch
        _:_ -> io:format("~-10s ... unreadable \n", [printed(Table)])
    end.

owner_name(Owner) ->
    case process_info(Owner, registered_name) of
        {registered_name, Name} -> Name;
        _ -> Owner
    end.

%% Columns of 15, 17, 5, 6 and 8 characters and a last one as long as it is; a longer value pushes the rest right.
columns(Values) ->
    Texts = [printed(Value) || Value <- Values],
    Padded = lists:zipwith(fun pad/2, lists:droplast(Texts), [15, 17, 5, 6, 8]),
    io:format(" ~s ~s ~s ~s ~s ~s\n", Padded ++ [lists:last(Texts)]).

pad(Text, Width) ->
    Text ++ lists:duplicate(max(0, Width - length(Text)), $\s).

printed(Term) ->
    lists:flatten(io_lib:format("~p", [Term])).

%% i(Table): browse a table on the terminal, 40 lines a page, 80 columns wide.
i(Table) ->
    i(Table, 40).

i(Table, Height) ->
    i(Table, Height, 80).

i(Table, Height, Width) ->
    Tid = table_id(Table),
    browse(#{table => Tid, height => Height, width => Width}, ets:first(Tid), 1, 1).

-define(MORE_PROMPT, '(c)ontinue (q)uit (p)Digits (k)ill /Regexp -->').
-define(END_PROMPT, 'EOT  (q)uit (p)Digits (k)ill /Regexp -->').

%% Show the objects from Key on until the page is full (Line is the page's line, Number the object's number).
browse(View, '$end_of_table', Line, Number) ->
    command(View, ?END_PROMPT, eot, '$end_of_table', Line, Number);
browse(#{height := Height} = View, Key, Line, Number) when Line < Height ->
    show_key(View, Key, Line, Number);
browse(View, Key, Line, Number) ->
    command(View, ?MORE_PROMPT, normal, Key, Line, Number).

show_key(#{table := Tid} = View, Key, Line, Number) ->
    Objects = ets:lookup(Tid, Key),
    show_objects(View, Objects, Number),
    Count = length(Objects),
    browse(View, ets:next(Tid, Key), Line + Count, Number + Count).

show_objects(View, Objects, Number) ->
    lists:foldl(
        fun(Object, At) ->
            show_object(View, Object, At),
            At + 1
        end,
        Number,
        Objects
    ).

show_object(#{width := Width}, Object, Number) ->
    Text = printed(Object),
    Shown =
        case length(Text) > Width - 8 of
            true -> string:slice(Text, 0, Width - 13) ++ "  ...";
            false -> Text
        end,
    io:format("<~-4w> ~s~n", [Number, Shown]).

%% Read a command at a page end (Mode normal), the table's end (eot) or in a search ({re, Re}).
command(View, Prompt, Mode, Key, Line, Number) ->
    Again = fun() -> command(View, Prompt, Mode, Key, Line, Number) end,
    case read_command(Prompt) of
        "c\n" when Mode =:= normal ->
            show_key(View, Key, 1, Number);
        "c\n" when is_tuple(Mode) ->
            search(View, Key, element(2, Mode), 1, Number);
        "q\n" ->
            ok;
        "k\n" ->
            kill(View);
        [$p | Digits] ->
            print_numbered(View, Mode, Digits),
            Again();
        [$/ | Pattern] ->
            start_search(View, Pattern, Again);
        eof ->
            ok;
        _ ->
            Again()
    end.

kill(#{table := Tid}) ->
    ets:delete(Tid),
    ok.

read_command(Prompt) ->
    case io:get_line(Prompt) of
        "\n" -> "c\n";
        <<"\n">> -> "c\n";
        Line when is_binary(Line) -> unicode:characters_to_list(Line);
        Line -> Line
    end.

%% The command text without its trailing newlines and dollar signs.
command_text(Text) ->
    string:trim(Text, trailing, "$\n").

print_numbered(#{table := Tid}, Mode, Digits) ->
    case catch list_to_integer(command_text(Digits)) of
        {'EXIT', _} ->
            io:put_chars("Bad digits\n");
        Wanted when is_tuple(Mode) ->
            catch print_match(Tid, ets:first(Tid), Wanted, element(2, Mode));
        Wanted ->
            catch print_object(Tid, ets:first(Tid), Wanted)
    end.

print_object(Tid, Key, Wanted) ->
    Objects = ets:lookup(Tid, Key),
    case Wanted =< length(Objects) of
        true -> io:format("~p~n", [lists:nth(Wanted, Objects)]);
        false -> print_object(Tid, ets:next(Tid, Key), Wanted - length(Objects))
    end.

print_match(_Tid, '$end_of_table', _Wanted, _Re) ->
    ok;
print_match(Tid, Key, Wanted, Re) ->
    Objects = [Object || Object <- ets:lookup(Tid, Key), matches(Object, Re)],
    case Wanted =< length(Objects) of
        true -> io:format("~p~n", [lists:nth(Wanted, Objects)]);
        false -> print_match(Tid, ets:next(Tid, Key), Wanted - length(Objects), Re)
    end.

start_search(#{table := Tid} = View, Pattern, Again) ->
    case re:compile(command_text(Pattern), [unicode]) of
        {ok, Re} ->
            search(View, ets:first(Tid), Re, 1, 1);
        {error, {Reason, _At}} ->
            io:format("~ts\n", [Reason]),
            Again()
    end.

%% Show the objects matching Re from Key on until the page is full.
search(View, '$end_of_table', Re, Line, Number) ->
    command(View, ?END_PROMPT, {re, Re}, '$end_of_table', Line, Number);
search(#{height := Height, table := Tid} = View, Key, Re, Line, Number) when Line < Height ->
    search_objects(View, Key, ets:lookup(Tid, Key), Re, Line, Number);
search(View, Key, Re, Line, Number) ->
    command(View, ?MORE_PROMPT, {re, Re}, Key, Line, Number).

search_objects(#{table := Tid} = View, Key, [], Re, Line, Number) ->
    search(View, ets:next(Tid, Key), Re, Line, Number);
search_objects(View, Key, [Object | Rest], Re, Line, Number) ->
    case matches(Object, Re) of
        true ->
            show_object(View, Object, Number),
            search_objects(View, Key, Rest, Re, Line + 1, Number + 1);
        false ->
            search_objects(View, Key, Rest, Re, Line, Number)
    end.

matches(Object, Re) ->
    re:run(printed(Object), Re, [{capture, none}]) =:= match.
