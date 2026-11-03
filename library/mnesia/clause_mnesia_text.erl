%% mnesia's text files (plan 21 step MIM1E, docs/libraries/mnesia.md#text-files): load_textfile/1 creates the tables a
%% file lists and writes its records, dump_to_textfile/1 writes this node's tables to such a file. The file is a
%% {tables, [{Tab, CreateOptions}]} term followed by records named after their tables; the messages and results are
%% OTP's.
-module(clause_mnesia_text).
-export([load_textfile/1, dump_to_textfile/1]).

% The file's tables are created unless they exist, and its records written in one transaction; mnesia starts first, on
% a new disc schema when its directory has none.
load_textfile(File) ->
    ensure_started(),
    case parse(File) of
        {ok, {Tabs, Records}} ->
            Failed = create_tables([checked_table(T) || T <- Tabs]),
            write_records([R || R <- Records, not lists:member(element(1, R), Failed)]);
        Error ->
            Error
    end.

ensure_started() ->
    case clause_mnesia_monitor:is_running() of
        yes ->
            yes;
        _ ->
            Schema = filename:join(clause_mnesia_monitor:get_env(dir), "schema.DAT"),
            filelib:is_regular(Schema) orelse mnesia:create_schema([node()]),
            mnesia:start()
    end.

% {Tab, Options}, or {Tab, RecordName, Options}, which the creation then refuses.
checked_table({_, _} = Tab) -> Tab;
checked_table({_, _, _} = Tab) -> Tab;
checked_table(_) -> throw({error, badtab}).

% The tables that could not be created, whose records are left out; existing tables just take the records.
create_tables([{Tab, Options} | Rest]) ->
    try mnesia:table_info(Tab, where_to_read) of
        Node ->
            io:format("** Table ~tw already exists on ~p, just entering data~n", [Tab, Node]),
            create_tables(Rest)
    catch
        exit:_ -> create_table(Tab, Options) ++ create_tables(Rest)
    end;
create_tables([]) ->
    [].

create_table(Tab, Options) ->
    case mnesia:create_table(Tab, Options) of
        {aborted, Reason} ->
            io:format("** Failed to create table ~tw ~n** Reason = ~tw, Args = ~tp~n", [Tab, Reason, Options]),
            [Tab];
        _ ->
            io:format("New table ~tw~n", [Tab]),
            []
    end.

write_records(Records) ->
    mnesia:transaction(fun() -> lists:foreach(fun write_record/1, Records) end).

write_record(Record) ->
    Tab = element(1, Record),
    mnesia:write(Tab, setelement(1, Record, mnesia:table_info(Tab, record_name)), write).

% The file's tables and records: {ok, {Tabs, Records}} or {error, Reason}, with OTP's messages.
parse(File) ->
    case read_file(File) of
        {ok, Terms} ->
            try
                {ok, collect(Terms)}
            catch
                throw:Error -> Error
            end;
        Error ->
            Error
    end.

collect([{_, {tables, Tabs}} | Terms]) ->
    {Tabs, [known_record(Tabs, T) || T <- Terms]};
collect(_) ->
    io:format("No tables found\n", []),
    throw({error, bad_header}).

% A record of a table the file lists.
known_record(Tabs, {Line, Record}) when is_tuple(Record) ->
    case lists:keymember(element(1, Record), 1, Tabs) of
        true ->
            Record;
        false ->
            io:format("Object:~tp at line ~w unknown\n", [Record, Line]),
            throw({error, undefined_object})
    end;
known_record(_, Term) ->
    io:format("Object:~tp unknown\n", [Term]),
    throw({error, undefined_object}).

% Each term of the file with the line it starts on.
read_file(File) ->
    case file:open(File, [read]) of
        {ok, Device} ->
            Result = read_terms(Device, File, 1, []),
            ok = file:close(Device),
            Result;
        _ ->
            {error, open}
    end.

read_terms(Device, File, Line, Terms) ->
    case io:request(Device, {get_until, latin1, '', erl_scan, tokens, [Line]}) of
        {ok, Tokens, Next} ->
            case parsed(erl_parse:parse_term(Tokens), File) of
                {ok, Term} -> read_terms(Device, File, Next, [{Line, Term} | Terms]);
                error -> {error, read}
            end;
        {eof, _} ->
            {ok, lists:reverse(Terms)};
        Other ->
            io:format("Error1 **~p~n", [Other]),
            {error, read}
    end.

parsed({ok, Term}, _) ->
    {ok, Term};
parsed({error, {Line, Module, Reason}}, File) ->
    io:format("Error in line:~p of:~tp ~ts\n", [Line, File, Module:format_error(Reason)]),
    error.

% This node's tables other than the schema, their record names and attributes first; error without a running mnesia.
dump_to_textfile(File) ->
    Opened = file:open(File, [write]),
    case {clause_mnesia_monitor:is_running(), Opened} of
        {yes, {ok, Device}} ->
            Tabs = [T || T <- clause_mnesia_monitor:val({schema, local_tables}), T =/= schema, active_here(T)],
            io:format(Device, "~p.~n", [{tables, [definition(T) || T <- Tabs]}]),
            [dump_table(Device, T) || T <- Tabs],
            file:close(Device);
        {_, {ok, Device}} ->
            ok = file:close(Device),
            error;
        _ ->
            error
    end.

active_here(Tab) ->
    clause_mnesia_monitor:val({Tab, where_to_read}) =:= node().

definition(Tab) ->
    Item = fun(I) -> clause_mnesia_monitor:val({Tab, I}) end,
    {Tab, [{record_name, Item(record_name)}, {attributes, Item(attributes)}]}.

dump_table(Device, Tab) ->
    Pattern = clause_mnesia_monitor:val({Tab, wild_pattern}),
    {atomic, Records} = mnesia:transaction(fun() -> mnesia:match_object(Tab, Pattern, read) end),
    [io:format(Device, "~p.~n", [setelement(1, R, Tab)]) || R <- Records].
