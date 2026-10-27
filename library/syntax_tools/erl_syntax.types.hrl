%% OTP 29 erl_syntax types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([annotation_or_location/0,
              forms/0,
              padding/0,
              syntaxTree/0,
              syntaxTreeAttributes/0]).
-record(com,{pre = [] :: [syntaxTree()], post = [] :: [syntaxTree()]}).
-record(attr,{pos = erl_anno:new(0) :: term(),
              ann = [] :: [term()],
              com = none :: none | #com{}}).
-type syntaxTreeAttributes() :: #attr{}.
-record(tree,{type :: atom(), attr = #attr{} :: #attr{}, data :: term()}).
-type tree() :: #tree{}.
-record(wrapper,{type :: atom(),
                 attr = #attr{} :: #attr{},
                 tree :: erl_parse()}).
-type wrapper() :: #wrapper{}.
-type syntaxTree() :: tree() | wrapper() | erl_parse().
-type erl_parse() ::
          erl_parse:abstract_clause() |
          erl_parse:abstract_expr() |
          erl_parse:abstract_form() |
          erl_parse:abstract_type() |
          erl_parse:form_info() |
          erl_parse:af_binelement(term()) |
          erl_parse:af_generator() |
          [erl_parse:af_generator()] |
          erl_parse:af_remote_function().
-type annotation_or_location() :: erl_anno:anno() | erl_anno:location().
-type padding() :: none | integer().
-type forms() :: syntaxTree() | [syntaxTree()].
