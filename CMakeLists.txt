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

if(WIN32)
  set(TENFOLD_PROCESS_SRC src/lsp/Process_win32.cpp)
else()
  set(TENFOLD_PROCESS_SRC src/lsp/Process_posix.cpp)
endif()
if(MSVC)
  set(TENFOLD_WARN /W4)
  set(TENFOLD_WARN_CORE /W4 /wd4100)
else()
  set(TENFOLD_WARN -Wall -Wextra)
  set(TENFOLD_WARN_CORE -Wall -Wextra -Wno-unused-parameter)
endif()

add_library(tenfold_core STATIC
  src/core/TextBuffer.cpp
  src/core/Document.cpp
  src/core/PathUtil.cpp
  src/nav/NavigationHistory.cpp
  ${TENFOLD_PROCESS_SRC}
  src/lsp/JsonRpc.cpp
  src/lsp/LspClient.cpp
  src/semantic/CppLexer.cpp
  src/semantic/FastIndex.cpp
  src/semantic/CompileCommands.cpp
  src/semantic/XmlLite.cpp
  src/semantic/SemanticEngine.cpp
  src/workspace/Workspace.cpp
  src/app/Editor.cpp
)
target_include_directories(tenfold_core PUBLIC src third_party)
target_link_libraries(tenfold_core PUBLIC Threads::Threads)
target_compile_options(tenfold_core PRIVATE ${TENFOLD_WARN_CORE})

add_executable(tenfold-cli src/cli/main.cpp)
target_link_libraries(tenfold-cli PRIVATE tenfold_core)
target_compile_options(tenfold-cli PRIVATE ${TENFOLD_WARN})

enable_testing()
add_executable(tenfold-tests
  tests/test_main.cpp
  tests/test_textbuffer.cpp
  tests/test_navigation.cpp
  tests/test_fastindex.cpp
  tests/test_compdb.cpp
  tests/test_semantic.cpp
  tests/test_process.cpp
  tests/test_paths.cpp
)
target_link_libraries(tenfold-tests PRIVATE tenfold_core)
target_compile_definitions(tenfold-tests PRIVATE TENFOLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME unit COMMAND tenfold-tests)
