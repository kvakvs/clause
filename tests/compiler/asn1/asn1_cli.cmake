# ASN.1 sources through clau (plan 21 step LIB84, docs/asn1.md): diagnostics of bad specifications and options, and
# the generated header of the Kitchen fixture against asn1ct's (tests/fixtures/asn1/Kitchen.hrl).
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(check_asn1 name expected_exit expected_stderr)
    execute_process(COMMAND "${TOOL}" ${ARGN}
        WORKING_DIRECTORY "${WORK}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err
    )
    if(NOT "${result}" STREQUAL "${expected_exit}")
        message(FATAL_ERROR "${name}: expected exit ${expected_exit}, got ${result}: ${err}")
    endif()
    if(NOT err MATCHES "${expected_stderr}")
        message(FATAL_ERROR "${name}: unexpected stderr [${err}]")
    endif()
endfunction()

set(specs "${FIXTURES}/asn1")
check_asn1(syntax 1 "syntax\\.asn:4:1: error: expected '::=' near 'END'" --generate-asn1 "${specs}/syntax.asn")
check_asn1(undefined 1 "undefined\\.asn:3:22: error: undefined reference Missing"
    --generate-asn1 "${specs}/undefined.asn")
check_asn1(unsupported 1 "unsupported\\.asn:3:9: error: unsupported ASN\\.1 type"
    --generate-asn1 "${specs}/unsupported.asn")
check_asn1(import 1 "import\\.asn:4:9: error: cannot find ASN\\.1 module Absent" --generate-asn1 "${specs}/import.asn")
check_asn1(empty 1 "empty\\.asn:1:1: error: no ASN\\.1 module found" --generate-asn1 "${specs}/empty.asn")
check_asn1(unknown_option 2 "unknown ASN\\.1 option 'bogus'" --asn1-option bogus "${specs}/empty.asn")
check_asn1(jer 2 "jer \\(JSON encoding rules\\) is not supported" --asn1-option jer "${specs}/empty.asn")

# The header asn1ct writes for the Kitchen fixture, compared without the lines starting with a comment.
check_asn1(header 0 "^$" --generate-asn1 --asn1-outdir "${WORK}/kitchen"
    "${FIXTURES}/executables/asn1_ber/Kitchen.asn")
file(STRINGS "${WORK}/kitchen/Kitchen.hrl" generated REGEX "^([^%]|$)")
file(STRINGS "${specs}/Kitchen.hrl" expected REGEX "^([^%]|$)")
if(NOT generated STREQUAL expected)
    message(FATAL_ERROR "header: Kitchen.hrl differs from asn1ct's\ngenerated: ${generated}\nexpected: ${expected}")
endif()
if(NOT EXISTS "${WORK}/kitchen/Kitchen.erl")
    message(FATAL_ERROR "header: no Kitchen.erl was generated")
endif()
message(STATUS "asn1_cli: ok")
