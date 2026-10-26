%% Clause's qlc (plan 21 step LIB73, docs/libraries/library.md): query list comprehensions over lists, ets, dets and qlc:table/2
%% tables, written from OTP's documentation and observed behavior. Compiled queries come from the qlc transform (the
%% host's when a module includes qlc.hrl, qlc_pt's for string_to_handle/1,2,3 and erl_eval) as {qlc_lc, Fun, Opt}
%% handles. Evaluation prepares a handle (constant lookups, match specifications, merge and lookup joins, caches,
%% unique answers), sets it up into a chain of answers and continuation funs and walks that chain; info/1,2 describes
%% the prepared handle as abstract code. Handles, cursors, ets tables, process dictionary keys and temporary files keep
%% OTP's shapes and names, since programs see them.
-module(qlc).
-export([
    all_selections/1,
    append/1,
    append/2,
    aux_name/3,
    cursor/1,
    cursor/2,
    delete_cursor/1,
    e/1,
    e/2,
    eval/1,
    eval/2,
    fold/3,
    fold/4,
    format_error/1,
    info/1,
    info/2,
    keysort/2,
    keysort/3,
    name_suffix/2,
    next_answers/1,
    next_answers/2,
    parse_transform/2,
    parse_transform_info/0,
    q/1,
    q/2,
    sort/1,
    sort/2,
    string_to_handle/1,
    string_to_handle/2,
    string_to_handle/3,
    table/2,
    template_state/0,
    transform_from_evaluator/2,
    var_fold/3,
    var_ufold/2,
    vars/1
]).
-include("qlc.types.hrl").

%% Lists cached by {cache, list} spill to a temporary file above this many bytes, as do large merge join classes.
-define(MAX_LIST_SIZE, 512 * 1024).
-define(DEFAULT_NUM_OF_ANSWERS, 10).
%% The state of the template in the generated query fun.
-define(TEMPLATE_STATE, 1).
%% The goto table entry of a skipped filter or an unused generator.
-define(SKIP, (-1)).
%% The pattern variable of the simple queries inserted to hold cache and unique options.
-define(SIMPLE_QVAR, 'SQV').
-define(CHUNK_SIZE, 64 * 1024).
%% The tag of errors the evaluation throws to its caller; eval/2, fold/4, info/2 and cursors return the error.
-define(THROWN, '$qlc_thrown_error').
-define(LCACHE_FILE(Ref), {Ref, '$_qlc_cache_tmpfiles_'}).
-define(MERGE_JOIN_FILE, '$_qlc_merge_join_tmpfiles_').
-define(JWRAP(E1, E2), [E1 | E2]).
-define(QLC_Q(A1, A2, A3, A4, LC, Os), {call, A1, {remote, A2, {atom, A3, qlc}, {atom, A4, q}}, [LC | Os]}).

%% Query handles: {qlc_handle, H} with H one of the records below, as OTP builds them.
-record(qlc_handle, {h}).
-record(qlc_append, {hl}).
-record(qlc_table, {
    trav_fun,
    trav_MS,
    pre_fun,
    post_fun,
    info_fun,
    format_fun,
    lookup_fun,
    parent_fun,
    key_equality,
    lu_vals,
    ms = no_match_spec
}).
-record(qlc_sort, {h, keypos, unique, compressed, order, fs_opts, tmpdir_usage = allowed, tmpdir}).
-record(qlc_lc, {lc, opt}).
-record(qlc_cursor, {c}).
-record(qlc_opt, {
    unique = false,
    cache = false,
    max_lookup = -1,
    join = any,
    tmpdir = "",
    lookup = any,
    max_list = ?MAX_LIST_SIZE,
    tmpdir_usage = allowed
}).

%% Prepared handles. qlc_list is a list with an optional match specification; qlc_join a chosen merge or lookup join.
-record(qlc_list, {l, ms = no_match_spec}).
-record(qlc_join, {kind, opt, q1, c1, q2, c2}).
%% The cache and unique work of one prepared query, the join option and the chosen fast join.
-record(optz, {unique = false, cache = false, join_option = any, fast_join = no, opt}).
%% A prepared qlc_v1 query: the generated query fun, the code fun for info/2, the qualifiers with prepared generators.
-record(qlc, {lcf, codef, qdata, init_value, optz}).
%% A prepared simple_v1 query: a pattern variable over one list expression.
-record(simple_qlc, {p, le, anno, init_value, optz}).
%% A prepared handle with what preparation learned about its answers.
-record(prepared, {
    qh,
    sorted = no,
    sort_info = [],
    sort_info2 = [],
    lu_skip_quals = [],
    join = {[], []},
    is_unique_objects = false,
    is_cached = false
}).
%% The process that evaluates (the cursor's owner when a cursor does).
-record(setup, {parent}).
%% A join generator of the generated qualifier data.
-record(join, {op, q1, q2, wh1, wh2, cs_fun}).
%% One merge join: its temporary file id and the limits of keeping equal keys in memory.
-record(m, {id, tmpdir, max_list, tmp_usage}).

%%% Query handles

append(QHs) ->
    Hs = [
        case get_handle(QH) of
            badarg -> erlang:error(badarg, [QHs]);
            H -> H
        end
     || QH <- QHs
    ],
    #qlc_handle{h = #qlc_append{hl = Hs}}.

append(QH1, QH2) ->
    Hs = [
        case get_handle(QH) of
            badarg -> erlang:error(badarg, [QH1, QH2]);
            H -> H
        end
     || QH <- [QH1, QH2]
    ],
    #qlc_handle{h = #qlc_append{hl = Hs}}.

q(QLC) ->
    q(QLC, []).

q(#qlc_lc{} = QLC, Options) ->
    case options(Options, [unique, cache, max_lookup, join, lookup]) of
        [Unique, Cache, Max, Join, Lookup] ->
            Opt = #qlc_opt{unique = Unique, cache = Cache, max_lookup = Max, join = Join, lookup = Lookup},
            #qlc_handle{h = QLC#qlc_lc{opt = Opt}};
        _ ->
            erlang:error(badarg, [QLC, Options])
    end;
q(T1, T2) ->
    erlang:error(badarg, [T1, T2]).

sort(QH) ->
    sort(QH, []).

sort(QH, Options) ->
    case {options(Options, [tmpdir, order, unique, compressed, size, no_files]), get_handle(QH)} of
        {B1, B2} when B1 =:= badarg; B2 =:= badarg ->
            erlang:error(badarg, [QH, Options]);
        {[TmpDir, Order, Unique, Compressed | _], H} ->
            Sort = #qlc_sort{
                h = H,
                keypos = sort,
                unique = Unique,
                compressed = Compressed,
                order = Order,
                fs_opts = listify(Options),
                tmpdir = TmpDir
            },
            #qlc_handle{h = Sort}
    end.

keysort(KeyPos, QH) ->
    keysort(KeyPos, QH, []).

keysort(KeyPos, QH, Options) ->
    case {is_keypos(KeyPos), options(Options, [tmpdir, order, unique, compressed, size, no_files]), get_handle(QH)} of
        {true, [TmpDir, Order, Unique, Compressed | _], H} when H =/= badarg ->
            Sort = #qlc_sort{
                h = H,
                keypos = {keysort, KeyPos},
                unique = Unique,
                compressed = Compressed,
                order = Order,
                fs_opts = listify(Options),
                tmpdir = TmpDir
            },
            #qlc_handle{h = Sort};
        _ ->
            erlang:error(badarg, [KeyPos, QH, Options])
    end.

table(TraverseFun, Options) when is_function(TraverseFun) ->
    case is_function(TraverseFun, 0) orelse is_function(TraverseFun, 1) of
        false ->
            erlang:error(badarg, [TraverseFun, Options]);
        true ->
            Keys = [pre_fun, post_fun, info_fun, format_fun, lookup_fun, parent_fun, key_equality],
            case options(Options, Keys) of
                [PreFun, PostFun, InfoFun, FormatFun, LookupFun, ParentFun, KeyEquality] ->
                    Table = #qlc_table{
                        trav_fun = TraverseFun,
                        trav_MS = is_function(TraverseFun, 1),
                        pre_fun = PreFun,
                        post_fun = PostFun,
                        info_fun = InfoFun,
                        format_fun = FormatFun,
                        lookup_fun = LookupFun,
                        parent_fun = ParentFun,
                        key_equality = KeyEquality
                    },
                    #qlc_handle{h = Table};
                badarg ->
                    erlang:error(badarg, [TraverseFun, Options])
            end
    end;
table(T1, T2) ->
    erlang:error(badarg, [T1, T2]).

string_to_handle(String) ->
    string_to_handle(String, []).

string_to_handle(String, Options) ->
    string_to_handle(String, Options, erl_eval:new_bindings()).

%% The query of a string, transformed by qlc_pt and evaluated by erl_eval; errors come back as {error, Module, Info}.
string_to_handle(String, Options, Bindings) when is_list(String) ->
    case options(Options, [unique, cache, max_lookup, join, lookup]) of
        badarg ->
            erlang:error(badarg, [String, Options, Bindings]);
        [Unique, Cache, MaxLookup, Join, Lookup] ->
            Opt = #qlc_opt{unique = Unique, cache = Cache, max_lookup = MaxLookup, join = Join, lookup = Lookup},
            case string_expression(String, Options, Bindings) of
                {ok, Expr} -> string_query(Expr, Bindings, Opt);
                Error -> Error
            end
    end;
string_to_handle(T1, T2, T3) ->
    erlang:error(badarg, [T1, T2, T3]).

%% The one expression of a query string.
string_expression(String, Options, Bindings) ->
    case erl_scan:string(String, 1, [text]) of
        {ok, Tokens, _} ->
            case erl_eval:extended_parse_exprs(Tokens) of
                {ok, [Expr]} -> {ok, Expr};
                {ok, _Exprs} -> erlang:error(badarg, [String, Options, Bindings]);
                {error, ErrorInfo} -> qlc_error(ErrorInfo)
            end;
        {error, ErrorInfo, _EndLocation} ->
            qlc_error(ErrorInfo)
    end.

string_query(Expr, Bindings, Opt) ->
    case qlc_pt:transform_expression(Expr, Bindings) of
        {ok, {call, _, _QlcQ, Handle}} ->
            {value, QLC, _} = erl_eval:exprs(Handle, Bindings),
            #qlc_handle{h = QLC#qlc_lc{opt = Opt}};
        {not_ok, [{error, Error} | _]} ->
            qlc_error(Error)
    end.

%%% Evaluation

e(QH) ->
    eval(QH, []).

e(QH, Options) ->
    eval(QH, Options).

eval(QH) ->
    eval(QH, []).

