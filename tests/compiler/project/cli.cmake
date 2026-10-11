file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
file(WRITE "${TEST_DIR}/shared.erl" "-module(shared). value() -> ?VALUE.\n")
file(WRITE "${TEST_DIR}/project.toml" [=[schema_version=1
[[targets]]
name="app"
sources=["shared.erl"]
[targets.options]
defines=["VALUE=1"]
[[targets]]
name="tests"
sources=["shared.erl"]
[targets.options]
defines=["VALUE=2"]
]=])

# Check the project CLI contract through its public executable.
function(check name code stdout stderr)
    execute_process(COMMAND "${TOOL}" ${ARGN} WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 15)
    if(NOT "${result}" STREQUAL "${code}" OR NOT out MATCHES "${stdout}" OR NOT err MATCHES "${stderr}")
        message(FATAL_ERROR "${name}: exit=${result} stdout=[${out}] stderr=[${err}]")
    endif()
endfunction()
check(help 0 "--project.*--target" "^$" --help)
check(missing_project_operand 2 "^$" "expected a value" --project)
check(missing_target_operand 2 "^$" "expected a value" --project project.toml --target)
check(repeated_project 2 "^$" "more than once" --project project.toml --project project.toml)
check(target_without_project 2 "^$" "requires --project" --target app)
check(mixed 2 "^$" "positional" --project project.toml shared.erl)
check(help_no_io 0 "Usage:" "^$" --help --project missing.toml --target missing)
check(version_no_io 0 "^clau" "^$" --version --project missing.toml)
check(absent_project 1 "^$" "manifest" --project missing.toml)
check(unknown_target 2 "^$" "available targets: app, tests" --project project.toml --target APP)
check(parse_all 0 "^$" "^$" --parse-check --project project.toml)
check(project_suffix 0 "^$" "^$" --parse-check --project project)
check(project_suffix_absolute 0 "^$" "^$" --parse-check --project "${TEST_DIR}/project")
check(project_suffix_default 0 "^$" "^$" --project project)
file(COPY_FILE "${TEST_DIR}/project.toml" "${TEST_DIR}/exact")
file(WRITE "${TEST_DIR}/exact.toml" "invalid TOML")
check(project_exact_precedence 0 "^$" "^$" --parse-check --project exact)
file(WRITE "${TEST_DIR}/project" "schema_version=2\n")
check(project_existing_invalid 1 "^$" "schema_version" --parse-check --project project)
file(REMOVE "${TEST_DIR}/project")
file(MAKE_DIRECTORY "${TEST_DIR}/project")
check(project_existing_directory 1 "^$" "not an accessible regular file" --parse-check --project project)
file(REMOVE_RECURSE "${TEST_DIR}/project")
file(COPY_FILE "${TEST_DIR}/project.toml" "${TEST_DIR}/missing.toml.toml")
check(project_no_duplicate_suffix 1 "^$" "manifest" --parse-check --project missing.toml)
check(preprocess_all 0 "^$" "^$" --preprocess-check --project project.toml)
check(target_before_project 0 "^$" "^$" --target tests --project project.toml --parse-check)
check(print_all 0 "IntegerLiteral value=1.*IntegerLiteral value=2" "^$" --project project.toml --print-ast)
check(print_reversed 0 "IntegerLiteral value=2.*IntegerLiteral value=1" "^$" --project project.toml --print-ast --target tests --target app)
check(print_pp 0 "value.*1" "^$" --project project.toml --print-pp --target app)
check(output_check_conflict 2 "^$" "cannot be used" --project project.toml --parse-check -o sentinel)
check(output_multiple_conflict 2 "^$" "exactly one" --project project.toml -o sentinel)
check(default_pipeline 0 "^$" "^$" --project project.toml)
# Loading and planning the manifest is a stage of its own (plan step 62D3).
check(stage_times_plan 0 "^$" "^\\[time\\] plan [^\n]*project.toml: [0-9.]+ ms\n" --project project.toml --print-stage-times --parse-check)
# Each project target compiles as its own batch, named in its stage time.
check(stage_times 0 "^$" "\\[time\\] compile target app \\(1 module\\): [0-9.]+ ms( \\(cached\\))?\n.*\\[time\\] compile target tests \\(1 module\\)"
    --project project.toml --print-stage-times)
check(print_inputs_all 0 "^\\[target app\\]\n[^\n]*/shared\\.erl\n\\[target tests\\]\n[^\n]*/shared\\.erl\n$" "^$"
    --project project.toml --print-inputs)
