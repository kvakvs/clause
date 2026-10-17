%% Clause's string (plan 21 step LIB4, docs/builtins.md#string): every function is a runtime builtin with a
%% clause_builtin_placeholder body, walking chardata as OTP's string.erl does so results keep OTP's shapes.
-module(string).
-export([
    casefold/1,
    centre/2,
    centre/3,
    chars/2,
    chars/3,
    chomp/1,
    chr/2,
    concat/2,
    copies/2,
    cspan/2,
    equal/2,
    equal/3,
    equal/4,
    find/2,
    find/3,
    is_empty/1,
    jaro_similarity/2,
    join/2,
    left/2,
    left/3,
    len/1,
    length/1,
    lexemes/2,
    list_to_float/1,
    list_to_integer/1,
    lowercase/1,
    next_codepoint/1,
    next_grapheme/1,
    nth_lexeme/3,
    pad/2,
    pad/3,
    pad/4,
    prefix/2,
    rchr/2,
    replace/3,
    replace/4,
    reverse/1,
    right/2,
    right/3,
    rstr/2,
    slice/2,
    slice/3,
    span/2,
    split/2,
    split/3,
    str/2,
    strip/1,
    strip/2,
    strip/3,
    sub_string/2,
    sub_string/3,
    sub_word/2,
    sub_word/3,
    substr/2,
    substr/3,
    take/2,
    take/3,
    take/4,
    titlecase/1,
    to_float/1,
    to_graphemes/1,
    to_integer/1,
    to_lower/1,
    to_upper/1,
    tokens/2,
    trim/1,
    trim/2,
    trim/3,
    uppercase/1,
    words/1,
    words/2
]).

-include("string.types.hrl").

casefold(_) -> clause_builtin_placeholder.
centre(_, _) -> clause_builtin_placeholder.
centre(_, _, _) -> clause_builtin_placeholder.
chars(_, _) -> clause_builtin_placeholder.
chars(_, _, _) -> clause_builtin_placeholder.
chomp(_) -> clause_builtin_placeholder.
chr(_, _) -> clause_builtin_placeholder.
concat(_, _) -> clause_builtin_placeholder.
copies(_, _) -> clause_builtin_placeholder.
cspan(_, _) -> clause_builtin_placeholder.
equal(_, _) -> clause_builtin_placeholder.
equal(_, _, _) -> clause_builtin_placeholder.
equal(_, _, _, _) -> clause_builtin_placeholder.
find(_, _) -> clause_builtin_placeholder.
find(_, _, _) -> clause_builtin_placeholder.
is_empty(_) -> clause_builtin_placeholder.
jaro_similarity(_, _) -> clause_builtin_placeholder.
join(_, _) -> clause_builtin_placeholder.
left(_, _) -> clause_builtin_placeholder.
left(_, _, _) -> clause_builtin_placeholder.
len(_) -> clause_builtin_placeholder.
length(_) -> clause_builtin_placeholder.
lexemes(_, _) -> clause_builtin_placeholder.
list_to_float(_) -> clause_builtin_placeholder.
list_to_integer(_) -> clause_builtin_placeholder.
lowercase(_) -> clause_builtin_placeholder.
next_codepoint(_) -> clause_builtin_placeholder.
next_grapheme(_) -> clause_builtin_placeholder.
nth_lexeme(_, _, _) -> clause_builtin_placeholder.
pad(_, _) -> clause_builtin_placeholder.
pad(_, _, _) -> clause_builtin_placeholder.
pad(_, _, _, _) -> clause_builtin_placeholder.
prefix(_, _) -> clause_builtin_placeholder.
rchr(_, _) -> clause_builtin_placeholder.
replace(_, _, _) -> clause_builtin_placeholder.
replace(_, _, _, _) -> clause_builtin_placeholder.
reverse(_) -> clause_builtin_placeholder.
right(_, _) -> clause_builtin_placeholder.
right(_, _, _) -> clause_builtin_placeholder.
rstr(_, _) -> clause_builtin_placeholder.
slice(_, _) -> clause_builtin_placeholder.
slice(_, _, _) -> clause_builtin_placeholder.
span(_, _) -> clause_builtin_placeholder.
split(_, _) -> clause_builtin_placeholder.
split(_, _, _) -> clause_builtin_placeholder.
str(_, _) -> clause_builtin_placeholder.
strip(_) -> clause_builtin_placeholder.
strip(_, _) -> clause_builtin_placeholder.
strip(_, _, _) -> clause_builtin_placeholder.
sub_string(_, _) -> clause_builtin_placeholder.
sub_string(_, _, _) -> clause_builtin_placeholder.
sub_word(_, _) -> clause_builtin_placeholder.
sub_word(_, _, _) -> clause_builtin_placeholder.
substr(_, _) -> clause_builtin_placeholder.
substr(_, _, _) -> clause_builtin_placeholder.
take(_, _) -> clause_builtin_placeholder.
take(_, _, _) -> clause_builtin_placeholder.
take(_, _, _, _) -> clause_builtin_placeholder.
titlecase(_) -> clause_builtin_placeholder.
to_float(_) -> clause_builtin_placeholder.
to_graphemes(_) -> clause_builtin_placeholder.
to_integer(_) -> clause_builtin_placeholder.
to_lower(_) -> clause_builtin_placeholder.
to_upper(_) -> clause_builtin_placeholder.
tokens(_, _) -> clause_builtin_placeholder.
trim(_) -> clause_builtin_placeholder.
trim(_, _) -> clause_builtin_placeholder.
trim(_, _, _) -> clause_builtin_placeholder.
uppercase(_) -> clause_builtin_placeholder.
words(_) -> clause_builtin_placeholder.
words(_, _) -> clause_builtin_placeholder.
