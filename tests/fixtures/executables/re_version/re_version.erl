-module(re_version).
-export([main/1]).

%% The runtime links the PCRE2 release OTP 29 bundles (plan step 41ZA):
%% re:version/0 is PCRE2's own version string, as on OTP.
main([]) ->
    io:format("~p~n", [re:version()]),
    io:format("re_version: ok~n").
