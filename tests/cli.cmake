file(MAKE_DIRECTORY "${TEST_DIR}")
file(WRITE "${TEST_DIR}/source with spaces.erl" "-module(example).\n")
file(WRITE "${TEST_DIR}/-source.erl" "-module(example).\n")

function(check_cli name expected_exit expected_stdout expected_stderr)
    execute_process(COMMAND "${TOOL}" ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err
    )
    if(NOT "${result}" STREQUAL "${expected_exit}")
        message(FATAL_ERROR "${name}: expected exit ${expected_exit}, got ${result}: ${err}")
    endif()
    if(NOT out MATCHES "${expected_stdout}" OR NOT err MATCHES "${expected_stderr}")
        message(FATAL_ERROR "${name}: unexpected stdout=[${out}] stderr=[${err}]")
    endif()
endfunction()

check_cli(help 0 "Usage: clau" "^$" --help)
check_cli(short_help 0 "Usage: clau" "^$" -h)
check_cli(version 0 "^clau [0-9]+\\.[0-9]+\\.[0-9]+\n$" "^$" --version)
check_cli(no_inputs 2 "^$" "no input files")
# --print-inputs parses the listed inputs, adds the modules they name as compilation would, and lists the batch:
# listed sources in order, then each added source with the site that named it. Nothing is compiled.
file(WRITE "${TEST_DIR}/unparsable.erl" "-module(unparsable). -include(\"absent.hrl\"). f( ->\n")
file(WRITE "${TEST_DIR}/ref.erl" "-module(ref).\n-import(gen_tcp, [connect/3]).\n")
check_cli(print_inputs_help 0 "--print-inputs" "^$" --help)
check_cli(print_inputs 0 "^source with spaces.erl\n-source.erl\n$" "^$"
    --print-inputs "source with spaces.erl" -- -source.erl)
check_cli(print_inputs_library 0 "^ref\\.erl\n[^\n]*/gen_tcp\\.erl \\(referenced at ref\\.erl:2\\)\n" "^$"
    --print-inputs --print-ast ref.erl)
check_cli(print_inputs_parse_error 1 "^unparsable\\.erl\n$" "error:" --print-inputs unparsable.erl)
check_cli(print_inputs_missing 1 "^$" "cannot access" --print-inputs missing.erl)
check_cli(print_inputs_output 2 "^$" "cannot be used with.*--print-inputs" --print-inputs -o out unparsable.erl)
check_cli(print_inputs_backend 2 "^$" "compilation switches" --print-inputs -O2 unparsable.erl)
# --print-env validates the whole command and prints it as TOML with effective settings, never reading sources.
check_cli(print_env_help 0 "--print-env" "^$" --help)
check_cli(print_env 0 "^# clau compile environment[^\n]*\ncommand = \\[\n  \"clau\",\n  \"--print-env\",\n  \"-O2\",.*actions = \\[\n  \"compile\",\n\\].*sources = \\[\n  \"[^\n]*/unparsable\\.erl\",\n\\].*include_dirs = \\[\n  \"[^\n]*/inc\",\n\\]\ndefines = \\[\n  \"V=1\",\n\\].*target_triple = \"[^\"\n]+\"\noptimization = \"O2\"\ntype_specialization = true\n.*emit = \"none\"\nartifact_dir = \"[^\n]*/build/aot\"\n$" "^$"
    --print-env -O2 -DV=1 -I inc unparsable.erl)
check_cli(print_env_link 0 "actions = \\[\n  \"link\",.*entry = \"example:main\"\noutput = \"[^\n]*/app(\\.exe)?\"\nlinker = \"[^\n]*\"\nruntime_library = \"[^\n]+\"" "^$"
    --print-env -o app --entry example "source with spaces.erl")
