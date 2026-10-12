%% A stand-in for OTP's prim_file (plan step 41K): its name is reserved, so a search never takes it.
-module(prim_file).
-export([open/2]).

open(_, _) -> stand_in.
