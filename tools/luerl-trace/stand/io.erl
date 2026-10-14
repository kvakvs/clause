nl() -> io:put_chars("\n").
nl(_Device) -> io:put_chars("\n").
fwrite(Format) -> io:format(Format).
fwrite(Format, Args) -> io:format(Format, Args).
fwrite(_Device, Format, Args) -> io:format(Format, Args).
format(_Device, Format, Args) -> io:format(Format, Args).
put_chars(_Device, Chars) -> io:put_chars(Chars).
write(Term) -> io:format("~w", [Term]).
write(_Device, Term) -> io:format("~w", [Term]).
printable_range() -> latin1.
