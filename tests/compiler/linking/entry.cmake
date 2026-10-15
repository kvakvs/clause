# Entry selection contract (docs/executables.md): CLI --entry and manifest entry keys are
# validated against the compiled batch, with located diagnostics. Executable requests stop at an absent
# runtime library, which is checked only after the entry resolved and the batch compiled.
file(REMOVE_RECURSE "${WORK}")
file(COPY "${FIXTURES}/" DESTINATION "${WORK}")

# Run the compiler and require an exact exit status plus stderr pattern; no output file may appear.
function(check name code stderr)
    execute_process(COMMAND "${TOOL}" ${ARGN} WORKING_DIRECTORY "${WORK}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30 ENCODING UTF-8)
    if(NOT "${result}" STREQUAL "${code}" OR NOT out STREQUAL "" OR NOT err MATCHES "${stderr}")
        message(FATAL_ERROR "${name}: exit=${result} stdout=[${out}] stderr=[${err}]")
    endif()
    if(EXISTS "${WORK}/out" OR EXISTS "${WORK}/out.exe")
        message(FATAL_ERROR "${name}: created an executable output")
    endif()
endfunction()

set(not_defined "entry function helper:main/1 is not defined")
set(wrong_arity "helper.erl:11:1: entry function helper:pair/1 is not defined; found pair/2, but the entry takes the argument list \\(FUNCTION/1\\) or nothing \\(FUNCTION/0\\)")
set(unexported "helper.erl:9:1: entry function helper:hidden/1 is not exported")
set(missing "entry module nope is not among the compiled modules")

# Positional inputs: explicit entries validate in every compiling mode.
check(valid_default_function 0 "^$" --entry app app.erl helper.erl)
check(valid_named_function 0 "^$" --entry helper:start helper.erl)
check(valid_no_arguments 0 "^$" --entry helper:run helper.erl)
check(valid_emit_ir 0 "^$" --entry app --emit llvm-ir --artifact-dir ir app.erl)
check(missing_module 1 "^error: --entry: ${missing}\n$" --entry nope app.erl)
check(missing_function 1 "helper.erl:2:1: ${not_defined}\n$" --entry helper app.erl helper.erl)
check(wrong_arity 1 "${wrong_arity}" --entry helper:pair helper.erl)
check(unexported 1 "${unexported}\n$" --entry helper:hidden helper.erl)
check(unicode_module 1 "entry module möd is not among" --entry "möd" app.erl)
# An explicit /ARITY suffix selects exactly that arity.
check(explicit_zero 0 "^$" --entry helper:run/0 helper.erl)
check(explicit_one 0 "^$" --entry helper:start/1 helper.erl)
check(explicit_missing 1 "helper.erl:7:1: entry function helper:start/0 is not defined; found start/1\n$"
    --entry helper:start/0 helper.erl)
check(explicit_unexported 1 "${unexported}\n$" --entry helper:hidden/1 helper.erl)

# Executable requests detect the only module exporting main/1.
set(linked "^error: runtime library not found: [^\n]*absent\\.lib")
check(detected 1 "${linked}" -o out --runtime-library absent.lib app.erl helper.erl)
check(selected_over_detection 1 "${linked}" -o out --runtime-library absent.lib --entry other app.erl other.erl)
# Hints name the selection spelling; only project targets mention the manifest key.
set(spelling "MODULE\\[:FUNCTION\\[/ARITY\\]\\]")
set(hint "; choose the entry with --entry ${spelling}")
set(arity_hint " \\(an exported FUNCTION/1, or FUNCTION/0 run without the program arguments; FUNCTION defaults to main, ARITY to 1, else 0\\)\n$")
set(manifest_hint " or with entry = \"${spelling}\" in this target's \\[\\[targets\\]\\] table of the project manifest")
check(none_exported 1 "^error: no entry point: no module exports main/1${hint}${arity_hint}" -o out helper.erl)
check(ambiguous 1 "^error: ambiguous entry point: main/1 is exported by app, other${hint}${arity_hint}" -o out app.erl other.erl)
check(selected_invalid_with_output 1 "${unexported}" -o out --entry helper:hidden helper.erl)

# Usage errors stop before reading sources.
foreach(invalid IN ITEMS "a:b:c" ":main" "app:" "bad\tname" "app:main/2" "app:main/01" "app:/0")
    check(invalid_spelling 2 "--entry expects ${spelling} with 1..255-character names and ARITY 0 or 1"
        --entry "${invalid}" app.erl)
endforeach()
string(REPEAT "x" 256 long)
check(name_too_long 2 "--entry expects" --entry "${long}" app.erl)
check(repeated 2 "entry specified more than once" --entry app --entry app app.erl)
check(check_mode 2 "cannot be used with" --entry app --parse-check app.erl)
check(print_mode 2 "cannot be used with" --entry app --print-ast app.erl)
check(new_project 2 "cannot be combined" --entry app --new-project created)

# Manifest entry keys: valid, custom function, private, unknown module, detection and CLI override.
check(manifest_valid 0 "^$" --project project.toml --target app)
check(manifest_custom 0 "^$" --project project.toml --target custom)
check(manifest_explicit_arity 0 "^$" --project project.toml --target explicit)
check(manifest_private 1 "\\[target private\\]:\nerror: [^\n]*${unexported}" --project project.toml --target private)
check(manifest_absent 1 "project.toml:21:9 \\[target absent\\] \\(entry\\): ${missing}" --project project.toml --target absent)
check(manifest_detect 1 "\\[target detect\\]:\nerror: runtime library not found"
    --project project.toml --target detect -o out --runtime-library absent.lib)
check(manifest_ambiguous 1 "main/1 is exported by app, other${hint}${manifest_hint}${arity_hint}"
    --project project.toml --target ambiguous -o out)
check(manifest_check_ignores_entry 0 "^$" --project project.toml --parse-check)
check(cli_overrides_manifest 0 "^$" --project project.toml --target private --entry helper:start)
check(cli_override_one_target 2 "--entry requires exactly one selected target" --project project.toml --entry app)
check(cli_override_duplicate_selector 0 "^$" --project project.toml --target app --target app --entry app)

# Invalid manifest values fail while decoding, even for unselected targets.
foreach(value IN ITEMS "'a:b:c'" "':main'" "'app:'" "\"mod\\u0001\"" "'app:main/2'")
    file(WRITE "${WORK}/invalid.toml" "schema_version = 1\n[[targets]]\nname = 'x'\nsources = ['app.erl']\n[[targets]]\nname = 'y'\nsources = ['app.erl']\nentry = ${value}\n")
    check(manifest_invalid 1 "invalid.toml:8:9 \\[target y\\] \\(entry\\): invalid entry; expected ${spelling} with ARITY 0 or 1"
        --project invalid.toml --target x)
endforeach()
foreach(value IN ITEMS "''" "3" "['app']")
    file(WRITE "${WORK}/invalid.toml" "schema_version = 1\n[[targets]]\nname = 'x'\nsources = ['app.erl']\nentry = ${value}\n")
    check(manifest_wrong_type 1 "\\(entry\\): expected a nonempty string" --project invalid.toml)
endforeach()
file(WRITE "${WORK}/invalid.toml" "schema_version = 1\n[[targets]]\nname = 'x'\nsources = ['app.erl']\nentry_point = 'app'\n")
check(manifest_unknown_key 1 "\\(entry_point\\): unknown key" --project invalid.toml)
