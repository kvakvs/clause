-module(file_info).
-export([main/1]).

%% file:read_file_info/1,2 and file:write_file/3 (plan step 41Y2): sizes, types,
%% access and time formats (times and host-dependent fields checked for shape
%% only), modes of write_file/3 and errors. Files are created in the working
%% directory and removed again.
main([]) ->
    Name = "file_info.txt",
    Dir = "file_info.dir",
    _ = file:delete(Name),
    _ = file:del_dir(Dir),
    io:format("~w~n", [
        [
            file:write_file(Name, <<"abc">>, []),
            file:write_file(Name, ["de", <<"f">>], [append]),
            file:read_file(Name),
            file:write_file(Name, <<"x">>, [exclusive]),
            file:write_file(Name, <<"xy">>, [write]),
            file:read_file(Name),
            file:write_file(Name, [a], []),
            outcome(fun() -> file:write_file(Name, <<"z">>, bad) end),
            file:write_file("no_such_dir/file.txt", <<"z">>, [])
        ]
    ]),
    {ok, Local} = file:read_file_info(Name),
    {ok, Universal} = file:read_file_info(Name, [{time, universal}]),
    {ok, Posix} = file:read_file_info(Name, [{time, posix}, raw]),
    Now = erlang:system_time(second),
    ok = file:make_dir(Dir),
    {ok, Directory} = file:read_file_info(Dir),
    io:format("~w~n", [
        [
            element(2, Local),
            element(3, Local),
            element(4, Local),
            date_time(element(6, Local)),
            date_time(element(6, Universal)),
            is_integer(element(6, Posix)) andalso abs(element(6, Posix) - Now) < 60,
            is_integer(element(8, Local)),
            is_integer(element(9, Local)),
            element(3, Directory),
            element(2, Directory) >= 0,
            file:read_file_info("no_such_file.txt"),
            outcome(fun() -> file:read_file_info(Name, [{time, bad}]) end),
            outcome(fun() -> file:read_file_info(Name, bad) end)
        ]
    ]),
    ok = file:delete(Name),
    ok = file:del_dir(Dir),
    io:format("file_info: ok~n").

%% The #file_info{} fields used are read by position: size 2, type 3, access 4,
%% mtime 6, mode 8, links 9 (no OTP header is needed to build the test).

%% Whether a value is {{Year, Month, Day}, {Hour, Minute, Second}}.
date_time({{Y, M, D}, {H, Mi, S}}) ->
    is_integer(Y) andalso is_integer(M) andalso is_integer(D) andalso is_integer(H) andalso
        is_integer(Mi) andalso
        is_integer(S);
date_time(_) ->
    false.

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
