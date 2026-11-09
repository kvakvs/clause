%% OTP's mnesia_log as other nodes call it (plan 21 step MIM1H11a,
%% docs/libraries/mnesia-protocol.md#backups-over-nodes): a node backing up a checkpoint spawns the copier of a
%% table whose retainer is here. Backups are clause_mnesia_backup's. OTP's other internal functions are
%% declined: no other node calls them.
-module(mnesia_log).
-export([tab_copier/3]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    append/2,
    backup/1,
    backup/2,
    backup_checkpoint/2,
    backup_checkpoint/3,
    backup_log_header/0,
    backup_master/2,
    chunk_decision_log/1,
    chunk_decision_tab/1,
    chunk_log/1,
    chunk_log/2,
    close_decision_log/0,
    close_decision_tab/0,
    close_log/1,
    confirm_decision_log_dump/0,
    confirm_log_dump/1,
    dcd2ets/1,
    dcd2ets/2,
    dcd_log_header/0,
    dcd_version/0,
    dcl_log_header/0,
    dcl_version/0,
    decision_log_file/0,
    decision_log_version/0,
    decision_tab_file/0,
    decision_tab_version/0,
    ets2dcd/1,
    ets2dcd/2,
    init/0,
    init_log_dump/0,
    latest_log_file/0,
    log/1,
    log_decision/1,
    log_files/0,
    open_decision_log/0,
    open_decision_tab/0,
    open_log/4,
    open_log/6,
    prepare_decision_log_dump/0,
    prepare_log_dump/1,
    previous_decision_log_file/0,
    previous_log_file/0,
    purge_all_logs/0,
    purge_some_logs/0,
    save_decision_tab/1,
    slog/1,
    stop/0,
    trans_log_header/0,
    unsafe_close_log/1,
    version/0,
    view/0,
    view/1,
    write_trans_log_header/0
]).

% The copier of Tab's records and retained records for Receiver's backup, OTP's #backup_args{}.
tab_copier(Receiver, B, Tab) ->
    clause_mnesia_backup:tab_copier(Receiver, B, Tab).

append(_, _) -> erlang:error({not_implemented, append, 2}).
backup(_) -> erlang:error({not_implemented, backup, 1}).
backup(_, _) -> erlang:error({not_implemented, backup, 2}).
backup_checkpoint(_, _) -> erlang:error({not_implemented, backup_checkpoint, 2}).
backup_checkpoint(_, _, _) -> erlang:error({not_implemented, backup_checkpoint, 3}).
backup_log_header() -> erlang:error({not_implemented, backup_log_header, 0}).
backup_master(_, _) -> erlang:error({not_implemented, backup_master, 2}).
chunk_decision_log(_) -> erlang:error({not_implemented, chunk_decision_log, 1}).
chunk_decision_tab(_) -> erlang:error({not_implemented, chunk_decision_tab, 1}).
chunk_log(_) -> erlang:error({not_implemented, chunk_log, 1}).
chunk_log(_, _) -> erlang:error({not_implemented, chunk_log, 2}).
close_decision_log() -> erlang:error({not_implemented, close_decision_log, 0}).
close_decision_tab() -> erlang:error({not_implemented, close_decision_tab, 0}).
close_log(_) -> erlang:error({not_implemented, close_log, 1}).
confirm_decision_log_dump() -> erlang:error({not_implemented, confirm_decision_log_dump, 0}).
confirm_log_dump(_) -> erlang:error({not_implemented, confirm_log_dump, 1}).
dcd2ets(_) -> erlang:error({not_implemented, dcd2ets, 1}).
dcd2ets(_, _) -> erlang:error({not_implemented, dcd2ets, 2}).
dcd_log_header() -> erlang:error({not_implemented, dcd_log_header, 0}).
dcd_version() -> erlang:error({not_implemented, dcd_version, 0}).
dcl_log_header() -> erlang:error({not_implemented, dcl_log_header, 0}).
dcl_version() -> erlang:error({not_implemented, dcl_version, 0}).
decision_log_file() -> erlang:error({not_implemented, decision_log_file, 0}).
decision_log_version() -> erlang:error({not_implemented, decision_log_version, 0}).
decision_tab_file() -> erlang:error({not_implemented, decision_tab_file, 0}).
decision_tab_version() -> erlang:error({not_implemented, decision_tab_version, 0}).
ets2dcd(_) -> erlang:error({not_implemented, ets2dcd, 1}).
ets2dcd(_, _) -> erlang:error({not_implemented, ets2dcd, 2}).
init() -> erlang:error({not_implemented, init, 0}).
init_log_dump() -> erlang:error({not_implemented, init_log_dump, 0}).
latest_log_file() -> erlang:error({not_implemented, latest_log_file, 0}).
log(_) -> erlang:error({not_implemented, log, 1}).
log_decision(_) -> erlang:error({not_implemented, log_decision, 1}).
log_files() -> erlang:error({not_implemented, log_files, 0}).
open_decision_log() -> erlang:error({not_implemented, open_decision_log, 0}).
open_decision_tab() -> erlang:error({not_implemented, open_decision_tab, 0}).
open_log(_, _, _, _) -> erlang:error({not_implemented, open_log, 4}).
open_log(_, _, _, _, _, _) -> erlang:error({not_implemented, open_log, 6}).
prepare_decision_log_dump() -> erlang:error({not_implemented, prepare_decision_log_dump, 0}).
prepare_log_dump(_) -> erlang:error({not_implemented, prepare_log_dump, 1}).
previous_decision_log_file() -> erlang:error({not_implemented, previous_decision_log_file, 0}).
previous_log_file() -> erlang:error({not_implemented, previous_log_file, 0}).
purge_all_logs() -> erlang:error({not_implemented, purge_all_logs, 0}).
purge_some_logs() -> erlang:error({not_implemented, purge_some_logs, 0}).
save_decision_tab(_) -> erlang:error({not_implemented, save_decision_tab, 1}).
slog(_) -> erlang:error({not_implemented, slog, 1}).
stop() -> erlang:error({not_implemented, stop, 0}).
trans_log_header() -> erlang:error({not_implemented, trans_log_header, 0}).
unsafe_close_log(_) -> erlang:error({not_implemented, unsafe_close_log, 1}).
version() -> erlang:error({not_implemented, version, 0}).
view() -> erlang:error({not_implemented, view, 0}).
view(_) -> erlang:error({not_implemented, view, 1}).
write_trans_log_header() -> erlang:error({not_implemented, write_trans_log_header, 0}).
