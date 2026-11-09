%% OTP 29's http_uri (plan 21 step MIM4, docs/libraries/inets.md#http_uri): percent-encoding and decoding, which
%% answer as uri_string's quote/1 and percent_decode/1 do.
-module(http_uri).
-export([decode/1, encode/1]).

encode(Uri) ->
    uri_string:quote(Uri).

decode(Uri) ->
    uri_string:percent_decode(Uri).
