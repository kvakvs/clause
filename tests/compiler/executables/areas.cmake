# Area labels and gate placement of the golden cases (plan 11 step VAL7). Every case carries exactly one area label,
# so library work runs its area alone: ctest -L mnesia. A case without an area fails the configuration.

# Whole families by name prefix, as prefix=area.
set(golden_area_prefixes mnesia_=mnesia dist_=dist epmd_=dist ssl_=ssl public_key_=public_key crypto_=crypto
    file_=file)
# The other cases by name. core: language and runtime semantics, processes, ports, sockets, startup options.
set(golden_area_core atom_order atomics_counters behaviours binary_append binary_build binary_conversions binary_loops
    binary_match bit_integers bit_map_comprehensions builtin_bridge busy_ports byte_scans case_scope case_select
    catch_values closures conversions crash_reports deep_recursion demo dynamic_calls erlang_gaps exits export_all
    external_funs external_terms fairness float_arithmetic float_loops forward_lists fun_hashing fun_values
    garbage_collection generations heap_growth identities identity_guards if_select imports init_module integer_limit
    integer_text introspection large_binaries large_maps leaf_inlining links list_comprehensions list_loops literals
    loop_fairness mailbox_collection many_ports map_fold_loops maybe_else message_handoff min_heap_size module_info
    monitors named_funs names native_anonymous native_external native_records on_load_modules owned_maps
    packet_decoding persistent_term port_fairness port_identities port_input port_spawn portions prim_eval_receive
    process_dictionary processes proofs quick_builtins raise_classes receive_after record_info record_update
    record_wildcards records_reflection recursion runtime_args_files runtime_config_files runtime_env_options
    runtime_options selective_receive send slow_owner small_maps socket_ops sockets spawning stack_traces suspension
    tail_calls term_access term_sort time_timers try_after try_catch wakeups)
set(golden_area_dist connections external_identities this_node)
# file: also archives, compression, disk_log and dets.
set(golden_area_file dets_tables disk_log_ops filelib_ops filenames tar_ops zip_ops zlib_streams zstd_ops)
set(golden_area_kernel applications code_module code_paths inet_addresses inet_res_queries inet_socket_options
    kernel_sup loggers mim_undocumented net_names node_code os_commands os_maps process_groups rpcs)
set(golden_area_stdlib argparse_ops arrays base64_codec binary_module calendar_dates console digraph_tables
    erl_error_formats ets_concurrency ets_match_specs ets_owner ets_portions ets_table_files ets_tables ets_update
    ets_walk fread_scanning gen_events gen_fsms gen_servers gen_statems graph_module hashed_collections io_ansi_codes
    io_lib_text io_options io_output io_requests json_codec library lists_completion lists_data maps_completion math
    ordered_collections pretty_print proc_libs qlc_queries rand_sequences re_module re_version sofs_sets strings
    supervisors timers trees_queues unicode unicode_clusters unicode_forms unicode_tables uri_strings)
# tools: compiler, syntax_tools and the other applications (inets, asn1, eldap, xmerl, shell, escript).
set(golden_area_tools asn1_ber asn1_der asn1_per asn1_uper beam_ops compile_forms compiler_tables eldap_requests
    epp_dodger_forms epp_files erl_eval_exprs erl_expand_records_forms erl_lint_checks erl_parse_forms erl_pp_forms
    erl_prettypr_format erl_scan_tokens erl_syntax_trees escript_ops httpc_requests interpreted_modules merl_templates
    mim_applications ms_transform_forms msacc_accounting otp_headers prettypr_layout qlc_pt_transforms
    shell_docs_render shell_expand shell_session syntax_tools_comments syntax_tools_lib win32reg_ops xmerl_export
    xmerl_queries xmerl_scanning)
set(golden_areas core dist file kernel stdlib tools)

