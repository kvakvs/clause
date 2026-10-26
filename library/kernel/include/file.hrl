%% Clause's kernel/include/file.hrl (plan 21 step LIB23, docs/libraries/library.md): the records of file's documentation,
%% file:read_file_info/1,2 results and the descriptors of raw files, written for Clause.
-ifndef(FILE_HRL_).
-define(FILE_HRL_, 1).

-record(file_info, {
    size :: non_neg_integer() | undefined,
    type :: device | directory | other | regular | symlink | undefined,
    access :: read | write | read_write | none | undefined,
    atime :: file:date_time() | non_neg_integer() | undefined,
    mtime :: file:date_time() | non_neg_integer() | undefined,
    ctime :: file:date_time() | non_neg_integer() | undefined,
    mode :: non_neg_integer() | undefined,
    links :: non_neg_integer() | undefined,
    major_device :: non_neg_integer() | undefined,
    minor_device :: non_neg_integer() | undefined,
    inode :: non_neg_integer() | undefined,
    uid :: non_neg_integer() | undefined,
    gid :: non_neg_integer() | undefined
}).

-record(file_descriptor, {
    module :: module(),
    data :: term()
}).

-endif.
