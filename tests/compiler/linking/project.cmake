# Project executables (docs/projects.md#executables): each selected target with an output or entry links to its
# manifest output; every target compiles and links before any output is replaced.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/proj")
file(COPY "${SOURCE_ROOT}/tests/fixtures/linking/project/" DESTINATION "${WORK}/proj")
set(proj "${WORK}/proj")

# Run the compiler from outside the project directory; require exit status, empty stdout and a stderr pattern.
function(compile name code stderr)
    execute_process(COMMAND "${TOOL}" ${ARGN} WORKING_DIRECTORY "${WORK}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120 ENCODING UTF-8)
    if(NOT result STREQUAL "${code}" OR NOT out STREQUAL "" OR NOT err MATCHES "${stderr}")
        message(FATAL_ERROR "${name}: exit=${result} stdout=[${out}] stderr=[${err}]")
    endif()
    file(GLOB_RECURSE entries LIST_DIRECTORIES true "${WORK}/*")
    list(FILTER entries INCLUDE REGEX "/\\.clause-link[^/]*$")
    if(entries)
        message(FATAL_ERROR "${name}: link staging left behind: ${entries}")
    endif()
endfunction()

# Run a linked program and require its exit status and exact stdout.
function(run program code stdout)
    execute_process(COMMAND "${WORK}/${program}${HOST_SUFFIX}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
    string(REPLACE "\r\n" "\n" out "${out}")
    if(NOT result STREQUAL "${code}" OR NOT out STREQUAL "${stdout}")
        message(FATAL_ERROR "${program} ${ARGN}: exit=${result} stdout=[${out}] stderr=[${err}]")
    endif()
endfunction()

# Require exactly the listed executables (work-relative, without the host suffix) among the possible outputs.
function(expect_outputs name)
    foreach(program IN ITEMS proj/build/hello proj/bin/tool proj/build/library custom)
        set(present FALSE)
        if(EXISTS "${WORK}/${program}${HOST_SUFFIX}")
            set(present TRUE)
        endif()
        list(FIND ARGN "${program}" index)
        if(present AND index EQUAL -1 OR NOT present AND NOT index EQUAL -1)
            message(FATAL_ERROR "${name}: ${program} present=${present}, expected [${ARGN}]")
        endif()
    endforeach()
endfunction()

# Remove every published output so each case observes only its own results.
function(clean)
    file(REMOVE_RECURSE "${WORK}/proj/build" "${WORK}/proj/bin" "${WORK}/custom${HOST_SUFFIX}")
endfunction()

# All targets at both levels: manifest-relative outputs (default and explicit, directories created); the
# library target only compiles, although one of its modules exports main/1.
foreach(level IN ITEMS O0 O2)
    clean()
    compile(all-${level} 0 "^$" -${level} --project proj/project.toml)
    expect_outputs(all-${level} proj/build/hello proj/bin/tool)
    run(proj/build/hello 0 "{hello,[\"a\",\"b c\"]}\n" a "b c")
    run(proj/bin/tool 4 "{tool,[]}\n")
endforeach()

# Selection links only the selected targets; -o replaces the manifest output of a single selected target.
clean()
compile(selected 0 "^$" --project proj/project.toml --target tool --target library)
expect_outputs(selected proj/bin/tool)
clean()
compile(cli_output 0 "^$" --project proj/project.toml --target hello -o custom)
expect_outputs(cli_output custom)
run(custom 0 "{hello,[]}\n")
compile(cli_output_many 2 "--output requires exactly one compilation target" --project proj/project.toml -o custom)
# --entry turns a target without output or entry into an executable at its default output.
clean()
compile(cli_entry 0 "^$" --project proj/project.toml --target library --entry hello)
expect_outputs(cli_entry proj/build/library)
run(proj/build/library 0 "{library,[]}\n")

# Linker options apply to every linked target without -o; failures publish nothing.
clean()
compile(runtime_option 1 "\\[target hello\\]:\nerror: runtime library not found.*\\[target tool\\]:\nerror: runtime library not found"
    --project proj/project.toml --runtime-library absent.lib)
expect_outputs(runtime_option)
compile(linker_usage 2 "--linker and --runtime-library require --output or a linking project build"
    --project proj/project.toml --emit obj --linker clang)
compile(new_project_linker 2 "--linker and --runtime-library require" --new-project created --linker clang)

# A later target failing at link time keeps the earlier target's existing output byte-for-byte.
file(MAKE_DIRECTORY "${proj}/build")
file(WRITE "${proj}/build/hello${HOST_SUFFIX}" "old\n")
file(READ "${proj}/project.toml" manifest)
file(WRITE "${proj}/late.toml" "${manifest}" [=[
[[targets]]
name = "alias"
sources = ["tool.erl"]
entry = "tool:run"
output = "tool.erl"
]=])
compile(late_failure 1 "\\[target alias\\]:\nerror: artifact destination aliases an input: [^\n]*tool\\.erl"
    --project proj/late.toml)
file(READ "${proj}/build/hello${HOST_SUFFIX}" contents)
if(NOT contents STREQUAL "old\n")
    message(FATAL_ERROR "late_failure: earlier target output was replaced")
endif()
expect_outputs(late_failure proj/build/hello)

# Output aliasing: equal manifest outputs fail during planning; Windows names equal after ".exe" before publication.
clean()
file(WRITE "${proj}/same.toml" [=[schema_version = 1
[[targets]]
name = "first"
sources = ["tool.erl"]
entry = "tool:run"
output = "build/same"
[[targets]]
name = "second"
sources = ["tool.erl"]
entry = "tool:run"
output = "build/../build/same"
]=])
compile(same_output 1 "selected targets have colliding output destinations" --project proj/same.toml)
if(HOST_SUFFIX STREQUAL ".exe")
    file(READ "${proj}/same.toml" same)
    string(REPLACE "build/../build/same\"" "build/same.exe\"" suffixed "${same}")
    file(WRITE "${proj}/suffix.toml" "${suffixed}")
    compile(suffix_output 1 "target second: executable output [^\n]*same\\.exe is also the output of target first"
        --project proj/suffix.toml)
    if(EXISTS "${proj}/build/same.exe")
        message(FATAL_ERROR "suffix_output: a colliding executable was published")
    endif()
endif()

# ignore_dirs and ignore_modules (docs/projects.md#ignored-sources): ignored sources hold syntax errors and are never
# parsed; calls into their modules reach placeholders returning {module_ignored, Module}.
file(COPY "${SOURCE_ROOT}/tests/fixtures/linking/ignore/" DESTINATION "${WORK}/ignore")
execute_process(COMMAND "${TOOL}" --project ignore/project.toml --print-inputs WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
string(REPLACE "\r\n" "\n" out "${out}")
set(ignored "\nignored module dep_tests \\(referenced at [^\n]*/app\\.erl:13\\)\nignored module legacy \\(referenced at "
    "[^\n]*/app\\.erl:8\\)\nignored module telemetry \\(referenced at [^\n]*/app\\.erl:3\\)\n$")
string(CONCAT ignored ${ignored})
if(NOT result STREQUAL "0" OR NOT err STREQUAL "" OR out MATCHES "legacy\\.erl|telemetry\\.erl|dep_tests\\.erl" OR
   NOT out MATCHES "${ignored}")
    message(FATAL_ERROR "ignore_inputs: exit=${result} stdout=[${out}] stderr=[${err}]")
endif()
compile(ignore 0 "^$" --project ignore/project.toml)
run(ignore/build/ignore 0 "{module_ignored,legacy}\nignore: ok\n")
