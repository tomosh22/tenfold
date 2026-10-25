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

This repository contains the **engine and a headless front end**. The
graphical front end (GPU text rendering, tabs, panels, key bindings) is the
next milestone; everything here is UI-agnostic so it can sit on top without
changes. See `docs/ARCHITECTURE.md` and `docs/FEATURE_PARITY.md`.

## Build and test

Requirements: a C++20 compiler (GCC 13 / Clang 18 tested), CMake 3.20+,
and `clangd` on `PATH` (or pass `--clangd <path>`). Without clangd everything
still works in approximate mode.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
./build/tenfold-tests            # 31 tests, including clangd integration
```

CI / presets: `CMakePresets.json` mirrors the CI matrix, so
`cmake --preset release && cmake --build --preset release && ctest --preset release`
works locally (also `debug`, `asan`, and on Windows `msvc` / `clang-cl` from a VS developer prompt).
GitHub Actions (`.github/workflows/ci.yml`) builds gcc, clang, clang ASan+UBSan, MSVC,
clang-cl and AppleClang, and runs `clang-format --dry-run --Werror` on changed C++ files only.

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

## UI (experimental)

The SDL3 + OpenGL 3.3 front end is opt-in and not part of the default build:

```
cmake -S . -B build-ui -G Ninja -DCMAKE_BUILD_TYPE=Release -DTENFOLD_BUILD_UI=ON
ninja -C build-ui
xvfb-run -a ./build-ui/tenfold-ui-tests          # golden-image tests skip without a display
./build-ui/tenfold-ui [workspace]                # or: xvfb-run -a ./build-ui/tenfold-ui --screenshot out.png [--size WxH]
```

SDL3 is fetched (static, pinned to `release-3.2.30`); on Linux you need X11/GL dev packages. After an
intentional rendering change, refresh `tests/golden/b1_scene.png` with `tenfold-ui-tests --update-goldens`.

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

Build with the UI enabled (SDL3 is fetched, needs FreeType and an OpenGL 3.3 context) and open a workspace:

```sh
cmake -S . -B build-ui -G Ninja -DCMAKE_BUILD_TYPE=Release -DTENFOLD_BUILD_UI=ON && ninja -C build-ui
./build-ui/tenfold-ui examples/sample_project            # restores <workspace>/.tenfold/layout.json
./build-ui/tenfold-ui examples/sample_project src/geometry.cpp
```

Tabs, split panels (drag a tab to a panel edge) and the status bar are the Workbench; each tab is a text view. Main keys
(placeholders until the 10x key map is known; `assets/keybindings.default.txt`, overridable in
`<workspace>/.tenfold/keybindings.txt` and the user config dir; parse errors show in the status bar):

| Keys | Action |
| --- | --- |
| Ctrl+Tab / Ctrl+Shift+Tab | cycle recent tabs |
| Alt+Left / Alt+Right, Ctrl+-, mouse back/forward | navigation history back / forward |
| F12, Ctrl+F12, Shift+F12, Alt+O | definition, declaration, references, header/source |
| Ctrl+S, Ctrl+Z / Ctrl+Y, Ctrl+A | save, undo / redo, select all |
| Ctrl+C / X / V | copy, cut, paste (several carets: one line per caret) |
| Ctrl+Alt+Up / Down, Ctrl+D, Ctrl-click, Alt-drag | more carets, next occurrence, column select |
| Alt+1 .. 4 | focus panel |
| Ctrl+P, Ctrl+, Ctrl+Shift+F | file / symbol / workspace pickers: not available yet |
| Ctrl+Q | quit (Escape collapses carets, it does not quit) |

`tenfold-ui --shell` is the widget demo and `--demo` the renderer scene; `--screenshot out.png [--size WxH]` renders one
frame headless.

Generate a `compile_commands.json` from a Visual Studio solution without
running the editor:

```sh
./build/tenfold-cli compdb path/to/Game.sln --config Debug --platform x64 -o compile_commands.json
```

## What is where

```
src/core        TextBuffer (UTF-8 lines, CRLF aware), Document (undo/redo, listeners, URIs)
src/nav         NavigationHistory: back/forward with coalescing and edit tracking
src/lsp         Process, JSON-RPC framing, typed LspClient for clangd
src/semantic    CppLexer, FastIndex (heuristic symbols + fuzzy search),
                CompileCommands (compile_commands.json discovery; CMake, .sln/.vcxproj,
                heuristic generators; MSBuild property/condition evaluation),
                SemanticEngine (two-tier query policy)
src/workspace   Workspace scanning, settings (.tenfold/workspace.json)
src/app         Editor: documents + caret + navigation + semantic commands + main-thread queue
src/cli         tenfold-cli, the headless reference front end
tests           unit + integration tests (tiny self-contained framework)
examples        sample project used by the tests
docs            architecture and 10x feature-parity notes
third_party     nlohmann/json (single header)
```

## Numbers

Release build, 4 cores. Godot engine source tree (8755 files, 175 MB):
workspace scan 59 ms, fast index 1.4 s (507k symbols), editor interactive
2.4 s after a cold launch and 0.2 s warm; clangd has the opened translation
unit parsed 1.3 s later and answers go-to-definition in under 10 ms from
then on. Full details and caveats in `docs/IMPLEMENTATION_PLAN.md`.

## Status

Done and tested: text model, navigation history, LSP client, clangd
integration (definition, declaration, type definition, implementation,
references, hover, completion, signature help, document/workspace symbols,
rename, diagnostics, source/header switch), fast index, compile-command
discovery and generation, workspace scanning, editor model, CLI.

Not yet done: the graphical front end, Windows process spawning
(`src/lsp/Process.cpp` is POSIX; the interface is ready for a CreateProcess
implementation), build/debug/VCS integration, scripting. See
`docs/FEATURE_PARITY.md` for the full list and plan.
