%% clause_argparse: the builtins of argparse (plan 21 step LIB70, runtime/src/builtins/argparse/): validate/3 checks a
%% command, parse/6 parses arguments and help/4 formats the help, both answering with a request when they need
%% argparse.erl to call a fun or re:run/2,3. Each catalog builtin replaces its clause_builtin_placeholder body.
-module(clause_argparse).
-export([validate/3, parse/6, help/4]).

validate(_, _, _) -> clause_builtin_placeholder.
parse(_, _, _, _, _, _) -> clause_builtin_placeholder.
help(_, _, _, _) -> clause_builtin_placeholder.
