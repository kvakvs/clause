-module(shell).
-export([
    catch_exception/1,
    default_multiline_prompt/1,
    erl_pp_format_func/1,
    format_shell_func/1,
    hints/1,
    history/1,
    inverted_space_prompt/1,
    multiline_prompt_func/1,
    prompt_func/1,
    prompt_width/1,
    prompt_width/2,
    results/1,
    start_interactive/0,
    start_interactive/1,
    start_restricted/1,
    stop_restricted/0,
    strings/1,
    whereis/0
]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/library.md#runtime-module-replacements).
-export([
    get_function/2,
    get_state/0,
    help/0,
    local_allowed/3,
    local_func/0,
    local_func/1,
    non_local_allowed/3,
    read_and_add_records/5,
    server/1,
    server/2,
    start/0,
    start/1,
    start/2
]).

% The Erlang shell (plan 21 step LIB74, docs/library.md#interactive-shell), written from OTP's documentation and
% observed behaviour: this server reads commands from standard input, keeps their history and starts the evaluator
% (clause_shell_eval), which evaluates them with erl_eval and runs the shell's own commands. clause_shell is the
% terminal side that start_interactive/0,1 starts.

-define(DEF_HISTORY, 20).
-define(DEF_RESULTS, 20).
-define(RECORDS, shell_records).

% The server's state between commands.
-record(server, {
    % commands read and evaluated so far; the prompt shows the next number
    count = 0,
    % the evaluator process, restarted after an uncaught exception
    eval,
    % variable bindings, the record table (?RECORDS), the local functions table and the evaluator's dictionary
    bindings,
    records,
    functions,
    dict = [],
    % how many commands and results are kept, as configured when the last command ended
    history = ?DEF_HISTORY,
    results = ?DEF_RESULTS,
    % kept commands and results by number: #{N => Expressions} and #{N => Value}
    commands = #{},
    values = #{}
}).

%% --- Starting ---

%% Start the interactive shell on the standard streams unless one is started already.
start_interactive() ->
    clause_shell:start_interactive(default).

start_interactive(noshell) ->
    clause_shell:start_interactive(noshell);
start_interactive({noshell, Mode}) when Mode =:= raw; Mode =:= cooked ->
    clause_shell:start_interactive(noshell);
start_interactive({remote, _}) ->
    clause_shell:start_interactive(remote);
start_interactive({remote, _, {M, F, A}}) when is_atom(M), is_atom(F), is_list(A) ->
    clause_shell:start_interactive(remote);
start_interactive({Node, {M, F, A}}) when is_atom(Node), is_atom(M), is_atom(F), is_list(A) ->
    case Node =:= node() of
        true -> clause_shell:start_interactive({M, F, A});
        false -> clause_shell:start_interactive(remote)
    end;
start_interactive({M, F, A}) when is_atom(M), is_atom(F), is_list(A) ->
    clause_shell:start_interactive({M, F, A}).

%% The shell process of the interactive shell, or undefined.
whereis() ->
    clause_shell:whereis().

start() ->
    start(false, false).

start(init) ->
    start(false, true);
start(NoCtrlG) ->
    start(NoCtrlG, false).

start(NoCtrlG, StartSync) ->
    _ = code:ensure_loaded(user_default),
    Ancestors = [self() | ancestors()],
    spawn(fun() ->
        put('$ancestors', Ancestors),
        server(NoCtrlG, StartSync)
    end).

server(NoCtrlG, StartSync) ->
    put(no_control_g, NoCtrlG),
    server(StartSync).

%% The shell server: the banner, then commands until the end of input.
server(_StartSync) ->
    Bs = erl_eval:new_bindings(),
    RT = ets:new(?RECORDS, [public, ordered_set]),
    FT = ets:new(user_functions, [public, ordered_set]),
    _ = clause_shell_records:initiate(Bs, RT),
    process_flag(trap_exit, true),
    RestrictedError = restricted_check(),
    banner(),
    restricted_warning(RestrictedError),
    {History, Results} = history_and_results(),
    S = #server{bindings = Bs, records = RT, functions = FT, history = History, results = Results},
    loop(S#server{eval = clause_shell_eval:start(Bs, RT, FT, [])}).

% Whether the configured restricted shell module loads: undefined, or {Module, Reason}.
restricted_check() ->
    case application:get_env(stdlib, restricted_shell) of
        {ok, Module} when is_atom(Module) ->
            io:fwrite(<<"Restricted ">>, []),
            case code:ensure_loaded(Module) of
                {module, Module} -> undefined;
                {error, What} -> {Module, What}
            end;
        {ok, Term} ->
            {Term, not_an_atom};
        undefined ->
            undefined
    end.

restricted_warning(undefined) ->
    ok;
restricted_warning({Module, What}) ->
    io:fwrite(
        "Warning! Restricted shell module ~w not found: ~tp.\nOnly the commands q() and init:stop() will be allowed!\n",
        [Module, What]
    ),
    application:set_env(stdlib, restricted_shell, ?MODULE).

% The session slogan with the help hint.
banner() ->
    Hint =
        case erase(no_control_g) of
            true -> " (type help(). for help)";
            _ -> " (press Ctrl+G to abort, type help(). for help)"
        end,
    Default = io_lib:format(<<"Eshell V~s">>, [erlang:system_info(version)]),
    Slogan =
        case application:get_env(stdlib, shell_session_slogan, Default) of
            Fun when is_function(Fun, 0) -> Fun();
            Text -> Text
        end,
    try
        io:fwrite("~ts~ts\n", [unicode:characters_to_list(Slogan), Hint])
    catch
        _:_ -> io:fwrite("Warning! The slogan \"~p\" could not be printed.\n", [Slogan])
    end.

%% --- Reading and evaluating commands ---

loop(S0) ->
    N = S0#server.count + 1,
    {Prompt, S1} = prompt(N, S0),
    {Read, S2} = read_command(Prompt, S1),
    case Read of
        {ok, Es0} ->
            command(expand_history(Es0, N, S2), N, S2);
        {error, {Location, Mod, What}} ->
            benign(<<"~s: ~ts">>, [pos(Location), clause_shell_eval:format_error(Mod, What)]),
            loop(S2);
        {error, terminated} ->
            exit(S2#server.eval, kill),
            terminated;
        {error, interrupted} ->
            exit(S2#server.eval, kill),
            {_, S3} = reply(S2),
            loop(S3);
        {error, tokens} ->
            benign(<<"~w: Invalid tokens.">>, [N]),
            loop(S2);
        eof ->
            catch fatal(<<"Terminating erlang (~w)">>, [node()]),
            halt()
    end.

% Evaluate an expanded command and remember it with its value.
command({ok, Es}, N, S0) ->
    {V, S1} = evaluate(Es, cmd, S0),
    {History, Results} = history_and_results(),
    loop(remember(N, Es, V, S1#server{count = N, history = History, results = Results}));
command({error, E}, _N, S) ->
    benign(<<"~ts">>, [E]),
    loop(S).

% Keep command N and its value; forget those that fall out of the history and results lengths.
remember(N, Es, V, #server{commands = Cs, values = Vs, history = History, results = Results} = S) ->
    Keep = fun(Length) -> fun(M, _) -> M > N - Length end end,
    S#server{
        commands = maps:filter(Keep(History), Cs#{N => Es}),
        values = maps:filter(Keep(Results), Vs#{N => V})
    }.

% The prompt of command N: the prompt function's value, or N> (with the node name on an alive node).
prompt(N, S) ->
    case prompt_function() of
        {M, F} ->
            A = erl_anno:new(1),
            Arg = {cons, A, {tuple, A, [{atom, A, history}, {integer, A, N}]}, {nil, A}},
            Call = {call, A, {remote, A, {atom, A, M}, {atom, A, F}}, [Arg]},
            case evaluate([Call], pmt, S) of
                {{pmt, Value}, S1} ->
                    {Value, S1};
                {_, S1} ->
                    bad_prompt_func({M, F}),
                    {default_prompt(N), S1}
            end;
        default ->
            {default_prompt(N), S}
    end.

prompt_function() ->
    case application:get_env(stdlib, shell_prompt_func, default) of
        {M, F} = PromptFunc when is_atom(M), is_atom(F) ->
            PromptFunc;
        default ->
            default;
        Term ->
            bad_prompt_func(Term),
            default
    end.

bad_prompt_func(M) ->
    benign("Bad prompt function: ~tp", [M]).

default_prompt(N) ->
    case is_alive() of
        true -> io_lib:format(<<"(~s)~w> ">>, [node(), N]);
        false -> io_lib:format(<<"~w> ">>, [N])
    end.

% Read the next command in a reader process; the evaluator's exits are reported meanwhile.
read_command(Prompt, S) ->
    Ancestors = [self() | ancestors()],
    Reader = spawn_link(fun() ->
        put('$ancestors', Ancestors),
        exit(clause_shell_eval:read(Prompt))
    end),
    read_reply(Reader, S).

read_reply(Reader, #server{eval = Eval} = S) ->
    receive
        {shell_state, From} ->
            From ! {shell_state, S#server.bindings, S#server.records, S#server.functions},
            read_reply(Reader, S);
        {'EXIT', Reader, Read} ->
            {Read, S};
        {'EXIT', Eval, {Reason, Stacktrace}} ->
            clause_shell_eval:report_exception(error, {Reason, Stacktrace}, S#server.records),
            read_reply(Reader, restart(S));
        {'EXIT', Eval, Reason} ->
            clause_shell_eval:report_exception(error, {Reason, []}, S#server.records),
            read_reply(Reader, restart(S))
    end.

% A new evaluator with the server's bindings and dictionary.
restart(#server{bindings = Bs, records = RT, functions = FT, dict = Ds} = S) ->
    S#server{eval = clause_shell_eval:start(Bs, RT, FT, Ds)}.

% Have the evaluator run expressions (What is cmd, or pmt for a prompt function) and wait for its value.
evaluate(Es, What, #server{eval = Eval} = S) ->
    Eval ! {shell_cmd, self(), {eval, Es}, What},
    reply(S).

reply(S) ->
    case application:get_env(stdlib, shell_hints, true) =/= false of
        true -> reply(S, 5000);
        false -> reply(S, infinity)
    end.

reply(#server{eval = Ev, records = RT} = S, Timeout) ->
    receive
        {shell_rep, Ev, {value, V, Bs, Ds}} ->
            {V, S#server{bindings = Bs, dict = Ds}};
        {shell_rep, Ev, {command_error, {Location, M, Error}}} ->
            benign(<<"~s: ~ts">>, [pos(Location), clause_shell_eval:format_error(M, Error)]),
            {{'EXIT', Error}, S};
        {shell_req, Ev, Request} ->
            reply(request(Request, S), Timeout);
        {shell_state, From} ->
            From ! {shell_state, S#server.bindings, RT, S#server.functions},
            reply(S, Timeout);
        {ev_exit, {Ev, Class, Reason0}} ->
            receive
                {'EXIT', Ev, normal} -> ok
            end,
            clause_shell_eval:report_exception(Class, Reason0, RT),
            {{'EXIT', nocatch(Class, Reason0)}, restart(S)};
        {ev_caught, {Ev, Class, Reason0}} ->
            clause_shell_eval:report_exception(Class, benign, Reason0, RT),
            {{'EXIT', nocatch(Class, Reason0)}, S};
        {'EXIT', _, interrupt} ->
            exit(Ev, kill),
            reply(S, Timeout);
        {'EXIT', Ev, {Reason, Stacktrace}} ->
            clause_shell_eval:report_exception(exit, {Reason, Stacktrace}, RT),
            {{'EXIT', Reason}, restart(S)};
        {'EXIT', Ev, Reason} ->
            clause_shell_eval:report_exception(exit, {Reason, []}, RT),
            {{'EXIT', Reason}, restart(S)};
        {'EXIT', _, R} ->
            exit(Ev, R),
            exit(R);
        Other ->
            io:format("Throwing ~p~n", [Other]),
            reply(S, Timeout)
    after Timeout ->
        io:format("Command is taking a long time, type Ctrl+G, then enter 'i' to interrupt~n"),
        reply(S, infinity)
    end.

% The evaluator's requests: a kept command, the whole history, leaving the shell, a new dictionary.
request({get_cmd, N}, #server{eval = Ev} = S) ->
    Ev ! {shell_rep, self(), command(N, S)},
    S;
request(get_cmd, #server{eval = Ev, commands = Cs, values = Vs} = S) ->
    Ev ! {shell_rep, self(), {Cs, Vs}},
    S;
request(exit, #server{eval = Ev}) ->
    Ev ! {shell_rep, self(), exit},
    exit(normal);
request({update_dict, Ds}, #server{eval = Ev} = S) ->
    Ev ! {shell_rep, self(), ok},
    S#server{dict = Ds}.

% {Expressions, Value, N} of a kept command; undefined for what is gone.
command(N, #server{commands = Cs, values = Vs}) ->
    {maps:get(N, Cs, undefined), maps:get(N, Vs, undefined), N}.

nocatch(throw, {Term, Stack}) -> {{nocatch, Term}, Stack};
nocatch(_, Reason) -> Reason.

%% --- History expansion: e(N) and v(N) ---

expand_history(Es, N, S) ->
    try
        {ok, clause_shell_eval:expand_history(Es, fun(Num) -> history_command(Num, N, S) end)}
    catch
        throw:{error, _} = Error -> Error
    end.

% The command a history reference names: N, or the Nth before the current one when negative.
history_command(Num, Current, S) ->
    try erl_eval:expr(Num, erl_eval:new_bindings()) of
        {value, N, _} when is_integer(N), N < 0 -> command(Current + N, S);
        {value, N, _} -> command(N, S)
    catch
        _:_ -> {undefined, undefined, undefined}
    end.

%% --- Messages ---

benign(Format, Args) -> severity(<<"* ">>, Format, Args).

fatal(Format, Args) -> severity(<<"*** ">>, Format, Args).

severity(Tag, Format, Args) ->
    io:fwrite(<<"~ts\n">>, [[Tag, io_lib:fwrite(Format, Args)]]).

pos({Line, Col}) -> io_lib:format("~w:~w", [Line, Col]);
pos(Line) -> io_lib:format("~w", [Line]).

ancestors() ->
    case get('$ancestors') of
        undefined -> [];
        Ancestors -> Ancestors
    end.

%% --- Configuration ---

% The history and results lengths, reporting invalid configuration as OTP does.
history_and_results() ->
    check_env(shell_history_length),
    check_env(shell_saved_results),
    History = env(shell_history_length, ?DEF_HISTORY),
    Results = env(shell_saved_results, ?DEF_RESULTS),
    {History, min(Results, History)}.

env(Name, Default) ->
    case application:get_env(stdlib, Name, Default) of
        Value when is_integer(Value), Value >= 0 -> Value;
        _ -> Default
    end.

check_env(Name) ->
    case application:get_env(stdlib, Name, 0) of
        Value when is_integer(Value), Value >= 0 ->
            ok;
        Value ->
            Text = io_lib:fwrite("Invalid value of STDLIB configuration parameter~tw: ~tp\n", [Name, Value]),
            error_logger:info_report(lists:flatten(Text))
    end.

% Set an stdlib parameter; the previous value, or Default.
set_env(Name, Value, Default) ->
    Previous =
        case application:get_env(stdlib, Name) of
            undefined -> Default;
            {ok, Old} -> Old
        end,
    application_controller:set_env(stdlib, Name, Value),
    Previous.

%% Set how many previous commands are kept; the previous number.
history(N) when is_integer(N), N >= 0 ->
    set_env(shell_history_length, N, ?DEF_HISTORY).

%% Set how many previous results are kept; the previous number.
results(N) when is_integer(N), N >= 0 ->
    set_env(shell_saved_results, N, ?DEF_RESULTS).

%% Whether the evaluator survives exceptions; the previous setting.
catch_exception(Bool) ->
    set_env(shell_catch_exception, Bool, false).

%% Set the prompt function, default or {Module, Function}; the previous one.
prompt_func(PromptFunc) ->
    set_env(shell_prompt_func, PromptFunc, default).

%% Set the multiline prompt function; the previous one.
multiline_prompt_func(PromptFunc) ->
    set_env(shell_multiline_prompt, PromptFunc, default).

%% Set the formatting function of submitted expressions; the previous one.
format_shell_func(ShellFormatFunc) ->
    set_env(format_shell_func, ShellFormatFunc, default).

%% Whether lists print as strings when they can; the previous setting.
strings(Strings) ->
    set_env(shell_strings, Strings, true).

%% Whether the shell prints hints; the previous setting.
hints(Hints) ->
    set_env(shell_hints, Hints, true).

%% Exit the shell and start a restricted shell over the callback module.
start_restricted(Module) when is_atom(Module) ->
    case code:ensure_loaded(Module) of
        {module, Module} ->
            application:set_env(stdlib, restricted_shell, Module),
            exit(restricted_shell_started);
        {error, What} = Error ->
            Text = io_lib:fwrite("Restricted shell module ~w not found: ~tp\n", [Module, What]),
            error_logger:error_report(lists:flatten(Text)),
            Error
    end.

%% Exit a restricted shell and start a normal one.
stop_restricted() ->
    application:unset_env(stdlib, restricted_shell),
    exit(restricted_shell_stopped).

%% The restricted shell this module stands in for when the configured one is missing: q() and init:stop() only.
local_allowed(q, [], State) -> {true, State};
local_allowed(_, _, State) -> {false, State}.

non_local_allowed({init, stop}, [], State) -> {true, State};
non_local_allowed(_, _, State) -> {false, State}.

%% --- Formatting and prompts ---

%% A formatting function for format_shell_func/1: the expression or form as erl_pp prints it, comments dropped.
erl_pp_format_func(String) ->
    Options = [text, {reserved_word_fun, fun erl_scan:reserved_word/1}],
    case erl_scan:tokens([], string:trim(String) ++ "\n", {1, 1}, Options) of
        {done, {ok, Toks, _}, _} -> pp_format(Toks, String);
        _ -> String
    end.

pp_format(Toks, String) ->
    case erl_parse:parse_form(Toks) of
        {ok, Form} ->
            lists:flatten(erl_pp:form(Form));
        _ ->
            case erl_parse:parse_exprs(Toks) of
                {ok, Exprs} -> lists:flatten(erl_pp:exprs(Exprs)) ++ ".";
                _ -> String
            end
    end.

%% The width of a prompt in the encoding of standard input.
prompt_width(String) ->
    prompt_width(String, proplists:get_value(encoding, io:getopts(user))).

%% The printed width of a prompt: ANSI escape sequences take no room, wide characters two columns.
prompt_width(String, Encoding) ->
    case string:next_grapheme(String) of
        [] ->
            0;
        [$\e | Rest] ->
            case re:run(String, io_ansi_regexp(), [unicode]) of
                {match, [{0, N}]} ->
                    <<_:N/binary, After/binary>> = unicode:characters_to_binary(String),
                    prompt_width(After, Encoding);
                _ ->
                    char_width($\e, Encoding) + prompt_width(Rest, Encoding)
            end;
        [[First | Marks] | Rest] ->
            char_width(First, Encoding) + lists:sum([mark_width(C, Encoding) || C <- Marks]) +
                prompt_width(Rest, Encoding);
        [C | Rest] ->
            char_width(C, Encoding) + prompt_width(Rest, Encoding)
    end.

% The escape sequences a terminal does not print: CSI, OSC and two-character sequences.
io_ansi_regexp() ->
    "^\e(?:\\[[0-?]*[ -/]*[@-~]|\\][^\a\e]*(?:\a|\e\\\\)|[@-_])".

% The columns one character takes: as latin1 output escapes it, or as a terminal shows it (wide characters two,
% control characters none).
char_width(C, latin1) when C >= 512 -> 4 + length(integer_to_list(C, 16));
char_width(C, latin1) when C >= 128 -> 1 + length(integer_to_list(C, 8));
char_width(_, latin1) ->
    1;
char_width(C, _) when C < 32; C >= 127, C < 160 -> 0;
char_width(C, _) ->
    case unicode_util:is_wide(C) of
        true -> 2;
        false -> 1
    end.

% A character after the first of a grapheme cluster: combining marks take no room.
mark_width(C, latin1) ->
    char_width(C, latin1);
mark_width(C, _) ->
    case unicode_util:is_wide(C) of
        true -> 2;
        false -> 0
    end.

%% The default multiline prompt: two dots ending where the prompt ends.
default_multiline_prompt(Prompt) ->
    lists:duplicate(max(0, prompt_width(Prompt) - 3), $\s) ++ ".. ".

%% A multiline prompt of inverted spaces as wide as the prompt.
inverted_space_prompt(Prompt) ->
    "\e[7m" ++ lists:duplicate(prompt_width(Prompt) - 1, $\s) ++ "\e[27m ".

%% --- Shell helpers called from the shell's own modules ---

%% The shell's commands; shell_default's names do not override them.
local_func() ->
    [v, h, b, f, fd, ff, fl, lf, lr, lt, rd, rf, rl, rp, rr, tf, save_module, history, results, catch_exception].

local_func(Func) ->
    lists:member(Func, local_func()).

%% {shell_state, Bindings, Records, Functions} of the current shell.
get_state() ->
    shell:whereis() ! {shell_state, self()},
    receive
        {shell_state, Bs, RT, FT} -> {shell_state, Bs, ets:tab2list(RT), ets:tab2list(FT)}
    end.

%% The fun of a function defined in the shell, or undefined.
get_function(Func, Arity) ->
    {shell_state, _, _, FT} = get_state(),
    case lists:keyfind({function, {shell_default, Func, Arity}}, 1, FT) of
        {_, Fun} -> Fun;
        false -> undefined
    end.

read_and_add_records(File, Selected, Options, Bs, RT) ->
    clause_shell_records:read_and_add(File, Selected, Options, Bs, RT).

%% The shell's own commands, printed by help().
help() ->
    io:put_chars(clause_shell_eval:help_text()),
    io:nl(),
    true.
