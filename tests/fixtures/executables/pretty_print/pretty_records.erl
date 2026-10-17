-module(pretty_records).
-export([values/0]).

-record(#point{x = 0, y = 0}).
-record(#long_record{label, values = []}).
-record(#nested_record{point, details}).

values() ->
    [
        #point{x = 3, y = 4},
        #long_record{
            label = long,
            values = [alpha, beta, gamma, delta, epsilon, zeta, eta, theta, iota, kappa]
        },
        #nested_record{
            point = #point{x = 10, y = 20},
            details = #long_record{label = nested, values = [one, two, three, four, five]}
        }
    ].