# A test of more than about 10 s alone runs in the full gate only (label full_only), except this smoke set of heavy
# cases, about one per area, that stays in the fast gate. Seconds alone, fast mode, Windows x64 Debug, 2026-11-22.
set(golden_smoke_cases
    file_io            # 47
    gen_servers        # 52
    supervisors        # 44
    applications       # 36
    sockets            # 36
    loggers            # 40
    mnesia_dirty       # 75
    dist_start         # 39
    public_key_certs   # 61
    crypto_digests     # 36
    ssl_engine)        # 72
# Families that are full_only by rule, new cases included, apart from their smoke case.
set(golden_full_only_prefixes mnesia_ dist_ epmd_ ssl_)
# The other heavy cases, far above 10 s alone: they link kernel and stdlib, or more.
set(golden_full_only_cases beam_ops code_paths compile_forms connections crypto_ciphers crypto_pubkey dets_tables
    disk_log_ops eldap_requests epp_dodger_forms epp_files erl_lint_checks erl_prettypr_format erlang_gaps escript_ops
    ets_concurrency ets_match_specs ets_table_files ets_walk external_identities file_compressed file_info file_paths
    file_positioned file_ram file_readers file_sorter_ops filelib_ops filenames gen_events gen_fsms gen_statems
    httpc_requests inet_addresses inet_res_queries inet_socket_options interpreted_modules io_options io_output
    io_requests kernel_sup many_ports merl_templates mim_applications mim_undocumented msacc_accounting net_names
    node_code os_commands os_maps otp_headers port_spawn proc_libs process_groups public_key_cacerts public_key_codec
    public_key_crls public_key_keys public_key_ocsp public_key_path public_key_test_data qlc_queries record_wildcards
    records_reflection rpcs runtime_args_files runtime_config_files runtime_env_options shell_docs_render shell_expand
    shell_session socket_ops spawning syntax_tools_comments syntax_tools_lib tar_ops this_node win32reg_ops
    xmerl_export xmerl_queries xmerl_scanning zip_ops zstd_ops)

# Sets out_var to the one area of case: by family prefix, else by name; fails when there is none or more than one.
function(golden_case_area case out_var)
    set(found)
    foreach(pair IN LISTS golden_area_prefixes)
        string(REPLACE "=" ";" pair "${pair}")
        list(GET pair 0 prefix)
        list(GET pair 1 area)
        string(FIND "${case}" "${prefix}" position)
        if(position EQUAL 0)
            list(APPEND found ${area})
        endif()
    endforeach()
    foreach(area IN LISTS golden_areas)
        if(case IN_LIST golden_area_${area})
            list(APPEND found ${area})
        endif()
    endforeach()
    list(LENGTH found count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Golden case ${case} needs exactly one area label, has '${found}': add it to "
            "tests/compiler/executables/areas.cmake")
    endif()
    set(${out_var} ${found} PARENT_SCOPE)
endfunction()

# Sets out_var to true when case runs in the full gate only: a full_only family or name, and not a smoke case.
function(golden_case_full_only case out_var)
    set(full_only FALSE)
    foreach(prefix IN LISTS golden_full_only_prefixes)
        string(FIND "${case}" "${prefix}" position)
        if(position EQUAL 0)
            set(full_only TRUE)
        endif()
    endforeach()
    if(case IN_LIST golden_full_only_cases)
        set(full_only TRUE)
    endif()
    if(case IN_LIST golden_smoke_cases)
        set(full_only FALSE)
    endif()
    set(${out_var} ${full_only} PARENT_SCOPE)
endfunction()

# Fails when a list names a case that is not among the given cases, so renamed or removed cases leave no stale entries.
function(golden_check_names)
    set(cases ${ARGN})
    set(named ${golden_smoke_cases} ${golden_full_only_cases})
    foreach(area IN LISTS golden_areas)
        list(APPEND named ${golden_area_${area}})
    endforeach()
    foreach(name IN LISTS named)
        if(NOT name IN_LIST cases)
            message(FATAL_ERROR "tests/compiler/executables/areas.cmake names ${name}, which is not a golden case")
        endif()
    endforeach()
endfunction()
