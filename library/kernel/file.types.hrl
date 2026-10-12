%% OTP 29 file types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([
    date_time/0,
    fd/0,
    file_info/0,
    filename/0,
    filename_all/0,
    io_device/0,
    io_server/0,
    location/0,
    mode/0,
    name/0,
    name_all/0,
    posix/0
]).
-record(file_info, {
    size :: non_neg_integer() | undefined,
    type ::
        device
        | directory
        | other
        | regular
        | symlink
        | undefined,
    access ::
        read | write | read_write | none | undefined,
    atime ::
        file:date_time() | non_neg_integer() | undefined,
    mtime ::
        file:date_time() | non_neg_integer() | undefined,
    ctime ::
        file:date_time() | non_neg_integer() | undefined,
    mode :: non_neg_integer() | undefined,
    links :: non_neg_integer() | undefined,
    major_device :: non_neg_integer() | undefined,
    minor_device :: non_neg_integer() | undefined,
    inode :: non_neg_integer() | undefined,
    uid :: non_neg_integer() | undefined,
    gid :: non_neg_integer() | undefined
}).
-record(file_descriptor, {module :: module(), data :: term()}).
-type filename() :: string().
-type filename_all() :: string() | (RawFilename :: binary()).
-type file_info() :: #file_info{}.
-type file_descriptor() :: #file_descriptor{}.
-type fd() :: file_descriptor().
-type io_server() :: pid().
-type io_device() :: io_server() | fd().
-type location() ::
    integer()
    | {bof, Offset :: integer()}
    | {cur, Offset :: integer()}
    | {eof, Offset :: integer()}
    | bof
    | cur
    | eof.
-type mode() ::
    read
    | write
    | append
    | exclusive
    | raw
    | binary
    | {delayed_write, Size :: non_neg_integer(), Delay :: non_neg_integer()}
    | delayed_write
    | {read_ahead, Size :: pos_integer()}
    | read_ahead
    | compressed
    | compressed_one
    | {zstd, zstd:compress_parameters() | zstd:decompress_parameters()}
    | {encoding, unicode:encoding()}
    | sync.
-type deep_list() :: [char() | atom() | deep_list()].
-type name() :: string() | atom() | deep_list().
-type name_all() ::
    string() | atom() | deep_list() | (RawFilename :: binary()).
-type posix() ::
    eacces
    | eagain
    | ebadf
    | ebadmsg
    | ebusy
    | edeadlk
    | edeadlock
    | edquot
    | eexist
    | efault
    | efbig
    | eftype
    | eintr
    | einval
    | eio
    | eisdir
    | eloop
    | emfile
    | emlink
    | emultihop
    | enametoolong
    | enfile
    | enobufs
    | enodev
    | enolck
    | enolink
    | enoent
    | enomem
    | enospc
    | enosr
    | enostr
    | enosys
    | enotblk
    | enotdir
    | enotsup
    | enxio
    | eopnotsupp
    | eoverflow
    | eperm
    | epipe
    | erange
    | erofs
    | espipe
    | esrch
    | estale
    | etxtbsy
    | exdev.
-type date_time() :: calendar:datetime().
