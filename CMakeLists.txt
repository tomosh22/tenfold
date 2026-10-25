cmake_minimum_required(VERSION 3.20)
project(tenfold VERSION 0.1.0 LANGUAGES CXX)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(TenfoldVersion)  # generates <build>/generated/tenfold_version.h

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
  set(TENFOLD_VS_DEBUGGER_SRC src/debug/VsDebugger_win32.cpp src/debug/ComDispatch_win32.cpp)
else()
  set(TENFOLD_PROCESS_SRC src/lsp/Process_posix.cpp)
  set(TENFOLD_VS_DEBUGGER_SRC "")
endif()
if(MSVC)
  set(TENFOLD_WARN /W4)
  set(TENFOLD_WARN_CORE /W4 /wd4100)
  # getenv/fopen etc. are used portably on purpose; silence the _s-variant deprecations.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  # Sources are UTF-8 without BOM (U'中' literals in tests); MSVC otherwise reads them in the ANSI code page.
  add_compile_options(/utf-8)
else()
  set(TENFOLD_WARN -Wall -Wextra)
  set(TENFOLD_WARN_CORE -Wall -Wextra -Wno-unused-parameter)
endif()

add_library(tenfold_core STATIC
  src/core/TextBuffer.cpp
  src/core/Document.cpp
  src/core/PathUtil.cpp
  src/core/CrashHandler.cpp
  src/core/Log.cpp
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
  src/app/EditorEditing.cpp
  src/app/Bookmarks.cpp
  src/debug/Breakpoints.cpp
  src/debug/DebugSession.cpp
  src/debug/VsDteUtil.cpp
  ${TENFOLD_VS_DEBUGGER_SRC}
  src/build/AnsiStrip.cpp
  src/build/ErrorParser.cpp
  src/build/BuildRunner.cpp
  src/vcs/Vcs.cpp
  src/vcs/VcsExec.cpp
  src/vcs/LineDiff.cpp
  src/vcs/GitProvider.cpp
  src/vcs/PerforceProvider.cpp
  src/vcs/VcsManager.cpp
)
target_include_directories(tenfold_core PUBLIC src third_party "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(tenfold_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(tenfold_core PUBLIC ole32 oleaut32 uuid)
endif()
target_compile_options(tenfold_core PRIVATE ${TENFOLD_WARN_CORE})
# Commit recorded in the session header of the local log (core/Log.cpp); "unknown" outside a checkout.
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE TENFOLD_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT TENFOLD_GIT_COMMIT)
  set(TENFOLD_GIT_COMMIT unknown)
endif()
target_compile_definitions(tenfold_core PRIVATE "TENFOLD_GIT_COMMIT=\"${TENFOLD_GIT_COMMIT}\"")

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
  tests/test_vcxproj.cpp
  tests/test_semantic.cpp
  tests/test_process.cpp
  tests/test_paths.cpp
  tests/test_textsearch.cpp
  tests/test_filewatcher.cpp
  tests/test_multicaret.cpp
  tests/test_editing.cpp
  tests/test_lsp_features.cpp
  tests/test_build.cpp
  tests/test_symbol_search.cpp
  tests/test_crash.cpp
  tests/test_log.cpp
  tests/test_debug_breakpoints.cpp
  tests/test_debug_session.cpp
  tests/test_vs_debugger.cpp
  src/debug/FakeDebugger.cpp  # test support
  tests/test_vcs_diff.cpp
  tests/test_vcs_git.cpp
  tests/test_vcs_manager.cpp
  tests/test_vcs_p4.cpp
)
target_link_libraries(tenfold-tests PRIVATE tenfold_core)
# Fake clangd / crashing process used by tests/test_crash.cpp.
add_executable(tenfold-test-helper tests/test_helper_main.cpp)
target_link_libraries(tenfold-test-helper PRIVATE tenfold_core)
target_compile_options(tenfold-test-helper PRIVATE ${TENFOLD_WARN})
add_dependencies(tenfold-tests tenfold-test-helper)
# Fake `p4` used by tests/test_vcs_p4.cpp.
add_executable(tenfold-fake-p4 tests/fake_p4_main.cpp)
target_link_libraries(tenfold-fake-p4 PRIVATE tenfold_core)
target_compile_options(tenfold-fake-p4 PRIVATE ${TENFOLD_WARN})
add_dependencies(tenfold-tests tenfold-fake-p4)
target_compile_definitions(tenfold-tests PRIVATE TENFOLD_FAKE_P4="$<TARGET_FILE:tenfold-fake-p4>")
target_compile_definitions(tenfold-tests PRIVATE TENFOLD_TEST_HELPER="$<TARGET_FILE:tenfold-test-helper>")
target_compile_definitions(tenfold-tests PRIVATE TENFOLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME unit COMMAND tenfold-tests)

# ---- UI (experimental): SDL3 window + OpenGL 3.3 renderer. Off by default; the engine never depends on it. ----
option(TENFOLD_BUILD_UI "Build the SDL3/OpenGL UI (tenfold-ui, tenfold-ui-tests)" OFF)
# Direct3D 11 backend (Windows only): the default renderer of tenfold-ui there; --renderer=gl still selects OpenGL.
if(WIN32)
  option(TENFOLD_D3D11 "Build the Direct3D 11 renderer backend (Windows)" ON)
else()
  set(TENFOLD_D3D11 OFF)
