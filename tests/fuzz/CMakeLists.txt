# Fuzzing (W9h). See docs/TESTING.md ("Fuzzing").
#
#   tenfold-fuzz        the in-repo mutation fuzzer driver (fuzz_main.cpp) + every harness
#   tenfold-libfuzz-<t> one libFuzzer binary per harness (option TENFOLD_FUZZ_LIBFUZZER, compilers with the runtime)
#   regression replay   tests/fuzz/test_fuzz_regressions.cpp is part of tenfold-tests (every file under
#                       tests/fuzz/regressions/<target>/ is run through its harness)
#
# TENFOLD_FUZZ_COVERAGE instruments the engine with -fsanitize-coverage=trace-pc so that tenfold-fuzz can keep the
# inputs that reach new code (works with gcc and clang); libFuzzer builds use their own instrumentation.
option(TENFOLD_FUZZ_COVERAGE "Instrument tenfold_core with -fsanitize-coverage=trace-pc for tenfold-fuzz" OFF)
option(TENFOLD_FUZZ_LIBFUZZER "Build one libFuzzer binary per fuzz harness (needs -fsanitize=fuzzer)" OFF)

if(WIN32)
  return()  # the driver uses POSIX signals / fork; the replay tests are POSIX-only too (see test_fuzz_regressions.cpp)
endif()

set(_fz ${CMAKE_CURRENT_SOURCE_DIR})
set(TENFOLD_FUZZ_DIR ${_fz} CACHE INTERNAL "tests/fuzz")
set(TENFOLD_FUZZ_HARNESS_SRC ${_fz}/FuzzTarget.cpp ${_fz}/targets_core.cpp ${_fz}/targets_lsp.cpp)

# The settings / theme / key-binding parsers live in src/ui but need neither SDL nor GL: they are compiled here without
# the UI library (their embedded default texts are generated like the UI build does, see the top-level CMakeLists.txt).
set(_fz_gen "${CMAKE_BINARY_DIR}/generated")
set(_fz_embed "KeyMappings.default.10x_settings|DefaultKeyBindings.inc|TFKB" "settings.default.txt|DefaultSettings.inc|TFST"
              "themes/dark.txt|DefaultThemeDark.inc|TFTD" "themes/light.txt|DefaultThemeLight.inc|TFTL"
              "themes/vs-dark.txt|DefaultThemeVsDark.inc|TFTV")
foreach(_e ${_fz_embed})
  string(REPLACE "|" ";" _e "${_e}")
  list(GET _e 0 _src)
  list(GET _e 1 _dst)
  list(GET _e 2 _tag)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/${_src}")
  file(READ "${CMAKE_SOURCE_DIR}/assets/${_src}" _text)
  file(WRITE "${_fz_gen}/${_dst}" "R\"${_tag}(${_text})${_tag}\"\n")
endforeach()
list(APPEND TENFOLD_FUZZ_HARNESS_SRC ${_fz}/targets_settings.cpp
     ${CMAKE_SOURCE_DIR}/src/ui/settings/KeyBindings.cpp ${CMAKE_SOURCE_DIR}/src/ui/settings/Settings.cpp
     ${CMAKE_SOURCE_DIR}/src/ui/settings/Theme.cpp ${CMAKE_SOURCE_DIR}/src/ui/commands/KeyMap.cpp
     ${CMAKE_SOURCE_DIR}/src/ui/commands/CommandNames.cpp ${CMAKE_SOURCE_DIR}/src/ui/widgets/Widget.cpp)

# The coverage hook (never instrumented itself); linked into every binary that links an instrumented tenfold_core.
add_library(tenfold_fuzz_cov STATIC ${_fz}/fuzz_cov.cpp)

