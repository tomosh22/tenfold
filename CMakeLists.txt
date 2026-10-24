cmake_minimum_required(VERSION 3.20)
project(tenfold LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

add_library(tenfold_core STATIC
  src/core/TextBuffer.cpp
  src/core/Document.cpp
  src/nav/NavigationHistory.cpp
  src/lsp/Process.cpp
  src/lsp/JsonRpc.cpp
  src/lsp/LspClient.cpp
  src/semantic/CppLexer.cpp
  src/semantic/FastIndex.cpp
  src/semantic/CompileCommands.cpp
  src/semantic/XmlLite.cpp
  src/semantic/SemanticEngine.cpp
  src/workspace/Workspace.cpp
  src/search/TextSearch.cpp
  src/app/Editor.cpp
)
target_include_directories(tenfold_core PUBLIC src third_party)
target_link_libraries(tenfold_core PUBLIC Threads::Threads)
target_compile_options(tenfold_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Regex find-in-files. The literal search path never needs PCRE2; without it a
# regex query returns the error "regex support not built".
option(TENFOLD_WITH_PCRE2 "Regex search via PCRE2 (system library, else FetchContent pcre2-10.44)" ON)
option(TENFOLD_PCRE2_FORCE_FETCH "Always fetch and build PCRE2 instead of using the system one" OFF)
if(TENFOLD_WITH_PCRE2)
  set(_tf_pcre2_found FALSE)
  if(NOT TENFOLD_PCRE2_FORCE_FETCH)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(PCRE2 QUIET IMPORTED_TARGET libpcre2-8)
      if(PCRE2_FOUND)
        target_link_libraries(tenfold_core PUBLIC PkgConfig::PCRE2)
        set(_tf_pcre2_found TRUE)
        message(STATUS "PCRE2: system ${PCRE2_VERSION}")
      endif()
    endif()
  endif()
  if(NOT _tf_pcre2_found)
    include(FetchContent)
    set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PCRE2_SUPPORT_JIT ON CACHE BOOL "" FORCE)
    set(PCRE2_STATIC_PIC ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(pcre2 GIT_REPOSITORY https://github.com/PCRE2Project/pcre2.git GIT_TAG pcre2-10.44)
    FetchContent_MakeAvailable(pcre2)
    target_link_libraries(tenfold_core PUBLIC pcre2-8-static)
    message(STATUS "PCRE2: fetched pcre2-10.44")
  endif()
  target_compile_definitions(tenfold_core PUBLIC TENFOLD_HAVE_PCRE2=1)
endif()

add_executable(tenfold-cli src/cli/main.cpp)
target_link_libraries(tenfold-cli PRIVATE tenfold_core)
target_compile_options(tenfold-cli PRIVATE -Wall -Wextra)

enable_testing()
add_executable(tenfold-tests
  tests/test_main.cpp
  tests/test_textbuffer.cpp
  tests/test_navigation.cpp
  tests/test_fastindex.cpp
  tests/test_compdb.cpp
  tests/test_semantic.cpp
  tests/test_textsearch.cpp
)
target_link_libraries(tenfold-tests PRIVATE tenfold_core)
target_compile_definitions(tenfold-tests PRIVATE TENFOLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME unit COMMAND tenfold-tests)
