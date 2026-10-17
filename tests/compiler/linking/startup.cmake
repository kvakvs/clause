# Startup objects (docs/executables.md): emit modules plus clausev1_start, link them with the runtime through the
# native harness recipe, then check argv, return, halt and uncaught-error exit paths, and inspect startup IR.
include("${HOST_SETTINGS}")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/consumer")
set(fixtures "${SOURCE_ROOT}/tests/fixtures/linking/startup")

# Run the compiler in the fixture directory and require silent success.
function(compile name)
    execute_process(COMMAND "${TOOL}" ${ARGN} WORKING_DIRECTORY "${fixtures}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
    if(NOT result STREQUAL "0" OR NOT "${out}${err}" STREQUAL "")
        message(FATAL_ERROR "${name}: compilation failed: ${result}: ${out}${err}")
    endif()
endfunction()

# Emit one batch's objects; the startup object must be published next to the module objects.
function(emit name)
    compile(${name} ${ARGN} --emit obj --artifact-dir "${WORK}/${name}")
    file(GLOB_RECURSE objects "${WORK}/${name}/*.o" "${WORK}/${name}/*.obj")
    if(NOT objects MATCHES "clausev1_start\\.(o|obj)")
        message(FATAL_ERROR "${name}: no startup object among ${objects}")
    endif()
    set(objects_${name} "${objects}" PARENT_SCOPE)
endfunction()

emit(positional -O0 --entry app app.erl helper.erl)
emit(project -O2 --build-dir "${WORK}/project-build" --project project.toml)
emit(escript -O0 --entry boom boom.escript)

# One consumer build links each object set with the runtime; no harness source is involved.
file(WRITE "${WORK}/consumer/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.28)
project(StartupConsumer LANGUAGES CXX)
# Link the runtime the parent build already compiled (Clause::generated_program).
include("${RUNTIME_TARGETS}")
foreach(name IN ITEMS positional project escript)
    add_executable(${name} ${OBJECTS_${name}})
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
    target_link_libraries(${name} PRIVATE Clause::generated_program)
endforeach()
]=])
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${WORK}/consumer" -B "${WORK}/build" "-DSOURCE_ROOT=${SOURCE_ROOT}"
    "-DOBJECTS_positional=${objects_positional}" "-DOBJECTS_project=${objects_project}"
    "-DOBJECTS_escript=${objects_escript}" ${host_configure_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Consumer configure: ${output}${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK}/build" --config "${HOST_CONFIG}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Consumer link: ${output}${errors}")
endif()

# Run a linked program and require its exit status, exact stdout and an stderr pattern.
function(run program code stdout stderr)
    execute_process(COMMAND "${WORK}/build/bin/${HOST_CONFIG}/${program}${HOST_SUFFIX}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30 ENCODING UTF-8)
    string(REPLACE "\r\n" "\n" out "${out}")
    string(REPLACE "\r\n" "\n" err "${err}")
    if(NOT result STREQUAL "${code}" OR NOT out STREQUAL "${stdout}" OR NOT err MATCHES "${stderr}")
        message(FATAL_ERROR "${program} ${ARGN}: exit=${result} stdout=[${out}] stderr=[${err}]")
    endif()
endfunction()

foreach(program IN ITEMS positional project)
    run(${program} 0 "[]\n" "^$")
    run(${program} 0 "[\"a b\",[1078,1103]]\n" "^$" "a b" "жя")
    # ARGN drops empty elements, so the empty argument is passed directly.
    execute_process(COMMAND "${WORK}/build/bin/${HOST_CONFIG}/${program}${HOST_SUFFIX}" "" x
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
    if(NOT result STREQUAL "0" OR NOT out MATCHES "^\\[\\[\\],\"x\"\\]\r?\n$")
        message(FATAL_ERROR "${program}: empty argument: exit=${result} stdout=[${out}] stderr=[${err}]")
    endif()
    run(${program} 3 "" "^$" halt)
    run(${program} 0 "" "^$" halt0)
    run(${program} 7 "" "^$" big)
    run(${program} 1 "before\n" "^bye now\n$" slogan)
    run(${program} 1 "" "^uncaught exception error: badarg\n$" badhalt)
    run(${program} 1 "" "^uncaught exception error: {badmatch,{error,42}}\n$" crash)
    run(${program} 1 "" "^uncaught exception error: function_clause\n$" clause)
endforeach()
run(escript 127 "" "^escript: exception error: {badmatch,2}\n$")
run(escript 127 "" "^escript: exception throw: oops\n$" throw)

# Startup IR keeps the descriptor layout and service spelling of each target width and ABI.
set(word_64 i64)
set(word_32 i32)
foreach(target IN ITEMS "x86_64-pc-windows-msvc|64|?CLAUSE_main_v1@@YAHHPEAPEADPEBX@Z"
        "i686-pc-windows-msvc|32|?CLAUSE_main_v1@@YAHHPAPADPBX@Z"
        "x86_64-unknown-linux-gnu|64|_Z14CLAUSE_main_v1iPPcPKv"
        "armv7-unknown-linux-gnueabihf|32|_Z14CLAUSE_main_v1iPPcPKv")
    string(REPLACE "|" ";" target "${target}")
    list(GET target 0 triple)
    list(GET target 1 bits)
    list(GET target 2 symbol)
    set(word ${word_${bits}})
    foreach(level IN ITEMS O0 O2)
        set(directory "${WORK}/ir/${triple}-${level}")
        compile(ir -${level} --target-triple ${triple} --entry app --emit llvm-ir --artifact-dir "${directory}"
            app.erl helper.erl)
        file(READ "${directory}/clausev1_start.ll" ir)
        string(REPLACE "?" "\\?" pattern "${symbol}")
        set(descriptor "{ i32 23, i32 ${bits}, ptr @startup.modules, ${word} 2, ptr @startup.module, ${word} 3, ptr @startup.function, ${word} 4, i32 0 }")
        string(FIND "${ir}" "${descriptor}" found)
        if(found EQUAL -1 OR NOT ir MATCHES "define [a-z_ ]*i32 @main\\(i32 %0, ptr %1\\)"
                OR NOT ir MATCHES "call i32 @\"?${pattern}\"?\\(i32 %0, ptr %1, ptr [a-z ]*@startup.descriptor\\)"
                OR NOT ir MATCHES "\\[ptr @clausev1_617070__0.descriptor, ptr @clausev1_68656c706572__0.descriptor\\]")
            message(FATAL_ERROR "${triple} ${level}: unexpected startup IR:\n${ir}")
        endif()
    endforeach()
endforeach()
compile(ir --entry boom --emit llvm-ir --artifact-dir "${WORK}/ir/escript" boom.escript)
file(READ "${WORK}/ir/escript/clausev1_start.ll" ir)
if(NOT ir MATCHES "ptr @startup.function, i(32|64) 4, i32 1 }")
    message(FATAL_ERROR "Escript startup flag missing:\n${ir}")
endif()
