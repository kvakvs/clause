# Executable linking for positional inputs and a single project target (docs/executables.md#linking): -o drives Clang with the
# module and startup objects plus the runtime archive; failures publish nothing and keep existing outputs.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${SOURCE_ROOT}/tests/fixtures/linking/startup/" DESTINATION "${WORK}")
set(example "${SOURCE_ROOT}/examples/compile")

# Run the compiler in the work directory and require an exit status plus stderr pattern.
function(compile name code stderr)
    execute_process(COMMAND "${TOOL}" ${ARGN} WORKING_DIRECTORY "${WORK}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120 ENCODING UTF-8)
    if(NOT result STREQUAL "${code}" OR NOT out STREQUAL "" OR NOT err MATCHES "${stderr}")
        message(FATAL_ERROR "${name}: exit=${result} stdout=[${out}] stderr=[${err}]")
    endif()
    file(GLOB staging "${WORK}/.clause-link*")
    if(staging)
        message(FATAL_ERROR "${name}: link staging left behind: ${staging}")
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

# The documented two-module example at every level; an existing output is replaced by the new program.
file(WRITE "${WORK}/demo-O0${HOST_SUFFIX}" "old\n")
foreach(level IN ITEMS O0 O2 Os)
    compile(demo-${level} 0 "^$" -${level} -o demo-${level} "${example}/answer.erl" "${example}/client.erl")
    run(demo-${level} 0 "42\n-7\n{record,map,binary,list,integer,other}\n")
    file(SIZE "${WORK}/demo-${level}${HOST_SUFFIX}" size_${level})
endforeach()
# Size mode strips unreferenced generated and runtime sections at link time. Sections are rounded up to the file
# alignment (512 bytes on PE, up to a page elsewhere), so a smaller -Os image may still round one unit above -O2.
math(EXPR size_O2_aligned "${size_O2} + 4096")
if(NOT size_Os LESS size_O0 OR size_Os GREATER size_O2_aligned)
    message(FATAL_ERROR "-Os executable is not smaller: O0=${size_O0} O2=${size_O2} Os=${size_Os}")
endif()
# Windows programs embed a manifest that runs them as the invoking user (32-bit installer detection otherwise asks
# to elevate names like record_update.exe).
if(HOST_SUFFIX STREQUAL ".exe")
    file(STRINGS "${WORK}/demo-O2.exe" invoker REGEX "asInvoker")
    if(NOT invoker)
        message(FATAL_ERROR "demo-O2.exe has no embedded asInvoker manifest")
    endif()
endif()
# Arguments and exit statuses reach the linked startup; escripts keep exit 127.
file(MAKE_DIRECTORY "${WORK}/sub dir")
compile(app 0 "^$" -O2 -o "sub dir/app" app.erl helper.erl)
run("sub dir/app" 0 "[\"a b\"]\n" "a b")
run("sub dir/app" 3 "" halt)
compile(escript 0 "^$" -o boom boom.escript)
run(boom 127 "")
# A FUNCTION/0 entry ignores the program arguments; FUNCTION/1 wins when both are exported.
compile(no_arguments 0 "^$" --entry noargs:start -o noargs noargs.erl)
run(noargs 0 "started\n" ignored)
compile(both_arities 0 "^$" --entry noargs:both -o both noargs.erl)
run(both 0 "{one,[\"x\"]}\n" x)
# Stage times end with the link of the executable, named by its final path.
compile(stage_times 0 "\\[time\\] compile 1 module: [0-9.]+ ms\r?\n\\[time\\] link [^\n]*/timed(\\.exe)?: [0-9.]+ ms"
    --print-stage-times --entry noargs:start -o timed noargs.erl)
run(timed 0 "started\n")
# A project target links with an explicit -o (exactly one selected target); manifest and CLI entries apply.
compile(project 0 "^$" --project project.toml -o project)
run(project 0 "[\"x\"]\n" x)
compile(project_entry 0 "^$" --project project.toml --target app --entry app -O2 -o "sub dir/project")
run("sub dir/project" 3 "" halt)
compile(project_failure 1 "\\[target app\\]:\nerror: runtime library not found"
    --project project.toml -o kept.bin --runtime-library absent.lib)

# Every failure keeps an existing output byte-for-byte and leaves no staging directory.
set(kept "${WORK}/kept.bin")
file(WRITE "${kept}" "old\n")
function(failure name stderr)
    compile(${name} 1 "${stderr}" ${ARGN} app.erl helper.erl)
    file(READ "${WORK}/kept.bin" contents)
    if(NOT contents STREQUAL "old\n")
        message(FATAL_ERROR "${name}: existing output was modified")
    endif()
endfunction()
failure(absent_runtime "^error: runtime library not found: [^\n]*absent\\.lib; build the clause_runtime target"
    --runtime-library absent.lib -o kept.bin)
failure(not_archive "^error: runtime library is not a static library: [^\n]*app\\.erl"
    --runtime-library app.erl -o kept.bin)
failure(wrong_target "^error: runtime library [^\n]* contains [^\n]* objects, but the executable targets armv7-unknown-linux-gnueabihf"
    --target-triple armv7-unknown-linux-gnueabihf -o kept.bin)
failure(absent_linker "^error: linker not found: [^\n]*no-such-clang" --linker no-such-clang -o kept.bin)
failure(link_error "^error: linking [^\n]*kept\\.bin failed: [^\n]* exited with status [1-9][0-9]*:\n.*[Uu]ndefined"
    --runtime-library "${OTHER_LIBRARY}" -o kept.bin)
failure(parent_is_file "^error: output directory does not exist: [^\n]*kept\\.bin" -o kept.bin/app)
file(MAKE_DIRECTORY "${WORK}/folder.d")
failure(output_is_directory "^error: artifact destination is not a regular file: [^\n]*folder\\.d" -o folder.d)
failure(output_is_input "^error: artifact destination aliases an input: [^\n]*app\\.erl" -o app.erl)
file(READ "${WORK}/app.erl" source)
file(READ "${SOURCE_ROOT}/tests/fixtures/linking/startup/app.erl" original)
if(NOT source STREQUAL original)
    message(FATAL_ERROR "output_is_input: input was modified")
endif()
compile(usage 2 "--linker and --runtime-library require --output" --linker clang app.erl)
