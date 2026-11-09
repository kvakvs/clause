%% Undocumented OTP functions MongooseIM and its dependencies call (plan 21 step MIM7,
%% docs/libraries/library.md#undocumented-modules): inet_parse's addresses and domain names, inet_db's host name and
%% resolver options, io_lib_format:fwrite_g/1, io_lib_pretty:print/1-6, ram_file, pubkey_cert's certificate times
%% and pubkey_pbe's PBKDF2. Values that depend on the host (its name, resolver configuration) are only compared.
-module(mim_undocumented).
-export([main/1]).

-record(room, {name, opts}).

main(_) ->
    inet_parse(),
    inet_db(),
    fwrite_g(),
    pretty(),
    ram_files(),
    cert_times(),
    pbkdf2(),
    io:format("mim_undocumented: ok~n").

inet_parse() ->
    Texts = [
        "127.0.0.1",
        "::1",
        "1.2.3",
        "1.2.3.4.5",
        "0x7f.1",
        "::ffff:1.2.3.4",
        "fe80::1%eth0",
        "",
        "a",
        "256.1.1.1",
        "01.02.03.04",
        "127.1",
        " 1.2.3.4",
        "[::1]"
    ],
    Parsers = [address, strict_address, ipv4_address, ipv4strict_address, ipv6_address, ipv6strict_address],
    [show(Text, [inet_parse:F(Text) || F <- Parsers]) || Text <- Texts],
    show("binary_address", [inet_parse:address(<<"1.2.3.4">>), inet_parse:strict_address('1.2.3.4')]),
    Addresses = [
        {127, 0, 0, 1},
        {0, 0, 0, 0, 0, 0, 0, 1},
        {0, 0, 0, 0, 0, 16#ffff, 1, 2},
        {1, 2, 3},
        {256, 0, 0, 0},
        {1, 2, 3, 4, 5, 6, 7, 8},
        {16#fe80, 0, 0, 0, 0, 0, 0, 1},
        "1.2.3.4",
        {0, 0, 0, 0, 0, 0, 16#102, 16#304}
    ],
    show("ntoa", [inet_parse:ntoa(A) || A <- Addresses]),
    Names = [
        "example.com",
        "a",
        "",
        ".",
        "a.",
        ".a",
        "a..b",
        "a-b.c",
        "-a.b",
        "a-.b",
        "a.b-",
        "a_b.c",
        "_a.com",
        "1_.com",
        "1.2.3.4",
        "1.2.3",
        "a.1.2.3",
        "a.1.2.3.4",
        "1.2.3.4.a",
        "1-2.3.4.5",
        "99999.1.1.1",
        "xn--d1a",
        "a b",
        lists:duplicate(300, $a),
        "A.COM",
        "9-",
        "a--b",
        [233, $., $c],
        "*.a.com",
        [$a, 0],
        <<"a.com">>,
        a
    ],
    show("domain", [{N, inet_parse:domain(N)} || N <- Names]).

inet_db() ->
    {ok, Host} = inet:gethostname(),
    show("gethostname", inet_db:gethostname() =:= Host),
    Kept = [domain, nameserver, nameservers, search, alt_nameserver],
    show("host_options", [{O, is_list(inet_db:res_option(O))} || O <- Kept]),
    Options = [
        timeout,
        retry,
        inet6,
        usevc,
        edns,
        udp_payload_size,
        lookup,
        servfail_retry_timeout,
        recurse,
        dnssec_ok,
        random,
        verbose,
        hostname,
        port,
        "domain"
    ],
    show("res_option", [{O, class(fun() -> inet_db:res_option(O) end)} || O <- Options]).

fwrite_g() ->
    Floats = [1.0, 0.1, 1.0e10, 1.0e-10, 123456.789, -0.0, 5.0e-324, 1.0e23, 2.5e15, 1.0e16],
    show("fwrite_g", [io_lib_format:fwrite_g(F) || F <- Floats]),
    show("fwrite_g_integer", class(fun() -> io_lib_format:fwrite_g(1) end)).

%% The text io_lib_pretty prints, flattened: the deep lists holding it differ.
pretty() ->
    Room = #room{name = <<"lobby">>, opts = [{affiliations, [{owner, "alice"}]}, {persistent, true}]},
    Term = {rooms, [Room, Room#room{name = <<"kitchen">>}], #{count => 2, last => Room}},
    Fields = fun
        (room, 2) -> [name, opts];
        (_, _) -> no
    end,
    Long = lists:seq(1, 60),
    Prints = [
        {print1, io_lib_pretty:print(Term)},
        {print2_fun, io_lib_pretty:print(Term, Fields)},
        {print2_not_list, io_lib_pretty:print(Long, 10)},
        {print2_options, io_lib_pretty:print(Long, [{column, 10}, {line_length, 40}])},
        {print2_record_option, io_lib_pretty:print(Room, [{record_print_fun, Fields}, {line_length, 30}])},
        {print3, io_lib_pretty:print(Term, 5, Fields)},
        {print4, io_lib_pretty:print(Long, 5, 30, -1)},
        {print5, io_lib_pretty:print(Term, 1, 50, -1, Fields)},
        {print6, io_lib_pretty:print(Long, 1, 80, -1, 20, Fields)},
        {print6_depth, io_lib_pretty:print(Term, 1, 80, 4, -1, Fields)},
        {strings, io_lib_pretty:print(["abc", <<"def">>, [1000]], [{strings, false}])},
        {unicode, io_lib_pretty:print(["abc", [1000]], [{encoding, unicode}])}
    ],
    [show(Name, lists:flatten(Text)) || {Name, Text} <- Prints],
    ok.

ram_files() ->
    {ok, Fd} = ram_file:open("", [write, read, binary]),
    show("ram_write", [file:write(Fd, <<"hello ">>), ram_file:write(Fd, "world")]),
    show("ram_size", [ram_file:get_size(Fd), file:position(Fd, cur)]),
    show("ram_reads", [
        file:pread(Fd, 0, 11),
        ram_file:pread(Fd, 6, 5),
        ram_file:pread(Fd, [{0, 5}, {20, 1}]),
        ram_file:position(Fd, 2),
        ram_file:read(Fd, 3)
    ]),
    show("ram_writes", [
        ram_file:pwrite(Fd, 0, <<"J">>),
        ram_file:pwrite(Fd, [{11, <<"!">>}]),
        ram_file:get_size(Fd),
        ram_file:position(Fd, 5),
        ram_file:truncate(Fd),
        ram_file:get_size(Fd),
        ram_file:sync(Fd),
        ram_file:datasync(Fd),
        file:pread(Fd, 0, 20)
    ]),
    show("ram_close", [ram_file:close(Fd), ram_file:get_size(Fd)]),
    {ok, Read} = ram_file:open(<<"abc">>, []),
    show("ram_read_only", [ram_file:get_size(Read), ram_file:read(Read, 10), ram_file:write(Read, "x")]),
    show("ram_bad_mode", ram_file:open(<<"abc">>, [bogus])),
    File = "mim_undocumented.tmp",
    ok = file:write_file(File, "on disk"),
    {ok, Disk} = file:open(File, [read]),
    show("disk_get_size", class(fun() -> ram_file:get_size(Disk) end)),
    ok = file:close(Disk),
    ok = file:delete(File),
    show("not_a_file", class(fun() -> ram_file:get_size(foo) end)).

cert_times() ->
    Times = [
        {utcTime, "200101000000Z"},
        {utcTime, "500101000000Z"},
        {utcTime, "991231235959Z"},
        {utcTime, "000229120000Z"},
        {generalTime, "20500101000000Z"},
        {generalTime, "19700101000000Z"}
    ],
    show("time_str_2_gregorian_sec", [pubkey_cert:time_str_2_gregorian_sec(T) || T <- Times]),
    show("time_bad", class(fun() -> pubkey_cert:time_str_2_gregorian_sec({utcTime, "bad"}) end)).

%% PBKDF2 over a fun that tells each call: OTP's order of calls and its key.
pbkdf2() ->
    Self = self(),
    Hmac = fun(Hash, Key, Data, Length) ->
        Self ! {prf, Hash, iolist_to_binary(Data), Length},
        crypto:mac(hmac, Hash, Key, Data)
    end,
    Keys = [pubkey_pbe:pbdkdf2(<<"secret">>, <<"salt">>, 2, Length, Hmac, sha256, 32) || Length <- [16, 40]],
    show("pbdkdf2", Keys),
    show("pbdkdf2_crypto", lists:last(Keys) =:= crypto:pbkdf2_hmac(sha256, <<"secret">>, <<"salt">>, 2, 40)),
    show("prf_calls", calls([])),
    show(
        "pbdkdf2_list",
        pubkey_pbe:pbdkdf2("pw", "salt", 1, 8, fun(H, K, D, _) -> crypto:mac(hmac, H, K, D) end, sha, 20)
    ).

calls(Acc) ->
    receive
        {prf, Hash, Data, Length} -> calls([{Hash, byte_size(Data), Length} | Acc])
    after 0 -> lists:reverse(Acc)
    end.

class(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Label, Value) -> io:format("~ts: ~p~n", [label(Label), Value]).

label(Label) when is_atom(Label) -> atom_to_list(Label);
label(Label) -> Label.
