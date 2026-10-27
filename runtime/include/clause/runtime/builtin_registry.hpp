#pragma once
#include "callable.hpp"
#include <array>
#include <clause/abi/builtins.hpp>
#include <clause/abi/modules.hpp>
#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>

namespace clause::runtime {
// Native implementation of one builtin: reads exactly its arity of argument words, records an Erlang error or a
// failure in the context's checked channel and returns the result word (ignored after a failure).
using BuiltinBody = Word (*)(ProcessContext &context, std::span<const Word> arguments);

// One builtin to register. The names must outlive the registry (production tables use string literals).
struct BuiltinEntry final {
    std::string_view module;
    std::string_view function;
    std::size_t arity = 0;
    BuiltinBody body = nullptr;
    // A bounded fast path generated code runs first for a quick builtin; it returns 0 to leave the call to `body`
    // (quick builtins).
    BuiltinBody quick = nullptr;
};

// A registered builtin as calls enter it: a FrameDescriptor with a null body, which marks a builtin, then its
// implementation. Dynamic calls and funs enter it like generated code (docs/builtins.md).
struct BuiltinFrame final {
    abi::v1::FrameDescriptor frame{};
    BuiltinBody body = nullptr;
    // The entry's quick fast path, or null (quick builtins).
    BuiltinBody quick = nullptr;
};

static_assert(std::is_standard_layout_v<BuiltinFrame>);
static_assert(offsetof(BuiltinFrame, frame) == 0);

// The production builtins of one runtime, by exact module, function and arity.
class BuiltinRegistry final {
  public:
    BuiltinRegistry() = default;
    BuiltinRegistry(const BuiltinRegistry &) = delete;
    BuiltinRegistry &operator=(const BuiltinRegistry &) = delete;
    ~BuiltinRegistry() = default;

    // Register every entry or none: an invalid entry or a name registered already (or twice in `entries`)
    // rejects the whole batch, as does an allocation failure.
    RegistryResult<void> add(std::span<const BuiltinEntry> entries);
    // The builtin registered under these names; null when none is.
    const BuiltinFrame *find(std::string_view module, std::string_view function, std::size_t arity) const noexcept;
    // The builtin of a bridge index (abi::v1::bridge_builtins); null when the index is unknown or unregistered.
    const BuiltinFrame *bridge(std::size_t index) const noexcept;

    // Count registered builtins.
    std::size_t size() const noexcept { return entries_.size(); }

    // The modules that have registered builtins, in name order (step 41H).
    std::vector<std::string_view> modules() const;

    // module:function/arity of a registered builtin's frame, empty when unknown; a linear scan. Added for call tracing.
    std::string name_of(const abi::v1::FrameDescriptor &frame) const;

