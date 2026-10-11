-module(worker).
-export([run/1, double/1, triple/1, idle/0]).

% Reached through literal targets only: spawn/3, apply/3 and fun M:F/A. Nothing reaches idle/0.

run(Parent) -> Parent ! {done, double(21)}.

double(X) -> X * 2.

triple(X) -> X * 3.

idle() -> idle.