add_library(tenfold_fuzz_harness OBJECT ${TENFOLD_FUZZ_HARNESS_SRC})
target_link_libraries(tenfold_fuzz_harness PUBLIC tenfold_core)
target_include_directories(tenfold_fuzz_harness PUBLIC ${_fz} "${_fz_gen}")
target_compile_options(tenfold_fuzz_harness PRIVATE ${TENFOLD_WARN_CORE})
target_compile_definitions(tenfold_fuzz_harness PRIVATE TENFOLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(TENFOLD_FUZZ_COVERAGE)
  target_compile_options(tenfold_core PRIVATE -fsanitize-coverage=trace-pc)
  target_compile_options(tenfold_fuzz_harness PRIVATE -fsanitize-coverage=trace-pc)
  target_link_libraries(tenfold_core PUBLIC tenfold_fuzz_cov)
endif()

add_executable(tenfold-fuzz ${_fz}/fuzz_main.cpp $<TARGET_OBJECTS:tenfold_fuzz_harness>)
target_link_libraries(tenfold-fuzz PRIVATE tenfold_fuzz_harness tenfold_fuzz_cov)
target_compile_definitions(tenfold-fuzz PRIVATE TENFOLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(tenfold-fuzz PRIVATE ${TENFOLD_WARN})

# Regression replay inside the engine test binary.
target_sources(tenfold-tests PRIVATE ${_fz}/test_fuzz_regressions.cpp $<TARGET_OBJECTS:tenfold_fuzz_harness>)
target_include_directories(tenfold-tests PRIVATE ${_fz} "${_fz_gen}")

if(TENFOLD_FUZZ_LIBFUZZER)
  set(_libfuzz_targets lexer extract indexstore xml msbuild msbuildexpr compdb compdbcmd jsonrpc buildoutput gitstatus p4output
                       linediff vtscreen search keybindings settings theme)
  foreach(_t ${_libfuzz_targets})
    add_executable(tenfold-libfuzz-${_t} ${_fz}/libfuzzer_main.cpp $<TARGET_OBJECTS:tenfold_fuzz_harness>)
    target_link_libraries(tenfold-libfuzz-${_t} PRIVATE tenfold_fuzz_harness)
    target_compile_definitions(tenfold-libfuzz-${_t} PRIVATE TENFOLD_FUZZ_TARGET="${_t}")
    target_compile_options(tenfold-libfuzz-${_t} PRIVATE -fsanitize=fuzzer-no-link)
    target_link_options(tenfold-libfuzz-${_t} PRIVATE -fsanitize=fuzzer)
  endforeach()
  target_compile_options(tenfold_core PRIVATE -fsanitize=fuzzer-no-link)
  target_compile_options(tenfold_fuzz_harness PRIVATE -fsanitize=fuzzer-no-link)
endif()

# ---- harnesses that need the UI library (layout.json, the explorer's project readers): TENFOLD_BUILD_UI only ----
# Called from the top-level CMakeLists.txt once tenfold_ui exists.
function(tenfold_fuzz_add_ui_targets)
  add_library(tenfold_fuzz_harness_ui OBJECT ${TENFOLD_FUZZ_DIR}/targets_ui.cpp)
  target_link_libraries(tenfold_fuzz_harness_ui PUBLIC tenfold_ui tenfold_core)
  target_include_directories(tenfold_fuzz_harness_ui PUBLIC ${TENFOLD_FUZZ_DIR} ${CMAKE_SOURCE_DIR}/tests)
  target_compile_options(tenfold_fuzz_harness_ui PRIVATE ${TENFOLD_WARN})
  if(TENFOLD_FUZZ_COVERAGE)
    target_compile_options(tenfold_fuzz_harness_ui PRIVATE -fsanitize-coverage=trace-pc)
  endif()
  target_sources(tenfold-fuzz PRIVATE $<TARGET_OBJECTS:tenfold_fuzz_harness_ui>)
  target_link_libraries(tenfold-fuzz PRIVATE tenfold_ui)
  # The UI test binary replays the layout / explorer regressions (the settings parsers are already in tenfold_ui).
  target_sources(tenfold-ui-tests PRIVATE ${TENFOLD_FUZZ_DIR}/test_fuzz_regressions_ui.cpp ${TENFOLD_FUZZ_DIR}/FuzzTarget.cpp
                 $<TARGET_OBJECTS:tenfold_fuzz_harness_ui>)
  target_include_directories(tenfold-ui-tests PRIVATE ${TENFOLD_FUZZ_DIR})
endfunction()
