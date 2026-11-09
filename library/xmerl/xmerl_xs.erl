%% OTP's xmerl_xs as libraries call it (plan 21 step MIM3b, docs/libraries/xmerl.md#xmerl_xpath): select/2 is
%% xmerl_xpath:string/2. Its XSLT-like transformation functions are declined.
-module(xmerl_xs).
-export([select/2]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([built_in_rules/2, value_of/1, xslapply/2]).

select(Path, Node) ->
    xmerl_xpath:string(Path, Node).

built_in_rules(_, _) -> erlang:error({not_implemented, built_in_rules, 2}).
value_of(_) -> erlang:error({not_implemented, value_of, 1}).
xslapply(_, _) -> erlang:error({not_implemented, xslapply, 2}).
