-module(broken_user).
-compile({parse_transform, pt_count}).
-export([f/0]).

%% Deliberately does not parse: the transform still runs and gets the error as a form.
f() -> .
