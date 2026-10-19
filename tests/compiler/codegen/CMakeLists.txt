get_target_property(sdk_dir clause_llvm_sdk CLAUSE_LLVM_CMAKE_DIR)
add_test(NAME codegen_dependency COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/dependency/$<CONFIG>"
    "-DSDK_DIR=${sdk_dir}"
    "-DHOST_SETTINGS=${PROJECT_BINARY_DIR}/TestHost.cmake"
    "-DHOST_CONFIG=$<CONFIG>"
    "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/dependency.cmake")
# A nested CMake configure and build against the LLVM SDK takes minutes when every core is busy with other tests.
set_tests_properties(codegen_dependency PROPERTIES TIMEOUT 300 LABELS full_only)

foreach(suite IN ITEMS results ownership target verification emission term_abi features mangling)
    add_executable(codegen_${suite} ${suite}.cpp)
    clause_project_options(codegen_${suite})
    target_include_directories(codegen_${suite} PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
    target_link_libraries(codegen_${suite} PRIVATE clause_codegen clause_frontend)
    add_test(NAME codegen_${suite} COMMAND codegen_${suite})
endforeach()
# Only backend-internal tests see LLVM headers; results.cpp verifies the opaque consumer boundary.
target_link_libraries(codegen_ownership PRIVATE clause_llvm_sdk)
target_link_libraries(codegen_target PRIVATE clause_llvm_sdk)
target_link_libraries(codegen_verification PRIVATE clause_llvm_sdk)
target_link_libraries(codegen_emission PRIVATE clause_llvm_sdk)
target_link_libraries(codegen_mangling PRIVATE clause_llvm_sdk)
target_link_libraries(codegen_term_abi PRIVATE clause_llvm_sdk clause_abi)
get_target_property(test_backends clause_llvm_sdk CLAUSE_LLVM_BACKENDS)
foreach(backend IN LISTS test_backends)
    target_compile_definitions(codegen_target PRIVATE "CLAUSE_LLVM_${backend}=1")
    target_compile_definitions(codegen_emission PRIVATE "CLAUSE_LLVM_${backend}=1")
    target_compile_definitions(codegen_term_abi PRIVATE "CLAUSE_LLVM_${backend}=1")
endforeach()

target_link_libraries(codegen_features PRIVATE clause_abi)
add_test(NAME codegen_feature_output COMMAND "${CMAKE_COMMAND}"
    "-DPROGRAM=$<TARGET_FILE:codegen_features>" "-DFEATURE_NAME=pattern matching"
    -P "${PROJECT_SOURCE_DIR}/tests/abi/feature_output.cmake")

if(CLAUSE_BUILD_RUNTIME)
    add_executable(codegen_runtime_terms runtime_terms.cpp)
    clause_project_options(codegen_runtime_terms)
    target_include_directories(codegen_runtime_terms PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
    target_link_libraries(codegen_runtime_terms PRIVATE clause_codegen clause_frontend clause_llvm_sdk
        Clause::generated_program)
    add_test(NAME codegen_runtime_terms COMMAND codegen_runtime_terms)
endif()

# Real-source backend adapter remains private until CLI artifact integration (steps 35/36).
add_executable(codegen_specialization specialization.cpp)
clause_project_options(codegen_specialization)
target_include_directories(codegen_specialization PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(codegen_specialization PRIVATE clause_codegen clause_semantic clause_llvm_sdk)
target_compile_definitions(codegen_specialization PRIVATE LOWERING_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/codegen")
add_test(NAME codegen_specialization COMMAND codegen_specialization)

add_executable(codegen_lowering lowering.cpp)
clause_project_options(codegen_lowering)
target_include_directories(codegen_lowering PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(codegen_lowering PRIVATE clause_codegen clause_semantic clause_llvm_sdk)
target_compile_definitions(codegen_lowering PRIVATE LOWERING_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/codegen")
add_test(NAME codegen_lowering COMMAND codegen_lowering)

if("X86" IN_LIST test_backends)
    target_compile_definitions(codegen_lowering PRIVATE CLAUSE_LLVM_X86=1)
endif()

if(CLAUSE_BUILD_RUNTIME)
    add_executable(codegen_registration_emit registration_emit.cpp)
    clause_project_options(codegen_registration_emit)
    target_include_directories(codegen_registration_emit PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
    target_link_libraries(codegen_registration_emit PRIVATE clause_codegen clause_semantic clause_llvm_sdk)
    target_compile_definitions(codegen_registration_emit PRIVATE LOWERING_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/codegen")
    foreach(level IN ITEMS O0 O2)
        add_test(NAME codegen_registration_${level} COMMAND "${CMAKE_COMMAND}"
            "-DEMITTER=$<TARGET_FILE:codegen_registration_emit>" "-DOPTIMIZATION=${level}"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/registration-${level}/$<CONFIG>"
            "-DHOST_SETTINGS=${PROJECT_BINARY_DIR}/TestHost.cmake"
            "-DHOST_CONFIG=$<CONFIG>" "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/registration.cmake")
        set_tests_properties(codegen_registration_${level} PROPERTIES TIMEOUT 120)
    endforeach()

    add_executable(codegen_failure_emit failure_emit.cpp)
    add_executable(codegen_service_emit service_emit.cpp)
    clause_project_options(codegen_service_emit)
    target_include_directories(codegen_service_emit PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
    target_link_libraries(codegen_service_emit PRIVATE clause_codegen clause_semantic clause_llvm_sdk)
    target_compile_definitions(codegen_service_emit PRIVATE LOWERING_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/codegen")
    foreach(mode IN ITEMS O0-on O0-off O2-on O2-off)
        add_test(NAME codegen_service_${mode} COMMAND "${CMAKE_COMMAND}"
            "-DEMITTER=$<TARGET_FILE:codegen_service_emit>" "-DOPTIMIZATION=${mode}"
            "-DCONSUMER=${CMAKE_CURRENT_SOURCE_DIR}/service_consumer.cpp"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/service-${mode}/$<CONFIG>"
            "-DHOST_SETTINGS=${PROJECT_BINARY_DIR}/TestHost.cmake" "-DHOST_CONFIG=$<CONFIG>" "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/registration.cmake")
        set_tests_properties(codegen_service_${mode} PROPERTIES TIMEOUT 180)
    endforeach()
    clause_project_options(codegen_failure_emit)
    target_include_directories(codegen_failure_emit PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
    target_link_libraries(codegen_failure_emit PRIVATE clause_codegen clause_semantic clause_llvm_sdk)
    target_compile_definitions(codegen_failure_emit PRIVATE LOWERING_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/codegen")
    foreach(mode IN ITEMS O0-on O0-off O2-on O2-off)
        add_test(NAME codegen_failure_${mode} COMMAND "${CMAKE_COMMAND}"
            "-DEMITTER=$<TARGET_FILE:codegen_failure_emit>" "-DOPTIMIZATION=${mode}"
            "-DCONSUMER=${CMAKE_CURRENT_SOURCE_DIR}/failure_consumer.cpp"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/failure-${mode}/$<CONFIG>"
            "-DHOST_SETTINGS=${PROJECT_BINARY_DIR}/TestHost.cmake"
            "-DHOST_CONFIG=$<CONFIG>" "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/registration.cmake")
        set_tests_properties(codegen_failure_${mode} PROPERTIES TIMEOUT 180)
    endforeach()

    add_executable(codegen_specialization_emit specialization_emit.cpp)
    clause_project_options(codegen_specialization_emit)
    target_include_directories(codegen_specialization_emit PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
    target_link_libraries(codegen_specialization_emit PRIVATE clause_codegen clause_semantic clause_llvm_sdk)
    target_compile_definitions(codegen_specialization_emit PRIVATE LOWERING_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/codegen")
    if("X86" IN_LIST test_backends)
        target_compile_definitions(codegen_specialization_emit PRIVATE CLAUSE_LLVM_X86=1)
    endif()
    add_test(NAME codegen_specialized_execution COMMAND "${CMAKE_COMMAND}"
        "-DEMITTER=$<TARGET_FILE:codegen_specialization_emit>"
        "-DCONSUMER=${CMAKE_CURRENT_SOURCE_DIR}/specialization_consumer.cpp"
        "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/specialization/$<CONFIG>"
        "-DHOST_SETTINGS=${PROJECT_BINARY_DIR}/TestHost.cmake"
        "-DHOST_CONFIG=$<CONFIG>" "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/registration.cmake")
    set_tests_properties(codegen_specialized_execution PROPERTIES TIMEOUT 120)
endif()

add_executable(codegen_serialization serialization.cpp)
add_executable(codegen_limits limits.cpp)
clause_project_options(codegen_limits)
target_include_directories(codegen_limits PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(codegen_limits PRIVATE clause_codegen clause_semantic clause_llvm_sdk)
target_compile_definitions(codegen_limits PRIVATE LOWERING_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/codegen")
add_test(NAME codegen_limits COMMAND codegen_limits)
add_executable(codegen_write_failure write_failure.cpp)
clause_project_options(codegen_write_failure)
target_include_directories(codegen_write_failure PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(codegen_write_failure PRIVATE clause_artifacts)
add_test(NAME codegen_write_failure COMMAND codegen_write_failure "${CMAKE_CURRENT_BINARY_DIR}/write-failure/$<CONFIG>")
clause_project_options(codegen_serialization)
target_include_directories(codegen_serialization PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(codegen_serialization PRIVATE clause_codegen clause_semantic clause_llvm_sdk)
target_compile_definitions(codegen_serialization PRIVATE LOWERING_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/codegen")
add_test(NAME codegen_serialization COMMAND codegen_serialization)

add_executable(codegen_artifacts artifacts.cpp)
clause_project_options(codegen_artifacts)
target_include_directories(codegen_artifacts PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_link_libraries(codegen_artifacts PRIVATE clause_artifacts clause_project clause_semantic)
add_test(NAME codegen_artifacts COMMAND codegen_artifacts "${CMAKE_CURRENT_BINARY_DIR}/artifact-files/$<CONFIG>")

add_test(NAME codegen_options COMMAND "${CMAKE_COMMAND}" "-DTOOL=$<TARGET_FILE:clau>"
    "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/options/$<CONFIG>" -P "${CMAKE_CURRENT_SOURCE_DIR}/options.cmake")

add_test(NAME codegen_positional COMMAND "${CMAKE_COMMAND}" "-DTOOL=$<TARGET_FILE:clau>"
    "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/positional/$<CONFIG>" "-DFIXTURES=${PROJECT_SOURCE_DIR}/tests/fixtures/codegen"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/positional.cmake")

add_test(NAME codegen_project COMMAND "${CMAKE_COMMAND}" "-DTOOL=$<TARGET_FILE:clau>"
    "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/project/$<CONFIG>" -P "${CMAKE_CURRENT_SOURCE_DIR}/project.cmake")

add_test(NAME codegen_progress COMMAND "${CMAKE_COMMAND}" "-DTOOL=$<TARGET_FILE:clau>"
    "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/progress/$<CONFIG>" -P "${CMAKE_CURRENT_SOURCE_DIR}/progress.cmake")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME fixture_sources COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/fixture_sources.py"
    "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/fixture-sources")
set_tests_properties(fixture_sources PROPERTIES TIMEOUT 60)
add_test(NAME codegen_resource_cli COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/resource_cli.py"
    "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/resource-cli/$<CONFIG>")
# Disabled 2026-10-12 (user): it expects 1025 inputs to exceed the module limit, which commit 33481a7 raised to
# 16384; exceeding that takes 16385 files and does not fit a Windows command line positionally.
set_tests_properties(codegen_resource_cli PROPERTIES DISABLED TRUE)
get_filename_component(llvm_tools "${sdk_dir}/../../../bin" ABSOLUTE)
find_program(CLAUSE_LLVM_AS NAMES llvm-as HINTS "${llvm_tools}" NO_DEFAULT_PATH REQUIRED)
find_program(CLAUSE_LLVM_DIS NAMES llvm-dis HINTS "${llvm_tools}" NO_DEFAULT_PATH REQUIRED)
find_program(CLAUSE_LLVM_READOBJ NAMES llvm-readobj HINTS "${llvm_tools}" NO_DEFAULT_PATH REQUIRED)
find_program(CLAUSE_LLVM_NM NAMES llvm-nm HINTS "${llvm_tools}" NO_DEFAULT_PATH REQUIRED)
string(REPLACE ";" "," backend_list "${test_backends}")
add_test(NAME codegen_cross_targets COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cross_targets.py"
    "$<TARGET_FILE:clau>" "${CLAUSE_LLVM_READOBJ}" "${CLAUSE_LLVM_NM}"
    "${CMAKE_CURRENT_BINARY_DIR}/cross-targets/$<CONFIG>" "${backend_list}")
set_tests_properties(codegen_cross_targets PROPERTIES TIMEOUT 120)
add_test(NAME codegen_placeholders COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/placeholders.py"
    "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/placeholders/$<CONFIG>"
    "${PROJECT_SOURCE_DIR}/abi/include/clause/abi/features.hpp")
set_tests_properties(codegen_placeholders PROPERTIES TIMEOUT 120)
add_test(NAME codegen_inspection COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/inspection.py"
    "$<TARGET_FILE:clau>" "${CLAUSE_LLVM_AS}" "${CLAUSE_LLVM_DIS}"
    "${CMAKE_CURRENT_BINARY_DIR}/inspection/$<CONFIG>" "${PROJECT_SOURCE_DIR}/tests/fixtures/codegen")

# Added for --print-asm: reassembles listings and compares them with emitted objects.
find_program(CLAUSE_LLVM_MC NAMES llvm-mc HINTS "${llvm_tools}" NO_DEFAULT_PATH REQUIRED)
find_program(CLAUSE_LLVM_OBJDUMP NAMES llvm-objdump HINTS "${llvm_tools}" NO_DEFAULT_PATH REQUIRED)
add_test(NAME codegen_assembly_inspection COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/assembly_inspection.py" "$<TARGET_FILE:clau>" "${CLAUSE_LLVM_MC}"
    "${CLAUSE_LLVM_OBJDUMP}" "${CMAKE_CURRENT_BINARY_DIR}/assembly-inspection/$<CONFIG>"
    "${PROJECT_SOURCE_DIR}/tests/fixtures/codegen" "${backend_list}")
set_tests_properties(codegen_assembly_inspection PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "assembly_inspection: ok")

add_test(NAME codegen_types COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/type_inspection.py"
    "$<TARGET_FILE:clau>" "${CMAKE_CURRENT_BINARY_DIR}/types/$<CONFIG>")

add_test(NAME codegen_proofs COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/proofs.py"
    "$<TARGET_FILE:clau>" "${PROJECT_SOURCE_DIR}/tests/fixtures/executables/proofs")
# Added for float loops (plan 20 OPT1.2).
add_test(NAME codegen_float_loops COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/float_loops.py"
    "$<TARGET_FILE:clau>" "${PROJECT_SOURCE_DIR}/tests/fixtures/executables/float_loops")
set_tests_properties(codegen_float_loops PROPERTIES PASS_REGULAR_EXPRESSION "float_loops: ok")

if(CLAUSE_BUILD_RUNTIME)
    foreach(level IN ITEMS O0 O2)
        add_test(NAME codegen_native_${level} COMMAND "${CMAKE_COMMAND}"
            "-DTOOL=$<TARGET_FILE:clau>" "-DOPTIMIZATION=${level}"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-${level}/$<CONFIG>"
            "-DHOST_SETTINGS=${PROJECT_BINARY_DIR}/TestHost.cmake"
            "-DHOST_CONFIG=$<CONFIG>" "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/native.cmake")
        set_tests_properties(codegen_native_${level} PROPERTIES TIMEOUT 180)
        add_test(NAME codegen_after_fault_${level} COMMAND "${CMAKE_COMMAND}"
            "-DTOOL=$<TARGET_FILE:clau>" "-DOPTIMIZATION=${level}"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/after-${level}/$<CONFIG>"
            "-DHOST_SETTINGS=${PROJECT_BINARY_DIR}/TestHost.cmake"
            "-DHOST_CONFIG=$<CONFIG>" "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/after.cmake")
        set_tests_properties(codegen_after_fault_${level} PROPERTIES TIMEOUT 180)
    endforeach()
    add_test(NAME codegen_differential COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/differential.py" "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}"
        "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/differential/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(codegen_differential PROPERTIES TIMEOUT 240)
    add_test(NAME codegen_atoms COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/atoms.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/atoms/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(codegen_atoms PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_evidence COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/evidence.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/patternmatch/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_evidence PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_bindings COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/bindings.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/bindings/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_bindings PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_patterns COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/patterns.py"
        "$<TARGET_FILE:clau>" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/patterns/$<CONFIG>")
    set_tests_properties(patternmatch_patterns PROPERTIES TIMEOUT 300 COST 11)
    add_test(NAME patternmatch_immediate COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/immediate.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/immediate/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_immediate PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_services COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/services.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/services/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_services PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_booleans COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/booleans.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/booleans/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_booleans PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_clauses COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/clauses.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/clauses/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_clauses PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_sequences COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/sequences.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/sequences/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_sequences PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_containers COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/containers.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/containers/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_containers PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_integers COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/integers.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/integers/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_integers PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_floats COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/floats.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/floats/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_floats PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_maps COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/maps.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/maps/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_maps PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_bits COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/bits.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/bits/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_bits PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_records COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/records.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/records/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_records PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_guard_catalog COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/guard_catalog.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/guard_catalog/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_guard_catalog PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_facts COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/facts.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/facts/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_facts PROPERTIES TIMEOUT 300)
    add_test(NAME patternmatch_closure COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/closure.py"
        "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/closure/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(patternmatch_closure PROPERTIES TIMEOUT 300)
    add_test(NAME codegen_measurements COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/measurements.py" "$<TARGET_FILE:clau>" "${CMAKE_COMMAND}"
        "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/measurements/$<CONFIG>"
        "${PROJECT_BINARY_DIR}/TestHost.cmake" "$<CONFIG>" "${CMAKE_EXECUTABLE_SUFFIX}"
        "$<TARGET_FILE:codegen_specialization_emit>")
    set_tests_properties(codegen_measurements PROPERTIES TIMEOUT 240 LABELS full_only)
    add_test(NAME codegen_examples COMMAND "${CMAKE_COMMAND}" "-DTOOL=$<TARGET_FILE:clau>"
        "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/examples/$<CONFIG>"
        "-DHOST_SETTINGS=${PROJECT_BINARY_DIR}/TestHost.cmake"
        "-DHOST_CONFIG=$<CONFIG>" "-DHOST_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/examples.cmake")
    set_tests_properties(codegen_examples PROPERTIES TIMEOUT 180)
endif()

if(CLAUSE_BUILD_RUNTIME AND CLAUSE_OTP_AUDITS)
    add_test(NAME patternmatch_upstream COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/compiler/patternmatch/upstream.py"
        "$<TARGET_FILE:clau>" "${PROJECT_SOURCE_DIR}" "${CLAUSE_OTP_SOURCE_ROOT}"
        "${CMAKE_CURRENT_BINARY_DIR}/upstream/$<CONFIG>" "${CLAUSE_ESCRIPT}")
    set_tests_properties(patternmatch_upstream PROPERTIES TIMEOUT 300 LABELS "otp-audit")
endif()
