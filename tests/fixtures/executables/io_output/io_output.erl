-module(io_output).
-export([main/1]).

%% The io output functions of plan step 41U: nl/0,1, fwrite/1,2,3, format/3 and
%% put_chars/2 on standard output, standard error, an open file (also by its
%% registered name) and invalid devices.
main([]) ->
    io:nl(),
    io:fwrite("fwrite/1~n"),
    io:fwrite("fwrite/~w~n", [2]),
    io:fwrite(standard_io, "fwrite/~w~n", [3]),
    io:format(user, "format/~w~n", [3]),
    io:put_chars(standard_io, [<<"put_chars">>, "/2"]),
    io:nl(standard_io),
    io:format(standard_error, "to standard error~n", []),
    io:put_chars(standard_error, <<"more">>),
    io:nl(standard_error),
    io:format("~w~n", [[outcome(F) || F <- errors()]]),
    files(),
    io:format("io_output: ok~n").

%% Device and argument errors.
errors() ->
    Dead = spawn(fun() -> ok end),
    Monitor = monitor(process, Dead),
    receive
        {'DOWN', Monitor, process, Dead, _} -> ok
    end,
    [
        fun() -> io:format(no_such_device, "x", []) end,
        fun() -> io:nl(no_such_device) end,
        fun() -> io:put_chars(no_such_device, "x") end,
        fun() -> io:nl(1) end,
        fun() -> io:put_chars(1, "x") end,
        fun() -> io:fwrite(1, "x", []) end,
        fun() -> io:format(Dead, "x", []) end,
        fun() -> io:nl(self()) end,
        fun() -> io:format(standard_io, id("~w"), []) end,
        fun() -> io:fwrite(id("~w")) end,
        fun() -> io:put_chars(standard_io, foo) end,
        fun() -> io:fwrite(standard_error, id("~q"), [x]) end
    ].

id(X) -> X.

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        {ok, Fun()}
    catch
        Class:Reason -> {Class, Reason}
    end.

%% Writing to an open file directly and through its registered name.
files() ->
    Name = "io_output.txt",
    {ok, File} = file:open(Name, [write]),
    ok = io:format(File, "line ~w~n", [1]),
    ok = io:put_chars(File, ["line ", $2]),
    ok = io:nl(File),
    true = register(io_output_file, File),
    ok = io:fwrite(io_output_file, "line ~s~n", ["3"]),
    ok = file:close(File),
    {ok, Content} = file:read_file(Name),
    ok = file:delete(Name),
    io:format("~s", [Content]).