  private:
    using Key = std::tuple<std::string_view, std::string_view, std::size_t>;
    // Record a registered frame under its bridge index, if its name is in the catalog.
    void link_bridge(const Key &key, const BuiltinFrame &frame) noexcept;
    // Map nodes keep each frame's address stable: fun definitions and generated calls hold them.
    std::map<Key, BuiltinFrame> entries_;
    // Registered frames by bridge index, for CLAUSE_builtin_v1.
    std::array<const BuiltinFrame *, abi::v1::bridge_builtins.size()> bridge_{};
};

// The builtin of a FrameDescriptor with a null body, as the registry built it.
inline const BuiltinFrame &builtin_frame(const abi::v1::FrameDescriptor &frame) noexcept {
    return *reinterpret_cast<const BuiltinFrame *>(&frame);
}

// The frame of a trapping builtin's continuation (docs/builtins.md#portions), entered with `arity` state registers;
// it has no name in the registry.
constexpr BuiltinFrame continuation_frame(const BuiltinBody body, const std::size_t arity) noexcept {
    return {.frame = {.module = nullptr,
                      .module_atom = 0,
                      .function_atom = 0,
                      .arity = arity,
                      .body = nullptr,
                      .slots = 0,
                      .roots = 0},
            .body = body};
}

// Run one portion of `builtin` on its arguments; returns the result, or 0 with an Erlang error or failure recorded
// in the checked channel or after the builtin trapped (ProcessStack::take_trap). Host exceptions become failures.
Word call_builtin_portion(ProcessContext &context, const BuiltinFrame &builtin, const Word *arguments) noexcept;
// Run `builtin` to completion, continuing each of its traps at once without yielding.
Word call_builtin(ProcessContext &context, const BuiltinFrame &builtin, const Word *arguments) noexcept;

// The erlang builtins of the original bridge catalog: guard BIFs, operators, display, halt, raising and
// function_exported/3.
std::span<const BuiltinEntry> erlang_builtins() noexcept;
// The tuple builtins: setelement/3, make_tuple/2,3, tuple_to_list/1, list_to_tuple/1.
std::span<const BuiltinEntry> term_access_builtins() noexcept;
// The list builtins that run in portions: length/1, '++'/2, '--'/2.
std::span<const BuiltinEntry> list_builtins() noexcept;
// The conversion builtins: atom_to_list/1, list_to_atom/1, integer_to_list/1,2, list_to_integer/1,2,
// float_to_list/1,2, binary_to_list/1, list_to_binary/1, iolist_to_binary/1, pid_to_list/1, ref_to_list/1.
std::span<const BuiltinEntry> conversion_builtins() noexcept;
// The io builtins: io:format/1,2,3, io:fwrite/1,2,3, io:put_chars/1,2 and io:nl/0,1.
std::span<const BuiltinEntry> io_builtins() noexcept;
// The process builtins: self/0, make_ref/0, spawn/1,3, spawn_link/1,3, is_process_alive/1, '!'/2, send/2, link/1,
// unlink/1, exit/2, exit_signal/2, process_flag/2, spawn_monitor/1,3, monitor/2, demonitor/1,2, register/2,
// unregister/1, whereis/1 and registered/0.
std::span<const BuiltinEntry> process_builtins() noexcept;
// The port builtins (docs/ports.md): open_port/2, port_close/1, port_command/2,3, port_connect/2, port_control/3,
// port_call/2,3, port_info/1,2, port_to_list/1, list_to_port/1 and ports/0.
std::span<const BuiltinEntry> port_builtins() noexcept;
// The os builtins: os:type/0, os:getenv/0,1, os:env/0, os:putenv/2, os:unsetenv/1, os:getpid/0, os:version/0.
std::span<const BuiltinEntry> os_builtins() noexcept;
// The lists module's data functions of step 41T: sublist/2,3, droplast/1, nthtail/2, last/1, duplicate/2, join/2,
// flatten/1,2; max/1, min/1, usort/1, umerge/1,2; keymember/3, keysearch/3, keydelete/3.
std::span<const BuiltinEntry> lists_sublist_builtins() noexcept;
std::span<const BuiltinEntry> lists_ordering_builtins() noexcept;
std::span<const BuiltinEntry> lists_key_builtins() noexcept;
// The lists merges and zips: merge/1,2, merge3/3, umerge3/3, keymerge/3, ukeymerge/3, ukeysort/2; zip/2,3,
// zip3/3,4, unzip/1, unzip3/1, enumerate/1,2,3.
std::span<const BuiltinEntry> lists_merge_builtins() noexcept;
std::span<const BuiltinEntry> lists_zip_builtins() noexcept;
// The math module of step 41V: every OTP 29 math function over the C library.
std::span<const BuiltinEntry> math_builtins() noexcept;
// The maps builtins of step 41X: get/3, is_key/2, remove/2, new/0, size/1, merge/2, take/2, update/3, with/2,
// without/2, from_keys/2.
std::span<const BuiltinEntry> maps_builtins() noexcept;
// The ordered-list collections of plan 21 step LIB1: ordsets, orddict and proplists functions without funs.
std::span<const BuiltinEntry> ordsets_builtins() noexcept;
std::span<const BuiltinEntry> orddict_builtins() noexcept;
std::span<const BuiltinEntry> proplists_builtins() noexcept;
// The hashed collections of plan 21 step LIB2: dict, sets and the private clause_hashed helpers.
std::span<const BuiltinEntry> dict_builtins() noexcept;
std::span<const BuiltinEntry> sets_builtins() noexcept;
// The trees and queues of plan 21 step LIB3: gb_trees, gb_sets and queue.
std::span<const BuiltinEntry> gb_trees_builtins() noexcept;
std::span<const BuiltinEntry> gb_sets_builtins() noexcept;
std::span<const BuiltinEntry> queue_builtins() noexcept;
// The arrays of plan 21 step LIB3B: array and the private clause_array helpers.
std::span<const BuiltinEntry> array_builtins() noexcept;
// Module calendar (plan 21 step LIB5; runtime/src/builtins/calendar.cpp).
std::span<const BuiltinEntry> calendar_builtins() noexcept;
// Module timer's conversions (plan 21 step LIB22; runtime/src/builtins/timer.cpp).
std::span<const BuiltinEntry> timer_module_builtins() noexcept;
// Module base64 (plan 21 step LIB5; runtime/src/builtins/encoding/).
std::span<const BuiltinEntry> base64_builtins() noexcept;
// Module clause_asn1: the codec of generated ASN.1 modules (plan 21 step LIB84; runtime/src/builtins/asn1/).
std::span<const BuiltinEntry> asn1_builtins() noexcept;
// Module clause_public_key: public_key's term walks and tables (plan 21 step LIB80B; runtime/src/builtins/public_key/).
std::span<const BuiltinEntry> public_key_builtins() noexcept;
// Module ssl's cipher suite table and name walks (plan 21 step LIB81A; runtime/src/builtins/ssl/).
std::span<const BuiltinEntry> ssl_builtins() noexcept;
// Module json and its private clause_json scanners (plan 21 step LIB5E; runtime/src/builtins/encoding/json.cpp).
std::span<const BuiltinEntry> json_builtins() noexcept;
// Module filename's walks over one name, and its joins and splits (plan 21 step LIB5G; runtime/src/builtins/filename/).
std::span<const BuiltinEntry> filename_builtins() noexcept;
std::span<const BuiltinEntry> filename_join_builtins() noexcept;
// Module uri_string: parsing, composition and coding (plan 21 step LIB5G; runtime/src/builtins/uri/).
std::span<const BuiltinEntry> uri_parse_builtins() noexcept;
std::span<const BuiltinEntry> uri_compose_builtins() noexcept;
std::span<const BuiltinEntry> uri_codec_builtins() noexcept;
// Module string (plan 21 step LIB4; runtime/src/builtins/string/): the text functions.
std::span<const BuiltinEntry> string_text_builtins() noexcept;
std::span<const BuiltinEntry> string_search_builtins() noexcept;
std::span<const BuiltinEntry> string_lexeme_builtins() noexcept;
std::span<const BuiltinEntry> string_number_builtins() noexcept;
std::span<const BuiltinEntry> string_latin1_builtins() noexcept;
// json's default encoder and formatter (plan 21 step LIB5F; runtime/src/builtins/encoding/json_encode.cpp).
std::span<const BuiltinEntry> json_encode_builtins() noexcept;
// Modules rand and random and the private clause_rand generators (plan 21 step LIB6; runtime/src/builtins/rand/).
std::span<const BuiltinEntry> rand_builtins() noexcept;
// The persistent_term builtins of step 41M2: put/2, put_new/2, get/0,1,2, erase/1, info/0.
std::span<const BuiltinEntry> persistent_term_builtins() noexcept;
// The unicode builtins of step 41Y1: characters_to_binary/1,2,3, characters_to_list/1,2, bin_is_7bit/1,
// bom_to_encoding/1, encoding_to_bom/1.
std::span<const BuiltinEntry> unicode_builtins() noexcept;
// The code module of step 41Z: ensure_loaded/1, ensure_modules_loaded/1, is_loaded/1, all_loaded/0, which/1,
// module_status/0,1, is_sticky/1, answered for linked code.
std::span<const BuiltinEntry> code_module_builtins() noexcept;
// clause_interp's registry of interpreted modules (plan 21 LIB77B; builtins/interpreted.cpp).
std::span<const BuiltinEntry> interpreter_builtins() noexcept;
// The init module of step 41L: get_plain_arguments/0, get_arguments/0, get_argument/1, script_name/0.
std::span<const BuiltinEntry> init_builtins() noexcept;
// The atomics module of step 41N: new/2, get/2, put/3, add/3, add_get/3, sub/3, sub_get/3, exchange/3,
// compare_exchange/4, info/1 over handle references.
std::span<const BuiltinEntry> atomics_builtins() noexcept;
// The erts_internal functions of step 41N stdlib needs: cmp_term/2, map_next/3.
std::span<const BuiltinEntry> erts_internal_builtins() noexcept;
// The Unicode table queries of step 41O1: clause_unicode:table/1, class/1, cases/1, flags/1, compose_pair/2 and
// unicode_util:spec_version/0, pattern_whitespace/0.
std::span<const BuiltinEntry> unicode_table_builtins() noexcept;
// unicode_util's walks and unicode's forms and properties (plan 21 step LIB4B; runtime/src/builtins/unicode/forms.cpp).
std::span<const BuiltinEntry> unicode_form_builtins() noexcept;
// io_lib's format engine, writers and checks, and collect functions (plan 21 step LIB4C; runtime/src/builtins/io_lib/).
std::span<const BuiltinEntry> io_lib_format_builtins() noexcept;
std::span<const BuiltinEntry> io_lib_write_builtins() noexcept;
std::span<const BuiltinEntry> io_lib_collect_builtins() noexcept;
// io_lib's format-directed scanner fread/2,3 (plan 21 step LIB4E; runtime/src/builtins/io_lib/fread.cpp).
std::span<const BuiltinEntry> io_lib_fread_builtins() noexcept;
// clause_io_lib:apply_ordered/4, which reruns a write with maps_order funs' sorted keys (plan 21 step LIB4F;
// runtime/src/builtins/io_lib/key_orders.cpp).
std::span<const BuiltinEntry> io_lib_order_builtins() noexcept;
// Modules erl_anno and erl_internal (plan 21 step LIB8; runtime/src/builtins/front_end/).
std::span<const BuiltinEntry> erl_anno_builtins() noexcept;
std::span<const BuiltinEntry> erl_internal_builtins() noexcept;
// clause_erl_scan:scan/6, erl_scan's scanner (plan 21 step LIB30; runtime/src/builtins/front_end/erl_scan.cpp).
std::span<const BuiltinEntry> erl_scan_builtins() noexcept;
// clause_erl_parse:parse/1 and abstract/3, and erl_parse's normalise/1, tokens/1,2, first_anno/1 and precedence
// tables (plan 21 step LIB31; runtime/src/builtins/front_end/erl_parse.cpp).
std::span<const BuiltinEntry> erl_parse_builtins() noexcept;
// clause_erl_eval:bit_type/2, bit_segment/3 and bit_take/3, erl_eval's bit syntax segments (plan 21 step LIB32;
// runtime/src/builtins/front_end/erl_eval.cpp).
std::span<const BuiltinEntry> erl_eval_builtins() noexcept;
// erl_expand_records:module/2 in C++ (plan 21 step LIB34C; runtime/src/builtins/front_end/expand_records.cpp).
std::span<const BuiltinEntry> expand_records_builtins() noexcept;
// erl_lint (plan 21 step LIB34C; runtime/src/builtins/front_end/lint*.cpp).
std::span<const BuiltinEntry> lint_builtins() noexcept;
// erl_eval's pattern matching and constant folding (plan 21 step LIB34C;
// runtime/src/builtins/front_end/eval_match.cpp).
std::span<const BuiltinEntry> eval_match_builtins() noexcept;
// epp's -if conditions (plan 21 step LIB34C; runtime/src/builtins/front_end/epp_eval.cpp).
std::span<const BuiltinEntry> epp_builtins() noexcept;
// erl_pp's printer (plan 21 step LIB34C; runtime/src/builtins/front_end/pp*.cpp).
std::span<const BuiltinEntry> erl_pp_builtins() noexcept;
// erl_posix_msg:message/1 (plan 21 step LIB50; runtime/src/builtins/erl_posix_msg.cpp).
std::span<const BuiltinEntry> erl_posix_msg_builtins() noexcept;
// clause_io, the terminal queries of io and io_ansi (plan 21 step LIB63; runtime/src/builtins/terminal.cpp).
std::span<const BuiltinEntry> clause_io_builtins() noexcept;
// clause_net, the name and interface lookups of net and inet (plan 21 step LIB60; runtime/src/builtins/net/).
std::span<const BuiltinEntry> clause_net_builtins() noexcept;
// clause_filelib's wildcard pattern builtins (plan 21 step LIB51; runtime/src/builtins/filelib.cpp).
std::span<const BuiltinEntry> filelib_builtins() noexcept;
// clause_argparse, argparse's validation, parser and help (plan 21 step LIB70; runtime/src/builtins/argparse/).
std::span<const BuiltinEntry> argparse_builtins() noexcept;
// clause_qlc_pt, the qlc parse transform at run time (plan 21 step LIB73; runtime/src/builtins/qlc_pt/).
std::span<const BuiltinEntry> qlc_pt_builtins() noexcept;
// clause_compile, the source quotes of compile's messages (plan 21 step LIB77E1;
// runtime/src/builtins/compile_messages.cpp).
std::span<const BuiltinEntry> compile_message_builtins() noexcept;
// The erlang gaps of plan 21 step LIB9A: checksums and iovecs, tuple edits, option-taking process builtins and
// system flags.
std::span<const BuiltinEntry> checksum_builtins() noexcept;
std::span<const BuiltinEntry> tuple_edit_builtins() noexcept;
std::span<const BuiltinEntry> process_option_builtins() noexcept;
std::span<const BuiltinEntry> system_flag_builtins() noexcept;
// decode_packet/3 of plan 21 step LIB9C.
std::span<const BuiltinEntry> packet_builtins() noexcept;
// suspend_process/1,2, resume_process/1 and process_display/2 of plan 21 step LIB9B.
std::span<const BuiltinEntry> suspension_builtins() noexcept;
// Module sofs and its clause_sofs helpers (plan 21 step LIB7A; runtime/src/builtins/sofs/).
std::span<const BuiltinEntry> sofs_callbacks_builtins() noexcept;
std::span<const BuiltinEntry> sofs_constructors_builtins() noexcept;
std::span<const BuiltinEntry> sofs_families_builtins() noexcept;
std::span<const BuiltinEntry> sofs_products_builtins() noexcept;
std::span<const BuiltinEntry> sofs_relations_builtins() noexcept;
std::span<const BuiltinEntry> sofs_selectors_builtins() noexcept;
std::span<const BuiltinEntry> sofs_sets_builtins() noexcept;
// Module graph (plan 21 step LIB7B; runtime/src/builtins/graph/).
std::span<const BuiltinEntry> graph_query_builtins() noexcept;
std::span<const BuiltinEntry> graph_traversal_builtins() noexcept;
// Module ets (plan 21 step LIB10; runtime/src/builtins/ets/).
std::span<const BuiltinEntry> ets_table_builtins() noexcept;
std::span<const BuiltinEntry> ets_info_builtins() noexcept;
std::span<const BuiltinEntry> ets_write_builtins() noexcept;
std::span<const BuiltinEntry> ets_read_builtins() noexcept;
std::span<const BuiltinEntry> ets_walk_builtins() noexcept;
std::span<const BuiltinEntry> ets_update_builtins() noexcept;
// Match specifications in ets and erlang:match_spec_test/3 (plan 21 step LIB11; runtime/src/builtins/ets/).
std::span<const BuiltinEntry> ets_select_builtins() noexcept;
std::span<const BuiltinEntry> ets_select_edit_builtins() noexcept;
std::span<const BuiltinEntry> ets_match_spec_builtins() noexcept;
// digraph's path searches and digraph_utils' traversals (plan 21 step LIB12; runtime/src/builtins/digraph/).
std::span<const BuiltinEntry> digraph_builtins() noexcept;
// clause_disk_log:items/1, the items of a halt log file (plan 21 step LIB12; runtime/src/builtins/disk_log/).
std::span<const BuiltinEntry> disk_log_builtins() noexcept;
// clause_dets, the dets file engine (plan 21 step LIB53; runtime/src/builtins/dets/).
std::span<const BuiltinEntry> dets_builtins() noexcept;
// Modules erl_features and records (plan 21 step LIB8B).
std::span<const BuiltinEntry> erl_features_builtins() noexcept;
std::span<const BuiltinEntry> records_builtins() noexcept;
// The re module over PCRE2 of step 41ZA: version/0.
std::span<const BuiltinEntry> re_builtins() noexcept;
// The zlib streams of step 41Q: clause_zlib:open/0, close/1, set_controller/2, deflate_init/2, inflate_init/2,
// deflate/3, inflate/2, *_end/1, *_reset/1, deflate_params/2 and the dictionary functions.
std::span<const BuiltinEntry> zlib_builtins() noexcept;
// The zstd contexts and dictionaries of plan 21 step LIB54E: the clause_zstd builtins under library/stdlib/zstd.erl.
std::span<const BuiltinEntry> zstd_builtins() noexcept;
// The re builtins of step 41ZB: clause_re:compile/2, re:inspect/2 and clause_re:run/4.
std::span<const BuiltinEntry> re_pattern_builtins() noexcept;
std::span<const BuiltinEntry> re_matching_builtins() noexcept;
// The mailbox steps of step 41S behind prim_eval:'receive'/2: clause_receive:peek/0, skip/0, take/0, restart/0 and
// wait/1.
std::span<const BuiltinEntry> receive_builtins() noexcept;
// The binary module of step 41W: match/2,3, matches/2,3, split/2,3, compile_pattern/1; part/2,3, at/2, first/1,
// last/1, bin_to_list/1,2,3, copy/1,2, longest_common_prefix/1, longest_common_suffix/1, referenced_byte_size/1,
// join/2; decode_unsigned/1,2, encode_unsigned/1,2, encode_hex/1,2, decode_hex/1.
std::span<const BuiltinEntry> binary_search_builtins() noexcept;
std::span<const BuiltinEntry> binary_part_builtins() noexcept;
std::span<const BuiltinEntry> binary_encoding_builtins() noexcept;
// The binary, atom and number conversions of step 41D: atom_to_binary/1,2, binary_to_atom/1,2,
// binary_to_existing_atom/1,2, list_to_existing_atom/1, integer_to_binary/1,2, binary_to_integer/1,2,
// float_to_binary/1,2, binary_to_float/1, list_to_float/1, binary_to_list/3, bitstring_to_list/1,
// list_to_bitstring/1, split_binary/2, iolist_size/1, list_to_pid/1 and list_to_ref/1.
std::span<const BuiltinEntry> binary_conversion_builtins() noexcept;
// The external term format of step 41E: term_to_binary/1,2, term_to_iovec/1,2, external_size/1,2, and
// binary_to_term/1,2.
std::span<const BuiltinEntry> term_encoding_builtins() noexcept;
std::span<const BuiltinEntry> term_decoding_builtins() noexcept;
// The time builtins of step 41F (monotonic, system and calendar time, units, unique integers) and its timers
// (start_timer/3,4, send_after/3,4, cancel_timer/1,2, read_timer/1,2).
std::span<const BuiltinEntry> time_builtins() noexcept;
std::span<const BuiltinEntry> timer_builtins() noexcept;
// Process and system introspection of step 41G1: process_info/1,2, processes/0, group_leader/0,2,
// garbage_collect/0,1,2, yield/0, and statistics/1, system_info/1, memory/0,1.
std::span<const BuiltinEntry> introspection_builtins() noexcept;
std::span<const BuiltinEntry> system_builtins() noexcept;
// Spawn options, spawn requests, aliases and hibernation of step 41G2: spawn_opt/2..5, spawn_request/1..5,
// spawn_request_abandon/1, alias/0,1, unalias/1, hibernate/3 (monitor/3 is among process_builtins()).
std::span<const BuiltinEntry> spawning_builtins() noexcept;
// Fun information, hashing and MD5 of step 41G3: fun_info/1,2, fun_to_list/1, is_builtin/3, make_fun/3 (plan 21
// step LIB32); phash/2, phash2/1,2; md5/1, md5_init/0, md5_update/2, md5_final/1.
std::span<const BuiltinEntry> fun_info_builtins() noexcept;
std::span<const BuiltinEntry> hashing_builtins() noexcept;
std::span<const BuiltinEntry> md5_builtins() noexcept;
// erlang:apply/2,3 for dynamic calls (plan 21 step LIB22; runtime/src/builtins/erlang/apply.cpp).
std::span<const BuiltinEntry> apply_builtins() noexcept;
// The answers of a non-distributed node and of linked code (step 41H): nodes/0,1,2, is_alive/0,
// monitor_node/2,3, disconnect_node/1; module_loaded/1, loaded/0, pre_loaded/0, delete_module/1,
// prepare_loading/2, finish_loading/1, get_module_info/1,2.
std::span<const BuiltinEntry> node_builtins() noexcept;
std::span<const BuiltinEntry> code_builtins() noexcept;
// The process dictionary builtins: put/2, get/0,1, get_keys/0,1, erase/0,1. Added for step 41C.
std::span<const BuiltinEntry> dictionary_builtins() noexcept;
// Every production builtin family runtime startup registers, together covering abi::v1::bridge_builtins but for the
// extensions' builtins.
std::span<const std::span<const BuiltinEntry>> production_builtins() noexcept;

// The builtins of one runtime extension archive (clause_runtime_crypto), registered after the production ones when a
// program links the archive. Added for the crypto runtime archive (plan 21 step LIB79).
struct RuntimeExtension {
    // Names the extension in diagnostics.
    std::string_view name;
    // The builtin families the extension adds.
    std::span<const std::span<const BuiltinEntry>> families;
    // Runs before the runtime starts: why the program cannot run, empty when it can. Added for crypto's pinned
    // FIPS mode (plan 21 step LIB79F).
    std::string (*start)() = nullptr;
};
} // namespace clause::runtime
