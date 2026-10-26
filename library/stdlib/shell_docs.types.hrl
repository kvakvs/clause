%% OTP 29 shell_docs types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([chunk_element_attr/0,chunk_elements/0]).
-type chunk_elements() :: [chunk_element()].
-type chunk_element() ::
          {chunk_element_type(),
           chunk_element_attrs(),
           chunk_elements()} |
          binary().
-type chunk_element_attrs() :: [chunk_element_attr()].
-type chunk_element_attr() :: {atom(), unicode:chardata()}.
-type chunk_element_type() ::
          chunk_element_inline_type() | chunk_element_block_type().
-type chunk_element_inline_type() :: a | code | em | strong | i | b.
-type chunk_element_block_type() ::
          p | 'div' | blockquote | br | pre | ul | ol | li | dl | dt |
          dd | h1 | h2 | h3 | h4 | h5 | h6.
