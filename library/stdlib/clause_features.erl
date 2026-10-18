-module(clause_features).
-export([beam_meta/1, module_features/1]).

% Helpers of erl_features (plan 21 step LIB8B); runtime builtins replace these bodies at registration.
% The Meta chunk of a BEAM file's bytes; missing without one, error for bytes that are not a BEAM file.
beam_meta(_) -> clause_builtin_placeholder.

% The features a module of the program was compiled with; not_found for a module the program lacks.
module_features(_) -> clause_builtin_placeholder.
