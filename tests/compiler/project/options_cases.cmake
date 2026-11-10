# Demonstrate decoded values, feature precedence and separate header/source search roots.
set(options_dir "${WORK}/options")
file(MAKE_DIRECTORY "${options_dir}/include" "${options_dir}/generated" "${outside}/override/include")
file(WRITE "${options_dir}/generated/main.erl" [=[-module(options).
-include("choice.hrl").
-include_lib("demo/include/common.hrl").
value() -> {?FLAG, ?COUNT, ?LABEL, ?PICK, ?LIB, ?EXTRA}.
-if(?FEATURE_ENABLED(maybe_expr)).
maybe_feature() -> true.
-else.
maybe_feature() -> false.
-endif.
-if(?FEATURE_ENABLED(compr_assign)).
comprehension_feature() -> true.
-else.
comprehension_feature() -> false.
-endif.
]=])
file(WRITE "${options_dir}/include/choice.hrl" "-define(PICK,11).\n")
file(WRITE "${options_dir}/vendor/demo/include/common.hrl" "-define(LIB,22).\n")
file(WRITE "${outside}/override/include/common.hrl" "-define(LIB,33).\n")
file(WRITE "${outside}/second/choice.hrl" "-define(PICK,44).\n")
file(WRITE "${options_dir}/project.toml" [=[schema_version=1
[[targets]]
name='app'
sources=['main.erl']
[targets.options]
source_search_paths=['src','generated']
include_dirs=['include']
defines=['FLAG','COUNT=2','LABEL="demo"']
enable_features=['maybe_expr']
disable_features=['compr_assign']
[targets.options.applications]
demo='vendor/demo'
]=])
check(decoded_options 0 "Atom name=true.*value=2.*StringLiteral value=\"demo\".*value=11.*value=22.*value=42.*Atom name=true.*Atom name=false" "^$"
    "${options_dir}/project.toml" --print-ast -DEXTRA=42)
check(option_overrides 0 "value=44.*value=33.*value=42.*Atom name=false.*Atom name=true" "^$"
    "${options_dir}/project.toml" --print-ast -DEXTRA=42 -I cli-include -I second --app-dir demo=override
    --disable-feature maybe_expr --enable-feature compr_assign)
check(invalid_literal 1 "^$" "expected literal Erlang term" "${options_dir}/project.toml" --parse-check "-DBAD=1+2")
check(unknown_feature 1 "^$" "unknown.*feature" "${options_dir}/project.toml" --parse-check --enable-feature unknown_feature)
# An unrelated fallback root must not become a header search root.
file(WRITE "${options_dir}/main.erl" "-module(options). -include(\"hidden.hrl\").\n")
file(WRITE "${options_dir}/generated/hidden.hrl" "-define(HEADER,1).\n")
check(source_roots_are_not_headers 1 "^$" "hidden.hrl" "${options_dir}/project.toml" --parse-check)

# include_dirs patterns expand to the directories they match: `*` one component, `**` any number (none included).
set(globbed "${WORK}/include_globs")
file(WRITE "${globbed}/deps/a/include/a.hrl" "-define(A,1).\n")
file(WRITE "${globbed}/deps/b/include/b.hrl" "-define(B,2).\n")
file(WRITE "${globbed}/apps/x/y/include/c.hrl" "-define(C,3).\n")
file(WRITE "${globbed}/apps/top.hrl" "-define(T,4).\n")
file(WRITE "${globbed}/main.erl" [=[-module(globbed).
-include("a.hrl").
-include("b.hrl").
-include("c.hrl").
-include("top.hrl").
value() -> {?A, ?B, ?C, ?T}.
]=])
file(WRITE "${globbed}/project.toml" "schema_version=1\n[[targets]]\nname='app'\nsources=['main.erl']\n[targets.options]\ninclude_dirs=['deps/*/include','apps/**']\n")
check(include_dir_patterns 0 "value=1.*value=2.*value=3.*value=4" "^$" "${globbed}/project.toml" --print-ast)
file(WRITE "${globbed}/unmatched.toml" "schema_version=1\n[[targets]]\nname='app'\nsources=['main.erl']\n[targets.options]\ninclude_dirs=['deps/*/missing']\n")
check(include_dir_unmatched 1 "^$" "unmatched include directory pattern: deps/\\*/missing" "${globbed}/unmatched.toml" --parse-check)

