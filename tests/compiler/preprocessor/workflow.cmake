file(MAKE_DIRECTORY "${WORK}/first" "${WORK}/second" "${WORK}/environment" "${WORK}/app/include")

# Real files exercise per-invocation options, include policy, diagnostics and recovery.
function(check name source code expected diagnostic)
    file(WRITE "${WORK}/${name}.erl" "${source}")
    execute_process(COMMAND "${TOOL}" --print-pp ${ARGN} "${name}.erl" WORKING_DIRECTORY "${WORK}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10 ENCODING UTF-8)
    if(NOT status STREQUAL "${code}" OR NOT output MATCHES "${expected}" OR NOT error MATCHES "${diagnostic}")
        message(FATAL_ERROR "${name}: ${status}: ${output}${error}")
    endif()
endfunction()
check(predefined "{?A,?B,?N,?S}." 0 "true.*123.*atom.*-123.*ab" "^$"
    -DA "-DB={123, atom}" -DN=-123 "-DS=[97,98]")
foreach(expression IN ITEMS "(1 bsl 100) + 1 > (1 bsl 100)" "defined(MODULE)"
    "not defined(MISSING)" "1 == 1.0 andalso 1 =/= 1.0" "length([1,2|[]]) =:= 2"
    "map_get(key, #{key => 3}) =:= 3" "element(2,{a,b}) =:= b" "is_pid(self())"
    "bit_size(<<1:3>>) =:= 3" "is_integer(5,1,9)" "true orelse (1 div 0 =:= 0)"
    "trunc(0.75) =:= 0" "trunc(-1.75) =:= -1" "round(1.5) =:= 2"
    "trunc(9007199254740992.0) =:= 9007199254740992"
    "trunc(1.2676506002282294e30) =:= (1 bsl 100)"
    "trunc(-1.2676506002282294e30) =:= -(1 bsl 100)"
    "trunc(1.7976931348623157e308) =:= ((1 bsl 1024) - (1 bsl 971))"
    "false =:= (false andalso 1 div 0)" "node() =:= nonode@nohost")
    check(condition "-if(${expression}). yes. -else. no. -endif." 0 "yes \\." "^$")
endforeach()
file(WRITE "${WORK}/a.hrl" "-ifndef(GUARD). -define(GUARD,true). -define(X,7). -include(\"a.hrl\"). -endif.")
file(WRITE "${WORK}/app/include/x.hrl" "-define(APP,9).")
check(guarded_include "-include(\"a.hrl\"). -include_lib(\"test/include/x.hrl\"). {?X,?APP}."
    0 "7 , 9" "a.hrl.*a.hrl.*x.hrl" --app-dir test=app --verbose)
# Release layouts name applications `<name>-<version>`: the highest numeric version wins, a non-numeric suffix is not
# a version, and duplicates warn once per run however many files include from them.
file(WRITE "${WORK}/libs/vapp-1.9/include/v.hrl" "-define(V,1).")
file(WRITE "${WORK}/libs/vapp-1.10/include/v.hrl" "-define(V,2).")
file(WRITE "${WORK}/libs/vapp-2.x/include/v.hrl" "-define(V,9).")
file(WRITE "${WORK}/libs/solo-3/include/s.hrl" "-define(S,3).")
check(versioned_include "-include_lib(\"vapp/include/v.hrl\"). -include_lib(\"solo/include/s.hrl\"). {?V,?S}."
    0 "2 , 3" "duplicate versioned application directories in [^\n]*/libs: vapp-1.9, vapp-1.10; using vapp-1.10" -I libs)
file(WRITE "${WORK}/versioned_again.erl" "-include_lib(\"vapp/include/v.hrl\"). ?V.")
execute_process(COMMAND "${TOOL}" --print-pp -I libs versioned_include.erl versioned_again.erl
    WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10
    ENCODING UTF-8)
string(REGEX MATCHALL "duplicate versioned" reports "${error}")
list(LENGTH reports count)
if(NOT status EQUAL 0 OR NOT count EQUAL 1)
    message(FATAL_ERROR "versioned_once: ${status}: ${count} reports: ${output}${error}")
endif()

file(WRITE "${WORK}/first/x.hrl" "-define(X,1).")
file(WRITE "${WORK}/second/x.hrl" "-define(X,2).")
file(WRITE "${WORK}/environment/x.hrl" "-define(E,3).")
set(ENV{HDR} "${WORK}/environment")
check(include_order "-include(\"x.hrl\"). -include(\"$HDR/x.hrl\"). {?X,?E}."
    0 "1 , 3" "^$" -I second -I first)
file(WRITE "${WORK}/bad.hrl" "-error(included).")
check(include_error "-include(\"bad.hrl\")." 1 "file" "bad.hrl.*included.*include_error.erl")
string(ASCII 233 latin)
file(WRITE "${WORK}/latin.hrl" "% coding: latin-1\n-define(LATIN,'caf${latin}').")
check(latin_include "-include(\"latin.hrl\"). ?LATIN." 0 "café" "^$")
string(ASCII 255 invalid)
file(WRITE "${WORK}/invalid.hrl" "${invalid}")
check(invalid_include "-include(\"invalid.hrl\")." 1 "file" "UTF-8")
file(WRITE "${WORK}/feature.hrl" "-feature(maybe_expr,disable).")
check(included_feature "-include(\"feature.hrl\"). {maybe,else}." 0 "'maybe' , 'else'" "^$")
check(inactive_include "-if(false). -include(\"invalid.hrl\"). -endif." 0 "file" "^$")

set(chain "-define(M0,0).\n")
foreach(index RANGE 1 79)
    math(EXPR previous "${index} - 1")
    string(APPEND chain "-define(M${index},?M${previous}).\n")
endforeach()
check(macro_chain "${chain}?M79." 0 "0 \\." "^$")
string(REPEAT "-ifdef(ON).\n" 64 opens)
string(REPEAT "-else. -error(wrong_branch). -endif.\n" 64 closes)
check(condition_depth "-define(ON,true).\n${opens}selected.\n${closes}" 0 "selected \\." "^$")

# Truncation seeds preserve the old bounded-recovery corpus through separate CLI runs.
set(seed "-define(F(X), {X, ~S/-endif./, 1.0}).\n-undef(F).")
string(LENGTH "${seed}" length)
foreach(size RANGE 0 ${length})
    string(SUBSTRING "${seed}" 0 ${size} prefix)
    file(WRITE "${WORK}/truncated.erl" "${prefix}")
    foreach(pass RANGE 1 2)
        execute_process(COMMAND "${TOOL}" --print-pp truncated.erl WORKING_DIRECTORY "${WORK}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5 ENCODING UTF-8)
        if(NOT status MATCHES "^[01]$")
            message(FATAL_ERROR "Truncation ${size} crashed or timed out: ${status}")
        endif()
        set(record "${status}\n${output}\n${error}")
        if(pass EQUAL 1)
            set(first "${record}")
        elseif(NOT first STREQUAL record)
            message(FATAL_ERROR "Truncation ${size} is nondeterministic")
        endif()
    endforeach()
endforeach()


check(big_decimal "?BIG." 0 "-123456789012345678901234567890" "^$" -DBIG=-123456789012345678901234567890)
