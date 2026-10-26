%% Clause's error_handler (plan 21 step LIB23, docs/libraries/library.md): OTP's handler of calls to undefined functions,
%% callable directly. Programs are linked ahead of time, so the runtime raises undef itself and never calls it.
-module(error_handler).
-export([undefined_function/3, undefined_lambda/3, breakpoint/3, raise_undef_exception/3, stub_function/3]).

%% Calls the export once its module loads, else the module's '$handle_undefined_function'/2, else raises undef.
undefined_function(Module, Function, Args) ->
    case code:ensure_loaded(Module) of
        {module, Module} ->
            case erlang:function_exported(Module, Function, length(Args)) of
                true -> apply(Module, Function, Args);
                false -> undefined_function_handler(Module, Function, Args)
            end;
        _ ->
            raise_undef({Module, Function, Args, []})
    end.

undefined_function_handler(Module, Function, Args) ->
    case erlang:function_exported(Module, '$handle_undefined_function', 2) of
        true -> Module:'$handle_undefined_function'(Function, Args);
        false -> raise_undef({Module, Function, Args, []})
    end.

undefined_lambda(Module, Fun, Args) ->
    case code:ensure_loaded(Module) of
        {module, Module} -> apply(Fun, Args);
        _ -> raise_undef({Fun, Args, []})
    end.

%% The interpreter's entry for break points: Clause has no interpreter.
breakpoint(_, _, _) -> erlang:error({not_implemented, breakpoint, 3}).

raise_undef_exception(Module, Function, Args) -> raise_undef({Module, Function, Args, []}).

stub_function(Module, Function, Args) -> exit({undef, [{Module, Function, Args, []}]}).

%% Raises undef with the undefined call on top of the caller's stack.
raise_undef(Frame) ->
    try
        erlang:error(undef)
    catch
        error:undef:Stack -> erlang:raise(error, undef, [Frame | tl(Stack)])
    end.
