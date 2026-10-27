%% clause_prettypr: the internal builtins of prettypr (plan 21 step LIB77E3a, runtime/src/builtins/prettypr.cpp). Each
%% catalog builtin replaces its clause_builtin_placeholder body.
-module(clause_prettypr).
-export([width/1, words/1, layout/1]).

%% width(String): the columns a flat string takes, tabs to the next multiple of 8.
width(_) -> clause_builtin_placeholder.

%% words(String): the words of a string separated by spaces, tabs and newlines, in order.
words(_) -> clause_builtin_placeholder.

%% layout(Layout): the text of a layout document best/3 returned, with nests as indentation.
layout(_) -> clause_builtin_placeholder.
