%% A module of the loggers test that handles calls of its undefined functions (error_handler:undefined_function/3).
-module(loggers_undefined).
-export(['$handle_undefined_function'/2]).

'$handle_undefined_function'(Function, Args) -> {handled, Function, Args}.
