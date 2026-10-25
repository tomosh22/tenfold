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
  # getenv/fopen etc. are used portably on purpose; silence the _s-variant deprecations.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
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
  src/semantic/FastIndexStore.cpp
  src/semantic/CompileCommands.cpp
  src/semantic/XmlLite.cpp
  src/semantic/SemanticEngine.cpp
  src/workspace/Workspace.cpp
  src/search/TextSearch.cpp
  src/workspace/FileWatcher.cpp
  src/app/Editor.cpp
)
target_include_directories(tenfold_core PUBLIC src third_party)
target_link_libraries(tenfold_core PUBLIC Threads::Threads)
target_compile_options(tenfold_core PRIVATE ${TENFOLD_WARN_CORE})

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
target_compile_options(tenfold-cli PRIVATE ${TENFOLD_WARN})

enable_testing()
add_executable(tenfold-tests
  tests/test_main.cpp
  tests/test_textbuffer.cpp
  tests/test_navigation.cpp
  tests/test_fastindex.cpp
  tests/test_fastindexstore.cpp
  tests/test_compdb.cpp
  tests/test_semantic.cpp
  tests/test_process.cpp
  tests/test_paths.cpp
  tests/test_textsearch.cpp
  tests/test_filewatcher.cpp
  tests/test_multicaret.cpp
)
target_link_libraries(tenfold-tests PRIVATE tenfold_core)
target_compile_definitions(tenfold-tests PRIVATE TENFOLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME unit COMMAND tenfold-tests)

# ---- UI (experimental): SDL3 window + OpenGL 3.3 renderer. Off by default; the engine never depends on it. ----
option(TENFOLD_BUILD_UI "Build the SDL3/OpenGL UI (tenfold-ui, tenfold-ui-tests)" OFF)
if(TENFOLD_BUILD_UI)
  # FreeType 2.13: system library when available, else fetched (only the rasterizer, no optional codecs).
  find_package(Freetype QUIET)
  if(Freetype_FOUND)
    set(TENFOLD_FREETYPE_TARGET Freetype::Freetype)
    message(STATUS "FreeType: system ${FREETYPE_VERSION_STRING}")
  else()
    include(FetchContent)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(freetype GIT_REPOSITORY https://github.com/freetype/freetype.git GIT_TAG VER-2-13-3)
    FetchContent_MakeAvailable(freetype)
    set(TENFOLD_FREETYPE_TARGET freetype)
    message(STATUS "FreeType: fetched VER-2-13-3")
  endif()

  add_library(tenfold_text STATIC
    src/ui/gfx/Font.cpp
    src/ui/gfx/Atlas.cpp
    src/ui/gfx/TextLayout.cpp
  )
  target_include_directories(tenfold_text PUBLIC src)
  target_link_libraries(tenfold_text PUBLIC ${TENFOLD_FREETYPE_TARGET})
  target_compile_options(tenfold_text PRIVATE ${TENFOLD_WARN})

  add_executable(tenfold-text-tests
    tests/test_main.cpp
    tests/text/test_font.cpp
    tests/text/test_atlas.cpp
    tests/text/test_layout.cpp
    tests/text/test_bench.cpp
  )
  target_include_directories(tenfold-text-tests PRIVATE tests)
  target_link_libraries(tenfold-text-tests PRIVATE tenfold_text)
  target_compile_definitions(tenfold-text-tests PRIVATE TENFOLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME text COMMAND tenfold-text-tests)

  include(FetchContent)
  # Static SDL3 with only the video/events subsystems we use.
  foreach(_sdl_off SDL_SHARED SDL_TESTS SDL_TEST_LIBRARY SDL_INSTALL SDL_AUDIO SDL_RENDER SDL_GPU SDL_CAMERA
                   SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_POWER SDL_SENSOR SDL_DIALOG SDL_VULKAN)
    set(${_sdl_off} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.2.30 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)

  add_library(tenfold_ui STATIC
    src/ui/platform/Window.cpp
    src/ui/platform/Input.cpp
    src/ui/platform/Clipboard.cpp
    src/ui/gfx/GlLoader.cpp
    src/ui/gfx/GlRenderer.cpp
    src/ui/gfx/PngWrite.cpp
    src/ui/gfx/TextRenderer.cpp
  )
  target_include_directories(tenfold_ui PUBLIC src)
  target_link_libraries(tenfold_ui PUBLIC SDL3::SDL3-static tenfold_text)
  target_compile_options(tenfold_ui PRIVATE ${TENFOLD_WARN})

  add_executable(tenfold-ui src/ui_app/main.cpp)
  target_link_libraries(tenfold-ui PRIVATE tenfold_ui tenfold_core)
  target_compile_options(tenfold-ui PRIVATE ${TENFOLD_WARN})

  add_executable(tenfold-ui-tests
    tests/ui/ui_test_main.cpp
    tests/ui/test_png.cpp
    tests/ui/test_input.cpp
    tests/ui/test_renderer.cpp
    tests/ui/test_text_view.cpp
  )
  target_link_libraries(tenfold-ui-tests PRIVATE tenfold_ui)
  target_compile_definitions(tenfold-ui-tests PRIVATE TENFOLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME ui-unit COMMAND tenfold-ui-tests)
endif()
