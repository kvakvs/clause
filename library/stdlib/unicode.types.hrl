%% OTP 29 unicode types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([category/0,
              chardata/0,
              charlist/0,
              encoding/0,
              external_chardata/0,
              external_charlist/0,
              latin1_binary/0,
              latin1_char/0,
              latin1_chardata/0,
              latin1_charlist/0,
              unicode_binary/0]).
-type encoding() ::
          latin1 | unicode | utf8 | utf16 |
          {utf16, endian()} |
          utf32 |
          {utf32, endian()}.
-type endian() :: big | little.
-type unicode_binary() :: binary().
-type charlist() ::
          maybe_improper_list(char() | unicode_binary() | charlist(),
                              unicode_binary() | []).
-type chardata() :: charlist() | unicode_binary().
-type external_unicode_binary() :: binary().
-type external_chardata() ::
          external_charlist() | external_unicode_binary().
-type external_charlist() ::
          maybe_improper_list(char() |
                              external_unicode_binary() |
                              external_charlist(),
                              external_unicode_binary() | []).
-type latin1_binary() :: binary().
-type latin1_char() :: byte().
-type latin1_chardata() :: latin1_charlist() | latin1_binary().
-type latin1_charlist() ::
          maybe_improper_list(latin1_char() |
                              latin1_binary() |
                              latin1_charlist(),
                              latin1_binary() | []).
-type category() ::
          {letter, uppercase | lowercase | titlecase | modifier | other} |
          {mark, non_spacing | spacing_combining | enclosing} |
          {number, decimal | letter | other} |
          {separator, space | line | paragraph} |
          {other, control | format | surrogate | private | not_assigned} |
          {punctuation,
           connector | dash | open | close | initial | final | other} |
          {symbol, math | currency | modifier | other}.