endif()
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
    src/ui/widgets/Widget.cpp
    src/ui/widgets/TabStrip.cpp
    src/ui/widgets/Splitter.cpp
    src/ui/widgets/Panel.cpp
    src/ui/widgets/StatusBar.cpp
    src/ui/widgets/TextView.cpp
    src/ui/widgets/InputLine.cpp
    src/ui/widgets/ListPopup.cpp
    src/ui/features/PickerUtil.cpp
    src/ui/features/SymbolSearch.cpp
    src/ui/features/FileSearch.cpp
    src/ui/features/NavResultPicker.cpp
    src/ui/features/PopupBox.cpp
    src/ui/features/FindInFiles.cpp
    src/ui/features/FindReplace.cpp
    src/ui/features/GoToLine.cpp
    src/ui/features/ConfigurationPicker.cpp
    src/ui/features/BookmarkList.cpp
    src/ui/features/CompletionPopup.cpp
    src/ui/features/HoverTooltip.cpp
    src/ui/features/BuildPanel.cpp
    src/ui/features/SignatureHelp.cpp
    src/ui/features/DiagnosticsPanel.cpp
    src/ui/features/VcsUi.cpp
    src/ui/features/DiffTool.cpp
    src/ui/view/Viewport.cpp
    src/ui/view/Layout.cpp
    src/ui/view/Highlighter.cpp
    src/ui/view/Scrollbar.cpp
    src/ui/view/WrapLayout.cpp
    src/ui/view/RowBlocks.cpp
    src/ui/widgets/ShellDemo.cpp
    src/ui/commands/Command.cpp
    src/ui/commands/CommandRegistry.cpp
    src/ui/commands/KeyMap.cpp
    src/ui/commands/EditorCommands.cpp
    src/ui/settings/KeyBindings.cpp
    src/ui/settings/Settings.cpp
    src/ui/settings/Theme.cpp
    src/ui/shell/DocumentView.cpp
    src/ui/shell/Workbench.cpp
    src/ui/shell/EditorApp.cpp
    src/ui/shell/FontSetup.cpp
    src/ui/features/NavigationUi.cpp
    src/ui/features/BreakpointList.cpp
  )
  # Default key bindings are embedded as a raw string literal so the app works without the asset dir.
  set(TENFOLD_DEFAULT_KEYS "${CMAKE_SOURCE_DIR}/assets/keybindings.default.txt")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TENFOLD_DEFAULT_KEYS}")
  file(READ "${TENFOLD_DEFAULT_KEYS}" _tf_default_keys)
  file(WRITE "${CMAKE_BINARY_DIR}/generated/DefaultKeyBindings.inc" "R\"TFKB(${_tf_default_keys})TFKB\"\n")
  # Default settings and the two shipped themes are embedded the same way.
  foreach(_tf_emb "settings.default.txt|DefaultSettings.inc|TFST" "themes/dark.txt|DefaultThemeDark.inc|TFTD"
                  "themes/light.txt|DefaultThemeLight.inc|TFTL"
                  "themes/vs-dark.txt|DefaultThemeVsDark.inc|TFTV")
    string(REPLACE "|" ";" _tf_emb "${_tf_emb}")
    list(GET _tf_emb 0 _tf_src)
    list(GET _tf_emb 1 _tf_dst)
    list(GET _tf_emb 2 _tf_tag)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/${_tf_src}")
    file(READ "${CMAKE_SOURCE_DIR}/assets/${_tf_src}" _tf_text)
    file(WRITE "${CMAKE_BINARY_DIR}/generated/${_tf_dst}" "R\"${_tf_tag}(${_tf_text})${_tf_tag}\"\n")
  endforeach()
  if(TENFOLD_D3D11)
    # HLSL is embedded and compiled at start-up with D3DCompile (d3dcompiler_47.dll ships with Windows 10+), so no
    # fxc/dxc is needed at build time.
    target_sources(tenfold_ui PRIVATE src/ui/gfx/D3d11Renderer.cpp src/ui/gfx/WindowsSwapChain.cpp)
    target_compile_definitions(tenfold_ui PUBLIC TENFOLD_HAVE_D3D11=1)
    target_link_libraries(tenfold_ui PUBLIC d3d11 dxgi d3dcompiler)
  endif()
  target_include_directories(tenfold_ui PRIVATE "${CMAKE_BINARY_DIR}/generated")
  target_include_directories(tenfold_ui PUBLIC src)
  target_link_libraries(tenfold_ui PUBLIC SDL3::SDL3-static tenfold_text tenfold_core)
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
    tests/ui/test_widgets.cpp
    tests/ui/test_commands.cpp
    tests/ui/test_workbench.cpp
    # Before test_editor_view.cpp: its 1920x1080 perf test leaves the shared GL fixture in a state that breaks later goldens.
    tests/ui/test_navigation_ui.cpp
    tests/ui/test_settings.cpp
    tests/ui/test_editor_app.cpp
    tests/ui/test_editor_view.cpp
    tests/ui/test_view_colouring.cpp
    tests/ui/test_pickers.cpp
    tests/ui/test_b5b.cpp
    tests/ui/test_build_ui.cpp
    tests/ui/test_b8.cpp
    tests/ui/test_wrap.cpp
    tests/ui/test_debugger_ui.cpp
    src/debug/FakeDebugger.cpp  # test support (the scripted backend of the debugger UI tests)
    tests/ui/test_vcs_ui.cpp
    tests/ui/test_config_ui.cpp
  )
  target_include_directories(tenfold-ui-tests PRIVATE tests)
  target_link_libraries(tenfold-ui-tests PRIVATE tenfold_ui)
  target_compile_definitions(tenfold-ui-tests PRIVATE TENFOLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME ui-unit COMMAND tenfold-ui-tests)
endif()

# ---- Packaging: install() rules, bundled clangd, CPack (cmake/TenfoldPackage.cmake) ----
include(TenfoldPackage)
