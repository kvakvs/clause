%% clause_epp_dodger: the internal builtins of epp_dodger (plan 21 step LIB77E4c, runtime/src/builtins/epp_dodger/).
%% Each catalog builtin replaces its clause_builtin_placeholder body.
-module(clause_epp_dodger).
-export([scan/2, quickscan/1, rewrite_form/1]).

%% scan(Tokens, Clever): scan_form/2's tokens of a form, directives made '?preprocessor declaration?' functions and
%% macros made atoms and '? <macro> (' tuples; throws {error, macro_args} for unclosed macro arguments.
scan(_, _) -> clause_builtin_placeholder.

%% quickscan(Tokens): quickscan_form/1's tokens, directives killed and macros made '?NAME' atoms or string parts.
quickscan(_) -> clause_builtin_placeholder.

%% rewrite_form(Form): a parsed form with directive functions back as attributes and macro names back as macros.
rewrite_form(_) -> clause_builtin_placeholder.