check(print_inputs_target 0 "^[^\n]*/shared\\.erl\n$" "^$" --project project.toml --target tests --print-inputs)
check(print_inputs_entry 2 "^$" "cannot be used with.*--print-inputs" --project project.toml --print-inputs --entry shared)
# Modules named but not listed are found in source_search_paths, in turn naming further modules.
file(WRITE "${TEST_DIR}/main.erl" "-module(main).\n-export([main/1]).\nmain(_) -> helper:f().\n")
file(WRITE "${TEST_DIR}/lib/helper.erl" "-module(helper).\n-export([f/0]).\nf() -> deep:g().\n")
file(WRITE "${TEST_DIR}/lib/deep.erl" "-module(deep).\n-export([g/0]).\ng() -> ok.\n")
file(WRITE "${TEST_DIR}/refs.toml" "schema_version=1\n[[targets]]\nname='refs'\nsources=['main.erl']\n[targets.options]\nsource_search_paths=['lib']\n")
check(print_inputs_referenced 0 "^[^\n]*/main\\.erl\n[^\n]*/lib/helper\\.erl \\(referenced at [^\n]*/main\\.erl:3\\)\n[^\n]*/lib/deep\\.erl \\(referenced at [^\n]*/lib/helper\\.erl:3\\)\n$" "^$"
    --project refs.toml --print-inputs)
check(print_env_project 0 "^# clau compile environment[^\n]*\ncommand = .*\n\n\\[\\[targets\\]\\]\nname = \"app\"\nproject = \"[^\n]*/project\\.toml\"\nsources = \\[\n  \"[^\n]*/shared\\.erl\",\n\\].*defines = \\[\n  \"VALUE=1\",\n\\].*\n\n\\[\\[targets\\]\\]\nname = \"tests\".*defines = \\[\n  \"VALUE=2\",\n\\]" "^$"
    --print-env --project project.toml)
check(print_env_project_emit 0 "^# clau compile environment.*name = \"tests\".*emit = \"obj\"\nartifact_dir = \"[^\n]*/build/aot/[^/\n]+\"\n$" "^$"
    --print-env --emit obj --project project.toml --target tests)
check(default_output 1 "^$" "no entry point: no module exports main/1" --project project.toml --target app -o sentinel)
check(verbose_project 0 "^$" "^\\[pp\\] [^\n]*shared.erl\n\\[parse\\] [^\n]*shared.erl\n\\[comp\\].*phase=emission.*target=\"tests\""
    --verbose --no-cache --project project --target tests)
check(verbose_project_pp 0 "^$" "^\\[pp\\] [^\n]*shared.erl\n\\[index\\] listed=[^\n]*\n$"
    --verbose --project project.toml --target app --preprocess-check)
check(duplicate_macro 1 "^$" "redefining macro" --project project.toml --parse-check -DVALUE=3)
check(terminator 2 "^$" "positional" --project project.toml -- --target)
execute_process(COMMAND "${TOOL}" --project project.toml --print-ast --target tests --target tests
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE code OUTPUT_VARIABLE text ERROR_VARIABLE errors)
string(REGEX MATCHALL "IntegerLiteral value=2" matches "${text}")
list(LENGTH matches count)
if(NOT code EQUAL 0 OR NOT count EQUAL 1 OR NOT errors STREQUAL "")
    message(FATAL_ERROR "Repeated target was not deduplicated")
endif()
# Project compiles may keep their build cache (plan steps 62D2/62D4), nothing else.
file(GLOB build_entries RELATIVE "${TEST_DIR}/build" "${TEST_DIR}/build/*")
if(EXISTS "${TEST_DIR}/sentinel" OR (build_entries AND NOT build_entries STREQUAL "cache"))
    message(FATAL_ERROR "Project processing created output: ${build_entries}")
endif()