eval(QH, Options) ->
    case {options(Options, [unique_all, cache_all, tmpdir, max_list_size, tmpdir_usage]), get_handle(QH)} of
        {B1, B2} when B1 =:= badarg; B2 =:= badarg ->
            erlang:error(badarg, [QH, Options]);
        {[GUnique, GCache, TmpDir, MaxList, TmpUsage], H} ->
            returning_errors(fun() ->
                Prep = prepare_qlc(H, [], GUnique, GCache, TmpDir, MaxList, TmpUsage),
                case setup_qlc(Prep, #setup{parent = self()}) of
                    {L, Post, _LocalPost} when is_list(L) ->
                        post_funs(Post),
                        L;
                    {Objs, Post, _LocalPost} when is_function(Objs) ->
                        try
                            collect(Objs)
                        after
                            post_funs(Post)
                        end
                end
            end)
    end.

fold(Fun, Acc0, QH) ->
    fold(Fun, Acc0, QH, []).

fold(Fun, Acc0, QH, Options) ->
    case {options(Options, [unique_all, cache_all, tmpdir, max_list_size, tmpdir_usage]), get_handle(QH)} of
        {B1, B2} when B1 =:= badarg; B2 =:= badarg ->
            erlang:error(badarg, [Fun, Acc0, QH, Options]);
        {[GUnique, GCache, TmpDir, MaxList, TmpUsage], H} ->
            returning_errors(fun() ->
                Prep = prepare_qlc(H, not_a_list, GUnique, GCache, TmpDir, MaxList, TmpUsage),
                case setup_qlc(Prep, #setup{parent = self()}) of
                    {Objs, Post, _LocalPost} when is_function(Objs); is_list(Objs) ->
                        try
                            fold_loop(Fun, Objs, Acc0)
                        after
                            post_funs(Post)
                        end
                end
            end)
    end.

%% The value of Evaluate, or the error the evaluation threw.
returning_errors(Evaluate) ->
    try
        Evaluate()
    catch
        throw:{?THROWN, Error} -> Error
    end.

collect([] = L) ->
    L;
collect([Answer | Cont]) ->
    [Answer | collect(Cont)];
collect(Cont) ->
    case Cont() of
        Answers when is_list(Answers) -> collect(Answers);
        Term -> throw_error(Term)
    end.

fold_loop(Fun, [Obj | Cont], Acc) ->
    fold_loop(Fun, Cont, Fun(Obj, Acc));
fold_loop(_Fun, [], Acc) ->
    Acc;
fold_loop(Fun, Cont, Acc) ->
    case Cont() of
        Objects when is_list(Objects) -> fold_loop(Fun, Objects, Acc);
        Term -> Term
    end.

%%% Cursors

cursor(QH) ->
    cursor(QH, []).

cursor(QH, Options) ->
    Keys = [unique_all, cache_all, tmpdir, spawn_options, max_list_size, tmpdir_usage],
    case {options(Options, Keys), get_handle(QH)} of
        {B1, B2} when B1 =:= badarg; B2 =:= badarg ->
            erlang:error(badarg, [QH, Options]);
        {[GUnique, GCache, TmpDir, SpawnOptions, MaxList, TmpUsage], H} ->
            Settings = {GUnique, GCache, TmpDir, MaxList, TmpUsage},
            case cursor_process(H, Settings, spawn_options(SpawnOptions)) of
                Pid when is_pid(Pid) -> #qlc_cursor{c = {Pid, self()}};
                Error -> Error
            end
    end.

delete_cursor(#qlc_cursor{c = {_, Owner}} = C) when Owner =/= self() ->
    erlang:error(not_cursor_owner, [C]);
delete_cursor(#qlc_cursor{c = {Pid, _}}) ->
    stop_cursor(Pid);
delete_cursor(T) ->
    erlang:error(badarg, [T]).

next_answers(C) ->
    next_answers(C, ?DEFAULT_NUM_OF_ANSWERS).

next_answers(#qlc_cursor{c = {_, Owner}} = C, NumOfAnswers) when Owner =/= self() ->
    erlang:error(not_cursor_owner, [C, NumOfAnswers]);
next_answers(#qlc_cursor{c = {Pid, _}} = C, NumOfAnswers) ->
    N =
        case NumOfAnswers of
            all_remaining -> -1;
            _ when is_integer(NumOfAnswers), NumOfAnswers > 0 -> NumOfAnswers;
            _ -> erlang:error(badarg, [C, NumOfAnswers])
        end,
    next_loop(Pid, [], N);
next_answers(T1, T2) ->
    erlang:error(badarg, [T1, T2]).

%% Spawn the cursor process, which prepares and sets up the query, then answers its owner's requests one answer at a
%% time. The owner runs the parent funs of tables meanwhile.
cursor_process(H, {GUnique, GCache, TmpDir, MaxList, TmpUsage}, SpawnOptions) ->
    Parent = self(),
    Setup = #setup{parent = Parent},
    Evaluate = fun() ->
        %% Cleanup after the owner exits needs trapped exits; the user is assumed not to undo it.
        process_flag(trap_exit, true),
        MonRef = erlang:monitor(process, Parent),
        {Objs, Post, _LocalPost} =
            try
                Prep = prepare_qlc(H, not_a_list, GUnique, GCache, TmpDir, MaxList, TmpUsage),
                setup_qlc(Prep, Setup)
            catch
                Class:Reason:Stacktrace ->
                    Parent ! {self(), {caught, Class, Reason, Stacktrace}},
                    exit(normal)
            end,
        Parent ! {self(), ok},
        wait_for_request(Parent, MonRef, Post),
        reply(Parent, MonRef, Post, Objs)
    end,
    Pid = spawn_opt(Evaluate, SpawnOptions),
    parent_fun(Pid, Parent).

%% Answer the parent fun requests of the cursor's tables until the cursor is set up.
parent_fun(Pid, Parent) ->
    receive
        {Pid, ok} ->
            Pid;
        {TPid, {parent_fun, Fun}} ->
            V =
                try
                    {value, Fun()}
                catch
                    Class:Reason:Stacktrace -> {parent_fun_caught, Class, Reason, Stacktrace}
                end,
            TPid ! {Parent, V},
            parent_fun(Pid, Parent);
        {Pid, {caught, throw, {?THROWN, Error}, _Stacktrace}} ->
            Error;
        {Pid, {caught, Class, Reason, Stacktrace}} ->
            erlang:raise(Class, Reason, Stacktrace)
    end.

reply(Parent, MonRef, Post, []) ->
    no_more(Parent, MonRef, Post);
reply(Parent, MonRef, Post, [Answer | Cont]) ->
    Parent ! {self(), {answer, Answer}},
    wait_for_request(Parent, MonRef, Post),
    reply(Parent, MonRef, Post, Cont);
reply(Parent, MonRef, Post, Cont) ->
    Reply =
        try
            case is_function(Cont) of
                true -> Cont();
                false -> throw_error(Cont)
            end
        catch
            Class:Reason:Stacktrace ->
                post_funs(Post),
                Parent ! {self(), {caught, Class, Reason, Stacktrace}},
                exit(normal)
        end,
    reply(Parent, MonRef, Post, Reply).

no_more(Parent, MonRef, Post) ->
    Parent ! {self(), no_more},
    wait_for_request(Parent, MonRef, Post),
    no_more(Parent, MonRef, Post).

wait_for_request(Parent, MonRef, Post) ->
    receive
        {Parent, stop} ->
            post_funs(Post),
            exit(normal);
        {Parent, more} ->
            ok;
        {'EXIT', Parent, _Reason} ->
            post_funs(Post),
            exit(normal);
        {'DOWN', MonRef, process, Parent, _Info} ->
            post_funs(Post),
            exit(normal);
        {'EXIT', Pid, _Reason} when Pid =:= self() ->
            %% A trapped signal of the cursor itself is ignored.
            wait_for_request(Parent, MonRef, Post);
        Other ->
            error_logger:error_msg("The qlc cursor ~w received an unexpected message:\n~tp\n", [self(), Other]),
            wait_for_request(Parent, MonRef, Post)
    end.

next_loop(Pid, L, N) when N =/= 0 ->
    case monitor_request(Pid, more) of
        no_more ->
            lists:reverse(L);
        {answer, Answer} ->
            next_loop(Pid, [Answer | L], N - 1);
        {caught, throw, {?THROWN, Error}, _Stacktrace} ->
            Error;
        {caught, Class, Reason, Stacktrace} ->
            erlang:raise(Class, Reason, Stacktrace ++ current_stacktrace());
        error ->
            erlang:error({qlc_cursor_pid_no_longer_exists, Pid})
    end;
next_loop(_Pid, L, _N) ->
    lists:reverse(L).

%% The caller's stack for a cursor's exception, from a caught one: process_info/2 has no current_stacktrace here.
current_stacktrace() ->
    try
        erlang:error(stacktrace)
    catch
        error:stacktrace:Stacktrace ->
            lists:dropwhile(fun({M, F, _, _}) -> {M, F} =:= {?MODULE, current_stacktrace} end, Stacktrace)
    end.

stop_cursor(Pid) ->
    erlang:monitor(process, Pid),
    unlink(Pid),
    receive
        {'EXIT', Pid, _Reason} ->
            receive
                {'DOWN', _, process, Pid, _} -> ok
            end
    after 0 ->
        Pid ! {self(), stop},
        receive
            {'DOWN', _, process, Pid, _} -> ok
        end
    end.

%% Send a request to Pid and wait for its reply; error when Pid is gone.
monitor_request(Pid, Request) ->
    Ref = erlang:monitor(process, Pid),
    Pid ! {self(), Request},
    receive
        {'DOWN', Ref, process, Pid, _Info} ->
            receive
                {'EXIT', Pid, _Reason} -> ok
            after 1 -> ok
            end,
            error;
        {'EXIT', Pid, _Reason} ->
            receive
                {'DOWN', _, process, Pid, _} -> error
            end;
        {Pid, Reply} ->
            erlang:demonitor(Ref, [flush]),
            Reply
    end.

%%% Errors, info and the transform

format_error(not_a_query_list_comprehension) ->
    io_lib:format("argument is not a query list comprehension", []);
format_error({used_generator_variable, V}) ->
    io_lib:format("generated variable ~w must not be used in list expression", [V]);
format_error(binary_generator) ->
    io_lib:format("cannot handle binary generators", []);
format_error(too_complex_join) ->
    io_lib:format("cannot handle join of three or more generators efficiently", []);
format_error(too_many_joins) ->
    io_lib:format("cannot handle more than one join efficiently", []);
format_error(nomatch_pattern) ->
    io_lib:format("pattern cannot possibly match", []);
format_error(nomatch_filter) ->
    io_lib:format("filter evaluates to 'false'", []);
format_error({Location, Mod, Reason}) when is_integer(Location); tuple_size(Location) =:= 2 ->
    io_lib:format("~s: ~ts~n", [location_text(Location), lists:flatten(Mod:format_error(Reason))]);
format_error({bad_object, FileName}) ->
    io_lib:format("the temporary file \"~ts\" holding answers is corrupt", [FileName]);
format_error(bad_object) ->
    io_lib:format("the keys could not be extracted from some term", []);
format_error({file_error, FileName, Reason}) ->
    io_lib:format("\"~ts\": ~tp~n", [FileName, file:format_error(Reason)]);
format_error({premature_eof, FileName}) ->
    io_lib:format("\"~ts\": end-of-file was encountered inside some binary term", [FileName]);
format_error({tmpdir_usage, Why}) ->
    io_lib:format("temporary file was needed for ~w~n", [Why]);
format_error({error, Module, Reason}) ->
    Module:format_error(Reason);
format_error(E) ->
    io_lib:format("~tp~n", [E]).

location_text({Line, Column}) -> io_lib:format("~w:~w", [Line, Column]);
location_text(Line) -> io_lib:format("~w", [Line]).

info(QH) ->
    info(QH, []).

info(QH, Options) ->
    Keys = [unique_all, cache_all, flat, format, n_elements, depth, tmpdir, max_list_size, tmpdir_usage],
    case {options(Options, Keys), get_handle(QH)} of
        {B1, B2} when B1 =:= badarg; B2 =:= badarg ->
            erlang:error(badarg, [QH, Options]);
        {[GUnique, GCache, Flat, Format, NElements, Depth, TmpDir, MaxList, TmpUsage], H} ->
            returning_errors(fun() ->
                Prep = prepare_qlc(H, [], GUnique, GCache, TmpDir, MaxList, TmpUsage),
                Info = le_info(Prep, {NElements, Depth}),
                formatted_info(Info, abstract(Info, Flat, NElements, Depth), Format)
            end)
    end.

%% info/2's answer in the requested format; debug (undocumented, for tests) is the intermediate description.
formatted_info(_Info, AbstractCode, abstract_code) ->
    abstract_code(AbstractCode);
formatted_info(_Info, AbstractCode, string) ->
    Hook = fun({special, _Anno, String}, _I, _P, _F) -> String end,
    lists:flatten(erl_pp:expr(AbstractCode, 0, Hook));
formatted_info(Info, _AbstractCode, debug) ->
    Info.

parse_transform(Forms, Options) ->
    qlc_pt:parse_transform(Forms, Options).

parse_transform_info() ->
    #{error_location => column}.

transform_from_evaluator(LC, Bindings) ->
    qlc_pt:transform_from_evaluator(LC, Bindings).

%%% Helpers OTP's transform calls (undocumented exports)

template_state() ->
    ?TEMPLATE_STATE.

aux_name(Name, N, AllNames) ->
    {VN, _} = aux_name1(Name, N, AllNames),
    VN.

name_suffix(A, Suffix) ->
    list_to_atom(lists:concat([A, Suffix])).

vars(E) ->
    var_ufold(fun({var, _A, V}) -> V end, E).

var_ufold(F, E) ->
    ordsets:from_list(var_fold(F, [], E)).

var_fold(F, A, {var, _, V} = Var) when V =/= '_' ->
    [F(Var) | A];
var_fold(F, A, T) when is_tuple(T) ->
    var_fold(F, A, tuple_to_list(T));
var_fold(F, A, [E | Es]) ->
    var_fold(F, var_fold(F, A, E), Es);
var_fold(_F, A, _T) ->
    A.

all_selections([]) ->
    [[]];
all_selections([{I, Cs} | ICs]) ->
    [[{I, C} | L] || C <- Cs, L <- all_selections(ICs)].

%% The first name Name<N>, Name<N+1>, ... not in AllNames, with its number.
aux_name1(Name, N, AllNames) ->
    SN = name_suffix(Name, N),
    case gb_sets:is_member(SN, AllNames) of
        true -> aux_name1(Name, N + 1, AllNames);
        false -> {SN, N}
    end.

%%% Options

get_handle(#qlc_handle{h = #qlc_lc{opt = {qlc_opt, U, C, M}} = H}) ->
    %% The options of an R11B-0 handle.
    H#qlc_lc{opt = #qlc_opt{unique = U, cache = C, max_lookup = M}};
get_handle(#qlc_handle{h = H}) ->
    H;
get_handle(L) when is_list(L) ->
    L;
get_handle(_) ->
    badarg.

%% The values of Keys in Options (defaults for absent keys, atoms as their true options), or badarg when an option
%% is bad, unknown or repeated.
options(Options, Keys) when is_list(Options) ->
    options(Options, Keys, []);
options(Option, Keys) ->
    options([Option], Keys, []).

options(Options0, [Key | Keys], Values) when is_list(Options0) ->
    Options =
        case lists:member(Key, Options0) of
            true -> [atom_option(Key) | lists:delete(Key, Options0)];
            false -> Options0
        end,
    case option_value(Key, lists:keyfind(Key, 1, Options)) of
        badarg -> badarg;
        {ok, Value} -> options(lists:keydelete(Key, 1, Options), Keys, [Value | Values])
    end;
options([], [], Values) ->
    lists:reverse(Values);
options(_Options, _Keys, _Values) ->
    badarg.

option_value(Key, false) ->
    {ok, default_option(Key)};
option_value(Key, {Key, Value}) ->
    case valid_option(Key, Value) of
        true -> {ok, option_internal(Key, Value)};
        false -> badarg
    end;
option_value(_Key, Option) ->
    erlang:error({case_clause, Option}).

%% Whether Value is allowed for Key.
valid_option(Key, undefined) when
    Key =:= format_fun; Key =:= info_fun; Key =:= lookup_fun; Key =:= parent_fun; Key =:= post_fun; Key =:= pre_fun
->
    true;
valid_option(info_fun, Fun) ->
    is_function(Fun, 1);
valid_option(pre_fun, Fun) ->
    is_function(Fun, 1);
valid_option(post_fun, Fun) ->
    is_function(Fun, 0);
valid_option(lookup_fun, Fun) ->
    is_function(Fun, 2);
valid_option(format_fun, Fun) ->
    is_function(Fun, 1);
valid_option(parent_fun, Fun) ->
    is_function(Fun, 0);
valid_option(max_lookup, Max) ->
    Max =:= infinity orelse (is_integer(Max) andalso Max >= 0);
valid_option(key_equality, KE) ->
    KE =:= '==' orelse KE =:= '=:=';
valid_option(join, Join) ->
    lists:member(Join, [any, nested_loop, merge, lookup]);
valid_option(lookup, Lookup) ->
    is_boolean(Lookup) orelse Lookup =:= any;
valid_option(max_list_size, Max) ->
    is_integer(Max) andalso Max >= 0;
valid_option(tmpdir_usage, Usage) ->
    lists:member(Usage, [allowed, not_allowed, info_msg, warning_msg, error_msg]);
valid_option(unique, Unique) ->
    is_boolean(Unique);
valid_option(cache, Cache) ->
    is_boolean(Cache) orelse lists:member(Cache, [list, ets, no]);
valid_option(unique_all, Unique) ->
    is_boolean(Unique);
valid_option(cache_all, Cache) ->
    is_boolean(Cache) orelse lists:member(Cache, [list, ets, no]);
valid_option(spawn_options, Spawn) ->
    Spawn =:= default orelse is_proper_list(Spawn);
valid_option(flat, Flat) ->
    is_boolean(Flat);
valid_option(format, Format) ->
    lists:member(Format, [string, abstract_code, debug]);
valid_option(n_elements, N) ->
    N =:= infinity orelse (is_integer(N) andalso N > 0);
valid_option(depth, Depth) ->
    Depth =:= infinity orelse (is_integer(Depth) andalso Depth >= 0);
valid_option(order, Order) ->
    is_function(Order, 2) orelse Order =:= ascending orelse Order =:= descending;
valid_option(compressed, Compressed) ->
    is_boolean(Compressed);
valid_option(tmpdir, _TmpDir) ->
    true;
valid_option(size, Size) ->
    is_integer(Size) andalso Size > 0;
valid_option(no_files, NoFiles) ->
    is_integer(NoFiles) andalso NoFiles > 1;
valid_option(_Key, _Value) ->
    false.

%% The internal value of a valid option.
option_internal(max_lookup, infinity) -> -1;
option_internal(Key, ets) when Key =:= cache; Key =:= cache_all -> true;
option_internal(Key, no) when Key =:= cache; Key =:= cache_all -> false;
option_internal(compressed, true) -> [compressed];
option_internal(compressed, false) -> [];
option_internal(_Key, Value) -> Value.

default_option(pre_fun) -> undefined;
default_option(post_fun) -> undefined;
default_option(info_fun) -> undefined;
default_option(format_fun) -> undefined;
default_option(lookup_fun) -> undefined;
default_option(max_lookup) -> -1;
default_option(join) -> any;
default_option(lookup) -> any;
default_option(parent_fun) -> undefined;
default_option(key_equality) -> '=:=';
default_option(spawn_options) -> default;
default_option(flat) -> true;
default_option(format) -> string;
default_option(n_elements) -> infinity;
default_option(depth) -> infinity;
default_option(max_list_size) -> ?MAX_LIST_SIZE;
default_option(tmpdir_usage) -> allowed;
default_option(cache) -> false;
default_option(cache_all) -> false;
default_option(unique) -> false;
default_option(unique_all) -> false;
%% The file_sorter defaults.
default_option(order) -> ascending;
default_option(compressed) -> [];
default_option(tmpdir) -> "";
default_option(size) -> 524288;
default_option(no_files) -> 16.

atom_option(cache) -> {cache, true};
atom_option(unique) -> {unique, true};
atom_option(cache_all) -> {cache_all, true};
atom_option(unique_all) -> {unique_all, true};
atom_option(lookup) -> {lookup, true};
atom_option(flat) -> {flat, true};
atom_option(Key) -> Key.

is_proper_list([_ | L]) -> is_proper_list(L);
is_proper_list(L) -> L =:= [].

spawn_options(default) ->
    [link];
spawn_options(SpawnOptions) ->
    Linked =
        case lists:member(link, SpawnOptions) of
            true -> SpawnOptions;
            false -> [link | SpawnOptions]
        end,
    lists:delete(monitor, Linked).

is_keypos(KeyPos) when is_integer(KeyPos), KeyPos > 0 -> true;
is_keypos([]) -> false;
is_keypos(L) -> is_keyposs(L).

is_keyposs([KeyPos | KeyPoss]) when is_integer(KeyPos), KeyPos > 0 -> is_keyposs(KeyPoss);
is_keyposs(KeyPoss) -> KeyPoss =:= [].

listify(L) when is_list(L) -> L;
listify(T) -> [T].

%%% Preparation: what each handle evaluates to, and which lookups, match specifications and joins apply

prepare_qlc(H, InitialValue, GUnique, GCache, TmpDir, MaxList, TmpUsage) ->
    GOpt = #qlc_opt{unique = GUnique, cache = GCache, tmpdir = TmpDir, max_list = MaxList, tmpdir_usage = TmpUsage},
    case opt_le(prep_le(H, GOpt), 1) of
        #prepared{qh = #qlc{} = QLC} = Prep ->
            Prep#prepared{qh = QLC#qlc{init_value = InitialValue}};
        #prepared{qh = #simple_qlc{} = SimpleQLC} = Prep ->
            Prep#prepared{qh = SimpleQLC#simple_qlc{init_value = InitialValue}};
        Prep ->
            Prep
    end.

%% Prepare one handle under the global options: whether its answers are unique and cached, and the lists the list
%% expressions of queries evaluate to.
prep_le(#qlc_lc{lc = LCFun, opt = #qlc_opt{} = Opt0} = H, GOpt) ->
    #qlc_opt{unique = GUnique, cache = GCache, tmpdir = TmpDir, max_list = MaxList, tmpdir_usage = TmpUsage} = GOpt,
    Cache =
        case GCache of
            false -> Opt0#qlc_opt.cache;
            _ -> GCache
        end,
    Opt = Opt0#qlc_opt{
        unique = Opt0#qlc_opt.unique orelse GUnique,
        cache = Cache,
        tmpdir = TmpDir,
        max_list = MaxList,
        tmpdir_usage = TmpUsage
    },
    prep_qlc_lc(LCFun(), Opt, GOpt, H);
prep_le(#qlc_table{info_fun = IF} = T, GOpt) ->
    {SortInfo, Sorted} = table_sort_info(T),
    IsUnique = grd(IF, is_unique_objects),
    Prep = #prepared{qh = T, sort_info = SortInfo, sorted = Sorted, is_unique_objects = IsUnique},
    Opt =
        case (IsUnique orelse not GOpt#qlc_opt.unique) andalso T#qlc_table.ms =:= no_match_spec of
            true -> GOpt#qlc_opt{cache = false};
            false -> GOpt
        end,
    may_create_simple(Opt, Prep);
prep_le(#qlc_append{hl = HL}, GOpt) ->
    Flat = lists:flatmap(fun append_member/1, [prep_le(H, GOpt) || H <- HL]),
    case Flat of
        [] ->
            short_list([]);
        [Prep] ->
            Prep;
        PrepL ->
            Cache = lists:all(fun(#prepared{is_cached = IsCached}) -> IsCached =/= false end, PrepL),
            may_create_simple(GOpt, #prepared{qh = #qlc_append{hl = PrepL}, is_cached = Cache})
    end;
prep_le(#qlc_sort{h = H} = Sort, GOpt) ->
    prep_sort(Sort#qlc_sort{h = prep_le(H, GOpt)}, GOpt);
prep_le([_, _ | _] = L, GOpt) ->
    Prep = #prepared{qh = #qlc_list{l = L}, is_cached = true},
    Opt =
        case GOpt#qlc_opt.unique of
            false -> GOpt#qlc_opt{cache = false};
            true -> GOpt
        end,
    may_create_simple(Opt, Prep);
prep_le(L, _GOpt) when is_list(L) ->
    short_list(L);
prep_le(T, _GOpt) ->
    erlang:error({unsupported_qlc_handle, #qlc_handle{h = T}}).

%% The prepared handles one prepared member of an append contributes: none for [], the members of a nested append.
append_member(#prepared{qh = #qlc_list{l = []}}) -> [];
append_member(#prepared{qh = #qlc_append{hl = HL}}) -> HL;
append_member(Prep) -> [Prep].

%% Evaluate the list expression of a generator and prepare its handle.
eval_le(LEFun, GOpt) ->
    case LEFun() of
        {error, qlc, _} = Error ->
            throw_error(Error);
        R ->
            case get_handle(R) of
                badarg -> erlang:error(badarg, [R]);
                H -> prep_le(H, GOpt)
            end
    end.

prep_qlc_lc({simple_v1, PVar, LEFun, Location}, Opt, GOpt, _H) ->
    check_lookup_option(Opt, false),
    prep_simple_qlc(PVar, anno(Location), eval_le(LEFun, GOpt), Opt);
prep_qlc_lc({qlc_v1, QFun, CodeF, Qdata0, QOpt}, Opt, GOpt, _H) ->
    PrepareGenerator = fun
        ({_QNum, _GoI, _SI, fil} = QualData, ModGens) ->
            {QualData, ModGens};
        ({_QNum, _GoI, _SI, {gen, #join{}}} = QualData, ModGens) ->
            {QualData, ModGens};
        ({QNum, GoI, SI, {gen, LEFun}}, ModGens0) ->
            {Prep, ModGens} = prep_generator(QNum, eval_le(LEFun, GOpt), QOpt, Opt, ModGens0),
            {{QNum, GoI, SI, {gen, Prep}}, ModGens}
    end,
    {Qdata, ModGens} = lists:mapfoldl(PrepareGenerator, [], Qdata0),
    check_lookup_option(Opt, lists:keymember(true, 2, ModGens)),
    case ModGens of
        [{_QNum, _LookUp, all, OnePrep}] ->
            check_join_option(Opt),
            OnePrep;
        _ ->
            Prep0 = prep_qlc(QFun, CodeF, Qdata, QOpt, Opt),
            LUSkipQuals = [{QNum, Fs} || {QNum, _LookUp, Fs, _Prep} <- ModGens],
            prep_join(Prep0#prepared{lu_skip_quals = LUSkipQuals}, QOpt, Opt)
    end;
prep_qlc_lc(_, _Opt, _GOpt, H) ->
    erlang:error({unsupported_qlc_handle, #qlc_handle{h = H}}).

%% Apply the lookups and match specification the transform found for generator QNum; a modified generator is noted
%% with the filters it replaces or makes skippable.
prep_generator(QNum, Prep0, QOpt, Opt, ModGens) ->
    PosFun = fun(KeyEquality) -> pos_fun(KeyEquality, QOpt, QNum) end,
    MSFs =
        case match_specs(QOpt, QNum) of
            undefined -> {no_match_spec, []};
            {_, _} = Found -> Found
        end,
    case prep_gen(Prep0#prepared.qh, Prep0, PosFun, MSFs, Opt) of
        {replace, Fs, LookUp, Prep} -> {Prep, [{QNum, LookUp, Fs, Prep} | ModGens]};
        {skip, SkipFils, LookUp, Prep} -> {Prep, [{QNum, LookUp, SkipFils, Prep} | ModGens]};
        {no, _Fs, _LookUp, Prep} -> {Prep, ModGens}
    end.

%% The constants of a generator's columns as the table compares keys.
pos_fun('=:=', QOpt, QNum) ->
    {'=:=', constants(QOpt, QNum)};
pos_fun('==', QOpt, QNum) ->
    try
        {'==', equal_constants(QOpt, QNum)}
    catch
        _:_ -> {'=:=', constants(QOpt, QNum)}
    end.

prep_gen(#qlc_table{lu_vals = LuV0, ms = MS0} = LE0, Prep0, PosFun0, {MS, Fs}, Opt) ->
    #qlc_table{trav_MS = TravMS, info_fun = IF, lookup_fun = LUFun, key_equality = KeyEquality} = LE0,
    {LuV, {STag, SkipFils}} = find_const_positions(IF, LUFun, PosFun0(KeyEquality), Opt),
    LU = LuV =/= false,
    if
        LuV0 =/= undefined; MS0 =/= no_match_spec ->
            {no, [], false, Prep0};
        MS =/= no_match_spec, LU ->
            MS1 =
                case Fs =:= SkipFils orelse STag =:= Fs of
                    %% The lookup covers the guard of the match specification.
                    true -> lookup_covered_ms(MS);
                    false -> MS
                end,
            {replace, Fs, LU, Prep0#prepared{qh = LE0#qlc_table{lu_vals = LuV, ms = MS1}}};
        LU ->
            {skip, SkipFils, LU, Prep0#prepared{qh = LE0#qlc_table{lu_vals = LuV}}};
        TravMS, MS =/= no_match_spec ->
            Prep = Prep0#prepared{qh = LE0#qlc_table{ms = MS}, is_unique_objects = false},
            {replace, Fs, false, may_create_simple(Opt, Prep)};
        true ->
            {no, [], false, Prep0}
    end;
prep_gen(#qlc_list{l = []}, Prep0, _PosFun, {_MS, Fs}, _Opt) ->
    %% Unique and cached.
    {replace, Fs, false, Prep0};
prep_gen(#qlc_list{ms = no_match_spec} = LE0, Prep0, _PosFun, {MS, Fs}, Opt) when MS =/= no_match_spec ->
    Prep = Prep0#prepared{qh = LE0#qlc_list{ms = MS}, is_cached = false},
    {replace, Fs, false, may_create_simple(Opt, Prep)};
prep_gen(#qlc_list{}, Prep0, _PosFun, {MS, Fs}, Opt) when MS =/= no_match_spec ->
    LE = #prepared{qh = #qlc_list{l = Prep0, ms = MS}, is_cached = false},
    {replace, Fs, false, may_create_simple(Opt, LE)};
prep_gen(_LE0, Prep0, _PosFun, _MSFs, _Opt) ->
    {no, [], false, Prep0}.

%% A match specification whose guard the lookup makes redundant.
lookup_covered_ms([{'$1', _Guard, ['$1']}]) -> no_match_spec;
lookup_covered_ms([{Head, _Guard, Body}]) -> [{Head, [], Body}].

%% Wrap Prep in a simple query when the options ask for unique or cached answers it does not already have.
may_create_simple(#qlc_opt{unique = Unique, cache = Cache} = Opt, Prep) ->
    #prepared{is_cached = IsCached, is_unique_objects = IsUnique} = Prep,
    case (Unique andalso not IsUnique) orelse (Cache =/= false andalso not IsCached) of
        true -> prep_simple_qlc(?SIMPLE_QVAR, anno(1), Prep, Opt);
        false -> Prep
    end.

prep_simple_qlc(PVar, Anno, LE, Opt) ->
    check_join_option(Opt),
    #prepared{is_cached = IsCached, sort_info = SortInfo, sorted = Sorted, is_unique_objects = IsUnique} = LE,
    #qlc_opt{unique = Unique, cache = Cache} = Opt,
    Cachez =
        case Unique orelse not IsCached of
            true -> Cache;
            false -> false
        end,
    Optz = #optz{unique = Unique andalso not IsUnique, cache = Cachez, opt = Opt},
    QLC = #simple_qlc{p = PVar, le = LE, anno = Anno, init_value = not_a_list, optz = Optz},
    %% The join of LE is not copied.
    #prepared{
        qh = QLC,
        is_unique_objects = IsUnique orelse Unique,
        sort_info = SortInfo,
        sorted = Sorted,
        is_cached = IsCached orelse Cachez =/= false
    }.

prep_sort(#qlc_sort{h = #prepared{sorted = yes} = Prep}, _GOpt) ->
    Prep;
prep_sort(#qlc_sort{h = #prepared{is_unique_objects = IsUniqueObjs}} = Sort0, GOpt) ->
    Sort1 = sort_tmpdir(sort_unique(IsUniqueObjs, Sort0), GOpt),
    Sort = Sort1#qlc_sort{tmpdir_usage = GOpt#qlc_opt.tmpdir_usage},
    {SortInfo, Sorted} = sort_sort_info(Sort),
    #prepared{
        qh = Sort,
        is_cached = true,
        sort_info = SortInfo,
        sorted = Sorted,
        is_unique_objects = Sort#qlc_sort.unique orelse IsUniqueObjs
    }.

prep_qlc(QFun, CodeF, Qdata0, QOpt, Opt) ->
    #qlc_opt{unique = Unique, cache = Cache, join = Join} = Opt,
    Optz = #optz{unique = Unique, cache = Cache, join_option = Join, opt = Opt},
    {Qdata, SortInfo} = qlc_sort_info(Qdata0, QOpt),
    QLC = #qlc{lcf = QFun, codef = CodeF, qdata = Qdata, init_value = not_a_list, optz = Optz},
    #prepared{qh = QLC, sort_info = SortInfo, is_unique_objects = Unique, is_cached = Cache =/= false}.

%% The template columns known to be sorted, from constant template columns and generators sorted on a column the
%% template repeats. Used to avoid sorting merge join inputs that are sorted already.
qlc_sort_info(Qdata0, QOpt) ->
    Annotate = fun
        ({_QNum, _GoI, _SI, fil} = Qd, Info) ->
            {Qd, Info};
        ({_QNum, _GoI, _SI, {gen, #join{}}} = Qd, Info) ->
            {Qd, Info};
        ({QNum, GoI, SI, {gen, PrepLE0}}, Info) ->
            PrepLE = sort_info(PrepLE0, QNum, QOpt),
            I = [
                {{Column, Order}, [{traverse, QNum, C}]}
             || {{C, Order}, What} <- PrepLE#prepared.sort_info2,
                What =:= [],
                Column <- equal_template_columns(QOpt, {QNum, C})
            ],
            {{QNum, GoI, SI, {gen, PrepLE}}, [I | Info]}
    end,
    {Qdata, SortInfoL} = lists:mapfoldl(Annotate, [], Qdata0),
    SortInfo0 =
        [{{Pos, Ord}, [template]} || Pos <- constant_columns(QOpt, 0), Ord <- orders(yes)] ++
            lists:append(SortInfoL),
    {Qdata, family_union(SortInfo0)}.

%% The sorted columns of a generator's handle as its pattern sees them.
sort_info(#prepared{sort_info = SI, sorted = S} = Prep, QNum, QOpt) ->
    SI1 =
        [
            {{C, Ord}, []}
         || S =/= no,
            Sz <- [size_of_qualifier(QOpt, QNum)],
            is_integer(Sz),
            Sz > 0,
            NConstCols <- [size_of_constant_prefix(QOpt, QNum)],
            NConstCols < Sz,
            C <- [NConstCols + 1],
            Ord <- orders(S)
        ] ++
            [{{Pos, Ord}, []} || Pos <- constant_columns(QOpt, QNum), Ord <- orders(yes)] ++
            [{PosOrd, []} || {PosOrd, _} <- SI],
    Prep#prepared{sort_info2 = lists:usort(SI1)}.

orders(ascending = O) -> [O];
orders(yes) -> [ascending].

sort_unique(true, #qlc_sort{fs_opts = SortOptions, keypos = sort} = Sort) ->
    Sort#qlc_sort{unique = false, fs_opts = lists:keydelete(unique, 1, lists:delete(unique, SortOptions))};
sort_unique(_, Sort) ->
    Sort.

sort_tmpdir(S, #qlc_opt{tmpdir = ""}) -> S;
sort_tmpdir(S, Opt) -> S#qlc_sort{tmpdir = Opt#qlc_opt.tmpdir}.

%% A list of fewer than two elements: sorted, unique and cached.
short_list(L) ->
    #prepared{qh = #qlc_list{l = L}, sorted = yes, is_unique_objects = true, is_cached = true}.

%% The key or index position whose constants can be looked up, as {{Pos, Values}, {Tag, SkipFilters}}, or
%% {false, {some, []}}. The key position wins; else the index needing the fewest lookups.
find_const_positions(IF, LUFun, {KeyEquality, PosFun}, #qlc_opt{max_lookup = Max, lookup = Lookup}) when
    is_function(LUFun), is_function(PosFun), is_function(IF), Lookup =/= false
->
    case call(IF, keypos, undefined, []) of
        undefined ->
            Indices = call(IF, indices, undefined, []),
            find_const_position_idx(Indices, KeyEquality, PosFun, Max, []);
        KeyPos ->
            case pos_vals(KeyPos, KeyEquality, PosFun(KeyPos), Max) of
                false -> find_const_position_idx(IF(indices), KeyEquality, PosFun, Max, []);
                PosValuesSkip -> PosValuesSkip
            end
    end;
find_const_positions(_IF, _LUFun, _KEPosFun, _Opt) ->
    {false, {some, []}}.

find_const_position_idx([I | Is], KeyEquality, PosFun, Max, L0) ->
    case pos_vals(I, KeyEquality, PosFun(I), Max) of
        false ->
            find_const_position_idx(Is, KeyEquality, PosFun, Max, L0);
        {{_Pos, Values}, _SkipFils} = PosValuesFils ->
            find_const_position_idx(Is, KeyEquality, PosFun, Max, [{length(Values), PosValuesFils} | L0])
    end;
find_const_position_idx(_, _KeyEquality, _PosFun, _Max, []) ->
    {false, {some, []}};
find_const_position_idx(_, _KeyEquality, _PosFun, _Max, L) ->
    [{_, PVF} | _] = lists:sort(L),
    PVF.

pos_vals(Pos, '==', {usort_needed, Values, SkipFils}, Max) ->
    pos_vals_max(Pos, lists:usort(Values), SkipFils, Max);
pos_vals(Pos, '=:=', {usort_needed, Values, SkipFils}, Max) ->
    pos_vals_max(Pos, lists:sort(nub(Values)), SkipFils, Max);
pos_vals(Pos, _KeyEquality, {values, Values, SkipFils}, Max) ->
    pos_vals_max(Pos, Values, SkipFils, Max);
pos_vals(_Pos, _KeyEquality, _T, _Max) ->
    false.

%% The values without duplicates (matching), each kept at its last occurrence.
nub([]) ->
    [];
nub([E | L]) ->
    Es = nub(L),
    case lists:member(E, Es) of
        true -> Es;
        false -> [E | Es]
    end.

pos_vals_max(Pos, Values, Skip, Max) when Max =:= -1; Max >= length(Values) -> {{Pos, Values}, Skip};
pos_vals_max(_Pos, _Values, _Skip, _Max) -> false.

%% The lookup and merge joins the transform's join information allows, kept for opt_le/2 to choose from.
prep_join(Prep, QOpt, Opt) ->
    case join_opt(QOpt) of
        undefined ->
            check_join_option(Opt),
            Prep;
        {NEqual, NMatch} ->
            prep_join(NEqual, NMatch, Prep, QOpt, Opt);
        EqualMatch ->
            prep_join(EqualMatch, EqualMatch, Prep, QOpt, Opt)
    end.

prep_join(Equal, Match, Prep, QOpt, Opt) ->
    {Ix, M} = pref_join(Equal, Match, Prep, QOpt, Opt),
    Prep#prepared{join = {Ix, M}, sort_info = family_union(Prep#prepared.sort_info ++ M)}.

%% The transform makes sure only two generators take part.
pref_join(Equal, Match, Prep, QOpt, #qlc_opt{join = JoinOpt}) ->
    JQs = [{KeyEquality, QCs} || {KeyEquality, QCsL} <- [{'==', Equal}, {'=:=', Match}], QCs <- QCsL],
    IxL = [pref_lookup_join(KE, QCs, Prep, QOpt) || JoinOpt =:= any orelse JoinOpt =:= lookup, {KE, QCs} <- JQs],
    ML = [pref_merge_join(KE, QCs, Prep, QOpt) || JoinOpt =:= any orelse JoinOpt =:= merge, {KE, QCs} <- JQs],
    {lists:usort(lists:append(IxL)), lists:usort(lists:append(ML))}.

pref_lookup_join(KeyEquality, {[{Q1, C1}, {Q2, C2}], Skip}, Prep, QOpt) when is_integer(C1), is_integer(C2) ->
    #prepared{qh = #qlc{qdata = QData}} = Prep,
    Is1 = lookup_qual_data(QData, Q1, KeyEquality),
    Lu2 = [pref_lookup_join2(Q2, C2, Q1, C1, Skip, QOpt, KeyEquality) || IC1 <- Is1, IC1 =:= C1],
    Is2 = lookup_qual_data(QData, Q2, KeyEquality),
    Lu1 = [pref_lookup_join2(Q1, C1, Q2, C2, Skip, QOpt, KeyEquality) || IC2 <- Is2, IC2 =:= C2],
    family(Lu1 ++ Lu2).

lookup_qual_data(QData, QNum, KeyEquality) ->
    {QNum, _, _, {gen, PrepLE}} = lists:keyfind(QNum, 1, QData),
    join_indices(PrepLE, KeyEquality).

%% The positions a lookup join can look up in a table. A match specification derived from a query handle rather
%% than a filter rules the join out; opt_join_lu/3 checks that.
join_indices(
    #prepared{qh = #qlc_table{info_fun = IF, lookup_fun = LUFun, key_equality = KE, lu_vals = undefined}},
    KE
) when is_function(LUFun) ->
    KeyPos =
        case call(IF, keypos, undefined, []) of
            undefined -> [];
            Kp -> [Kp]
        end,
    case call(IF, indices, undefined, []) of
        undefined -> KeyPos;
        Is0 -> lists:usort(KeyPos ++ Is0)
    end;
join_indices(_Prep, _KeyEquality) ->
    [].

pref_lookup_join2(Q1, C1, Q2, C2, Skip, QOpt, KeyEquality) ->
    TemplCols = compared_template_columns(QOpt, {Q1, C1}, KeyEquality),
    {{Q1, C1, Q2, C2}, {lookup_join, TemplCols, KeyEquality, Skip}}.

pref_merge_join(KE, {[{Q1, C1}, {Q2, C2}], Skip}, Prep, QOpt) when is_integer(C1), is_integer(C2) ->
    #prepared{qh = #qlc{qdata = QData}} = Prep,
    Sort1 = merge_qual_data(QData, Q1),
    Sort2 = merge_qual_data(QData, Q2),
    family_union(pref_merge(KE, Q1, C1, Q2, C2, Skip, Sort1, Sort2, QOpt)).

merge_qual_data(QData, QNum) ->
    {QNum, _, _, {gen, #prepared{sort_info2 = SortInfo}}} = lists:keyfind(QNum, 1, QData),
    SortInfo.

%% A merge join of the two columns, sorting the inputs not already sorted on them.
pref_merge(KE, Q1, C1, Q2, C2, Skip, Sort1, Sort2, QOpt) ->
    Col1 = {Q1, C1},
    Col2 = {Q2, C2},
    DoSort = [
        QC
     || {{_QNum, Col} = QC, SortL} <- [{Col1, Sort1}, {Col2, Sort2}],
        lists:keymember({Col, ascending}, 1, SortL) =:= false
    ],
    J = [{{Q1, C1, Q2, C2}, {merge_join, DoSort, KE, Skip}}],
    [{{Column, ascending}, J} || Column <- equal_template_columns(QOpt, Col1)] ++ [{other, J}].

table_sort_info(#qlc_table{info_fun = IF}) ->
    case call(IF, is_sorted_key, undefined, []) of
        undefined ->
            {[], no};
        false ->
            {[], no};
        true ->
            case call(IF, keypos, undefined, []) of
                undefined -> {[], no};
                KeyPos -> {[{{KeyPos, ascending}, []}], no}
            end
    end.

sort_sort_info(#qlc_sort{keypos = sort, order = Ord0}) ->
    {[], sort_order(Ord0)};
sort_sort_info(#qlc_sort{keypos = {keysort, Kp0}, order = Ord0}) ->
    Kp =
        case Kp0 of
            [Pos | _] -> Pos;
            _ -> Kp0
        end,
    {[{{Kp, sort_order(Ord0)}, []}], no}.

sort_order(F) when is_function(F) -> no;
sort_order(Order) -> Order.

check_join_option(#qlc_opt{join = any}) -> ok;
check_join_option(#qlc_opt{join = Join}) -> erlang:error(no_join_to_carry_out, [{join, Join}]).

check_lookup_option(#qlc_opt{lookup = true}, false) -> erlang:error(no_lookup_to_carry_out, [{lookup, true}]);
check_lookup_option(_QOpt, _LuV) -> ok.

%% The transform's answers about a query (QOpt) and its qualifiers.
compared_template_columns(QOpt, QNumColumn, KeyEquality) -> (QOpt(template))(QNumColumn, KeyEquality).
equal_template_columns(QOpt, QNumColumn) -> (QOpt(template))(QNumColumn, '==').
size_of_constant_prefix(QOpt, QNum) -> (QOpt(n_leading_constant_columns))(QNum).
constants(QOpt, QNum) -> (QOpt(constants))(QNum).
equal_constants(QOpt, QNum) -> (QOpt(equal_constants))(QNum).
join_opt(QOpt) -> QOpt(join).
match_specs(QOpt, QNum) -> (QOpt(match_specs))(QNum).
constant_columns(QOpt, QNum) -> (QOpt(constant_columns))(QNum).
size_of_qualifier(QOpt, QNum) -> (QOpt(size))(QNum).

%%% Optimization: the first generator and the top query are never cached, simple queries without options vanish
%%% and nested ones coalesce, and the preferred merge or lookup join is chosen.

opt_le(#prepared{qh = #simple_qlc{le = LE0, optz = Optz0} = QLC} = Prep0, GenNum) ->
    case LE0 of
        #prepared{qh = #simple_qlc{p = LEPVar, le = LE2, optz = Optz2}} ->
            %% Coalesce two simple queries.
            Cachez =
                case Optz2#optz.cache of
                    false -> Optz0#optz.cache;
                    Cache2 -> Cache2
                end,
            Optz = Optz0#optz{cache = Cachez, unique = Optz0#optz.unique orelse Optz2#optz.unique},
            PVar =
                case LEPVar of
                    ?SIMPLE_QVAR -> QLC#simple_qlc.p;
                    _ -> LEPVar
                end,
            opt_le(Prep0#prepared{qh = QLC#simple_qlc{p = PVar, le = LE2, optz = Optz}}, GenNum);
        _ ->
            Optz1 = no_cache_of_first_generator(Optz0, GenNum),
            case {opt_le(LE0, 1), Optz1} of
                {LE, #optz{unique = false, cache = false}} -> LE;
                {LE, _} -> Prep0#prepared{qh = QLC#simple_qlc{le = LE, optz = Optz1}}
            end
    end;
opt_le(#prepared{qh = #qlc{qdata = Qdata0, optz = Optz0} = QLC, lu_skip_quals = LUSkipQuals0} = Prep0, GenNum) ->
    #optz{join_option = JoinOption, opt = Opt} = Optz0,
    {LUQNum, Join, JoinSkipFs, DoSort} = opt_join(Prep0#prepared.join, JoinOption, Qdata0, Opt, LUSkipQuals0),
    {LUSkip, LUSkipQuals} = lists:partition(fun({QNum, _Fs}) -> QNum =:= LUQNum end, LUSkipQuals0),
    LUSkipFs = lists:flatmap(fun({_QNum, Fs}) -> Fs end, LUSkipQuals),
    %% A match specification of the lookup join's table runs after the join: its filter is not skipped.
    Qdata1 =
        case LUSkip of
            [] -> Qdata0;
            _ -> activate_join_lookup_filter(LUQNum, Qdata0)
        end,
    Qdata2 = skip_lookup_filters(Qdata1, LUSkipFs ++ JoinSkipFs),
    OptimizeGenerator = fun
        ({QNum, GoI, SI, {gen, #prepared{} = PrepLE}}, GenNum1) ->
            NewPrepLE = maybe_sort(PrepLE, QNum, DoSort, Opt),
            {{QNum, GoI, SI, {gen, opt_le(NewPrepLE, GenNum1)}}, GenNum1 + 1};
        (Qd, GenNum1) ->
            {Qd, GenNum1}
    end,
    {Qdata, _} = lists:mapfoldl(OptimizeGenerator, 1, Qdata2),
    Optz1 = no_cache_of_first_generator(Optz0, GenNum),
    Prep0#prepared{qh = QLC#qlc{qdata = Qdata, optz = Optz1#optz{fast_join = Join}}};
opt_le(#prepared{qh = #qlc_append{hl = HL}} = Prep, GenNum) ->
    Prep#prepared{qh = #qlc_append{hl = [opt_le(H, GenNum) || H <- HL]}};
opt_le(#prepared{qh = #qlc_sort{h = H} = Sort} = Prep, GenNum) ->
    Prep#prepared{qh = Sort#qlc_sort{h = opt_le(H, GenNum)}};
opt_le(Prep, _GenNum) ->
    Prep.

no_cache_of_first_generator(Optz, GenNum) when GenNum > 1 -> Optz;
no_cache_of_first_generator(Optz, 1) -> Optz#optz{cache = false}.

%% Key-sort a merge join input not already sorted on its join column.
maybe_sort(LE, QNum, DoSort, Opt) ->
    case lists:keyfind(QNum, 1, DoSort) of
        {QNum, Col} ->
            #qlc_opt{tmpdir = TmpDir, tmpdir_usage = TmpUsage} = Opt,
            Sort = #qlc_sort{
                h = LE,
                keypos = {keysort, Col},
                unique = false,
                compressed = [],
                order = ascending,
                fs_opts = [{tmpdir, Dir} || Dir <- [TmpDir], Dir =/= ""],
                tmpdir_usage = TmpUsage,
                tmpdir = TmpDir
            },
            #prepared{qh = Sort, sorted = no, join = no};
        false ->
            LE
    end.

skip_lookup_filters(Qdata, []) ->
    Qdata;
skip_lookup_filters(Qdata0, LUSkipFs) ->
    [
        case lists:member(QNum, LUSkipFs) of
            true -> {QNum, GoI, ?SKIP, fil};
            false -> Qd
        end
     || {QNum, GoI, _, _} = Qd <- Qdata0
    ].

activate_join_lookup_filter(QNum, Qdata) ->
    {_, GoI2, SI2, {gen, Prep2}} = lists:keyfind(QNum, 1, Qdata),
    Table2 = Prep2#prepared.qh,
    NPrep2 = Prep2#prepared{qh = Table2#qlc_table{ms = no_match_spec}},
    lists:keyreplace(QNum, 1, Qdata, {QNum, GoI2, SI2, {gen, NPrep2}}).

%% The join to carry out: a lookup join if any (the lookup generator, the join, the filters it skips), else the
%% merge join that sorts least, else none.
opt_join({Ix0, M0}, JoinOption, Qdata, Opt, LUSkipQuals) ->
    %% Prefer lookup joins that skip filters.
    case lists:reverse(lists:keysort(2, opt_join_lu(Ix0, Qdata, LUSkipQuals))) of
        [{{Q1, C1, Q2, C2}, Skip, KE, LUFun} | _] ->
            J = #qlc_join{kind = {lookup, KE, LUFun}, q1 = Q1, c1 = C1, q2 = Q2, c2 = C2, opt = Opt},
            {Q2, J, Skip, []};
        [] ->
            case opt_join_merge(M0) of
                [{{Q1, C1, Q2, C2}, {merge_join, DoSort, KE, Skip}} | _] ->
                    J = #qlc_join{kind = {merge, KE}, opt = Opt, q1 = Q1, c1 = C1, q2 = Q2, c2 = C2},
                    {not_a_qnum, J, Skip, DoSort};
                [] when JoinOption =:= nested_loop ->
                    {not_a_qnum, no, [], []};
                _ when JoinOption =/= any ->
                    erlang:error(cannot_carry_out_join, [JoinOption]);
                _ ->
                    {not_a_qnum, no, [], []}
            end
    end.

opt_join_lu([{{_Q1, _C1, Q2, _C2} = J, [{lookup_join, _KEols, JKE, Skip0} | _]} | LJ], Qdata, LUSkipQuals) ->
    {Q2, _, _, {gen, Prep2}} = lists:keyfind(Q2, 1, Qdata),
    #qlc_table{ms = MS, key_equality = KE, lookup_fun = LUFun} = Prep2#prepared.qh,
    %% A match specification from a query handle (no filter to skip) rules out the lookup join.
    case MS =/= no_match_spec andalso not lists:keymember(Q2, 1, LUSkipQuals) of
        true -> opt_join_lu(LJ, Qdata, LUSkipQuals);
        false -> [{J, skip_if_possible(JKE, KE, Skip0), KE, LUFun} | opt_join_lu(LJ, Qdata, LUSkipQuals)]
    end;
opt_join_lu([], _Qdata, _LUSkipQuals) ->
    [].

%% The merge joins, preferring fewer sorted inputs and then skipped join filters.
opt_join_merge(M) ->
    L = [
        {-length(DoSort), length(Skip), {QCs, {merge_join, DoSort, KE, Skip}}}
     || {_KpOrderOrOther, MJ} <- M,
        {QCs, {merge_join, DoSort, KE, Skip0}} <- MJ,
        Skip <- [skip_if_possible(KE, '==', Skip0)]
    ],
    lists:reverse([J || {_, _, J} <- lists:sort(L)]).

%% A '=:=' join filter is not skipped when the join compares with '=='.
skip_if_possible('=:=', '==', _) -> [];
skip_if_possible(_, _, {_SkipTag, Skip}) -> Skip.

%%% Setup: answers as lists ending in [] or a continuation fun, with post funs cleaning up afterwards

%% {Objects, Post, LocalPost}: Post holds the cleanup funs, LocalPost those (a subset) to run as soon as all objects
%% are found. Errors found while setting up are thrown after the cleanup; errors found later are answers.
setup_qlc(Prep, Setup) ->
    setup_le(Prep, [], Setup).

setup_le(#prepared{qh = #simple_qlc{le = LE, optz = Optz}}, Post0, Setup) ->
    {Objs, Post, LocalPost} = setup_le(LE, Post0, Setup),
    unique_cache(Objs, Post, LocalPost, Optz);
setup_le(#prepared{qh = #qlc{lcf = QFun, qdata = Qdata, init_value = V, optz = Optz}}, Post0, Setup) ->
    {GoTo, FirstState, Post, LocalPost} = setup_quals(Qdata, Post0, Setup, Optz),
    unique_cache(fun() -> QFun(FirstState, V, GoTo) end, Post, LocalPost, Optz);
setup_le(#prepared{qh = #qlc_table{post_fun = PostFun} = Table}, Post, Setup) ->
    %% table_handle/3 calls the pre fun.
    H = table_handle(Table, Post, Setup),
    {H, [PostFun | Post], []};
setup_le(#prepared{qh = #qlc_append{hl = PrepL}}, Post0, Setup) ->
    SetupMember = fun(Prep, {Post1, LPost1}) ->
        {Objs, Post2, LPost2} = setup_le(Prep, Post1, Setup),
        {Objs, {Post2, LPost1 ++ LPost2}}
    end,
    {ObjsL, {Post, LocalPost}} = lists:mapfoldl(SetupMember, {Post0, []}, PrepL),
    {fun() -> append_loop(ObjsL, 0) end, Post, LocalPost};
setup_le(#prepared{qh = #qlc_sort{h = Prep} = Sort}, Post0, Setup) ->
    #qlc_sort{
        keypos = Kp,
        unique = Unique,
        compressed = Compressed,
        order = Order,
        fs_opts = SortOptions0,
        tmpdir_usage = TmpUsage,
        tmpdir = TmpDir
    } = Sort,
    SortOptions = sort_options_global_tmp(SortOptions0, TmpDir),
    LF = fun(Objs) -> sort_list(Objs, Order, Unique, Kp, SortOptions, Post0) end,
    case setup_le(Prep, Post0, Setup) of
        {L, Post, LocalPost} when is_list(L) ->
            {LF(L), Post, LocalPost};
        {Objs, Post, LocalPost} ->
            FF = fun(Objs1) -> file_sort_handle(Objs1, Kp, SortOptions, TmpDir, Compressed, Post, LocalPost) end,
            sort_handle(Objs, LF, FF, SortOptions, Post, LocalPost, {TmpUsage, sorting})
    end;
setup_le(#prepared{qh = #qlc_list{l = L, ms = MS}}, Post, _Setup) when MS =:= no_match_spec; L =:= [] ->
    {L, Post, []};
setup_le(#prepared{qh = #qlc_list{l = L, ms = MS}}, Post, _Setup) when is_list(L) ->
    {ets:match_spec_run(L, ets:match_spec_compile(MS)), Post, []};
setup_le(#prepared{qh = #qlc_list{l = H0, ms = MS}}, Post0, Setup) ->
    {Objs0, Post, LocalPost} = setup_le(H0, Post0, Setup),
    {ets:match_spec_run(Objs0, ets:match_spec_compile(MS)), Post, LocalPost}.

%% The goto table the generated query fun runs over (a tuple built here), and the first state. A filter has the states
%% to go to when false and when true; a generator the state of the previous generator (or stop), the state after it
%% and its answers. A join generator gets the joined answers of its two generators when a join is carried out.
setup_quals(Qdata, Post0, Setup, Optz) ->
    {GoTo0, Post1, LocalPost0} = setup_quals(0, Qdata, [], Post0, [], Setup),
    GoTo1 = lists:keysort(1, GoTo0),
    FirstState0 = next_state(Qdata),
    {GoTo2, FirstState, Post, LocalPost1} =
        case Optz#optz.fast_join of
            #qlc_join{kind = {merge, _KE}, c1 = C1, c2 = C2, opt = Opt} = MJ ->
                MergeFun = fun(_Rev, {H1, WH1}, {H2, WH2}) ->
                    fun() -> merge_join(WH1(H1), C1, WH2(H2), C2, Opt) end
                end,
                setup_join(MJ, Qdata, GoTo1, FirstState0, MergeFun, Post1);
            #qlc_join{kind = {lookup, _KE, LuF}, c1 = C1, c2 = C2} = LJ ->
                LookupFun = fun
                    (true, _, {H2, WH2}) -> fun() -> lookup_join(WH2(H2), C1, LuF, C2, true) end;
                    (false, {H1, WH1}, _) -> fun() -> lookup_join(WH1(H1), C1, LuF, C2, false) end
                end,
                setup_join(LJ, Qdata, GoTo1, FirstState0, LookupFun, Post1);
            no ->
                {flat_goto(GoTo1), FirstState0, Post1, []}
        end,
    {list_to_tuple(GoTo2), FirstState, Post, LocalPost0 ++ LocalPost1}.

setup_quals(GenLoopS, [{_QNum, GoI, ?SKIP, fil} | Qdata], Gs, P, LP, Setup) ->
    %% A skipped filter's states make the generated code fail if ever reached.
    setup_quals(GenLoopS, Qdata, [{GoI, [?SKIP, ?SKIP]} | Gs], P, LP, Setup);
setup_quals(GenLoopS, [{_QNum, GoI, _SI, fil} | Qdata], Gs, P, LP, Setup) ->
    setup_quals(GenLoopS, Qdata, [{GoI, [GenLoopS, next_state(Qdata)]} | Gs], P, LP, Setup);
setup_quals(GenLoopS, [{_QNum, GoI, _SI, {gen, #join{}}} | Qdata], Gs, P, LP, Setup) ->
    setup_quals(GenLoopS, Qdata, [{GoI, [?SKIP, ?SKIP, ?SKIP]} | Gs], P, LP, Setup);
setup_quals(GenLoopS, [{_QNum, GoI, SI, {gen, LE}} | Qdata], Gs, P, LP, Setup) ->
    {V, NP, LP1} = setup_le(LE, P, Setup),
    setup_quals(SI + 1, Qdata, [{GoI, [GenLoopS, next_state(Qdata), V]} | Gs], NP, LP ++ LP1, Setup);
setup_quals(GenLoopS, [], Gs, P, LP, _Setup) ->
    {[{1, [GenLoopS]} | Gs], P, LP}.

%% Set up the join generator joining Q1 and Q2 over their set-up handles and route the goto table through it: the
%% two generators' slots become unused and the states that led to them lead to the join generator. The transform
%% gives each generator three fixed slots and at most one join generator.
setup_join(J, Qdata, GoTo0, FirstState0, JoinFun, Post0) ->
    #qlc_join{q1 = QNum1a, q2 = QNum2a, opt = Opt} = J,
    {{_QN, JGoI, JSI, _}, Rev, QNum1, QNum2, WH1, WH2, _CsFun} = find_join_data(Qdata, QNum1a, QNum2a),
    [{GoI1, SI1}] = [{GoI, SI} || {QNum, GoI, SI, _} <- Qdata, QNum =:= QNum1],
    [{GoI2, SI2}] = [{GoI, SI} || {QNum, GoI, SI, _} <- Qdata, QNum =:= QNum2],
    [H1] = [H || {GoI, [_Back, _Forth, H]} <- GoTo0, GoI =:= GoI1],
    [{BackH2, H2}] = [{Back, H} || {GoI, [Back, _Forth, H]} <- GoTo0, GoI =:= GoI2],
    H0 = JoinFun(Rev, {H1, WH1}, {H2, WH2}),
    %% The options of the query apply to the introduced join query as well.
    {H, Post, LocalPost} = unique_cache(H0, Post0, [], join_unique_cache(Opt)),
    [JBack] = [Back || {GoI, [Back, _, _]} <- GoTo0, GoI =:= GoI1],
    JForth = next_after(Qdata, SI1, QNum2),
    GoTo1 = lists:map(
        fun
            ({GoI, _}) when GoI =:= JGoI -> {JGoI, [JBack, JForth, H]};
            ({GoI, _}) when GoI =:= GoI1; GoI =:= GoI2 -> {GoI, [?SKIP, ?SKIP, ?SKIP]};
            (Go) -> Go
        end,
        GoTo0
    ),
    Reroute = fun
        (S) when S =:= SI1 -> JSI;
        (S) when S =:= SI2 -> next_after(Qdata, S, QNum2);
        (S) when S =:= SI1 + 1 -> JSI + 1;
        (S) when S =:= SI2 + 1, SI1 + 1 =:= BackH2 -> JSI + 1;
        (S) when S =:= SI2 + 1 -> BackH2;
        (S) -> S
    end,
    GoTo = lists:map(Reroute, flat_goto(GoTo1)),
    FirstState =
        case SI1 =:= FirstState0 of
            true -> JSI;
            false -> FirstState0
        end,
    {GoTo, FirstState, Post, LocalPost}.

join_unique_cache(#qlc_opt{cache = Cache, unique = Unique} = Opt) ->
    #optz{cache = Cache, unique = Unique, opt = Opt}.

flat_goto(GoTo) ->
    lists:append([L || {_, L} <- GoTo]).

%% The state after the qualifier of state S, passing over Q2 when it comes next.
next_after([{_, _, S, _} | Qdata], S, QNum2) ->
    case Qdata of
        [{QNum2, _, _, _} | Qdata1] -> next_state(Qdata1);
        _ -> next_state(Qdata)
    end;
next_after([_ | Qdata], S, QNum2) ->
    next_after(Qdata, S, QNum2).

%% The state of the next qualifier that runs: join generators and skipped filters do not.
next_state([{_, _, _, {gen, #join{}}} | Qdata]) -> next_state(Qdata);
next_state([{_, _, ?SKIP, fil} | Qdata]) -> next_state(Qdata);
next_state([{_, _, S, _} | _]) -> S;
next_state([]) -> template_state().

%% The join generator joining QNum1 and QNum2, and whether it names them in reverse order.
find_join_data(Qdata, QNum1, QNum2) ->
    [Found] = [
        {Qd, Rev, Q1, Q2, WH1, WH2, CsF}
     || {_QN, _GoI, _SI, {gen, #join{q1 = Q1, q2 = Q2, wh1 = WH1, wh2 = WH2, cs_fun = CsF}}} = Qd <- Qdata,
        Rev <- join_direction(Q1, Q2, QNum1, QNum2)
    ],
    Found.

join_direction(Q1, Q2, Q1, Q2) -> [false];
join_direction(Q1, Q2, Q2, Q1) -> [true];
join_direction(_, _, _, _) -> [].

%% Call the parent and pre funs and return the table's first answers: looked up keys, or a traverse fun.
table_handle(#qlc_table{} = Table, Post, Setup) ->
    #qlc_table{
        trav_fun = TraverseFun,
        trav_MS = TravMS,
        pre_fun = PreFun,
        lookup_fun = LuF,
        parent_fun = ParentFun,
        lu_vals = LuVals,
        ms = MS
    } = Table,
    #setup{parent = Parent} = Setup,
    ParentValue = parent_value(ParentFun, Parent, Post),
    StopFun =
        case Parent =:= self() of
            true ->
                undefined;
            false ->
                Cursor = #qlc_cursor{c = {self(), Parent}},
                fun() -> delete_cursor(Cursor) end
        end,
    _ = call(PreFun, [{parent_value, ParentValue}, {stop_fun, StopFun}], ok, Post),
    case LuVals of
        {Pos, Vals} when MS =:= no_match_spec ->
            LuF(Pos, Vals);
        {Pos, Vals} ->
            case LuF(Pos, Vals) of
                [] ->
                    [];
                Objs when is_list(Objs) ->
                    ets:match_spec_run(Objs, ets:match_spec_compile(MS));
                Error ->
                    post_funs(Post),
                    throw_error(Error)
            end;
        _ when not TravMS ->
            no_match_spec = MS,
            TraverseFun;
        _ when MS =:= no_match_spec ->
            fun() -> TraverseFun([{'$1', [], ['$1']}]) end;
        _ ->
            fun() -> TraverseFun(MS) end
    end.

%% The parent fun's value, called in the parent process (the cursor's owner asks it).
parent_value(undefined, _Parent, _Post) ->
    undefined;
parent_value(ParentFun, Parent, Post) when Parent =:= self() ->
    try
        ParentFun()
    catch
        Class:Reason:Stacktrace ->
            post_funs(Post),
            erlang:raise(Class, Reason, Stacktrace)
    end;
parent_value(ParentFun, Parent, Post) ->
    case monitor_request(Parent, {parent_fun, ParentFun}) of
        error ->
            %% The parent has died.
            post_funs(Post),
            exit(normal);
        {value, Value} ->
            Value;
        {parent_fun_caught, Class, Reason, Stacktrace} ->
            post_funs(Post),
            erlang:raise(Class, Reason, Stacktrace)
    end.

%%% Unique and cached answers

unique_cache(L, Post, LocalPost, Optz) when is_list(L) ->
    case Optz#optz.unique of
        true -> {unique_sort_list(L), Post, LocalPost};
        false -> {L, Post, LocalPost}
    end;
unique_cache(H, Post, LocalPost, #optz{unique = false, cache = false}) ->
    {H, Post, LocalPost};
unique_cache(H, Post, LocalPost, #optz{unique = true, cache = false}) ->
    E = ets:new(qlc, [set, private]),
    {fun() -> no_dups(H, E) end, [del_table(E) | Post], LocalPost};
unique_cache(H, Post, LocalPost, #optz{unique = false, cache = true}) ->
    E = ets:new(qlc, [set, private]),
    {fun() -> cache(H, E, LocalPost) end, [del_table(E) | Post], [empty_table(E)]};
unique_cache(H, Post, LocalPost, #optz{unique = true, cache = true}) ->
    UT = ets:new(qlc, [bag, private]),
    MT = ets:new(qlc, [set, private]),
    {fun() -> ucache(H, UT, MT, LocalPost) end, [del_table(UT), del_table(MT) | Post], [
        empty_table(UT), empty_table(MT)
    ]};
unique_cache(H, Post, LocalPost, #optz{unique = false, cache = list} = Optz) ->
    Ref = make_ref(),
    F = del_lcache(Ref),
    #qlc_opt{tmpdir = TmpDir, max_list = MaxList, tmpdir_usage = TmpUsage} = Optz#optz.opt,
    {fun() -> lcache(H, Ref, LocalPost, TmpDir, MaxList, TmpUsage) end, [F | Post], [F]};
unique_cache(H, Post0, LocalPost0, #optz{unique = true, cache = list} = Optz) ->
    unique_list_cache(H, Post0, LocalPost0, Optz#optz.opt).

%% Unique answers cached in a list: tagged with their positions, unique-sorted on the answer, sorted back on the tag.
unique_list_cache(H, Post0, LocalPost0, #qlc_opt{tmpdir = TmpDir, max_list = MaxList, tmpdir_usage = TmpUsage}) ->
    Size =
        if
            MaxList >= 1 bsl 31 -> (1 bsl 31) - 1;
            MaxList =:= 0 -> 1;
            true -> MaxList
        end,
    SortOptions = [{size, Size}, {tmpdir, TmpDir}],
    USortOptions = [{unique, true} | SortOptions],
    TmpUsageM = {TmpUsage, caching},
    LF1 = fun(Objs) -> lists:ukeysort(1, Objs) end,
    FF1 = fun(Objs) -> file_sort_handle(Objs, {keysort, 1}, USortOptions, TmpDir, [], Post0, LocalPost0) end,
    {UH, Post1, LocalPost1} = sort_handle(tag_objects(H, 1), LF1, FF1, USortOptions, Post0, LocalPost0, TmpUsageM),
    LF2 = fun(Objs) -> lists:keysort(2, Objs) end,
    FF2 = fun(Objs) -> file_sort_handle(Objs, {keysort, 2}, SortOptions, TmpDir, [], Post1, LocalPost1) end,
    {SH, Post, LocalPost} = sort_handle(UH, LF2, FF2, SortOptions, Post1, LocalPost1, TmpUsageM),
    case is_list(SH) of
        %% The tags go once and for all.
        true -> {untag_objects2(SH), Post, LocalPost};
        %% Every traversal removes the tags.
        false -> {fun() -> untag_objects(SH) end, Post, LocalPost}
    end.

unique_sort_list(L) ->
    unique_list(L, ets:new(qlc, [set, private])).

unique_list([], E) ->
    true = ets:delete(E),
    [];
unique_list([Object | Objects], E) ->
    case ets:member(E, Object) of
        false ->
            true = ets:insert(E, {Object}),
            [Object | unique_list(Objects, E)];
        true ->
            unique_list(Objects, E)
    end.

del_table(Ets) ->
    fun() -> true = ets:delete(Ets) end.

empty_table(Ets) ->
    fun() -> true = ets:delete_all_objects(Ets) end.

no_dups([] = Cont, UTab) ->
    true = ets:delete_all_objects(UTab),
    Cont;
no_dups([Object | Cont], UTab) ->
    case ets:member(UTab, Object) of
        false ->
            true = ets:insert(UTab, {Object}),
            %% A fun even when Cont is a list: objects are copied to the table only when asked for.
            [Object | fun() -> no_dups(Cont, UTab) end];
        true ->
            no_dups(Cont, UTab)
    end;
no_dups(F, UTab) ->
    case F() of
        Objects when is_list(Objects) -> no_dups(Objects, UTab);
        Term -> Term
    end.

%% Answers of a cached query: the first traversal stores them with sequence numbers in MTab (whose key 0 marks the
%% start), later ones read them back. When all answers are stored, LocalPost empties the generators' tables.
cache(H, MTab, LocalPost) ->
    case ets:member(MTab, 0) of
        false ->
            true = ets:insert(MTab, {0}),
            cache(H, MTab, 1, LocalPost);
        true ->
            cache_recall(MTab, 1)
    end.

cache([] = Cont, _MTab, _SeqNo, LocalPost) ->
    local_post(LocalPost),
    Cont;
cache([Object | Cont], MTab, SeqNo, LocalPost) ->
    true = ets:insert(MTab, {SeqNo, Object}),
    [Object | fun() -> cache(Cont, MTab, SeqNo + 1, LocalPost) end];
cache(F, MTab, SeqNo, LocalPost) ->
    case F() of
        Objects when is_list(Objects) -> cache(Objects, MTab, SeqNo, LocalPost);
        Term -> Term
    end.

cache_recall(MTab, SeqNo) ->
    case ets:lookup(MTab, SeqNo) of
        [] = Cont -> Cont;
        [{SeqNo, Object}] -> [Object | fun() -> cache_recall(MTab, SeqNo + 1) end]
    end.

%% Unique cached answers: UTab holds {Hash, SeqNo, Object}, MTab {SeqNo, Hash}.
ucache(H, UTab, MTab, LocalPost) ->
    case ets:member(MTab, 0) of
        false ->
            true = ets:insert(MTab, {0}),
            ucache(H, UTab, MTab, 1, LocalPost);
        true ->
            ucache_recall(UTab, MTab, 1)
    end.

ucache([] = Cont, _UTab, _MTab, _SeqNo, LocalPost) ->
    local_post(LocalPost),
    Cont;
ucache([Object | Cont], UTab, MTab, SeqNo, LocalPost) ->
    Hash = erlang:phash2(Object),
    case ets:lookup(UTab, Hash) of
        [] ->
            ucache3(Object, Cont, Hash, UTab, MTab, SeqNo, LocalPost);
        HashSeqObjects ->
            case lists:keymember(Object, 3, HashSeqObjects) of
                true -> ucache(Cont, UTab, MTab, SeqNo, LocalPost);
                false -> ucache3(Object, Cont, Hash, UTab, MTab, SeqNo, LocalPost)
            end
    end;
ucache(F, UTab, MTab, SeqNo, LocalPost) ->
    case F() of
        Objects when is_list(Objects) -> ucache(Objects, UTab, MTab, SeqNo, LocalPost);
        Term -> Term
    end.

ucache3(Object, Cont, Hash, UTab, MTab, SeqNo, LocalPost) ->
    true = ets:insert(UTab, {Hash, SeqNo, Object}),
    true = ets:insert(MTab, {SeqNo, Hash}),
    [Object | fun() -> ucache(Cont, UTab, MTab, SeqNo + 1, LocalPost) end].

ucache_recall(UTab, MTab, SeqNo) ->
    case ets:lookup(MTab, SeqNo) of
        [] = Cont ->
            Cont;
        [{SeqNo, Hash}] ->
            Object =
                case ets:lookup(UTab, Hash) of
                    [{Hash, SeqNo, Object0}] ->
                        Object0;
                    HashSeqObjects ->
                        {Hash, SeqNo, Object0} = lists:keyfind(SeqNo, 2, HashSeqObjects),
                        Object0
                end,
            [Object | fun() -> ucache_recall(UTab, MTab, SeqNo + 1) end]
    end.

%% Answers cached in a list under a process dictionary key, or on a temporary file once they exceed MaxList bytes.
lcache(H, Ref, LocalPost, TmpDir, MaxList, TmpUsage) ->
    Key = ?LCACHE_FILE(Ref),
    case get(Key) of
        undefined -> lcache1(H, {Key, LocalPost, TmpDir, MaxList, TmpUsage}, MaxList, []);
        {file, _Fd, _TmpFile, F} -> F();
        L when is_list(L) -> L
    end.

lcache1([] = Cont, {Key, LocalPost, _TmpDir, _MaxList, _TmpUsage}, _Sz, Acc) ->
    local_post(LocalPost),
    case get(Key) of
        undefined ->
            put(Key, lists:reverse(Acc)),
            Cont;
        {file, Fd, TmpFile, _F} ->
            case lcache_write(Fd, TmpFile, Acc) of
                ok -> Cont;
                Error -> Error
            end
    end;
lcache1(H, State, Sz, Acc) when Sz < 0 ->
    {_Key, _LocalPost, _TmpDir, MaxList, _TmpUsage} = State,
    case lcache_file(State) of
        {FileName, Fd} ->
            case lcache_write(Fd, FileName, Acc) of
                ok -> lcache1(H, State, MaxList, []);
                Error -> Error
            end;
        Error ->
            Error
    end;
lcache1([Object | Cont], State, Sz0, Acc) ->
    Sz = decr_list_size(Sz0, Object),
    [Object | lcache2(Cont, State, Sz, [Object | Acc])];
lcache1(F, State, Sz, Acc) ->
    case F() of
        Objects when is_list(Objects) -> lcache1(Objects, State, Sz, Acc);
        Term -> Term
    end.

lcache2([Object | Cont], State, Sz0, Acc) when Sz0 >= 0 ->
    Sz = decr_list_size(Sz0, Object),
    [Object | lcache2(Cont, State, Sz, [Object | Acc])];
lcache2(Cont, State, Sz, Acc) ->
    fun() -> lcache1(Cont, State, Sz, Acc) end.

%% The temporary file of a list cache, opened when first needed; an error when temporary files are not allowed.
lcache_file({Key, LocalPost, TmpDir, _MaxList, TmpUsage}) ->
    case get(Key) of
        {file, Fd, TmpFile, _F} ->
            {TmpFile, Fd};
        undefined when TmpUsage =:= not_allowed ->
            qlc_error({tmpdir_usage, caching});
        undefined ->
            maybe_error_logger(TmpUsage, caching),
            FileName = tmp_filename(TmpDir),
            {F, Fd} = open_file(FileName, [write], LocalPost),
            put(Key, {file, Fd, FileName, F}),
            {FileName, Fd}
    end.

lcache_write(Fd, FileName, L) ->
    write_binary_terms([term_to_binary(T) || T <- lists:reverse(L)], Fd, FileName).

del_lcache(Ref) ->
    fun() ->
        Key = ?LCACHE_FILE(Ref),
        case get(Key) of
            undefined ->
                ok;
            {file, Fd, TmpFile, _F} ->
                _ = file:close(Fd),
                _ = file:delete(TmpFile),
                erase(Key);
            _L ->
                erase(Key)
        end
    end.

tag_objects([Object | Cont], T) ->
    [{Object, T} | tag_objects2(Cont, T + 1)];
tag_objects([] = Cont, _T) ->
    Cont;
tag_objects(F, T) ->
    case F() of
        Objects when is_list(Objects) -> tag_objects(Objects, T);
        Term -> Term
    end.

tag_objects2([Object | Cont], T) -> [{Object, T} | tag_objects2(Cont, T + 1)];
tag_objects2(Objects, T) -> fun() -> tag_objects(Objects, T) end.

untag_objects([] = Objs) ->
    Objs;
untag_objects([{Object, _N} | Cont]) ->
    [Object | untag_objects2(Cont)];
untag_objects(F) ->
    case F() of
        Objects when is_list(Objects) -> untag_objects(Objects);
        Term -> Term
    end.

untag_objects2([{Object, _N} | Cont]) -> [Object | untag_objects2(Cont)];
untag_objects2([] = Cont) -> Cont;
untag_objects2(Objects) -> fun() -> untag_objects(Objects) end.

append_loop([[_ | _] = L], _N) ->
    L;
append_loop([F], _N) ->
    F();
append_loop([L | Hs], N) ->
    append_loop(L, N, Hs).

append_loop([], N, Hs) ->
    append_loop(Hs, N);
append_loop([Object | Cont], N, Hs) ->
    [Object | append_loop(Cont, N + 1, Hs)];
append_loop(F, 0, Hs) ->
    case F() of
        [] -> append_loop(Hs, 0);
        [Object | Cont] -> [Object | append_loop(Cont, 1, Hs)];
        Term -> Term
    end;
append_loop(F, _N, Hs) ->
    fun() -> append_loop(F, 0, Hs) end.

%%% Sorting

sort_list(L, CFun, true, sort, _SortOptions, _Post) when is_function(CFun) ->
    lists:usort(CFun, L);
sort_list(L, CFun, false, sort, _SortOptions, _Post) when is_function(CFun) ->
    lists:sort(CFun, L);
sort_list(L, ascending, true, sort, _SortOptions, _Post) ->
    lists:usort(L);
sort_list(L, descending, true, sort, _SortOptions, _Post) ->
    lists:reverse(lists:usort(L));
sort_list(L, ascending, false, sort, _SortOptions, _Post) ->
    lists:sort(L);
sort_list(L, descending, false, sort, _SortOptions, _Post) ->
    lists:reverse(lists:sort(L));
sort_list(L, ascending, true, {keysort, Kp}, _SortOptions, _Post) when is_integer(Kp) ->
    lists:ukeysort(Kp, L);
sort_list(L, ascending, false, {keysort, Kp}, _SortOptions, _Post) when is_integer(Kp) ->
    lists:keysort(Kp, L);
sort_list(L, descending, true, {keysort, Kp}, _SortOptions, _Post) when is_integer(Kp) ->
    lists:reverse(lists:ukeysort(Kp, L));
sort_list(L, descending, false, {keysort, Kp}, _SortOptions, _Post) when is_integer(Kp) ->
    lists:reverse(lists:keysort(Kp, L));
sort_list(L, _Order, _Unique, Sort, SortOptions, Post) ->
    In = fun(_) -> {L, fun(_) -> end_of_input end} end,
    Out = sort_list_output([]),
    do_sort(In, Out, Sort, [{format, term} | SortOptions], Post).

sort_list_output(L) ->
    fun
        (close) -> lists:append(lists:reverse(L));
        (Terms) when is_list(Terms) -> sort_list_output([Terms | L])
    end.

%% Sort answers that are not a list; the file sorter runs only once the answers are known to need temporary files.
sort_handle(H, ListFun, FileFun, SortOptions, Post, LocalPost, TmpUsageM) ->
    Size =
        case lists:keyfind(size, 1, SortOptions) of
            {size, Size0} -> Size0;
            false -> default_option(size)
        end,
    sort_cache(H, [], Size, {ListFun, FileFun, Post, LocalPost, TmpUsageM}).

sort_cache([], CL, _Sz, {LF, _FF, Post, LocalPost, _TmpUsageM}) ->
    {LF(lists:reverse(CL)), Post, LocalPost};
sort_cache(Objs, CL, Sz, C) when Sz < 0 ->
    sort_cache2(Objs, CL, false, C);
sort_cache([Object | Cont], CL, Sz0, C) ->
    sort_cache(Cont, [Object | CL], decr_list_size(Sz0, Object), C);
sort_cache(F, CL, Sz, C) ->
    case F() of
        Objects when is_list(Objects) ->
            sort_cache(Objects, CL, Sz, C);
        Term ->
            {_LF, _FF, Post, _LocalPost, _TmpUsageM} = C,
            post_funs(Post),
            throw_error(Term)
    end.

%% Past the size limit: one more object makes sure the file sorter will use temporary files, duplicates removed or
%% not.
sort_cache2([], CL, _Found, {LF, _FF, Post, LocalPost, _TmpUsageM}) ->
    {LF(lists:reverse(CL)), Post, LocalPost};
sort_cache2([Object | Cont], CL, _Found, C) ->
    sort_cache2(Cont, [Object | CL], true, C);
sort_cache2(F, CL, false, C) ->
    case F() of
        Objects when is_list(Objects) ->
            sort_cache2(Objects, CL, true, C);
        Term ->
            {_LF, _FF, Post, _LocalPost, _TmpUsageM} = C,
            post_funs(Post),
            throw_error(Term)
    end;
sort_cache2(_Cont, _CL, true, {_LF, _FF, Post, _LocalPost, {not_allowed, M}}) ->
    post_funs(Post),
    throw_reason({tmpdir_usage, M});
sort_cache2(Cont, CL, true, {_LF, FF, _Post, _LocalPost, {TmpUsage, M}}) ->
    maybe_error_logger(TmpUsage, M),
    FF(lists:reverse(CL, Cont)).

%% Sort with the file sorter: answers come back as a list, or as a fun reading the sorted temporary file.
file_sort_handle(H, Kp, SortOptions, TmpDir, Compressed, Post, LocalPost) ->
    In = sort_cursor_input(H, 0),
    Unique = lists:member(unique, SortOptions) orelse lists:keymember(unique, 1, SortOptions),
    Out = sort_cursor_list_output(TmpDir, Compressed, Unique),
    case do_sort(In, Out, Kp, SortOptions, Post) of
        {file, FileName} ->
            {F, Fd} = open_file(FileName, Compressed, Post),
            P = fun() ->
                _ = file:close(Fd),
                _ = file:delete(FileName)
            end,
            {F, [P | Post], LocalPost};
        {terms, BTerms} ->
            try
                {[binary_to_term(B) || B <- BTerms], Post, LocalPost}
            catch
                Class:Reason:Stacktrace ->
                    post_funs(Post),
                    erlang:raise(Class, Reason, Stacktrace)
            end
    end.

do_sort(In, Out, Sort, SortOptions, Post) ->
    try
        case file_sort(In, Out, Sort, SortOptions) of
            {error, Reason} -> throw_reason(Reason);
            Reply -> Reply
        end
    catch
        Class:Term:Stacktrace ->
            post_funs(Post),
            erlang:raise(Class, Term, Stacktrace)
    end.

file_sort(In, Out, sort, SortOptions) -> file_sorter:sort(In, Out, SortOptions);
file_sort(In, Out, {keysort, KeyPos}, SortOptions) -> file_sorter:keysort(KeyPos, In, Out, SortOptions).

sort_cursor_input(H, NoObjects) ->
    fun
        (close) -> ok;
        (read) -> sort_cursor_input_read(H, NoObjects)
    end.

sort_cursor_input_read([], NoObjects) ->
    {end_of_input, NoObjects};
sort_cursor_input_read([Object | Cont], NoObjects) ->
    {[term_to_binary(Object)], sort_cursor_input(Cont, NoObjects + 1)};
sort_cursor_input_read(F, NoObjects) ->
    case F() of
        Objects when is_list(Objects) -> sort_cursor_input_read(Objects, NoObjects);
        Term -> throw_error(Term)
    end.

%% The file sorter's output: the sorted terms stay in memory when they arrive in one piece, else go to a file.
sort_cursor_list_output(TmpDir, Z, Unique) ->
    fun
        (close) ->
            {terms, []};
        ({value, NoObjects}) ->
            fun
                (BTerms) when Unique; length(BTerms) =:= NoObjects ->
                    fun
                        (close) -> {terms, BTerms};
                        (BTerms1) -> sort_cursor_file(BTerms ++ BTerms1, TmpDir, Z)
                    end;
                (BTerms) ->
                    sort_cursor_file(BTerms, TmpDir, Z)
            end
    end.

sort_cursor_file(BTerms, TmpDir, Z) ->
    FileName = tmp_filename(TmpDir),
    case file:open(FileName, [write, raw, binary | Z]) of
        {ok, Fd} -> (write_terms(FileName, Fd))(BTerms);
        Error -> throw_file_error(FileName, Error)
    end.

write_terms(FileName, Fd) ->
    fun
        (close) ->
            _ = file:close(Fd),
            {file, FileName};
        (BTerms) ->
            case file:write(Fd, size_bin(BTerms, [])) of
                ok ->
                    write_terms(FileName, Fd);
                Error ->
                    _ = file:close(Fd),
                    throw_file_error(FileName, Error)
            end
    end.

sort_options_global_tmp(S, "") -> S;
sort_options_global_tmp(S, TmpDir) -> [{tmpdir, TmpDir} | lists:keydelete(tmpdir, 1, S)].

%%% Temporary files: terms as 4-byte sizes and external format, read back in chunks

tmp_filename(TmpDirOpt) ->
    Name = lists:concat([qlc, "_", node(), "_", os:getpid(), "_", erlang:unique_integer()]),
    TmpDir =
        case TmpDirOpt of
            "" ->
                {ok, CurDir} = file:get_cwd(),
                CurDir;
            TDir ->
                TDir
        end,
    filename:join(filename:absname(TmpDir), Name).

%% Open a temporary file; the fun reads its terms from the start each time it is called.
open_file(FileName, Extra, Post) ->
    case file:open(FileName, [read, raw, binary | Extra]) of
        {ok, Fd} ->
            Read = fun() ->
                case file:position(Fd, bof) of
                    {ok, 0} ->
                        TF = fun
                            ([], _) -> [];
                            (Ts, C) when is_list(Ts) -> lists:reverse(Ts, C)
                        end,
                        file_loop_read(<<>>, ?CHUNK_SIZE, {Fd, FileName}, TF);
                    Error ->
                        file_error(FileName, Error)
                end
            end,
            {Read, Fd};
        Error ->
            post_funs(Post),
            throw_file_error(FileName, Error)
    end.

file_loop(Bin0, FdName, Ts0, TF) ->
    Terms =
        try
            file_loop2(Bin0, Ts0)
        catch
            _:_ ->
                {_Fd, FileName} = FdName,
                qlc_error({bad_object, FileName})
        end,
    case Terms of
        {terms, <<Size:4/unit:8, B/bytes>> = Bin, []} ->
            file_loop_read(Bin, Size - byte_size(B) + 4, FdName, TF);
        {terms, <<Size:4/unit:8, _/bytes>> = Bin, Ts} ->
            TF(Ts, fun() -> file_loop_read(Bin, Size + 4, FdName, TF) end);
        {terms, B, Ts} ->
            TF(Ts, fun() -> file_loop_read(B, ?CHUNK_SIZE, FdName, TF) end);
        Error ->
            Error
    end.

file_loop2(<<Size:4/unit:8, B:Size/bytes, Bin/bytes>>, Ts) -> file_loop2(Bin, [binary_to_term(B) | Ts]);
file_loop2(Bin, Ts) -> {terms, Bin, Ts}.

%% Read on with at least MinBytesToRead bytes; a term split between B and the next chunk is completed first.
file_loop_read(B, MinBytesToRead, {Fd, FileName} = FdName, TF) ->
    case file:read(Fd, erlang:max(?CHUNK_SIZE, MinBytesToRead)) of
        {ok, Bin} when byte_size(B) =:= 0 ->
            file_loop(Bin, FdName, [], TF);
        {ok, Bin} ->
            case B of
                <<Size:4/unit:8, Tl/bytes>> when byte_size(Bin) + byte_size(Tl) >= Size ->
                    {B1, B2} = split_binary(Bin, Size - byte_size(Tl)),
                    %% TF is applied exactly once.
                    Single = fun([T], Fun) -> [T | Fun] end,
                    case file_loop(list_to_binary([B, B1]), FdName, [], Single) of
                        [T | Fun] ->
                            true = is_function(Fun),
                            file_loop(B2, FdName, [T], TF);
                        Error ->
                            Error
                    end;
                _ ->
                    file_loop(list_to_binary([B, Bin]), FdName, [], TF)
            end;
        eof when byte_size(B) =:= 0 ->
            TF([], foo);
        eof ->
            qlc_error({bad_object, FileName});
        Error ->
            file_error(FileName, Error)
    end.

write_binary_terms(BTerms, Fd, FileName) ->
    case file:write(Fd, size_bin(BTerms, [])) of
        ok -> ok;
        Error -> file_error(FileName, Error)
    end.

size_bin([], L) -> L;
size_bin([BinTerm | BinTerms], L) -> size_bin(BinTerms, [L, <<(byte_size(BinTerm)):4/unit:8>> | BinTerm]).

decr_list_size(Sz0, E) when is_integer(Sz0) ->
    Sz0 - erlang:external_size(E).

%%% Merge join: both inputs sorted on their join columns; equal keys of the second input are kept in memory, or on a
%%% temporary file when they exceed max_list_size bytes.

merge_join([] = Cont, _C1, _T2, _C2, _Opt) ->
    Cont;
merge_join([E1 | L1], C1, L2, C2, Opt) ->
    #qlc_opt{tmpdir = TmpDir, max_list = MaxList, tmpdir_usage = TmpUsage} = Opt,
    M = #m{id = merge_join_id(), tmpdir = TmpDir, max_list = MaxList, tmp_usage = TmpUsage},
    merge_join2(E1, element(C1, E1), L1, C1, L2, C2, M);
merge_join(F1, C1, L2, C2, Opt) ->
    case F1() of
        L1 when is_list(L1) -> merge_join(L1, C1, L2, C2, Opt);
        T1 -> T1
    end.

merge_join1(_E2, _K2, [] = Cont, _C1, _L2, _C2, M) ->
    end_merge_join(Cont, M);
merge_join1(E2, K2, [E1 | L1], C1, L2, C2, M) ->
    K1 = element(C1, E1),
    if
        K1 == K2 -> same_keys2(E1, K1, L1, C1, L2, C2, E2, M);
        K1 > K2 -> merge_join2(E1, K1, L1, C1, L2, C2, M);
        true -> merge_join1(E2, K2, L1, C1, L2, C2, M)
    end;
merge_join1(E2, K2, F1, C1, L2, C2, M) ->
    case F1() of
        L1 when is_list(L1) -> merge_join1(E2, K2, L1, C1, L2, C2, M);
        T1 -> T1
    end.

merge_join2(_E1, _K1, _L1, _C1, [] = Cont, _C2, M) ->
    end_merge_join(Cont, M);
merge_join2(E1, K1, L1, C1, [E2 | L2], C2, M) ->
    K2 = element(C2, E2),
    if
        K1 == K2 -> same_keys2(E1, K1, L1, C1, L2, C2, E2, M);
        K1 > K2 -> merge_join2(E1, K1, L1, C1, L2, C2, M);
        true -> merge_join1(E2, K2, L1, C1, L2, C2, M)
    end;
merge_join2(E1, K1, L1, C1, F2, C2, M) ->
    case F2() of
        L2 when is_list(L2) -> merge_join2(E1, K1, L1, C1, L2, C2, M);
        T2 -> T2
    end.

%% element(C2, E2_0) == K1.
same_keys2(E1, K1, L1, C1, [], _C2, E2_0, M) ->
    Cont = fun(_L1b) -> end_merge_join([], M) end,
    loop_same_keys(E1, K1, L1, C1, [E2_0], Cont, M);
same_keys2(E1, K1, L1, C1, [E2 | L2] = L2_0, C2, E2_0, M) ->
    K2 = element(C2, E2),
    if
        K1 == K2 -> same_keys1(E1, K1, L1, C1, E2, C2, E2_0, L2, M);
        K1 < K2 -> [?JWRAP(E1, E2_0) | fun() -> same_loop1(L1, K1, C1, E2_0, L2_0, C2, M) end]
    end;
same_keys2(E1, K1, L1, C1, F2, C2, E2_0, M) ->
    case F2() of
        L2 when is_list(L2) ->
            same_keys2(E1, K1, L1, C1, L2, C2, E2_0, M);
        T2 ->
            Cont = fun(_L1b) -> T2 end,
            loop_same_keys(E1, K1, L1, C1, [E2_0], Cont, M)
    end.

same_loop1([], _K1_0, _C1, _E2_0, _L2, _C2, M) ->
    end_merge_join([], M);
same_loop1([E1 | L1], K1_0, C1, E2_0, L2, C2, M) ->
    K1 = element(C1, E1),
    if
        K1 == K1_0 -> [?JWRAP(E1, E2_0) | fun() -> same_loop1(L1, K1_0, C1, E2_0, L2, C2, M) end];
        K1_0 < K1 -> merge_join2(E1, K1, L1, C1, L2, C2, M)
    end;
same_loop1(F1, K1_0, C1, E2_0, L2, C2, M) ->
    case F1() of
        L1 when is_list(L1) -> same_loop1(L1, K1_0, C1, E2_0, L2, C2, M);
        T1 -> T1
    end.

%% element(C2, E2_0) == K1, element(C2, E2) == K1_0.
same_keys1(E1_0, K1_0, [] = L1, C1, E2, C2, E2_0, L2, M) ->
    [?JWRAP(E1_0, E2_0), ?JWRAP(E1_0, E2) | fun() -> same_keys(K1_0, E1_0, L1, C1, L2, C2, M) end];
same_keys1(E1_0, K1_0, [E1 | _] = L1, C1, E2, C2, E2_0, L2, M) ->
    K1 = element(C1, E1),
    if
        K1_0 == K1 ->
            E2s = [E2, E2_0],
            Sz0 = decr_list_size(M#m.max_list, E2s),
            same_keys_cache(E1_0, K1_0, L1, C1, L2, C2, E2s, Sz0, M);
        K1_0 < K1 ->
            [?JWRAP(E1_0, E2_0), ?JWRAP(E1_0, E2) | fun() -> same_keys(K1_0, E1_0, L1, C1, L2, C2, M) end]
    end;
same_keys1(E1_0, K1_0, F1, C1, E2, C2, E2_0, L2, M) ->
    case F1() of
        L1 when is_list(L1) -> same_keys1(E1_0, K1_0, L1, C1, E2, C2, E2_0, L2, M);
        T1 -> loop_same(E1_0, [E2, E2_0], fun() -> T1 end)
    end.

%% No element E of L1 has element(C1, E) == K1.
same_keys(_K1, _E1, _L1, _C1, [] = Cont, _C2, M) ->
    end_merge_join(Cont, M);
same_keys(K1, E1, L1, C1, [E2 | L2], C2, M) ->
    K2 = element(C2, E2),
    if
        K1 == K2 -> [?JWRAP(E1, E2) | fun() -> same_keys(K1, E1, L1, C1, L2, C2, M) end];
        K1 < K2 -> merge_join1(E2, K2, L1, C1, L2, C2, M)
    end;
same_keys(K1, E1, L1, C1, F2, C2, M) ->
    case F2() of
        L2 when is_list(L2) -> same_keys(K1, E1, L1, C1, L2, C2, M);
        T2 -> T2
    end.

%% At least two elements of [E1 | L1] combine with E2s (more than one element), all of which fit in memory so far.
same_keys_cache(E1, K1, L1, C1, [], _C2, E2s, _Sz, M) ->
    Cont = fun(_L1b) -> end_merge_join([], M) end,
    loop_same_keys(E1, K1, L1, C1, E2s, Cont, M);
same_keys_cache(E1, K1, L1, C1, L2, C2, E2s, Sz0, M) when Sz0 < 0 ->
    case init_merge_join(M) of
        ok ->
            Sz = M#m.max_list,
            C = fun() -> same_keys_file(E1, K1, L1, C1, L2, C2, [], Sz, M) end,
            write_same_keys(E1, E2s, M, C);
        Error ->
            Error
    end;
same_keys_cache(E1, K1, L1, C1, [E2 | L2], C2, E2s, Sz0, M) ->
    K2 = element(C2, E2),
    if
        K1 == K2 ->
            same_keys_cache(E1, K1, L1, C1, L2, C2, [E2 | E2s], decr_list_size(Sz0, E2), M);
        K1 < K2 ->
            Cont = fun(L1b) -> merge_join1(E2, K2, L1b, C1, L2, C2, M) end,
            loop_same_keys(E1, K1, L1, C1, E2s, Cont, M)
    end;
same_keys_cache(E1, K1, L1, C1, F2, C2, E2s, Sz, M) ->
    case F2() of
        L2 when is_list(L2) ->
            same_keys_cache(E1, K1, L1, C1, L2, C2, E2s, Sz, M);
        T2 ->
            Cont = fun(_L1b) -> T2 end,
            loop_same_keys(E1, K1, L1, C1, E2s, Cont, M)
    end.

%% E2s holds every element of the second input with key K1.
loop_same_keys(E1, _K1, [], _C1, E2s, _Cont, M) ->
    end_merge_join(loop_same(E1, E2s, []), M);
loop_same_keys(E1, K1, L1, C1, E2s, Cont, M) ->
    loop_same(E1, E2s, fun() -> loop_keys(K1, L1, C1, E2s, Cont, M) end).

loop_same(_E1, [], L) -> L;
loop_same(E1, [E2 | E2s], L) -> loop_same(E1, E2s, [?JWRAP(E1, E2) | L]).

loop_keys(K, [E1 | L1] = L1_0, C1, E2s, Cont, M) ->
    K1 = element(C1, E1),
    if
        K1 == K -> loop_same_keys(E1, K1, L1, C1, E2s, Cont, M);
        K1 > K -> Cont(L1_0)
    end;
loop_keys(_K, [] = L1, _C1, _Es2, Cont, _M) ->
    Cont(L1);
loop_keys(K, F1, C1, E2s, Cont, M) ->
    case F1() of
        L1 when is_list(L1) -> loop_keys(K, L1, C1, E2s, Cont, M);
        T1 -> T1
    end.

%% The equal keys of the second input on a temporary file.
same_keys_file(E1, K1, L1, C1, [], _C2, E2s, _Sz, M) ->
    Cont = fun(_L1b) -> end_merge_join([], M) end,
    same_keys_file_write(E1, K1, L1, C1, E2s, M, Cont);
same_keys_file(E1, K1, L1, C1, L2, C2, E2s, Sz0, M) when Sz0 < 0 ->
    Sz = M#m.max_list,
    C = fun() -> same_keys_file(E1, K1, L1, C1, L2, C2, [], Sz, M) end,
    write_same_keys(E1, E2s, M, C);
same_keys_file(E1, K1, L1, C1, [E2 | L2], C2, E2s, Sz0, M) ->
    K2 = element(C2, E2),
    if
        K1 == K2 ->
            same_keys_file(E1, K1, L1, C1, L2, C2, [E2 | E2s], decr_list_size(Sz0, E2), M);
        K1 < K2 ->
            Cont = fun(L1b) -> merge_join1(E2, K2, L1b, C1, L2, C2, M) end,
            same_keys_file_write(E1, K1, L1, C1, E2s, M, Cont)
    end;
same_keys_file(E1, K1, L1, C1, F2, C2, E2s, Sz, M) ->
    case F2() of
        L2 when is_list(L2) ->
            same_keys_file(E1, K1, L1, C1, L2, C2, E2s, Sz, M);
        T2 ->
            Cont = fun(_L1b) -> T2 end,
            same_keys_file_write(E1, K1, L1, C1, E2s, M, Cont)
    end.

same_keys_file_write(E1, K1, L1, C1, E2s, M, Cont) ->
    write_same_keys(E1, E2s, M, fun() -> loop_keys_file(K1, L1, C1, Cont, M) end).

write_same_keys(_E1, [], _M, Cont) -> Cont();
write_same_keys(E1, Es2, M, Cont) -> write_same_keys(E1, Es2, M, [], Cont).

%% Write E2s and answer their joins with E1, saving one traversal of the file.
write_same_keys(_E1, [], M, E2s, Objs) ->
    case write_merge_join(M, E2s) of
        ok -> Objs;
        Error -> Error
    end;
write_same_keys(E1, [E2 | E2s0], M, E2s, Objs) ->
    write_same_keys(E1, E2s0, M, [term_to_binary(E2) | E2s], [?JWRAP(E1, E2) | Objs]).

loop_keys_file(K, [E1 | L1] = L1_0, C1, Cont, M) ->
    K1 = element(C1, E1),
    if
        K1 == K -> read_merge_join(M, E1, fun() -> loop_keys_file(K1, L1, C1, Cont, M) end);
        K1 > K -> Cont(L1_0)
    end;
loop_keys_file(_K, [] = L1, _C1, Cont, _M) ->
    Cont(L1);
loop_keys_file(K, F1, C1, Cont, M) ->
    case F1() of
        L1 when is_list(L1) -> loop_keys_file(K, L1, C1, Cont, M);
        T1 -> T1
    end.

end_merge_join(Reply, M) ->
    end_merge_join(M),
    Reply.

%% The temporary files of merge joins live in the process dictionary: they are created after the setup that
%% prepares the post funs, which remove them through end_all_merge_joins/0.
init_merge_join(#m{id = MergeId, tmpdir = TmpDir, tmp_usage = TmpUsage}) ->
    case tmp_merge_file(MergeId) of
        {Fd, FileName} ->
            case file:position(Fd, bof) of
                {ok, 0} ->
                    case file:truncate(Fd) of
                        ok -> ok;
                        Error -> file_error(FileName, Error)
                    end;
                Error ->
                    file_error(FileName, Error)
            end;
        none when TmpUsage =:= not_allowed ->
            qlc_error({tmpdir_usage, joining});
        none ->
            maybe_error_logger(TmpUsage, joining),
            FileName = tmp_filename(TmpDir),
            case file:open(FileName, [raw, binary, read, write]) of
                {ok, Fd} ->
                    put(?MERGE_JOIN_FILE, [{MergeId, Fd, FileName} | get(?MERGE_JOIN_FILE)]),
                    ok;
                Error ->
                    file_error(FileName, Error)
            end
    end.

write_merge_join(#m{id = MergeId}, BTerms) ->
    {Fd, FileName} = tmp_merge_file(MergeId),
    write_binary_terms(BTerms, Fd, FileName).

read_merge_join(#m{id = MergeId}, E1, Cont) ->
    {Fd, FileName} = tmp_merge_file(MergeId),
    case file:position(Fd, bof) of
        {ok, 0} ->
            Fun = fun
                ([], _) -> Cont();
                (Ts, C) when is_list(Ts) -> join_read_terms(E1, Ts, C)
            end,
            file_loop_read(<<>>, ?CHUNK_SIZE, {Fd, FileName}, Fun);
        Error ->
            file_error(FileName, Error)
    end.

join_read_terms(_E1, [], Objs) -> Objs;
join_read_terms(E1, [E2 | E2s], Objs) -> join_read_terms(E1, E2s, [?JWRAP(E1, E2) | Objs]).

end_merge_join(#m{id = MergeId}) ->
    case tmp_merge_file(MergeId) of
        none ->
            ok;
        {Fd, FileName} ->
            _ = file:close(Fd),
            _ = file:delete(FileName),
            put(?MERGE_JOIN_FILE, lists:keydelete(MergeId, 1, get(?MERGE_JOIN_FILE)))
    end.

end_all_merge_joins() ->
    Ids = [Id || {Id, _Fd, _FileName} <- lists:flatten([get(?MERGE_JOIN_FILE)])],
    lists:foreach(fun(Id) -> end_merge_join(#m{id = Id}) end, Ids),
    erase(?MERGE_JOIN_FILE).

merge_join_id() ->
    case get(?MERGE_JOIN_FILE) of
        undefined -> put(?MERGE_JOIN_FILE, []);
        _ -> ok
    end,
    make_ref().

tmp_merge_file(MergeId) ->
    case lists:keyfind(MergeId, 1, get(?MERGE_JOIN_FILE)) of
        {MergeId, Fd, FileName} -> {Fd, FileName};
        false -> none
    end.

%%% Lookup join: each answer of the traversed input looks up its key in the table.

lookup_join([E1 | L1], C1, LuF, C2, Rev) ->
    Next = fun() -> lookup_join(L1, C1, LuF, C2, Rev) end,
    case LuF(C2, [element(C1, E1)]) of
        [] -> lookup_join(L1, C1, LuF, C2, Rev);
        [E2] when Rev -> [?JWRAP(E2, E1) | Next];
        [E2] -> [?JWRAP(E1, E2) | Next];
        E2s when is_list(E2s), Rev -> [?JWRAP(E2, E1) || E2 <- E2s] ++ Next;
        E2s when is_list(E2s) -> [?JWRAP(E1, E2) || E2 <- E2s] ++ Next;
        Term -> Term
    end;
lookup_join([] = Cont, _C1, _LuF, _C2, _Rev) ->
    Cont;
lookup_join(F1, C1, LuF, C2, Rev) ->
    case F1() of
        L1 when is_list(L1) -> lookup_join(L1, C1, LuF, C2, Rev);
        T1 -> T1
    end.

%%% Info: the prepared handle as abstract code

%% An intermediate description of a prepared handle: {qlc, Template, Qualifiers, Options}, {table, Desc},
%% {append, Infos}, {sort, Info, Options}, {keysort, Info, KeyPos, Options}, {list, L} or {list, Info, MS}. Template,
%% filters and patterns are external format binaries of abstract code.
le_info(#prepared{qh = #simple_qlc{le = LE, p = P, anno = Anno, optz = Optz}}, InfOpt) ->
    QVar = term_to_binary({var, Anno, P}),
    {qlc, QVar, [{generate, QVar, le_info(LE, InfOpt)}], opt_info(Optz)};
le_info(#prepared{qh = #qlc{codef = CodeF, qdata = Qdata, optz = Optz}}, InfOpt) ->
    Code = CodeF(),
    QualInfo0 = qual_info(Qdata, Code, InfOpt),
    QualInfo1 =
        case Optz#optz.fast_join of
            #qlc_join{} = Join -> join_info(Join, QualInfo0, Qdata, Code);
            no -> QualInfo0
        end,
    {qlc, element(template_state(), Code), [I || I <- QualInfo1, I =/= skip], opt_info(Optz)};
le_info(#prepared{qh = #qlc_table{} = Table}, InfOpt) ->
    table_info(Table, InfOpt);
le_info(#prepared{qh = #qlc_append{hl = HL}}, InfOpt) ->
    {append, [le_info(H, InfOpt) || H <- HL]};
le_info(#prepared{qh = #qlc_sort{h = H, keypos = sort, fs_opts = SortOptions, tmpdir = TmpDir}}, InfOpt) ->
    {sort, le_info(H, InfOpt), sort_options_global_tmp(SortOptions, TmpDir)};
le_info(#prepared{qh = #qlc_sort{h = H, keypos = {keysort, Kp}, fs_opts = SortOptions, tmpdir = TmpDir}}, InfOpt) ->
    {keysort, le_info(H, InfOpt), Kp, sort_options_global_tmp(SortOptions, TmpDir)};
le_info(#prepared{qh = #qlc_list{l = L, ms = no_match_spec}}, _InfOpt) ->
    {list, L};
le_info(#prepared{qh = #qlc_list{l = L, ms = MS}}, _InfOpt) when is_list(L) ->
    {list, {list, L}, MS};
le_info(#prepared{qh = #qlc_list{l = L, ms = MS}}, InfOpt) ->
    {list, le_info(L, InfOpt), MS}.

%% A table as its format fun describes the selected objects; depth applies to match specifications too.
table_info(#qlc_table{format_fun = FormatFun, trav_MS = TravMS, ms = MS, lu_vals = LuVals}, {NElements, Depth}) ->
    DepthFun = depth_fun(Depth),
    case LuVals of
        _ when FormatFun =:= undefined ->
            {table, {'$MOD', '$FUN', []}};
        {Pos, Vals} ->
            Formatted =
                try
                    FormatFun({lookup, Pos, Vals, NElements, DepthFun})
                catch
                    _:_ -> FormatFun({lookup, Pos, Vals})
                end,
            case MS of
                no_match_spec -> {table, Formatted};
                _ -> {list, {table, Formatted}, depth(MS, Depth)}
            end;
        _ when TravMS, is_list(MS) ->
            {table, FormatFun({match_spec, depth(MS, Depth)})};
        _ when MS =:= no_match_spec ->
            try
                {table, FormatFun({all, NElements, DepthFun})}
            catch
                _:_ -> {table, FormatFun(all)}
            end
    end.

qual_info([{_QNum, _GoI, ?SKIP, fil} | Qdata], Code, InfOpt) ->
    [skip | qual_info(Qdata, Code, InfOpt)];
qual_info([{QNum, _GoI, _SI, fil} | Qdata], Code, InfOpt) ->
    [element(QNum + 1, Code) | qual_info(Qdata, Code, InfOpt)];
qual_info([{_QNum, _GoI, _SI, {gen, #join{}}} | Qdata], Code, InfOpt) ->
    [skip | qual_info(Qdata, Code, InfOpt)];
qual_info([{QNum, _GoI, _SI, {gen, LE}} | Qdata], Code, InfOpt) ->
    [{generate, element(QNum + 1, Code), le_info(LE, InfOpt)} | qual_info(Qdata, Code, InfOpt)];
qual_info([], _Code, _InfOpt) ->
    [].

%% The join as an inserted query over the two generators: [G1 | G2] answers with the join filter and options.
join_info(Join, QInfo, Qdata, Code) ->
    #qlc_join{kind = Kind, q1 = QNum1a, c1 = C1, q2 = QNum2a, c2 = C2, opt = Opt} = Join,
    {{JQNum, _, _, _}, Rev, QNum1, QNum2, _WH1, _WH2, CsFun} = find_join_data(Qdata, QNum1a, QNum2a),
    %% Only constants compared with '=='.
    {Cs1, Cs2, _Compat} = CsFun(),
    Anno = anno0(),
    G1_0 = {var, Anno, 'G1'},
    G2_0 = {var, Anno, 'G2'},
    JP = element(JQNum + 1, Code),
    {{I1, G1}, {I2, G2}, QInfoL} =
        case Kind of
            {merge, _} ->
                {JG1, QInfo1} = join_merge_info(QNum1, QInfo, Code, G1_0, Cs1),
                {JG2, QInfo2} = join_merge_info(QNum2, QInfo, Code, G2_0, Cs2),
                {JG1, JG2, QInfo1 ++ QInfo2};
            _ when Rev ->
                {JG2, QInfo2} = join_merge_info(QNum2, QInfo, Code, G2_0, Cs2),
                {J1, QInfo1} = join_lookup_info(QNum1, QInfo, G1_0),
                {{J1, G1_0}, JG2, QInfo2 ++ [QInfo1]};
            _ ->
                {JG1, QInfo1} = join_merge_info(QNum1, QInfo, Code, G1_0, Cs1),
                {J2, QInfo2} = join_lookup_info(QNum2, QInfo, G2_0),
                {JG1, {J2, G2_0}, QInfo1 ++ [QInfo2]}
        end,
    {JOptVal, JOp} = kind2op(Kind),
    JOpt = [{join, JOptVal}] ++ opt_info(join_unique_cache(Opt)),
    Element = fun(C, G) -> {call, Anno, {atom, Anno, element}, [{integer, Anno, C}, G]} end,
    JFil = term_to_binary({op, Anno, JOp, Element(C1, G1), Element(C2, G2)}),
    P = term_to_binary({cons, Anno, G1, G2}),
    JInfo = {generate, JP, {qlc, P, QInfoL ++ [JFil], JOpt}},
    {Before, [I1 | After]} = lists:split(QNum1 - 1, QInfo),
    Before ++ [JInfo] ++ lists:delete(I2, After).

kind2op({merge, _KE}) -> {merge, '=='};
kind2op({lookup, KE, _LUFun}) -> {lookup, KE}.

%% qlc:q([P0 || P0 = Pattern <- H1, ConstFilters]) with a fresh P0, ConstFilters testing the pattern's constant
%% columns; just the generator when the pattern is a variable and there are no constants.
join_merge_info(QNum, QInfo, Code, G, ExtraConstants) ->
    {generate, _, LEInfo} = I = lists:nth(QNum, QInfo),
    P = binary_to_term(element(QNum + 1, Code)),
    case {P, ExtraConstants} of
        {{var, _, _}, []} ->
            {{I, G}, [{generate, term_to_binary(G), LEInfo}]};
        _ ->
            {EPV, M} =
                case P of
                    {var, _, _} ->
                        {P, P};
                    _ ->
                        {PV, _} = aux_name1('P', 0, abstract_vars(P)),
                        V = {var, anno0(), PV},
                        {V, {match, anno0(), V, P}}
                end,
            LEI = {generate, term_to_binary(M), LEInfo},
            CFs = [term_to_binary(constant_filter(Col, ConstOps, EPV)) || {Col, ConstOps} <- ExtraConstants],
            {{I, G}, [{generate, term_to_binary(G), {qlc, term_to_binary(EPV), [LEI | CFs], []}}]}
    end.

%% The filter comparing column Col of the pattern variable with each constant, the comparisons joined by 'or'.
constant_filter(Col, ConstOps, EPV) ->
    A = anno0(),
    Call = {call, A, {atom, A, element}, [{integer, A, Col}, EPV]},
    list2op([{op, A, Op, abstract_term(Con), Call} || {Con, Op} <- ConstOps], 'or', A).

list2op([E], _Op, _Anno) -> E;
list2op([E | Es], Op, Anno) -> {op, Anno, Op, E, list2op(Es, Op, Anno)}.

join_lookup_info(QNum, QInfo, G) ->
    {generate, _, LEInfo} = I = lists:nth(QNum, QInfo),
    {I, {generate, term_to_binary(G), LEInfo}}.

%% The options a query shows: cache and unique when not the defaults, and nested_loop only when asked for (this
%% module does not know every nested loop it runs).
opt_info(#optz{unique = Unique, cache = Cache0, join_option = JoinOption}) ->
    Cache =
        case Cache0 of
            true -> ets;
            _ -> Cache0
        end,
    [{T, V} || {T, V} <- [{cache, Cache}, {unique, Unique}], V =/= default_option(T)] ++
        [{join, JoinOption} || JoinOption =:= nested_loop].

%% The abstract code of a description: one query, or with Flat a block binding the nested queries to variables.
abstract(Info, false = _Flat, NElements, Depth) ->
    abstract(Info, NElements, Depth);
abstract(Info, true = _Flat, NElements, Depth) ->
    Abstract = abstract(Info, NElements, Depth),
    Vars = abstract_vars(Abstract),
    {_, Body0, Expr} = flatten_abstr(Abstract, 1, Vars, []),
    case Body0 of
        [] -> Expr;
        [{match, _, Expr, Q}] -> Q;
        [{match, _, Expr, Q} | Body] -> {block, anno0(), lists:reverse(Body, [Q])};
        _ -> {block, anno0(), lists:reverse(Body0, [Expr])}
    end.

abstract(Info, NElements, Depth) ->
    abstract1(Info, NElements, Depth, anno1()).

abstract1({qlc, E0, Qs0, Opt}, NElements, Depth, A) ->
    Qs = [
        case Q of
            {generate, P, LE} -> {generate, A, binary_to_term(P), abstract1(LE, NElements, Depth, A)};
            F -> binary_to_term(F)
        end
     || Q <- Qs0
    ],
    Os =
        case Opt of
            [] -> [];
            _ -> [abstract_term(Opt, 1)]
        end,
    ?QLC_Q(A, A, A, A, {lc, A, binary_to_term(E0), Qs}, Os);
abstract1({table, {M, F, As0}}, _NElements, _Depth, Anno) when is_atom(M), is_atom(F), is_list(As0) ->
    As = [abstract_term(Arg, 1) || Arg <- As0],
    {call, Anno, {remote, Anno, {atom, Anno, M}, {atom, Anno, F}}, As};
abstract1({table, TableDesc}, _NElements, _Depth, _A) ->
    case io_lib:deep_char_list(TableDesc) of
        true ->
            {ok, Tokens, _} = erl_scan:string(lists:flatten(TableDesc ++ "."), 1, [text]),
            {ok, [Expr]} = erl_eval:extended_parse_exprs(Tokens),
            special(Expr);
        false ->
            %% An abstract expression.
            TableDesc
    end;
abstract1({append, Infos}, NElements, Depth, A) ->
    As = lists:foldr(fun(Info, As0) -> {cons, A, abstract1(Info, NElements, Depth, A), As0} end, {nil, A}, Infos),
    {call, A, {remote, A, {atom, A, qlc}, {atom, A, append}}, [As]};
abstract1({sort, Info, SortOptions}, NElements, Depth, A) ->
    {call, A, {remote, A, {atom, A, qlc}, {atom, A, sort}}, [
        abstract1(Info, NElements, Depth, A), abstract_term(SortOptions, 1)
    ]};
abstract1({keysort, Info, Kp, SortOptions}, NElements, Depth, A) ->
    {call, A, {remote, A, {atom, A, qlc}, {atom, A, keysort}}, [
        abstract_term(Kp, 1), abstract1(Info, NElements, Depth, A), abstract_term(SortOptions, 1)
    ]};
abstract1({list, L, MS}, NElements, Depth, A) ->
    Compiled =
        {call, A, {remote, A, {atom, A, ets}, {atom, A, match_spec_compile}}, [abstract_term(depth(MS, Depth), 1)]},
    {call, A, {remote, A, {atom, A, ets}, {atom, A, match_spec_run}}, [abstract1(L, NElements, Depth, A), Compiled]};
abstract1({list, L}, NElements, Depth, _A) when NElements =:= infinity; NElements >= length(L) ->
    abstract_term(depth(L, Depth), 1);
abstract1({list, L}, NElements, Depth, _A) ->
    abstract_term(depth(lists:sublist(L, NElements), Depth) ++ '...', 1).

%% A table description parsed by erl_eval: its {value, _, Term} parts (pids, ports, references) as terms.
special({value, _, Thing}) -> abstract_term(Thing);
special(Tuple) when is_tuple(Tuple) -> list_to_tuple(special(tuple_to_list(Tuple)));
special([E | Es]) -> [special(E) | special(Es)];
special(Expr) -> Expr.

%% info/2's abstract_code answer: pids, ports and references printed as strings.
abstract_code({special, Anno, String}) -> {string, Anno, String};
abstract_code(Tuple) when is_tuple(Tuple) -> list_to_tuple(abstract_code(tuple_to_list(Tuple)));
abstract_code([H | T]) -> [abstract_code(H) | abstract_code(T)];
abstract_code(Term) -> Term.

depth(List, infinity) -> List;
depth(List, Depth) -> [depth1(E, Depth) || E <- List].

depth_fun(infinity = _Depth) -> fun(E) -> E end;
depth_fun(Depth) -> fun(E) -> depth1(E, Depth) end.

%% Term with its parts below Depth replaced by '...'.
depth1([] = L, _D) ->
    L;
depth1(_Term, 0) ->
    '...';
depth1(Tuple, D) when is_tuple(Tuple) ->
    depth_tuple(Tuple, tuple_size(Tuple), 1, D - 1, []);
depth1(List, 1) when is_list(List) ->
    ['...'];
depth1(List, D) when is_list(List) ->
    depth_list(List, D - 1);
depth1(Binary, D) when byte_size(Binary) > D - 1 ->
    D1 = D - 1,
    <<Bin:D1/bytes, _/bytes>> = Binary,
    <<Bin/bytes, "...">>;
depth1(T, _Depth) ->
    T.

depth_list([] = L, _D) -> L;
depth_list(_L, 0) -> '...';
depth_list([E | Es], D) -> [depth1(E, D) | depth_list(Es, D - 1)].

depth_tuple(_Tuple, Sz, I, _D, L) when I > Sz -> list_to_tuple(lists:reverse(L));
depth_tuple(_L, _Sz, _I, 0, L) -> list_to_tuple(lists:reverse(L, ['...']));
depth_tuple(Tuple, Sz, I, D, L) -> depth_tuple(Tuple, Sz, I + 1, D - 1, [depth1(element(I, Tuple), D) | L]).

abstract_term(Term) ->
    abstract_term(Term, 0).

abstract_term(Term, Line) ->
    abstr_term(Term, anno(Line)).

%% A term as abstract code: funs as their code or names, pids, ports and references as special strings.
abstr_term(Tuple, Anno) when is_tuple(Tuple) ->
    {tuple, Anno, [abstr_term(E, Anno) || E <- tuple_to_list(Tuple)]};
abstr_term([_ | _] = L, Anno) ->
    case io_lib:char_list(L) of
        true -> erl_parse:abstract(L, erl_anno:line(Anno));
        false -> abstr_list(L, Anno)
    end;
abstr_term(Fun, Anno) when is_function(Fun) ->
    case erl_eval:fun_data(Fun) of
        {fun_data, _Bs, Cs} -> {'fun', Anno, {clauses, Cs}};
        {named_fun_data, _Bs, Name, Cs} -> {named_fun, Anno, Name, Cs};
        false -> compiled_fun(Fun, Anno)
    end;
abstr_term(PPR, Anno) when is_pid(PPR); is_port(PPR); is_reference(PPR) ->
    {special, Anno, lists:flatten(io_lib:write(PPR))};
abstr_term(Map, Anno) when is_map(Map) ->
    {map, Anno, [{map_field_assoc, Anno, abstr_term(K, Anno), abstr_term(V, Anno)} || {K, V} <- maps:to_list(Map)]};
abstr_term(Simple, Anno) ->
    erl_parse:abstract(Simple, erl_anno:line(Anno)).

compiled_fun(Fun, Anno) ->
    {name, Name} = erlang:fun_info(Fun, name),
    {arity, Arity} = erlang:fun_info(Fun, arity),
    case erlang:fun_info(Fun, type) of
        {type, external} ->
            {module, Module} = erlang:fun_info(Fun, module),
            {'fun', Anno, {function, {atom, Anno, Module}, {atom, Anno, Name}, {integer, Anno, Arity}}};
        {type, local} ->
            {'fun', Anno, {function, Name, Arity}}
    end.

abstr_list([H | T], Anno) -> {cons, Anno, abstr_term(H, Anno), abstr_list(T, Anno)};
abstr_list(T, Anno) -> abstr_term(T, Anno).

%% Bind the nested queries to fresh variables V1, V2, ...; pattern variables of generators cannot be used in list
%% expressions, so this is safe.
flatten_abstr(?QLC_Q(A1, A2, A3, A4, LC0, Os), VN0, Vars, Body0) ->
    {lc, Anno, E, Qs0} = LC0,
    Flatten = fun
        ({generate, AnnoG, P, LE0}, {VN1, Body1}) ->
            {VN2, Body2, LE} = flatten_abstr(LE0, VN1, Vars, Body1),
            {{generate, AnnoG, P, LE}, {VN2, Body2}};
        (Fil, VNBody) ->
            {Fil, VNBody}
    end,
    {Qs, {VN3, Body}} = lists:mapfoldl(Flatten, {VN0, Body0}, Qs0),
    {V, VN} = aux_name1('V', VN3, Vars),
    Var = {var, A1, V},
    QLC = ?QLC_Q(A1, A2, A3, A4, {lc, Anno, E, Qs}, Os),
    {VN + 1, [{match, A1, Var, QLC} | Body], Var};
flatten_abstr(T0, VN0, Vars, Body0) when is_tuple(T0) ->
    {VN, Body, L} = flatten_abstr(tuple_to_list(T0), VN0, Vars, Body0),
    {VN, Body, list_to_tuple(L)};
flatten_abstr([E0 | Es0], VN0, Vars, Body0) ->
    {VN1, Body1, E} = flatten_abstr(E0, VN0, Vars, Body0),
    {VN, Body, Es} = flatten_abstr(Es0, VN1, Vars, Body1),
    {VN, Body, [E | Es]};
flatten_abstr(E, VN, _Vars, Body) ->
    {VN, Body, E}.

abstract_vars(Abstract) ->
    gb_sets:from_list(ordsets:to_list(vars(Abstract))).

%%% Small helpers

%% Report the use of a temporary file through error_logger as tmpdir_usage asks, with the caller's stack trace.
maybe_error_logger(allowed, _) ->
    ok;
maybe_error_logger(Name, Why) ->
    Trimmer = fun(M, _F, _A) -> M =:= erl_eval end,
    Formatter = fun(Term, I) -> io_lib:print(Term, I, 80, -1) end,
    X = erl_error:format_stacktrace(1, caller_stacktrace(), Trimmer, Formatter),
    Args = [Why, lists:flatten(X)],
    Format = "qlc: temporary file was needed for ~w\n~ts\n",
    case Name of
        info_msg -> error_logger:info_msg(Format, Args);
        warning_msg -> error_logger:warning_msg(Format, Args);
        error_msg -> error_logger:error_msg(Format, Args)
    end.

%% The stack trace below maybe_error_logger/2, deep enough to show at least three frames outside this module.
caller_stacktrace() ->
    Depth = erlang:system_flag(backtrace_depth, 8),
    try
        caller_stacktrace(erlang:max(1, Depth))
    after
        erlang:system_flag(backtrace_depth, Depth)
    end.

caller_stacktrace(Depth) ->
    _ = erlang:system_flag(backtrace_depth, Depth),
    {'EXIT', {foo, Stacktrace}} = (catch erlang:error(foo)),
    Callers = lists:takewhile(fun({M, _, _, _}) -> M =/= ?MODULE end, lists:reverse(Stacktrace)),
    case length(Callers) < 3 andalso length(Stacktrace) =:= Depth of
        true -> caller_stacktrace(Depth + 5);
        false -> below_logger(Stacktrace)
    end.

below_logger(Stacktrace) ->
    case lists:dropwhile(fun({M, F, _, _}) -> {M, F} =/= {?MODULE, maybe_error_logger} end, Stacktrace) of
        [_Logger | Callers] -> Callers;
        [] -> Stacktrace
    end.

post_funs(L) ->
    end_all_merge_joins(),
    local_post(L).

local_post(L) ->
    lists:foreach(
        fun
            (undefined) -> ok;
            (F) -> catch F()
        end,
        L
    ).

%% Fun(Arg), or Default without a fun; a failing fun runs the post funs first.
call(undefined, _Arg, Default, _Post) ->
    Default;
call(Fun, Arg, _Default, Post) ->
    try
        Fun(Arg)
    catch
        Class:Reason:Stacktrace ->
            post_funs(Post),
            erlang:raise(Class, Reason, Stacktrace)
    end.

grd(undefined, _Arg) -> false;
grd(Fun, Arg) -> Fun(Arg) =:= true.

anno0() -> anno(0).
anno1() -> anno(1).
anno(L) -> erl_anno:new(L).

family(L) ->
    sofs:to_external(sofs:relation_to_family(sofs:relation(L))).

family_union(L) ->
    R = sofs:relation(L, [{atom, [atom]}]),
    sofs:to_external(sofs:family_union(sofs:relation_to_family(R))).

qlc_error(Reason) ->
    {error, ?MODULE, Reason}.

file_error(File, {error, Reason}) ->
    qlc_error({file_error, File, Reason}).

-spec throw_file_error(string(), {error, atom()}) -> no_return().
throw_file_error(File, {error, Reason}) ->
    throw_reason({file_error, File, Reason}).

-spec throw_reason(term()) -> no_return().
throw_reason(Reason) ->
    throw_error(qlc_error(Reason)).

-spec throw_error(term()) -> no_return().
throw_error(Error) ->
    throw({?THROWN, Error}).
