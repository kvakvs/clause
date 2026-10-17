%% OTP 29 uri_string types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([error/0,uri_map/0,uri_string/0]).
-type uri_string() :: iodata().
-type error() :: {error, atom(), term()}.
-type uri_map() ::
          #{fragment => unicode:chardata(),
            host => unicode:chardata(),
            path => unicode:chardata(),
            port => non_neg_integer() | undefined,
            query => unicode:chardata(),
            scheme => unicode:chardata(),
            userinfo => unicode:chardata()}.
