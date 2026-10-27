%% Clause's merl.hrl (plan 21 step LIB77E5, docs/libraries/syntax_tools.md#merl): ?Q(Text) quotes source text as
%% syntax trees and ?Q(Text, Env) substitutes metavariables in it, both with the current line; merl_transform's parse
%% transform expands them at compile time unless MERL_NO_TRANSFORM is defined first. As OTP's header, MERL_HRL guards
%% it but is never defined here.
-ifndef(MERL_HRL).

-define(Q(Text), merl:quote(?LINE, Text)).
-define(Q(Text, Env), merl:qquote(?LINE, Text, Env)).

-ifndef(MERL_NO_TRANSFORM).
-compile({parse_transform, merl_transform}).
-endif.

-endif.
