-module(disk_log_ops).
-export([main/1, head/1, bad_head/0]).
-compile({nowarn_deprecated_function, [{disk_log, inc_wrap_file, 1}]}).

%% disk_log, wrap_log_reader and logger_disk_log_h (plan 21 step LIB52): halt,
%% wrap and rotate logs in both formats, chunks, steps, blocking, notifications,
%% size and header changes, truncate, reopen, repair, read-only logs, info,
%% errors and format_error/1; wrap logs read by wrap_log_reader; and a logger
%% handler writing to a disk log. Files are compared by digests of their exact
%% bytes (OTP's formats), pids print as pid. Files are made in a directory under
%% the working directory and removed again.
main([]) ->
    Dir = "disk_log_ops.dir",
    _ = file:del_dir_r(Dir),
    ok = file:make_dir(Dir),
    put(dir, Dir),
    halt_logs(),
    external_logs(),
    wrap_logs(),
    rotate_logs(),
    blocking(),
    owners(),
    repair(),
    read_only(),
    errors(),
    reader(),
    handler(),
    show(all_left, disk_log:all()),
    ok = file:del_dir_r(Dir),
    io:format("disk_log_ops: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, clean(Value)]).

%% Pids and continuations shown by shape only.
clean(Pid) when is_pid(Pid) -> pid;
clean({continuation, _, Pos, B}) when is_binary(B) -> {continuation, Pos, byte_size(B)};
clean({continuation, _, Pos, B}) -> {continuation, Pos, B};
clean({wrap_reader, _, _, File, No, _, First}) -> {wrap_reader, File, No, First};
clean(List) when is_list(List) -> [clean(E) || E <- List];
clean(Tuple) when is_tuple(Tuple) -> list_to_tuple(clean(tuple_to_list(Tuple)));
clean(Other) -> Other.

path(Name) -> filename:join(get(dir), Name).

digest(Name) ->
    case file:read_file(path(Name)) of
        {ok, Bin} -> {byte_size(Bin), binary:encode_hex(erlang:md5(Bin))};
        Error -> Error
    end.

files() -> lists:sort(element(2, file:list_dir(get(dir)))).

%% The notifications received so far.
notes() ->
    receive
        {disk_log, _Node, Log, Info} -> [{Log, Info} | notes()]
    after 0 -> []
    end.

%% Every term of a log, chunk by chunk.
terms(Log) -> terms(Log, start, []).

terms(Log, Cont, Acc) ->
    case disk_log:chunk(Log, Cont) of
        eof -> lists:append(lists:reverse(Acc));
        {Cont2, Terms} -> terms(Log, Cont2, [Terms | Acc]);
        {Cont2, Terms, Bad} -> terms(Log, Cont2, [[{bad, Bad} | Terms] | Acc]);
        Error -> lists:append(lists:reverse([[Error] | Acc]))
    end.

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

info(Log, Keys) ->
    Info = disk_log:info(Log),
    [lists:keyfind(K, 1, Info) || K <- Keys].

head(Arg) -> {ok, {head, Arg}}.

bad_head() -> not_ok.

halt_logs() ->
    File = path("h.LOG"),
    show(open, disk_log:open([{name, h}, {file, File}, {head, my_head}, {notify, true}])),
    show(log, [
        disk_log:log(h, {a, 1}),
        disk_log:log_terms(h, [b, "c", <<"d">>]),
        disk_log:alog(h, e),
        disk_log:alog_terms(h, [f, g]),
        disk_log:sync(h),
        disk_log:blog(h, term_to_binary(h)),
        disk_log:blog_terms(h, [term_to_binary(i)])
    ]),
    show(terms, terms(h)),
    show(
        info,
        info(h, [name, type, format, size, items, users, head, mode, status, full, no_written_items])
    ),
    {Cont, First} = disk_log:chunk(h, start, 2),
    show(chunk_n, [First, Cont, disk_log:chunk(h, Cont, 3), disk_log:chunk_info(Cont)]),
    show(bchunk, [element(2, disk_log:bchunk(h, start, 2)), disk_log:chunk_step(h, start, 1)]),
    show(truncate, [
        disk_log:truncate(h), terms(h), disk_log:truncate(h, new_head), terms(h), notes()
    ]),
    show(change_header, [disk_log:change_header(h, {head, other}), disk_log:truncate(h), terms(h)]),
    show(change_size, [
        disk_log:change_size(h, 20),
        disk_log:change_size(h, 100),
        disk_log:log(h, big),
        disk_log:log_terms(h, lists:seq(1, 20)),
        disk_log:log(h, x),
        info(h, [full, no_written_items]),
        notes(),
        disk_log:change_size(h, infinity),
        disk_log:log(h, y)
    ]),
    show(close, [disk_log:close(h), disk_log:close(h), digest("h.LOG")]),
    show(reopen_existing, [disk_log:open([{name, h}, {file, File}]), terms(h)]),
    show(reopen, [
        disk_log:reopen(h, path("h2.LOG")),
        terms(h),
        disk_log:log(h, after_reopen),
        disk_log:reopen(h, path("h2.LOG"), reopened_head),
        disk_log:reopen(h, File),
        terms(h),
        disk_log:close(h)
    ]),
    show(renamed, [digest("h2.LOG")]),
    show(head_func, [
        disk_log:open([{name, hf}, {file, path("hf.LOG")}, {head_func, {?MODULE, head, [x]}}]),
        disk_log:log(hf, item),
        terms(hf),
        disk_log:close(hf),
        disk_log:open([{name, hf2}, {file, path("hf2.LOG")}, {head_func, {?MODULE, bad_head, []}}])
    ]).

external_logs() ->
    File = path("e.txt"),
    show(external, [
        disk_log:open([{name, e}, {file, File}, {format, external}, {head, "head\n"}]),
        disk_log:blog(e, "line one\n"),
        disk_log:blog_terms(e, [<<"two\n">>, ["thr", "ee\n"]]),
        disk_log:balog(e, <<"four\n">>),
        disk_log:log(e, term),
        disk_log:alog(e, term),
        disk_log:chunk(e, start),
        disk_log:btruncate(e, "new head\n"),
        disk_log:blog(e, "after\n"),
        disk_log:close(e),
        file:read_file(File)
    ]).

wrap_logs() ->
    Base = path("w"),
    Opts = [
        {name, w}, {file, Base}, {type, wrap}, {size, {60, 3}}, {notify, true}, {head, wrap_head}
    ],
    show(wrap_open, disk_log:open(Opts)),
    show(wrap_log, [disk_log:log(w, {item, I}) || I <- lists:seq(1, 12)]),
    show(wrap_notes, notes()),
    show(wrap_terms, terms(w)),
    show(
        wrap_info,
        info(w, [
            size,
            items,
            no_current_bytes,
            no_current_items,
            no_items,
            no_written_items,
            current_file,
            no_overflows
        ])
    ),
    show(wrap_info_again, info(w, [no_overflows])),
    show(wrap_step, [disk_log:chunk_step(w, start, 1), disk_log:chunk_step(w, start, 5)]),
    {ok, Step} = disk_log:chunk_step(w, start, 1),
    show(wrap_step_chunk, disk_log:chunk(w, Step)),
    show(next_file, [disk_log:next_file(w), disk_log:inc_wrap_file(w), notes(), terms(w)]),
    show(wrap_size, [
        disk_log:change_size(w, {80, 2}), disk_log:log_terms(w, [p, q, r, s]), notes(), files()
    ]),
    show(wrap_close, [
        disk_log:close(w), digest("w.idx"), digest("w.siz"), digest("w.1"), digest("w.2")
    ]),
    show(wrap_reopen, [
        disk_log:open([{name, w}, {file, Base}, {type, wrap}]), terms(w), disk_log:info(w) =/= []
    ]),
    show(wrap_mismatch, disk_log:open([{name, w}, {file, Base}, {type, wrap}, {size, {10, 10}}])),
    show(wrap_truncate, [disk_log:truncate(w), terms(w), files(), disk_log:close(w)]),
    show(wrap_infinity, disk_log:open([{name, w2}, {file, path("none")}, {type, wrap}])),
    Ext = path("we"),
    show(wrap_external, [
        disk_log:open([{name, we}, {file, Ext}, {type, wrap}, {format, external}, {size, {10, 2}}]),
        disk_log:blog_terms(we, [<<"aaaa">>, <<"bbbb">>, <<"cccc">>, <<"dddd">>]),
        disk_log:close(we),
        file:read_file(Ext ++ ".1"),
        file:read_file(Ext ++ ".2")
    ]).

rotate_logs() ->
    File = path("r.log"),
    show(rotate, [
        disk_log:open([{name, r}, {file, File}, {type, rotate}, {size, {10, 2}}]),
        disk_log:blog_terms(r, [<<"aaaaaa">>, <<"bbbbbb">>, <<"cccccc">>, <<"dddddd">>]),
        disk_log:sync(r),
        info(r, [type, format, size, no_current_bytes, no_items]),
        disk_log:next_file(r),
        disk_log:inc_wrap_file(r),
        disk_log:close(r),
        file:read_file(File),
        zlib:gunzip(element(2, file:read_file(File ++ ".0.gz"))),
        zlib:gunzip(element(2, file:read_file(File ++ ".1.gz"))),
        file:read_file_info(File ++ ".2.gz")
    ]).

blocking() ->
    {ok, b} = disk_log:open([{name, b}, {file, path("b.LOG")}]),
    Self = self(),
    show(block, [
        disk_log:block(b),
        disk_log:log(b, mine),
        disk_log:block(b),
        disk_log:unblock(b),
        disk_log:unblock(b)
    ]),
    ok = disk_log:block(b, true),
    Waiter = spawn(fun() -> Self ! {waited, disk_log:log(b, queued)} end),
    timer:sleep(50),
    show(queued, [disk_log:info(b) =/= [], is_process_alive(Waiter)]),
    ok = disk_log:unblock(b),
    receive
        {waited, Result} -> show(waited, Result)
    end,
    ok = disk_log:block(b, false),
    Other = spawn(fun() -> Self ! {refused, disk_log:log(b, refused), disk_log:unblock(b)} end),
    receive
        {refused, R1, R2} -> show(refused, [R1, R2, is_pid(Other)])
    end,
    ok = disk_log:unblock(b),
    show(blocked_terms, terms(b)),
    ok = disk_log:close(b).

owners() ->
    File = path("o.LOG"),
    Self = self(),
    show(owners, [
        disk_log:open([{name, o}, {file, File}, {notify, true}]),
        disk_log:open([{name, o}, {file, File}]),
        disk_log:open([{name, o}, {file, File}, {notify, false}]),
        disk_log:open([{name, o}, {file, File}, {linkto, none}]),
        disk_log:open([{name, o}, {file, path("other.LOG")}]),
        disk_log:open([{name, o}, {file, File}, {format, external}]),
        disk_log:open([{name, o}, {file, File}, {mode, read_only}]),
        info(o, [owners, users]),
        disk_log:change_notify(o, Self, false),
        disk_log:change_notify(o, Self, not_boolean),
        disk_log:change_notify(o, spawn(fun() -> ok end), true),
        disk_log:pid2name(element(2, lists:keyfind(owners, 1, disk_log:info(o)))),
        disk_log:all()
    ]),
    Pid = spawn(fun() ->
        {ok, o} = disk_log:open([{name, o}, {file, File}]),
        Self ! opened,
        receive
            stop -> ok
        end
    end),
    receive
        opened -> ok
    end,
    show(two_owners, length(element(2, lists:keyfind(owners, 1, disk_log:info(o))))),
    Ref = monitor(process, Pid),
    Pid ! stop,
    receive
        {'DOWN', Ref, _, _, _} -> ok
    end,
    timer:sleep(20),
    show(owner_gone, [
        length(element(2, lists:keyfind(owners, 1, disk_log:info(o)))),
        disk_log:close(o),
        disk_log:close(o),
        disk_log:close(o),
        disk_log:all()
    ]).

repair() ->
    File = path("p.LOG"),
    Self = self(),
    Writer = spawn(fun() ->
        {ok, p} = disk_log:open([{name, p}, {file, File}, {quiet, true}]),
        ok = disk_log:log_terms(p, [one, two, three]),
        ok = disk_log:sync(p),
        Self ! written,
        receive
            never -> ok
        end
    end),
    receive
        written -> ok
    end,
    {ok, Bytes} = file:read_file(File),
    exit(Writer, kill),
    timer:sleep(50),
    ok = file:write_file(path("q.LOG"), [Bytes, <<"junk">>]),
    show(need_repair, disk_log:open([{name, q}, {file, path("q.LOG")}, {repair, false}])),
    show(repaired, [
        disk_log:open([{name, q}, {file, path("q.LOG")}, {quiet, true}]),
        terms(q),
        disk_log:close(q)
    ]),
    show(after_repair, digest("q.LOG")),
    show(truncated_open, [
        disk_log:open([{name, q}, {file, path("q.LOG")}, {repair, truncate}]),
        terms(q),
        disk_log:close(q)
    ]).

read_only() ->
    {ok, ro} = disk_log:open([{name, ro}, {file, path("ro.LOG")}]),
    ok = disk_log:log_terms(ro, [x, y]),
    ok = disk_log:close(ro),
    {ok, Bin} = file:read_file(path("ro.LOG")),
    ok = file:write_file(path("ro2.LOG"), [
        Bin, <<0, 1, 2, 3, 4, 5, 6, 7, 8, 9>>, binary:part(Bin, 8, byte_size(Bin) - 8)
    ]),
    show(read_only, [
        disk_log:open([{name, ro}, {file, path("ro2.LOG")}, {mode, read_only}]),
        terms(ro),
        disk_log:log(ro, z),
        disk_log:alog(ro, z),
        disk_log:sync(ro),
        disk_log:truncate(ro),
        disk_log:change_size(ro, 10),
        disk_log:info(ro) =/= [],
        disk_log:close(ro)
    ]),
    show(not_a_log, [
        file:write_file(path("plain"), <<"plain text">>),
        disk_log:open([{name, nl}, {file, path("plain")}]),
        disk_log:open([{name, nl}, {file, path("plain")}, {mode, read_only}]),
        disk_log:open([{name, nl}, {file, path("missing")}, {mode, read_only}])
    ]).

errors() ->
    Bad = [
        [],
        [{name, x}, {type, bad}],
        [{name, x}, {repair, truncate}, {mode, read_only}],
        [{name, x}, {type, halt}, {size, {1, 2}}],
        [{name, x}, {size, 0}],
        [{name, x}, {head, ok}, {format, external}],
        [{name, x}, {head_func, bad}],
        [{name, {not_a_name}}],
        [bogus]
    ],
    show(open_errors, [disk_log:open(B) || B <- Bad]),
    show(no_such_log, [
        disk_log:log(nolog, x),
        disk_log:alog(nolog, x),
        disk_log:close(nolog),
        disk_log:info(nolog),
        disk_log:chunk(nolog, start),
        disk_log:chunk(nolog, bad_cont),
        disk_log:chunk_info(bad_cont),
        disk_log:pid2name(self()),
        outcome(fun() -> disk_log:chunk(nolog, start, 0) end)
    ]),
    {ok, h3} = disk_log:open([{name, h3}, {file, path("h3.LOG")}]),
    show(halt_errors, [
        disk_log:next_file(h3),
        disk_log:inc_wrap_file(h3),
        disk_log:chunk_step(h3, start, 1),
        disk_log:change_size(h3, {1, 2}),
        disk_log:change_header(h3, bad),
        disk_log:reopen(h3, path("h3.LOG")),
        disk_log:unblock(h3)
    ]),
    ok = disk_log:close(h3),
    Errors = [
        no_such_log,
        nonode,
        {badarg, size},
        {size_mismatch, {1, 2}, {3, 4}},
        {read_only_mode, l},
        {format_external, l},
        {blocked_log, l},
        {full, l},
        {not_blocked, l},
        {not_blocked_by_pid, l},
        {new_size_too_small, l, 10},
        {halt_log, l},
        {same_file_name, l},
        {arg_mismatch, notify, true, false},
        {name_already_open, l},
        {open_read_write, l},
        {open_read_only, l},
        {not_internal_wrap, l},
        {corrupt_log_file, "f"},
        {need_repair, "f"},
        {not_a_log_file, "f"},
        {invalid_header, h},
        end_of_log,
        {invalid_index_file, "f"},
        {no_continuation, c},
        {file_error, "f", enoent},
        {error, {full, l}},
        {node@host, {error, no_such_log}},
        other
    ],
    [io:format("~ts", [disk_log:format_error(E)]) || E <- Errors].

reader() ->
    Base = path("rw"),
    {ok, rw} = disk_log:open([{name, rw}, {file, Base}, {type, wrap}, {size, {50, 3}}]),
    ok = disk_log:log_terms(rw, [{n, I} || I <- lists:seq(1, 8)]),
    ok = disk_log:close(rw),
    {ok, R} = wrap_log_reader:open(Base),
    show(reader_all, read_all(R, [])),
    {ok, R2} = wrap_log_reader:open(Base, 2),
    show(reader_one, read_all(R2, [])),
    {ok, R3} = wrap_log_reader:open(list_to_atom(Base)),
    {R4, Some} = wrap_log_reader:chunk(R3, 2),
    show(reader_n, [Some, wrap_log_reader:close(R4)]),
    show(reader_errors, [
        wrap_log_reader:open(Base, 9),
        wrap_log_reader:open(path("none")),
        wrap_log_reader:open(path("h.LOG"))
    ]).

read_all(R, Acc) ->
    case wrap_log_reader:chunk(R) of
        {R1, eof} ->
            ok = wrap_log_reader:close(R1),
            lists:append(lists:reverse(Acc));
        {R1, Terms} ->
            read_all(R1, [Terms | Acc]);
        {R1, Terms, Bad} ->
            read_all(R1, [[{bad, Bad} | Terms] | Acc]);
        Error ->
            [Error]
    end.

handler() ->
    ok = logger:remove_handler(default),
    File = filename:absname(path("handler.log")),
    Formatter = {logger_formatter, #{template => [msg, "\n"], single_line => true}},
    Config = #{config => #{file => File, type => halt}, formatter => Formatter, level => info},
    show(add_handler, logger:add_handler(dlh, logger_disk_log_h, Config)),
    logger:info("first ~p", [1]),
    logger:notice(#{report => true}),
    logger:warning("third"),
    show(filesync, [logger_disk_log_h:filesync(dlh), logger_disk_log_h:filesync("dlh")]),
    {ok, #{config := HConfig}} = logger:get_handler_config(dlh),
    show(
        handler_config,
        lists:sort(
            maps:to_list(
                maps:with([type, max_no_bytes, max_no_files, filesync_repeat_interval], HConfig)
            )
        )
    ),
    show(change, [
        logger:update_handler_config(dlh, config, #{type => wrap}),
        logger:update_handler_config(dlh, config, #{filesync_repeat_interval => no_repeat}),
        logger:update_handler_config(dlh, config, #{bogus => 1})
    ]),
    show(bad_configs, [
        logger:add_handler(bad1, logger_disk_log_h, #{config => #{type => rotate}}),
        logger:add_handler(bad2, logger_disk_log_h, #{config => #{max_no_files => 0}}),
        logger:add_handler(bad3, logger_disk_log_h, #{config => #{file => <<"bin">>}})
    ]),
    show(remove_handler, [logger:remove_handler(dlh), file:read_file(File)]),
    Wrap = filename:absname(path("hw")),
    WConfig = #{
        config => #{file => Wrap, max_no_bytes => 20, max_no_files => 2}, formatter => Formatter
    },
    ok = logger:add_handler(dlw, logger_disk_log_h, WConfig),
    [logger:error("event ~p", [I]) || I <- lists:seq(1, 6)],
    ok = logger:remove_handler(dlw),
    show(wrap_handler, [file:read_file(Wrap ++ ".1"), file:read_file(Wrap ++ ".2")]).
