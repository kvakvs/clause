-module(clause_disk_log).
-export([items/1]).

%% Halt log files of disk_log's internal format (plan 21 step LIB12, docs/builtins.md#ets): the items of a log's bytes
%% as disk_log reads them in read_only mode, {opened | closed, [Binary], BadBytes}, or not_a_log_file; a runtime
%% builtin. ets:file2tab/1,2 and tabfile_info/1 read table files with it.
items(_) -> clause_builtin_placeholder.
