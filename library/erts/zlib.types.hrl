%% OTP 29 zlib types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([
    zflush/0,
    zlevel/0,
    zmemlevel/0,
    zstrategy/0,
    zstream/0,
    zwindowbits/0
]).
-type zstream() :: reference().
-type zflush() :: none | sync | full | finish.
-type zlevel() :: none | default | best_compression | best_speed | 0..9.
-type zstrategy() :: default | filtered | huffman_only | rle.
-type zmemlevel() :: 1..9.
-type zwindowbits() :: -15..-8 | 8..47.
