%% -compile(export_all) (plan 21 step MIM9c): every function of ea_all is exported, so remote calls, apply/3 and
%% external funs reach the ones -export does not list, and module_info(exports) lists them in definition order.
%% ea_quiet also has HiPE's no_native, which OTP ignores.
-module(export_all).
-export([main/1]).

main(_) ->
    show(ea_all:module_info(exports)),
    show([ea_all:double(4), apply(ea_all, helper, [x]), (fun ea_all:pair/2)(1, 2)]),
    Exported = [{double, 1}, {helper, 1}, {pair, 2}, {missing, 0}],
    show([erlang:function_exported(ea_all, Name, Arity) || {Name, Arity} <- Exported]),
    show(ea_quiet:module_info(exports)),
    show(ea_quiet:one()),
    io:format("export_all: ok~n").

show(Term) -> io:format("~p~n", [Term]).
