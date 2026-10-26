%% OTP 29 zstd types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([
    compress_parameters/0,
    context/0,
    decompress_parameters/0,
    dict/0
]).
-opaque context() :: {compress | decompress, term()}.
-opaque dict() :: {cdict | ddict, term()}.
-type compressionLevel() :: -22..22.
-type strategy() ::
    default
    | fast
    | dfast
    | greedy
    | lazy
    | lazy2
    | btlazy2
    | btopt
    | btultra
    | btultra2.
-type compress_parameters() ::
    #{
        dictionary => binary() | dict(),
        pledgedSrcSize => non_neg_integer(),
        compressionLevel => compressionLevel(),
        windowLog => non_neg_integer(),
        hashLog => non_neg_integer(),
        chainLog => non_neg_integer(),
        searchLog => non_neg_integer(),
        minMatch => non_neg_integer(),
        targetLength => non_neg_integer(),
        strategy => strategy(),
        targetCBlockSize => non_neg_integer(),
        enableLongDistanceMatching => boolean(),
        ldmHashLog => non_neg_integer(),
        ldmMinMatch => non_neg_integer(),
        ldmBucketSizeLog => non_neg_integer(),
        ldmHashRateLog => non_neg_integer(),
        contentSizeFlag => boolean(),
        checksumFlag => boolean(),
        dictIDFlag => boolean()
    }.
-type decompress_parameters() ::
    #{
        dictionary => binary() | dict(),
        windowLogMax => non_neg_integer()
    }.
