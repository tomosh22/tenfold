# Tenfold

A from-scratch C++ code editor engine in the spirit of the
[10x editor](https://10xeditor.com): instant startup, whole-workspace symbol
search in milliseconds, native speed everywhere. It exists to fix two things
that 10x gets wrong while keeping everything else it gets right:

1. **No back button.** Tenfold has a full Visual Studio style
   *Navigate Backward / Forward* history (`src/nav/`). Every jump records
   where you came from and where you landed, far caret moves are remembered,
   nearby points coalesce, edits keep old entries pointing at the right line,
   and a fresh jump truncates the forward stack. See
   `tests/test_navigation.cpp` for the exact semantics.
2. **A fast parser that misses things.** Tenfold's semantic engine
   (`src/semantic/`) is two-tier. A 10x-style lexer index gives instant,
   approximate answers, but the *authority* is clangd, the real Clang
   frontend driven by the project's actual compile flags. Templates,
   `auto`, typedef chains, explicit specialisations, `decltype`, overloads,
   overrides and macro-generated members all resolve exactly, because they
   are resolved by the compiler. Approximate answers are only ever shown
   while clangd is still thinking, and they are labelled as such.

The repository holds the UI-agnostic engine (`src/core`, `src/nav`, `src/lsp`, `src/semantic`, ...), a headless
front end (`tenfold-cli`) and the graphical editor `tenfold-ui` (SDL3, OpenGL 3.3 or Direct3D 11 on Windows, FreeType).
See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the design and [`docs/FEATURE_PARITY.md`](docs/FEATURE_PARITY.md)
for what matches 10x and Visual Studio today.

## Getting started

Unpack a package (Windows `.zip`, Linux `.tar.gz` / AppImage; clangd is bundled) and open a workspace folder:
`bin/tenfold-ui <folder>` (a folder with a Visual Studio `.sln`, a `compile_commands.json`, a CMake project, or just
sources). **`docs/USER_GUIDE.md`** is the guide for people coming from 10x and Visual Studio: installing, the two-tier
results (instant approximate, then clangd's authoritative), navigation history, Visual Studio solution and debugger
integration, Perforce and Git, settings, themes, key bindings, logs, and troubleshooting. If a symbol looks wrong or
approximate, `tenfold-cli why <folder> <file>` (or the `Help.ShowCompileCommand` command in the editor) prints the
compile command clangd uses for that file.

## Build and test

Requirements: a C++20 compiler (GCC 13 / Clang 18 tested), CMake 3.20+,
and `clangd` on `PATH` (or pass `--clangd <path>`). Without clangd everything
still works in approximate mode.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
./build/tenfold-tests            # engine tests, including clangd integration (676 on Linux)
```

CI / presets: `CMakePresets.json` mirrors the CI matrix, so
`cmake --preset release && cmake --build --preset release && ctest --preset release`
works locally (also `debug`, `asan`, and on Windows `msvc` / `clang-cl` from a VS developer prompt).
GitHub Actions (`.github/workflows/ci.yml`) builds gcc, clang, clang ASan+UBSan, MSVC,
clang-cl and AppleClang, and adds jobs for ThreadSanitizer, the corpora, the UI (Linux under Xvfb, Windows with Direct3D 11),
the Visual Studio debugger, fuzzing and packages; `clang-format --dry-run --Werror` runs on changed C++ files only. The
list of jobs and what they run is in [`docs/TESTING.md`](docs/TESTING.md).

### Install (packages) and build from source

Packages (Windows `.zip`, Linux `.tar.gz` / AppImage, macOS `.dmg`) are built by CI on pushes to `main` and `v*` tags.
Unpack anywhere and run `bin/tenfold-ui` (or `bin/tenfold-cli`); clangd ships in the same `bin/` with its
`lib/clang` headers and is preferred over any clangd on PATH, unless the `clangdPath` setting / `--clangd` is set.
`tenfold-cli --version` prints the version and commit. Third-party licences are in `licenses/`.

Building a package yourself (needs network once, to download the pinned clangd and verify its SHA-256):

```sh
cmake -S . -B build-pkg -G Ninja -DCMAKE_BUILD_TYPE=Release -DTENFOLD_BUILD_UI=ON
cmake --build build-pkg
cmake --build build-pkg --target fetch-clangd      # pin: tools/package/ClangdPin.cmake
(cd build-pkg && cpack)                             # ZIP on Windows, TGZ on Linux, DragNDrop on macOS
tools/package/smoke-test.sh build-pkg/tenfold-*.tar.gz
```

Use `-DTENFOLD_BUNDLE_CLANGD=OFF` to install without clangd. `tools/package/appimage.sh build-pkg` makes an AppImage.

## Building the UI

`tenfold-ui` (SDL3 + OpenGL 3.3, or Direct3D 11 on Windows; the packages above contain it) is the editor. It is
opt-in at configure time: `TENFOLD_BUILD_UI` defaults to `OFF` (see `CMakeLists.txt`), so a plain engine build does not
need SDL3 or FreeType, and the engine never depends on the UI. CI builds it with `-DTENFOLD_BUILD_UI=ON` on Linux, Windows
and for the packages.

```
cmake -S . -B build-ui -G Ninja -DCMAKE_BUILD_TYPE=Release -DTENFOLD_BUILD_UI=ON
ninja -C build-ui
xvfb-run -a ./build-ui/tenfold-ui-tests          # golden-image tests skip without a display
./build-ui/tenfold-ui [workspace]                # or: xvfb-run -a ./build-ui/tenfold-ui --screenshot out.png [--size WxH]
```

SDL3 is fetched (static, pinned to `release-3.2.30`); FreeType is the system library or fetched; on Linux you need
X11/GL dev packages. Besides `tenfold-ui` the UI build makes `tenfold-ui-tests` (605 tests on Linux) and
`tenfold-text-tests` (30; font, atlas and layout, no display needed). After an intentional rendering change, refresh the
golden images with `tenfold-ui-tests --update-goldens`.

## Try it

`tenfold-cli` is a scriptable front end over the editor model. Point it at a
workspace root (a folder, a `.sln`, or a `CMakeLists.txt` directory) and type
commands:

```sh
./build/tenfold-cli examples/sample_project
> open src/main.cpp:12:17      # caret on s->area(), where s is an auto variable
> def                          # -> geometry.h:38 Shape::area  [authoritative]
> back                         # -> main.cpp:12:17
> fwd                          # -> geometry.h:38
> open src/main.cpp:16:17      # b.get_w(), a macro-generated member
> def                          # -> geometry.h:53, the GEO_GETTER line
> sym geo::Tr                  # fuzzy, qualified symbol search
> refs | hover | complete | history | status | quit
```

`examples/sample_project` is deliberately full of constructs that defeat
heuristic parsers (alias to template, `auto` return types, `decltype`,
specialisations, macro-generated members). `tests/test_semantic.cpp` proves
that clangd resolves all of them, and that the fast index alone cannot see
the macro-generated one.

### The GUI editor

Build as above (or use a package) and open a workspace:

```sh
./build-ui/tenfold-ui examples/sample_project            # restores <workspace>/.tenfold/layout.json
./build-ui/tenfold-ui examples/sample_project src/geometry.cpp
```

Tabs, split panels (drag a tab to a panel edge) and the status bar are the Workbench; each tab is a text view. Key
bindings work like 10x's: the same `.10x_settings` format (`Control Shift F: Search`), 10x's command names and 10x's default
key map in [`assets/KeyMappings.default.10x_settings`](assets/KeyMappings.default.10x_settings) (overridable in
`KeyMappings.10x_settings` in the user config dir and in `<workspace>/.tenfold/`; an installed 10x's own key file is imported
below them; parse errors show in the status bar), and **`Control Shift X` opens the command palette**, which lists every
command with its 10x name and key. The 10x commands tenfold does not implement yet are listed in
[`docs/FEATURE_PARITY.md`](docs/FEATURE_PARITY.md). Pickers, search, editing, build, debugger and version-control commands, settings and themes are
described in [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md). Quitting or closing a modified tab asks to save first.

`tenfold-ui --shell` is the widget demo and `--demo` the renderer scene; `--screenshot out.png [--size WxH]` renders one
frame headless.

Generate a `compile_commands.json` from a Visual Studio solution without
running the editor:

```sh
./build/tenfold-cli compdb path/to/Game.sln --config Debug --platform x64 -o compile_commands.json
```

## What is where

```
src/core        TextBuffer (UTF-8 lines; CRLF, LF, CR, NEL, LS, PS like Visual Studio), Document (undo/redo, listeners, URIs),
                encodings, .editorconfig, logging, crash handler, Selection, Trash
src/nav         NavigationHistory: back/forward with coalescing and edit tracking
src/lsp         Process (POSIX and Windows), JSON-RPC framing, typed LspClient for clangd
src/semantic    CppLexer, FastIndex (heuristic symbols + fuzzy search) and its on-disk store,
                CompileCommands (compile_commands.json discovery; CMake, .sln/.vcxproj,
                heuristic generators; MSBuild property/condition evaluation),
                SemanticEngine (two-tier query policy), TuContext, IncludeNav, DefChoice
src/workspace   Workspace scanning, settings (.tenfold/workspace.json), FileWatcher
src/search      TextSearch (find in files), ReplaceInFiles
src/build       BuildRunner, ErrorParser
src/debug       Breakpoints, DebugSession, Visual Studio (EnvDTE) debugger backend
src/vcs         Git and Perforce providers, LineDiff, VcsManager
src/term        pseudo-terminal (POSIX, ConPTY), VT screen, key encoding
src/app         Editor: documents + caret + navigation + semantic commands + main-thread queue
src/corpus      the corpus runner behind `tenfold-cli corpus`
src/cli         tenfold-cli, the headless reference front end
src/ui          the GUI library: gfx (renderers, fonts), platform (SDL3), widgets, view, commands, settings, features, shell
src/ui_app      tenfold-ui, the editor executable
tests           unit + integration tests (tiny self-contained framework), corpora, fuzz, ui, text
tools           benchmark scripts, stress generators, packaging
examples        sample project used by the tests
docs            user guide, architecture, testing, feature parity, benchmarks, stress results, open questions
third_party     nlohmann/json (single header)
```

## Performance

Measured numbers, their methodology and the budgets are in [`docs/BENCHMARKS.md`](docs/BENCHMARKS.md). For example, on
the Godot source tree (8327 indexed files, 507k symbols; 4 cores, light load, "After Wave 1" there) the editor is
interactive after 1.9 s cold and 0.33 s warm, the fast index takes 1.2 s cold, a go-to-definition is answered in about
0.2 ms once clangd has parsed the file, and the editor process peaks at 374 MiB (clangd excluded). Stress results for pathological workspaces, the corpora, generated codebases and fuzzing are in
[`docs/STRESS_RESULTS.md`](docs/STRESS_RESULTS.md).

## Status

Done and tested: text model, navigation history, LSP client and clangd integration (definition, declaration, type
definition, implementation, references, hover, completion, signature help, document/workspace symbols, rename,
diagnostics, source/header switch, quick fixes and refactorings, call and type hierarchy, inlay hints), fast index, compile-command discovery and generation (including Visual Studio
solutions), workspace scanning and watching, editor model, CLI, the graphical editor (GPU text rendering, tabs, split
panels, 10x key bindings and command palette, pickers, find and replace, settings and themes), Windows support
(process spawning, Direct3D 11, ConPTY terminal), build integration, the Visual Studio debugger, Git and Perforce
integration, and packages with a bundled clangd.

Dropped by decision: scripting (the Python API is not used). Not done yet: Unreal Engine awareness, some 10x commands and settings, a scope-aware fast tier. See
[`docs/FEATURE_PARITY.md`](docs/FEATURE_PARITY.md) for the full list and
[`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md) section 9 for what is next.

More documentation: [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md), [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md),
[`docs/TESTING.md`](docs/TESTING.md), [`docs/OPEN_QUESTIONS.md`](docs/OPEN_QUESTIONS.md) (decisions) and
[`docs/VS_BEHAVIOUR.md`](docs/VS_BEHAVIOUR.md) (Visual Studio behaviour that tenfold follows).