check_cli(print_env_cache_dirs 0 "cache_inference_in_dirs = \\[\n  \"\\*/stdlib\",\n.*  \"\\*/syntax_tools-\\*\",\n  \"lib/\\*\",\n\\]\n" "^$"
    --print-env --cache-inference-in-dirs lib/* unparsable.erl)
check_cli(print_env_frontend 0 "actions = \\[\n  \"print-pp\",\n  \"print-ast\",\n\\]" "^$" --print-env --print-ast --print-pp unparsable.erl)
check_cli(print_env_quoted 0 "defines = \\[\n  \"Q=\\\\\"x\\\\\"\",\n\\]" "^$" --print-env "-DQ=\"x\"" unparsable.erl)
check_cli(print_env_missing 1 "^$" "cannot access" --print-env missing.erl)
check_cli(print_env_new_project 2 "^$" "--print-env cannot be combined with --new-project" --print-env --new-project p.toml)
check_cli(unknown_option 2 "^$" "unknown option" --unknown)
check_cli(missing_output 2 "^$" "expected a path after" -o)
check_cli(duplicate_output 2 "^$" "more than once" -o first -o second source.erl)
check_cli(help_with_bad_option 2 "^$" "unknown option" --help --unknown)
check_cli(help_with_missing_output 2 "^$" "expected a path after" --help --output)
check_cli(help_precedes_version 0 "Usage: clau" "^$" --version --help)
check_cli(output_consumes_option_spelling 0 "Usage: clau" "^$" -o --version --help)
check_cli(output_consumes_terminator 0 "Usage: clau" "^$" --output -- --help)
check_cli(option_after_terminator_is_input 1 "^$" "cannot access|not a regular file" -- --help)
check_cli(missing_input 1 "^$" "cannot access|not a regular file" missing.erl)
check_cli(directory_input 1 "^$" "not a regular file" .)
check_cli(default_pipeline 0 "^$" "^$" "source with spaces.erl")
check_cli(end_of_options 0 "^$" "^$" -- -source.erl)
file(WRITE "${TEST_DIR}/-source.erl" "-module(other).\n")
check_cli(multiple_inputs 0 "^$" "^$"
    "source with spaces.erl" ./-source.erl)
file(WRITE "${TEST_DIR}/-source.erl" "-module(example).\n")

# A failed link (here: an absent runtime library) neither creates nor replaces the output.
set(output "${TEST_DIR}/output file.bin")
file(WRITE "${TEST_DIR}/entry.erl" "-module(entry). -export([main/1]). main(_) -> ok.\n")
file(REMOVE "${output}")
check_cli(no_output_created 1 "^$" "runtime library not found"
    --output "${output}" --runtime-library absent.lib "source with spaces.erl" entry.erl)
if(EXISTS "${output}")
    message(FATAL_ERROR "Default pipeline created an output file")
endif()
file(WRITE "${output}" "preserve existing output\n")
check_cli(no_output_overwritten 1 "^$" "runtime library not found"
    -o "${output}" --runtime-library absent.lib "source with spaces.erl" entry.erl)
file(READ "${output}" contents)
if(NOT contents STREQUAL "preserve existing output\n")
    message(FATAL_ERROR "Default pipeline modified an existing output file")
endif()

# Preprocessing checks consume source and options but never create executable output.
file(WRITE "${TEST_DIR}/warning.erl" "-module(warning). -warning({notice,1}).\n")
file(WRITE "${TEST_DIR}/error.erl" "-module(error). -error(stop).\n")
file(WRITE "${TEST_DIR}/defines.erl" "-module(defines). -if(?VALUE =:= {1,atom} andalso ?FLAG). ok. -else. -error(bad_define). -endif.\n")
file(WRITE "${TEST_DIR}/first.erl" "-module(first). -define(PRIVATE,1).\n")
file(WRITE "${TEST_DIR}/second.erl" "-module(second). -ifdef(PRIVATE). -error(leaked). -endif.\n")
file(MAKE_DIRECTORY "${TEST_DIR}/first include" "${TEST_DIR}/second include" "${TEST_DIR}/app/include")
file(WRITE "${TEST_DIR}/first include/pick.hrl" "-define(PICK,1).\n")
file(WRITE "${TEST_DIR}/second include/pick.hrl" "-define(PICK,2).\n")
file(WRITE "${TEST_DIR}/app/include/lib.hrl" "-define(LIB,true).\n")
file(WRITE "${TEST_DIR}/includes.erl" "-include(\"pick.hrl\"). -include_lib(\"demo/include/lib.hrl\"). -if(?PICK =:= 2 andalso ?LIB). ok. -else. -error(lookup). -endif.\n")
file(WRITE "${TEST_DIR}/features.erl" "-if(?FEATURE_ENABLED(compr_assign) andalso not ?FEATURE_ENABLED(maybe_expr)). ok. -else. -error(features). -endif.\n")
check_cli(preprocess 0 "^$" "^$" --preprocess-check "source with spaces.erl")
check_cli(preprocess_warning 0 "^$" "warning:.*notice" --preprocess-check warning.erl)
check_cli(preprocess_error 1 "^$" "error:.*stop" --preprocess-check error.erl)
check_cli(preprocess_definitions 0 "^$" "^$" --preprocess-check -D "VALUE={1,atom}" -DFLAG defines.erl)
check_cli(preprocess_invalid_definition 1 "^$" "literal Erlang term" --preprocess-check -DVALUE=1+2 first.erl)
check_cli(preprocess_duplicate_definition 1 "^$" "redefining macro" --preprocess-check -DA=1 -DA=2 first.erl)
check_cli(preprocess_isolation 0 "^$" "^$" --preprocess-check first.erl second.erl)
check_cli(preprocess_failure_latches 1 "^$" "stop" --preprocess-check error.erl first.erl)
check_cli(preprocess_includes 0 "^$" "^$" --preprocess-check -I "first include" "-Isecond include" --app-dir demo=app includes.erl)
check_cli(preprocess_features 0 "^$" "^$" --preprocess-check --enable-feature compr_assign --disable-feature maybe_expr features.erl)
check_cli(preprocess_unknown_feature 1 "^$" "unknown.*feature" --preprocess-check --enable-feature missing first.erl)
check_cli(preprocess_missing_include_operand 2 "^$" "expected a value" --preprocess-check -I)
check_cli(preprocess_bad_application 2 "^$" "expected app=directory" --preprocess-check --app-dir broken first.erl)
check_cli(preprocess_output_conflict 2 "^$" "cannot be used" --preprocess-check -o "${output}" first.erl)
file(WRITE "${TEST_DIR}/a.out" "existing executable\n")
check_cli(preprocess_preserves_output 0 "^$" "^$" --preprocess-check first.erl)
check_cli(print_pp_help 0 "--print-pp" "^$" --help)
check_cli(print_pp_no_input 2 "^$" "no input files" --print-pp)
check_cli(print_pp_missing_input 1 "^$" "cannot access|not a regular file" --print-pp missing.erl)
check_cli(print_pp_output_conflict 2 "^$" "cannot be used" --print-pp -o "${output}" first.erl)
check_cli(print_pp 0 "- module [(] example [)] [.]\n$" "^$" --print-pp "source with spaces.erl")
check_cli(print_pp_combined 0 "- module [(] example [)] [.]\n$" "^$"
    --preprocess-check --print-pp "source with spaces.erl")
check_cli(print_pp_warning 0 "- module [(] warning [)] [.]\n$" "warning:.*notice" --print-pp warning.erl)
check_cli(print_pp_failure_latches 1 "- module [(] first [)] [.]\n$" "error:.*stop"
    --print-pp error.erl first.erl)
check_cli(print_pp_isolation 0 "- module [(] first [)].*- module [(] second [)]" "^$"
    --print-pp first.erl second.erl)
file(WRITE "${TEST_DIR}/printing.erl" "-module(printing). -include(\"pick.hrl\"). -ifdef(FLAG). f() -> {?PICK, ?VALUE}. -else. -error(branch). -endif.\n")
check_cli(print_pp_expansion 0 "f [(] [)] -> [{] 2 , 42 [}] [.]\n$" "^$"
    --print-pp -I "first include" "-Isecond include" -DFLAG -DVALUE=42 printing.erl)
check_cli(print_pp_end_options 0 "- module [(] example [)]" "^$" --print-pp -- -source.erl)
check_cli(print_ast_help 0 "--print-ast" "^$" --help)
check_cli(print_ast_no_input 2 "^$" "no input files" --print-ast)
check_cli(print_ast_output_conflict 2 "^$" "cannot be used" --print-ast -o "${output}" first.erl)
check_cli(print_ast 0 "^\\(Module forms=2\n.*ModuleAttribute name=example\\)\n\\)\n$" "^$"
    --print-ast "source with spaces.erl")
check_cli(print_ast_end_options 0 "ModuleAttribute name=example" "^$" --print-ast -- -source.erl)
check_cli(print_ast_warning 0 "ModuleAttribute name=warning" "warning:.*notice" --print-ast warning.erl)
check_cli(print_ast_failure_latches 1 "ModuleAttribute name=first\\)\n\\)\n$" "error:.*stop" --print-ast error.erl first.erl)
check_cli(print_ast_isolation 0 "ModuleAttribute name=first.*ModuleAttribute name=second" "^$"
    --print-ast first.erl second.erl)
check_cli(print_ast_expansion 0 "IntegerLiteral value=2.*IntegerLiteral value=42" "^$"
    --print-ast -I "first include" "-Isecond include" -DFLAG -DVALUE=42 printing.erl)
check_cli(print_ast_combined 0 "^- file .*\\(Module forms=2\n.*ModuleAttribute name=example" "^$"
    --print-ast --print-pp --preprocess-check "source with spaces.erl")
file(WRITE "${TEST_DIR}/parse-error.erl" "-module(broken). broken() -> . recovered() -> ok.\n")
check_cli(print_ast_recovery 1 "Function name=recovered" "error:.*parse-error.erl" --print-ast parse-error.erl)
file(WRITE "${TEST_DIR}/unsupported.erl" "-module(unsupported). -export([f/no]). f() -> ok.\n")
check_cli(print_ast_unsupported 1 "Function name=f" "error:" --print-ast unsupported.erl)
# Syntax checking shares all frontend options and never implies semantic validity.
check_cli(parse_check_help 0 "--parse-check" "^$" --help)
check_cli(parse_check 0 "^$" "^$" --parse-check "source with spaces.erl")
check_cli(parse_check_no_inputs 2 "^$" "no input files" --parse-check)
check_cli(parse_check_missing 1 "^$" "cannot access|not a regular file" --parse-check missing.erl)
check_cli(parse_check_warning 0 "^$" "warning:.*notice" --parse-check warning.erl)
check_cli(parse_check_pp_error 1 "^$" "error:.*stop" --parse-check error.erl)
check_cli(parse_check_syntax_error 1 "^$" "error:.*parse-error.erl" --parse-check parse-error.erl)
check_cli(parse_check_mixed 1 "^$" "parse-error.erl.*warning:" --parse-check parse-error.erl warning.erl)
check_cli(parse_check_isolation 0 "^$" "^$" --parse-check first.erl second.erl)
check_cli(parse_check_end_options 0 "^$" "^$" --parse-check -- -source.erl)
check_cli(parse_check_precedence 1 "^$" "error:" --preprocess-check --parse-check parse-error.erl)
check_cli(parse_check_ast 0 "ModuleAttribute name=first" "^$" --parse-check --print-ast first.erl)
check_cli(parse_check_pp 0 "- module [(] first [)]" "^$" --parse-check --print-pp first.erl)
check_cli(parse_check_expansion 0 "^$" "^$" --parse-check
    -I "first include" "-Isecond include" -DFLAG -DVALUE=42 printing.erl)
file(WRITE "${TEST_DIR}/parse-options.erl" "-module(parse_options). -include_lib(\"demo/include/lib.hrl\"). f() -> {?LIB, ?FEATURE_ENABLED(compr_assign), maybe}.\n")
check_cli(parse_check_options 0 "^$" "^$" --parse-check --app-dir demo=app
    --enable-feature compr_assign --disable-feature maybe_expr parse-options.erl)
file(WRITE "${TEST_DIR}/semantic-error.erl" "f() -> Unbound.\n")
check_cli(parse_check_semantics_deferred 0 "^$" "^$" --parse-check semantic-error.erl)
check_cli(parse_check_output_conflict 2 "^$" "cannot be used" --parse-check -o "${output}" first.erl)
check_cli(parse_check_no_new_output 2 "^$" "cannot be used" --parse-check -o new-output first.erl)
if(EXISTS "${TEST_DIR}/new-output")
    message(FATAL_ERROR "Parse checking created output")
endif()
file(READ "${output}" contents)
if(NOT contents STREQUAL "preserve existing output\n")
    message(FATAL_ERROR "Parse checking overwrote requested output")
endif()
file(READ "${TEST_DIR}/a.out" contents)
if(NOT contents STREQUAL "existing executable\n")
    message(FATAL_ERROR "Preprocessing overwrote executable output")
endif()

# The default pipeline uses the real preprocessor/parser and aggregates source failures.
check_cli(default_warning 0 "^$" "warning:.*notice" warning.erl)
check_cli(default_pp_error 1 "^$" "error:.*stop" error.erl)
check_cli(default_parse_error 1 "^$" "error:.*parse-error.erl" parse-error.erl)
check_cli(default_failure_latches 1 "^$" "parse-error.erl.*warning:.*notice" parse-error.erl warning.erl)
check_cli(default_isolation 0 "^$" "^$" first.erl second.erl)
check_cli(default_expansion 0 "^$" "^$"
    -I "first include" "-Isecond include" -DFLAG -DVALUE=42 printing.erl)
check_cli(default_options 0 "^$" "^$" --app-dir demo=app
    --enable-feature compr_assign --disable-feature maybe_expr parse-options.erl)

# Verbose traces describe only ingested files and never become printed source or AST.
file(WRITE "${TEST_DIR}/traced.erl" "-module(traced). -include(\"outer.hrl\"). -include_lib(\"demo/include/lib.hrl\").\n-if(false). -include(\"skipped.hrl\"). -endif. f() -> ?LIB.\n")
file(WRITE "${TEST_DIR}/outer.hrl" "-include(\"inner.hrl\").\n")
file(WRITE "${TEST_DIR}/inner.hrl" "-file(\"logical.hrl\", 1).\n")
set(pp_trace "\\[pp\\] traced.erl\n")
set(parse_trace "\\[parse\\] traced.erl\n")
set(include_trace "\\[pp\\] [^\n]*outer.hrl\n\\[pp\\] [^\n]*inner.hrl\n\\[pp\\] [^\n]*app/include/lib.hrl\n")
# The directory index reports last (plan step 62E).
set(index_trace "\\[index\\] listed=[^\n]*\n")
check_cli(verbose_help 0 "--verbose" "^$" --verbose --help)
check_cli(verbose_version 0 "^clau" "^$" --verbose --version)
check_cli(verbose_no_inputs 2 "^$" "no input files" --verbose)
check_cli(verbose_default 0 "^$" "^${pp_trace}${parse_trace}${include_trace}\\[comp\\].*phase=emission module=\"traced\"\n${index_trace}$" --verbose --app-dir demo=app traced.erl)
check_cli(verbose_pp_only 0 "^$" "^${pp_trace}${include_trace}${index_trace}$" --verbose --preprocess-check --app-dir demo=app traced.erl)
check_cli(verbose_parse 0 "^$" "^${pp_trace}${parse_trace}${include_trace}${index_trace}$" --verbose --parse-check --app-dir demo=app traced.erl)
check_cli(verbose_print_pp 0 "^- file .*f [(] [)] -> true [.]\n$" "^${pp_trace}${include_trace}${index_trace}$"
    --verbose --print-pp --app-dir demo=app traced.erl)
check_cli(verbose_print_ast 0 "^\\(Module .*Atom name=true\\)\n    \\)\n  \\)\n\\)\n$" "^${pp_trace}${parse_trace}${include_trace}${index_trace}$"
    --verbose --print-ast --app-dir demo=app traced.erl)
check_cli(verbose_multiple 0 "^$" "^\\[pp\\] first.erl\n\\[parse\\] first.erl\n\\[pp\\] second.erl\n\\[parse\\] second.erl\n\\[comp\\].*phase=emission module=\"second\"\n${index_trace}$"
    --verbose first.erl second.erl)
check_cli(verbose_opt_in 0 "^$" "^$" --app-dir demo=app traced.erl)

# Stage times: preprocess and parse per file once it is done, compile per batch, all in milliseconds on stderr.
# Before the compile total come its phases, each inference pass, lattice operations, input churn, the slowest inferred
# functions and recursive groups.
set(ms ": [0-9]+\\.[0-9][0-9][0-9] ms\n")
set(note ": [0-9]+\\.[0-9][0-9][0-9] ms \\([0-9]+ (of [0-9]+ )?work steps, [^\n]+\\)\n")
set(phases "\\[time\\] index 2 modules${ms}\\[time\\] calls 2 modules${ms}\\[time\\] declared-types 2 modules${ms}"
    "\\[time\\] inference 2 modules${ms}\\[time\\] inference pass 1 2 modules${note}"
    "\\[time\\] inference lattice 2 modules: [0-9]+\\.[0-9][0-9][0-9] ms \\(join [0-9]+ [^\n]+ nesting lookups[^\n]+\\)\n"
    "\\[time\\] inference churn 2 modules: [0-9]+\\.[0-9][0-9][0-9] ms \\(slots by changes [^\n]+\\)\n"
    "(\\[time\\] inference churned [^\n]+\n)*(\\[time\\] inference clean-changed [^\n]+\n)*"
    "(\\[time\\] inference facts [^\n]+\n)*(\\[time\\] inference largest [^\n]+\n)*"
    "(\\[time\\] inference slowest (first|second):module_info/[01]${note})(\\[time\\] inference (slowest|costliest|rounds) [^\n]+\n)*"
    "\\[time\\] contracts 2 modules${ms}\\[time\\] lower 2 modules${ms}\\[time\\] optimize 2 modules${ms}"
    "\\[time\\] emit 2 modules${ms}")
string(CONCAT phases ${phases})
check_cli(stage_times_help 0 "--print-stage-times" "^$" --help)
check_cli(stage_times_pp_only 0 "^$" "^\\[time\\] preprocess traced.erl${ms}$"
    --print-stage-times --preprocess-check --app-dir demo=app traced.erl)
check_cli(stage_times_parse 0 "^$" "^\\[time\\] preprocess traced.erl${ms}\\[time\\] parse traced.erl${ms}$"
    --print-stage-times --parse-check --app-dir demo=app traced.erl)
check_cli(stage_times_compile 0 "^$" "^\\[time\\] preprocess first.erl${ms}\\[time\\] parse first.erl${ms}\\[time\\] preprocess second.erl${ms}\\[time\\] parse second.erl${ms}${phases}\\[time\\] compile 2 modules${ms}$"
    --print-stage-times first.erl second.erl)