check(new_help 0 "--new-project" "^$" --help)
check(new_missing 2 "^$" "expected a value" --new-project)
check(new_empty 2 "^$" "expected a value" --new-project "")
check(new_repeat 2 "^$" "more than once" --new-project app --new-project other)
check(new_invalid_name 2 "^$" "requires a filename" --new-project .)
check(new_invalid_help 2 "^$" "requires a filename" --new-project . --help)
check(new_with_project 2 "^$" "cannot be combined" --new-project new --project project.toml)
check(new_with_target 2 "^$" "cannot be combined" --new-project new --target app)
check(new_with_input 2 "^$" "cannot be combined" --new-project new shared.erl)
check(new_with_output 2 "^$" "cannot be combined" --new-project new -o output)
check(new_with_check 2 "^$" "cannot be combined" --new-project new --parse-check)
check(new_with_pp 2 "^$" "cannot be combined" --new-project new --preprocess-check)
check(new_with_print 2 "^$" "cannot be combined" --new-project new --print-pp)
check(new_with_ast 2 "^$" "cannot be combined" --new-project new --print-ast)
check(new_with_include 2 "^$" "cannot be combined" --new-project new -I include)
check(new_with_define 2 "^$" "cannot be combined" --new-project new -DDEFAULT=true)
check(new_with_app 2 "^$" "cannot be combined" --new-project new --app-dir demo=demo)
check(new_with_enable 2 "^$" "cannot be combined" --new-project new --enable-feature maybe_expr)
check(new_with_disable 2 "^$" "cannot be combined" --new-project new --disable-feature compr_assign)
check(new_help_no_write 0 "Usage:" "^$" --new-project help-only --help)
check(new_version_no_write 0 "^clau" "^$" --new-project version-only --version)
if(EXISTS "${TEST_DIR}/help-only.toml" OR EXISTS "${TEST_DIR}/version-only.toml" OR EXISTS "${TEST_DIR}/new.toml")
    message(FATAL_ERROR "Informational or rejected creation wrote a manifest")
endif()
# Earlier project compiles kept a build cache; creation must not make a build directory itself.
file(REMOVE_RECURSE "${TEST_DIR}/build")
check(new_create 0 "Created .*starter.toml" "^$" --new-project starter)
file(READ "${TEST_DIR}/starter.toml" starter)
check(new_no_overwrite 1 "^$" "cannot create manifest exclusively" --new-project starter)
file(READ "${TEST_DIR}/starter.toml" after)
if(NOT starter STREQUAL after OR EXISTS "${TEST_DIR}/src" OR EXISTS "${TEST_DIR}/build")
    message(FATAL_ERROR "Creation modified an existing manifest or created extra directories")
endif()
check(new_suffix 0 "Created .*named.toml" "^$" --new-project named.toml)
if(EXISTS "${TEST_DIR}/named.toml.toml")
    message(FATAL_ERROR "Creation duplicated .toml extension")
endif()
file(MAKE_DIRECTORY "${TEST_DIR}/src")
file(WRITE "${TEST_DIR}/src/main.erl" "-module(main). main() -> ok.\n")
check(new_populate_check 0 "^$" "^$" --parse-check --project starter.toml)

# Default processing preserves target-specific options, selection, and error aggregation.
check(default_duplicate_macro 1 "^$" "redefining macro" --project project.toml -DVALUE=3)
file(WRITE "${TEST_DIR}/shared.erl" "-module(shared). -if(?VALUE =:= 1). broken() -> . -else. -warning(later_target). -endif.\n")
check(default_parse_error 1 "^$" "target app.*error:.*target tests.*warning:.*later_target" --project project.toml)
check(default_selected 0 "^$" "target tests.*warning:.*later_target" --project project.toml --target tests)
check(explicit_preprocess_only 0 "^$" "warning:.*later_target" --project project.toml --preprocess-check)
file(WRITE "${TEST_DIR}/shared.erl" "-module(shared). -if(?VALUE =:= 1). -error(stop). -else. -warning(later_target). -endif.\n")
check(default_pp_error 1 "^$" "target app.*error:.*stop.*target tests.*warning:.*later_target" --project project.toml)

include("${CMAKE_CURRENT_LIST_DIR}/manifest_cases.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/selection_cases.cmake")

# Declaration errors aggregate across files and retain the owning target.
file(WRITE "${TEST_DIR}/decl_bad.erl" "-module(decl_bad). -export([missing/0]).")
file(WRITE "${TEST_DIR}/decl_later.erl" "f() -> 1.")
file(WRITE "${TEST_DIR}/declarations.toml" "schema_version=1\n[[targets]]\nname='declarations'\nsources=['decl_bad.erl','decl_later.erl']\n")
check(declaration_batch 1 "^$" "target declarations.*undefined function missing/0.*missing module" --project declarations.toml)
check(declaration_syntax_only 0 "^$" "^$" --project declarations.toml --parse-check)
