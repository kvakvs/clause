-module(ea_quiet).
-compile([export_all, nowarn_export_all, no_native]).

one() -> two() - 1.

two() -> 2.
