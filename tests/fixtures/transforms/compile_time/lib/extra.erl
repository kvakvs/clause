%% A transform-time callee reading a library header through include_lib: #file_info.size is 2.
-module(extra).
-export([value/0]).
-include_lib("kernel/include/file.hrl").

value() -> #file_info.size + 8.
