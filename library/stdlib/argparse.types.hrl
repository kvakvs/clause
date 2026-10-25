%% OTP 29 argparse types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([arg_map/0,
              arg_type/0,
              args/0,
              argument/0,
              argument_help/0,
              cmd_path/0,
              command/0,
              handler/0]).
-type arg_type() ::
          boolean | float |
          {float, Choice :: [float()]} |
          {float, [{min, float()} | {max, float()}]} |
          integer |
          {integer, Choices :: [integer()]} |
          {integer, [{min, integer()} | {max, integer()}]} |
          string |
          {string, Choices :: [string()]} |
          {string, Re :: string()} |
          {string, Re :: string(), ReOptions :: [term()]} |
          binary |
          {binary, Choices :: [binary()]} |
          {binary, Re :: binary()} |
          {binary, Re :: binary(), ReOptions :: [term()]} |
          atom |
          {atom, Choices :: [atom()]} |
          {atom, unsafe} |
          {custom, fun((string()) -> term())}.
-type argument_help() ::
          {unicode:chardata(),
           [unicode:chardata() | type | default] |
           fun(() -> unicode:chardata())}.
-type argument_name() :: atom() | string() | binary().
-type argument() ::
          #{name := argument_name(),
            short => char(),
            long => string(),
            required => boolean(),
            default => term(),
            type => arg_type(),
            action =>
                store |
                {store, term()} |
                append |
                {append, term()} |
                count | extend,
            nargs =>
                pos_integer() |
                'maybe' |
                {'maybe', term()} |
                list | nonempty_list | all,
            help => hidden | unicode:chardata() | argument_help()}.
-type arg_map() :: #{argument_name() => term()}.
-type args() :: [string() | unicode:chardata()].
-type handler() ::
          optional |
          fun((arg_map()) -> term()) |
          {module(), Fn :: atom()} |
          {fun(() -> term()), term()} |
          {module(), atom(), term()}.
-type command_help() ::
          [unicode:chardata() | usage | commands | arguments | options].
-type command() ::
          #{commands => #{string() => command()},
            arguments => [argument()],
            help => hidden | unicode:chardata() | command_help(),
            handler => handler()}.
-type cmd_path() :: [string()].
