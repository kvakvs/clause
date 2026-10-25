%% Clause's stdlib/include/zip.hrl (plan 21 step LIB54, docs/library.md): the records of zip's documentation,
%% written for Clause.
-ifndef(ZIP_HRL_).
-define(ZIP_HRL_, 1).

% A file in an archive: its name, file information, comment, the offset of its local header and its compressed size.
-record(zip_file, {
    name :: string(),
    info :: file:file_info(),
    comment :: string(),
    offset :: non_neg_integer(),
    comp_size :: non_neg_integer()
}).

% The comment of an archive.
-record(zip_comment, {
    comment :: string()
}).

-endif.
