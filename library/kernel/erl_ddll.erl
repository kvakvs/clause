%% Clause's erl_ddll (plan 21, docs/libraries/library.md#runtime-module-replacements): Clause has no dynamically
%% loaded drivers, so it answers as OTP on a platform without DDLL functionality: no driver is loaded, loads fail
%% with no_ddll_available, unloads and reloads with not_loaded, and a monitor reports the driver unloaded at once.
%% format_error/1 gives OTP's texts.
-module(erl_ddll).
-export([
    demonitor/1,
    format_error/1,
    format_error_int/1,
    info/0,
    info/1,
    info/2,
    load/2,
    load_driver/2,
    loaded_drivers/0,
    monitor/2,
    reload/2,
    reload_driver/2,
    start/0,
    stop/0,
    try_load/3,
    try_unload/2,
    unload/1,
    unload_driver/1
]).

loaded_drivers() -> {ok, []}.

info() -> [].

% No driver is loaded, so none has information.
info(_Name) -> erlang:error(badarg).
info(_Name, _Tag) -> erlang:error(badarg).

load(Path, Name) -> try_load(Path, Name, [{monitor, pending_driver}]).
load_driver(Path, Name) -> try_load(Path, Name, [{monitor, pending_driver}]).

% Loading fails as without DDLL functionality, after OTP's argument checks.
try_load(Path, Name, Options) ->
    case is_chardata(Path) andalso is_name(Name) andalso is_list(Options) of
        true -> {error, no_ddll_available};
        false -> erlang:error(badarg, [Path, Name, Options])
    end.

unload(Name) -> try_unload(Name, []).
unload_driver(Name) -> try_unload(Name, [{monitor, pending_driver}, kill_ports]).

try_unload(Name, Options) ->
    case is_name(Name) andalso is_list(Options) of
        true -> {error, not_loaded};
        false -> erlang:error(badarg, [Name, Options])
    end.

reload(Path, Name) -> reload_checked(Path, Name).
reload_driver(Path, Name) -> reload_checked(Path, Name).

reload_checked(Path, Name) ->
    case is_chardata(Path) andalso is_name(Name) of
        true -> {error, not_loaded};
        false -> erlang:error(badarg, [Path, Name])
    end.

% A driver monitor: the driver is not loaded, so the 'DOWN' message comes at once.
monitor(driver, {Name, When} = Item) when When == loaded; When == unloaded ->
    case is_name(Name) of
        true ->
            Ref = make_ref(),
            self() ! {'DOWN', Ref, driver, Name, unloaded},
            Ref;
        false ->
            erlang:error(badarg, [driver, Item])
    end;
monitor(Tag, Item) ->
    erlang:error(badarg, [Tag, Item]).

demonitor(Ref) when is_reference(Ref) -> true;
demonitor(Ref) -> erlang:error(badarg, [Ref]).

is_name(Name) -> is_atom(Name) orelse is_chardata(Name).

is_chardata(Text) -> is_list(Text) orelse is_atom(Text).

% The text of an error a function of erl_ddll returned.
format_error(load_cancelled) -> "Loading was cancelled from other process";
format_error(Code) -> format_error_int(Code).

format_error_int(inconsistent) -> "Driver name and/or driver options are inconsistent with currently loaded driver";
format_error_int(linked_in_driver) -> "Driver is statically linked and cannot be loaded/unloaded";
format_error_int(permanent) -> "DDLL driver is permanent an cannot be unloaded/loaded";
format_error_int(not_loaded) -> "DDLL driver is not loaded";
format_error_int(not_loaded_by_this_process) -> "DDLL driver was not loaded by this process";
format_error_int(not_pending) -> "DDLL load not pending for this driver name";
format_error_int(already_loaded) -> "DDLL driver is already loaded successfully";
format_error_int(pending_reload) -> "Driver reloading is already pending";
format_error_int(pending_process) -> "Driver is loaded by others when attempting option {reload, pending_driver}";
format_error_int({open_error, Code}) when is_integer(Code) -> code_text(Code);
format_error_int(Code) -> code_text(error_code(Code)).

% The emulator's numbers of the load errors.
error_code(no_error) -> 0;
error_code(no_driver_init) -> -1;
error_code(driver_init_failed) -> -2;
error_code(bad_driver_name) -> -3;
error_code(driver_name_to_long) -> -4;
error_code(driver_incorrect_version) -> -5;
error_code(no_ddll_available) -> -6;
error_code(unspecified_error) -> -7;
error_code(symbol_not_found) -> -8;
error_code(Code) -> erlang:error(badarg, [Code]).

code_text(0) -> "No error";
code_text(-1) -> "No driver init in dynamic library";
code_text(-2) -> "Driver init failed";
code_text(-3) -> "Bad driver name";
code_text(-4) -> "Driver name to long";
code_text(-5) -> "Driver compiled with incorrect version of erl_driver.h";
code_text(-6) -> "DDLL functionality not available on this platform";
code_text(-7) -> "Unspecified dynamic library error";
code_text(-8) -> "Symbol not found in dynamic library";
code_text(_) -> "Unspecified error".

start() -> erlang:error({not_implemented, start, 0}).
stop() -> erlang:error({not_implemented, stop, 0}).
