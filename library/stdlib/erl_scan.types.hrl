%% OTP 29 erl_scan types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([error_info/0,
              options/0,
              return_cont/0,
              token/0,
              tokens/0,
              tokens_result/0]).
-type category() :: atom().
-type resword_fun() :: fun((atom()) -> boolean()).
-type text_fun() :: fun((atom(), string()) -> boolean()).
-type option() ::
          return | return_white_spaces | return_comments | text |
          {reserved_word_fun, resword_fun()} |
          {text_fun, text_fun()} |
          {compiler_internal, [term()]}.
-type options() :: option() | [option()].
-type symbol() :: atom() | float() | integer() | string().
-type token() ::
          {category(), Anno :: erl_anno:anno(), symbol()} |
          {category(), Anno :: erl_anno:anno()}.
-type tokens() :: [token()].
-type error_description() :: term().
-type error_info() ::
          {erl_anno:location(), module(), error_description()}.
-record(erl_scan,{resword_fun = fun reserved_word/1 :: resword_fun(),
                  text_fun =
                      fun(_, _) ->
                             false
                      end ::
                          text_fun(),
                  ws = false :: boolean(),
                  comment = false :: boolean(),
                  has_fun = false :: boolean(),
                  checks = false :: boolean(),
                  in_check = false :: boolean()}).
-type char_spec() :: string() | eof.
-type cont_fun() ::
          fun((char_spec(),
               #erl_scan{},
               erl_anno:line(),
               erl_anno:column(),
               tokens(),
               any()) ->
                  any()).
-opaque return_cont() ::
            {erl_scan_continuation,
             string(),
             erl_anno:column(),
             tokens(),
             erl_anno:line(),
             #erl_scan{},
             any(),
             cont_fun()}.
-type tokens_result() ::
          {ok, Tokens :: tokens(), EndLocation :: erl_anno:location()} |
          {eof, EndLocation :: erl_anno:location()} |
          {error,
           ErrorInfo :: error_info(),
           EndLocation :: erl_anno:location()}.
