-module(luerl_drive).
-export([main/0]).

main() ->
    io:format("drive: start~n"),
    St0 = luerl:init(),
    io:format("drive: init done~n"),
    lists:foreach(fun({N, Code}) -> step(N, Code, St0) end, snippets()),
    io:format("luerl_drive: ok~n").

step(N, Code, St0) ->
    io:format("drive: step ~p~n", [N]),
    try luerl:do_dec(Code, St0) of
        {ok, Result, _} -> io:format("drive: step ~p result ~p~n", [N, Result]);
        {lua_error, E, _} -> io:format("drive: step ~p lua_error ~p~n", [N, E]);
        Other -> io:format("drive: step ~p other ~p~n", [N, Other])
    catch
        C:E:S -> io:format("drive: step ~p raised ~p:~p~n~p~n", [N, C, E, S])
    end.

snippets() ->
    [{print, "print('hello from lua')"},
     {arith, "return 1 + 2, 10 / 4, 7 // 2, 7 % 3, 2 ^ 10, -7 // 2, 7.5 % 2"},
     {loop, "local t = {} for i = 1, 10 do t[i] = i * i end return #t, t[10]"},
     {format, "return string.format('%d %s %5.2f %x %q', 42, 'str', 3.14159, 255, 'a\nb')"},
     {format2, "return string.format('%g|%.3f|%5s|%-5s|%c|%e', 1e20, 1/3, 'a', 'b', 65, 12345.678)"},
     {strfns, "return string.upper('abc'), string.lower('ABC'), string.rep('ab', 3, ','), string.reverse('abc'), string.len('hello'), string.sub('hello', 2, 4)"},
     {find, "return string.find('hello world', 'o w'), string.match('key=val', '(%w+)=(%w+)')"},
     {gsub, "return string.gsub('hello world', 'o', '0')"},
     {gmatch, "local s = 0 for w in string.gmatch('one two three', '%a+') do s = s + #w end return s"},
     {bytes, "return string.byte('A'), string.char(72, 105)"},
     {sort, "local t = {5, 3, 8, 1} table.sort(t) return table.concat(t, ',')"},
     {sortf, "local t = {3, 1, 2} table.sort(t, function(a, b) return a > b end) return table.concat(t, ' ')"},
     {tins, "local t = {1, 2} table.insert(t, 3) table.insert(t, 1, 0) local r = table.remove(t) return r, #t, table.unpack(t)"},
     {pack, "local p = table.pack(1, nil, 3) return p.n"},
     {math1, "return math.floor(3.7), math.ceil(3.2), math.sqrt(16), math.max(1, 5, 3), math.min(2, 0), math.abs(-3), math.huge, math.pi"},
     {math2, "return math.sin(0), math.cos(0), math.exp(0), math.log(1), math.fmod(7, 3), math.tointeger(3.0), math.type(1), math.type(1.5)"},
     {ints, "return math.maxinteger, math.mininteger, 1 << 62, 3 & 5, 3 | 5, ~0, 5 >> 1"},
     {conv, "return tostring(1.5), tostring(10), tostring(nil), tostring(true), tonumber('0x10'), tonumber('  12  '), tonumber('1e2'), tonumber('z', 36)"},
     {printnum, "print(1e100, 0.1, 1/3, -0.0, 100, 2^63)"},
     {pcall, "return pcall(function() error('boom') end)"},
     {errobj, "return pcall(function() error({code = 1}) end)"},
     {select, "return select('#', 1, 2, 3), select(2, 'a', 'b', 'c')"},
     {meta, "local t = setmetatable({}, {__index = function(t, k) return k .. '!' end}) return t.x"},
     {fib, "local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end return fib(20)"},
     {coro, "local co = coroutine.create(function(a) local b = coroutine.yield(a + 1) return b * 2 end) local _, x = coroutine.resume(co, 1) local _, y = coroutine.resume(co, 10) return x, y"},
     {os, "return os.time() > 0, type(os.clock()), os.date('%Y') ~= nil, os.getenv('NO_SUCH_VAR_X')"},
     {utf8, "return utf8.char(228, 8364), utf8.len('abc')"},
     {iowrite, "io.write('io.write works ', 1, ' ', 2.5, '\n')"},
     {pairs, "local t = {a = 1, b = 2, c = 3} local n = 0 for k, v in pairs(t) do n = n + v end return n"},
     {ipairs, "local n = 0 for i, v in ipairs({10, 20, 30}) do n = n + i * v end return n"},
     {load, "local f = load('return 5 * 5') return f()"},
     {concat, "return 'a' .. 1 .. 2.0 .. 'b'"},
     {goto, "local i = 0 ::top:: i = i + 1 if i < 3 then goto top end return i"},
     {bad, "return nil + 1"},
     {syntax, "return )"}].