# application_include_dirs: each source finds its own application's header first (the directory above its src), as
# rebar3 compiles each application; the shared include_dirs alone give both sources the first application's.
set(per_app "${WORK}/application_includes")
file(WRITE "${per_app}/deps/a/include/protocol.hrl" "-define(V,1).\n")
file(WRITE "${per_app}/deps/b/include/protocol.hrl" "-define(V,2).\n")
file(WRITE "${per_app}/deps/a/src/a.erl" "-module(a).\n-include(\"protocol.hrl\").\nv() -> ?V.\n")
file(WRITE "${per_app}/deps/b/src/nested/b.erl" "-module(b).\n-include(\"protocol.hrl\").\nv() -> ?V.\n")
set(per_app_target "schema_version=1\n[[targets]]\nname='app'\nsources=['deps/a/src/a.erl','deps/b/src/nested/b.erl']\n")
file(WRITE "${per_app}/project.toml"
    "${per_app_target}[targets.options]\ninclude_dirs=['deps/*/include']\napplication_include_dirs=['include']\n")
check(application_include_dirs 0 "name=a.*value=1.*name=b.*value=2" "^$" "${per_app}/project.toml" --print-ast)
file(WRITE "${per_app}/shared.toml" "${per_app_target}[targets.options]\ninclude_dirs=['deps/*/include']\n")
check(shared_include_dirs 0 "name=a.*value=1.*name=b.*value=1" "^$" "${per_app}/shared.toml" --print-ast)

# source_search_paths patterns expand like include_dirs. Besides locating listed literal sources, the directories are
# searched, before the library, for a module the batch names but no source defines (here helper, other and lists).
set(searched "${WORK}/search_globs")
file(WRITE "${searched}/main.erl"
    "-module(main). -export([value/0]). value() -> helper:value() + other:value() + length(lists:reverse([1])).\n")
file(WRITE "${searched}/deps/a/src/helper.erl" "-module(helper). -export([value/0]). value() -> 1.\n")
file(WRITE "${searched}/apps/x/y/other.erl" "-module(other). -export([value/0]). value() -> 2.\n")
file(WRITE "${searched}/apps/listed.erl" "-module(listed). -export([value/0]). value() -> other:value().\n")
set(search_target "schema_version=1\n[[targets]]\nname='app'\n")
file(WRITE "${searched}/project.toml"
    "${search_target}sources=['main.erl','listed.erl']\n[targets.options]\nsource_search_paths=['deps/*/src','apps/**']\n")
check(search_path_modules 0 "^$" "^$" "${searched}/project.toml")
file(WRITE "${searched}/plain.toml" "${search_target}sources=['main.erl']\n")
check(search_path_absent 1 "^$" "unknown module helper" "${searched}/plain.toml")
file(WRITE "${searched}/unmatched.toml"
    "${search_target}sources=['main.erl']\n[targets.options]\nsource_search_paths=['deps/*/missing']\n")
check(search_path_unmatched 1 "^$" "unmatched source search path pattern: deps/\\*/missing" "${searched}/unmatched.toml"
    --parse-check)

# Separate OS processes compete for the same exclusive manifest destination.
execute_process(COMMAND "${TOOL}" --new-project "race λ"
    COMMAND "${TOOL}" --new-project "race λ"
    WORKING_DIRECTORY "${options_dir}" RESULTS_VARIABLE results
    OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
list(SORT results)
if(NOT results STREQUAL "0;1" OR NOT errors MATCHES "cannot create manifest exclusively")
    message(FATAL_ERROR "Concurrent creators: ${results}: ${output}${errors}")
endif()
file(WRITE "${options_dir}/src/main.erl" "-module(race).\n")
check(race_manifest 0 "ModuleAttribute name=race" "^$" "${options_dir}/race λ.toml" --print-ast)
execute_process(COMMAND "${TOOL}" --new-project "starter λ; space.TOML"
    WORKING_DIRECTORY "${options_dir}" RESULT_VARIABLE created OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
if(NOT created STREQUAL "0" OR NOT errors STREQUAL "" OR NOT EXISTS "${options_dir}/starter λ; space.TOML")
    message(FATAL_ERROR "Native creation filename changed: ${created}: ${errors}")
endif()
check(native_created_manifest 0 "ModuleAttribute name=race" "^$" "${options_dir}/starter λ; space.TOML" --print-ast)
file(CREATE_LINK "${options_dir}/absent" "${options_dir}/link.toml" SYMBOLIC RESULT link)
if(link STREQUAL "0")
    execute_process(COMMAND "${TOOL}" --new-project "${options_dir}/link" RESULT_VARIABLE status
        OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
    if(NOT status STREQUAL "1" OR NOT errors MATCHES "cannot create manifest exclusively" OR NOT IS_SYMLINK "${options_dir}/link.toml")
        message(FATAL_ERROR "Creation replaced a dangling link: ${status}: ${errors}")
    endif()
else()
    message(STATUS "SKIP capability: creation through dangling symlink: ${link}")
endif()

