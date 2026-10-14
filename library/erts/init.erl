%% Clause's replacement for OTP's init (docs/library.md#runtime-module-replacements), generated from
%% init.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). An executable is a started system without a boot script (plan step 41L,
%% docs/builtins.md#init-module): the command-line answers are runtime builtins, the rest answer as OTP's init
%% does once started, and restarting or rebooting into a new boot script is refused with notsup.
-module(init).
-export([
    archive_extension/0,
    boot/1,
    code_path_choice/0,
    ensure_loaded/1,
    fetch_loaded/0,
    get_argument/1,
    get_arguments/0,
    get_configfd/1,
    get_plain_arguments/0,
    get_status/0,
    make_permanent/2,
    notify_when_started/1,
    objfile_extension/0,
    reboot/0,
    restart/0,
    restart/1,
    run_on_load_handlers/0,
    script_id/0,
    script_name/0,
    set_configfd/2,
    stop/0,
    stop/1,
    wait_until_started/0
]).

archive_extension() -> ".ez".

boot(_) -> erlang:error(notsup).

%% No -code_path_choice flag exists, so OTP's default.
code_path_choice() -> strict.

%% Code loading by init ends with the boot; the code module answers instead.
ensure_loaded(_) -> not_allowed.

%% The code server has already fetched the boot-loaded modules.
fetch_loaded() -> [].

get_argument(_) -> erlang:error({not_implemented, get_argument, 1}).
get_arguments() -> erlang:error({not_implemented, get_arguments, 0}).

%% The -configfd values set_configfd/2 stored, none for others.
get_configfd(ConfigFdId) -> persistent_term:get({init, configfd, ConfigFdId}, none).

get_plain_arguments() -> erlang:error({not_implemented, get_plain_arguments, 0}).

get_status() -> {started, started}.

%% Valid boot and config names as OTP checks them; no restart can use them.
make_permanent(Boot, Config) when
    (Boot =:= false orelse is_list(Boot)), (Config =:= false orelse is_list(Config))
->
    {error, notsup};
make_permanent(_, _) ->
    {error, badarg}.

notify_when_started(_) -> started.

objfile_extension() -> ".beam".

%% Halts as stop/0 does: no heart program can reboot an executable.
reboot() -> stop().

restart() -> restart([]).

restart([]) ->
    erlang:error(notsup);
restart([{mode, Mode}]) when Mode =:= embedded; Mode =:= interactive ->
    erlang:error(notsup);
restart(Opts) when is_list(Opts) ->
    erlang:error(badarg, [Opts]).

%% No module of an executable has an on_load function.
run_on_load_handlers() -> ok.

script_id() -> {"Clause", "29"}.

script_name() -> erlang:error({not_implemented, script_name, 0}).

set_configfd(ConfigFdId, Config) ->
    persistent_term:put({init, configfd, ConfigFdId}, Config).

stop() -> erlang:halt(0).

%% Halts at once with OTP's status: an integer exits with it, a byte list halts with it as the slogan.
stop(Status) when is_integer(Status), Status >= 0 ->
    erlang:halt(Status);
stop(Status) when is_list(Status) ->
    case is_bytelist(Status) of
        true -> erlang:halt(Status);
        false -> erlang:error(badarg)
    end;
stop(_) ->
    erlang:error(badarg).

is_bytelist([B | Bs]) when is_integer(B), B >= 0, B < 256 -> is_bytelist(Bs);
is_bytelist([]) -> true;
is_bytelist(_) -> false.

wait_until_started() ->
    receive
        {init, started} -> ok
    end.
