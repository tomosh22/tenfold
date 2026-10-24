# Tenfold implementation plan

**Audience.** An orchestrating agent (Opus) that will break this plan into
tasks for implementing agents (Sonnet), review their output, and integrate.
Everything an implementer needs is either in this file or in a file this
plan points at. Read `README.md`, `docs/ARCHITECTURE.md` and
`docs/FEATURE_PARITY.md` first; they describe what already exists.

**Product goal.** A C++ code editor with the behaviour and speed of the
10x editor (10xeditor.com), plus two fixes: (1) a Visual Studio-style
Navigate Backward/Forward history, and (2) code intelligence that is
IntelliSense-accurate (templates, `auto`, typedefs, macros, overloads) while
staying instant. Everything else should match 10x as closely as we can
observe it. 10x is closed source, so some behaviours must be verified
against a running copy by the user; those are flagged **VERIFY WITH USER**.

---

## 0. State of the repository (what is already done)

Location: `/home/user/tenfold` (not yet a git repository; run `git init`
and make an initial commit as the first orchestration step, after asking
the user whether they want a remote).

Build: CMake + Ninja, C++20, GCC 13 and Clang 18 both compile it clean with
`-Wall -Wextra`. `build-rel/` is a Release build, `build/` Debug,
`build-asan/` ASan+UBSan. All 32 tests pass in all three.

```
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build-rel
./build-rel/tenfold-tests          # optional filter arg: ./build-rel/tenfold-tests nav_
./build-rel/tenfold-cli examples/sample_project
```

| Module | Files | What it does | Tests |
|---|---|---|---|
| core | `src/core/Position.h`, `TextBuffer.{h,cpp}`, `Document.{h,cpp}` | UTF-8 line buffer, CRLF detection, `replace()` with line delta, UTF-16 column conversion, `wordAt`; Document = path + version + undo/redo + change listeners + file URIs | `tests/test_textbuffer.cpp` |
| nav | `src/nav/NavigationHistory.{h,cpp}` | Back/forward stack: record, far-caret-move detection, coalescing, live-position push on back, truncate-on-new-jump, edit/rename/remove tracking | `tests/test_navigation.cpp` (9 scenario tests; these define the semantics) |
| lsp | `src/lsp/Process.{h,cpp}` (POSIX), `JsonRpc.{h,cpp}`, `LspClient.{h,cpp}` | Child process with pipes + env; Content-Length framing, request/response/notification dispatch, server→client requests; typed clangd client (definition, declaration, typeDefinition, implementation, references, hover, completion, signatureHelp, documentSymbol, workspace/symbol, rename, switchSourceHeader, diagnostics, clangd fileStatus, background-index `$/progress`, UTF-8 offset negotiation) | integration via `tests/test_semantic.cpp` |
| semantic | `src/semantic/CppLexer.{h,cpp}`, `FastIndex.{h,cpp}`, `XmlLite.{h,cpp}`, `CompileCommands.{h,cpp}`, `SemanticEngine.{h,cpp}` | Lexer; heuristic symbol extractor + parallel fuzzy index + `#include` capture; XML parser; compile_commands discovery, CMake configure, `.sln`/`.vcxproj` evaluation (MSBuild properties, conditions, `.props` imports, `%(…)` merging) → `clang-cl` commands, include-dir inference by set cover, heuristic commands; two-tier engine with budgeted delivery and cold-start augmentation | `tests/test_fastindex.cpp`, `tests/test_compdb.cpp`, `tests/test_semantic.cpp` |
| workspace | `src/workspace/Workspace.{h,cpp}` | Root resolution (folder / .sln / CMakeLists), recursive scan with ignore rules, settings in `.tenfold/workspace.json`, `findExecutable` | `tests/test_semantic.cpp` (`workspace_scan_sample_project`) |
| app | `src/app/Editor.{h,cpp}` | UI-agnostic editor model: documents, caret, `gotoLocation`/`back`/`forward`, semantic commands with auto-jump on single result, editing at caret, main-thread queue (`post`/`pump`/`waitFor`), diagnostics store | `tests/test_semantic.cpp` |
| cli | `src/cli/main.cpp` | Headless reference front end; `compdb` subcommand | manual (`README.md` walkthrough) |
| examples | `examples/sample_project` | Templates, aliases, specialisations, `auto`, `decltype`, macro-generated members, overrides, operators; `// @MARK:name` comments are test anchors | used by tests |
| fixtures | `tests/fixtures/vs/` | `.sln` + `.vcxproj` + `.props` with conditions and per-file metadata | `tests/test_compdb.cpp` |

Measured on this machine (4 cores, Release build):

| Scenario | Result |
|---|---|
| Godot engine source (8755 files, 175 MB of C/C++), cold `.tenfold` | workspace scan 59 ms; fast index 8327 files / 507k symbols in 1.4 s; include dirs inferred; clangd launched; editor interactive at 2.4 s |
| Godot cold, open `scene/3d/node_3d.cpp` and go to definition | TU parsed 1.3 s after opening (3.7 s from launch); `def` answered in < 10 ms once parsed |
| Godot warm (second launch, cached database and clangd index) | interactive at 0.2 s; TU parsed at 1.8 s |
| sample_project, 12 go-to-definition cases that defeat heuristic parsers | all authoritative and correct (`semantic_clangd_resolves_what_the_fast_parser_cannot`) |
| Fast index throughput | ~130 MB/s on 4 threads |

Known limitations to carry forward:

* `Process.cpp` is POSIX only.
* Undo is linear and per-edit (no grouping of consecutive keystrokes).
* `TextBuffer` is a `std::vector<std::string>`; fine to hundreds of MB, not for multi-GB logs.
* The heuristic symbol extractor is intentionally approximate; do not spend effort perfecting it, clangd is the authority.
* In heuristic compile-command mode we write a clangd user config under `.tenfold/clangd-config` and point clangd at it via `XDG_CONFIG_HOME`; this hides the user's own clangd user config for that process (documented; `.clangd` at the workspace root still works).

---

## 1. Non-negotiable constraints for every task

1. **Engine stays UI-agnostic.** Nothing under `src/core`, `src/nav`,
   `src/lsp`, `src/semantic`, `src/workspace`, `src/app` may include a
   windowing, rendering or input header. Front ends live in `src/ui*`.
2. **Main-thread discipline.** Background results reach the `Editor` only via
   `Editor::post()`. Front ends call `Editor::pump()` once per frame.
3. **Every jump goes through `Editor::gotoLocation`** so it lands in the
   navigation history. Find-in-files results, build errors, bookmarks,
   symbol pickers: all of them.
4. **clangd is authoritative; the fast index is approximate.** Never show an
   approximate result as if it were final; the `Confidence` tag must survive
   to the UI (greyed text / "verifying…" hint, then replaced).
5. **Tests with every change.** Unit tests use the tiny framework in
   `tests/test_main.h` (`TEST`, `CHECK`, `CHECK_EQ`, `REQUIRE`). Add new test
   files to `CMakeLists.txt`. Integration tests that need clangd must skip
   with a message when it is missing (see `clangdAvailable()`).
6. **Style.** 2-space indent, 120 columns, `camelCase` methods,
   `PascalCase` types, trailing underscore for members, `#pragma once`,
   includes grouped std / third_party / project. No exceptions across
   module boundaries except `RpcError` inside `lsp`. No new third-party
   dependencies without an entry in this plan's "Dependencies" section.
7. **Performance budgets** (Release, 4 cores, Godot-sized workspace):
   startup to interactive ≤ 3 s cold / ≤ 1 s warm; symbol search ≤ 50 ms
   (fast tier); keystroke-to-paint ≤ 8 ms; memory for the editor process
   (excluding clangd) ≤ 500 MB. Add a benchmark when you touch a hot path.
8. **Do not fabricate 10x behaviour.** Where this plan says VERIFY WITH USER,
   implement the engine side behind an interface and ask the orchestrator to
   get the exact behaviour from the user before finishing the UI side.

---

## 2. Orchestration protocol

* **Branching.** `main` always builds and passes `tenfold-tests`. One branch
  per task (`task/<phase>-<id>-<slug>`). The orchestrator merges after review.
* **Task brief template** (hand this to each Sonnet agent verbatim, filled in):

  ```
  Repository: /home/user/tenfold (read README.md, docs/ARCHITECTURE.md, docs/IMPLEMENTATION_PLAN.md §<task id>)
  Task: <id> <title>
  Goal: <one paragraph>
  Touch only: <files/dirs>
  Interfaces you must keep or add: <signatures>
  Acceptance: <tests to add, commands that must pass, measurable targets>
  Out of scope: <explicit>
  Report back: summary, files changed, test output, open questions, anything you had to assume.
  ```
* **Review checklist** for the orchestrator before merging: constraints §1
  all respected; new code has tests; `ninja -C build-rel && ./build-rel/tenfold-tests`
  passes; ASan build (`build-asan`) passes for anything touching threads or
  buffers; no formatting churn in untouched files; docs updated if a public
  interface changed; benchmark numbers attached for hot paths.
* **Parallelism.** Tasks in the same phase marked ∥ are independent and can
  run concurrently. Tasks marked → depend on the one named.
* **When blocked on a product decision**, write the question into
  `docs/OPEN_QUESTIONS.md` with a proposed default, implement the default
  behind a setting, and continue.

---

## 3. Phase A: hardening and portability of the engine

Goal: the engine is production-grade and runs on Windows before any pixels
are drawn. Estimated size: 8 tasks, all ∥ unless noted.

### A1 ∥ Windows `Process`
* Files: `src/lsp/Process.cpp` (split into `Process_posix.cpp` and
  `Process_win32.cpp`, selected in CMake), `CMakeLists.txt`.
* Implement `start` with `CreateProcessW`, anonymous pipes (inheritable
  read/write ends as appropriate), `STARTF_USESTDHANDLES`, job object so the
  child dies with us, `CREATE_NO_WINDOW`; `read`/`write` blocking on the
  pipe handles; `terminate` = `TerminateProcess`; `wait` with
  `WaitForSingleObject`; env merging with the parent block (`extraEnv`).
* Acceptance: existing tests pass on Windows with `clangd.exe` on PATH
  (CI job in A7). Unit test that spawns `cmd /c echo hi` and reads it.

### A2 ∥ Windows paths and MSVC flags for clangd
* Files: `src/semantic/SemanticEngine.cpp`, `src/semantic/CompileCommands.cpp`,
  `src/workspace/Workspace.cpp`.
* `findExecutable` must try `.exe`/`.cmd`, `;` PATH separator, and the
  editor's own `bin/` directory (we ship clangd).
* For `.vcxproj` databases add `--query-driver=<path to cl.exe>/**` when a
  `cl.exe` can be found (`vswhere` output or `VCToolsInstallDir`), so MSVC
  system headers resolve. Store the discovered toolchain in
  `.tenfold/workspace.json`.
* `pathToUri` for UNC paths; case-insensitive path comparison helper used by
  `Editor::document()` and `NavigationHistory` on Windows.
* Acceptance: unit tests for URI/paths on both path styles (they can run on
  Linux using string inputs), test fixture with a `cl.exe`-style
  `AdditionalOptions`.

### A3 ∥ Incremental fast-index updates and file watching
* Files: new `src/workspace/FileWatcher.{h,cpp}` (inotify on Linux,
  `ReadDirectoryChangesW` on Windows, FSEvents stub on macOS), `Workspace`,
  `SemanticEngine`.
* Interface:
  ```cpp
  class FileWatcher { public: using Callback = std::function<void(Event)>; bool start(root, ignoreFn, cb); void stop(); };
  struct Event { enum {Created, Modified, Deleted, Renamed} kind; fs::path path, oldPath; };
  ```
* Wire: created/modified code file → `FastIndex::updateFile`; deleted →
  `removeFile` + `NavigationHistory::onFileRemoved`; renamed →
  `onFileRenamed`; `compile_commands.json` or `CMakeLists.txt` changed →
  re-run `prepareCompileCommands` and restart clangd (clangd re-reads the
  database itself; only restart when the *location* changed). Debounce 200 ms.
* Open documents whose file changed on disk: emit an `Editor` event
  (`onExternalChange(Document&)`); the UI decides reload/keep.
* Acceptance: test creates a temp workspace, modifies a header, and sees the
  symbol appear in `FastIndex::lookup` within 1 s.

### A4 ∥ Persisted fast index
* Files: `src/semantic/FastIndex.{h,cpp}`, new `FastIndexStore.{h,cpp}`.
* Serialize `FileSymbols` (path, mtime, size, symbols, includes) to
  `.tenfold/fastindex.bin` (simple length-prefixed binary; version header).
  On start load the store, re-lex only files whose mtime/size changed or
  that are new, drop missing files. Write back on shutdown and every 60 s.
* Acceptance: Godot warm start fast-index phase < 300 ms (benchmark command
  in `tools/bench.sh`, see A8); round-trip unit test.

### A5 ∥ Undo grouping, selections and multi-caret model (engine side)
* Files: `src/core/Document.{h,cpp}`, new `src/core/Selection.h`,
  `src/app/Editor.{h,cpp}`.
* `Document::beginGroup()/endGroup()` so consecutive typed characters undo
  as a word (group boundaries: whitespace typed, 1 s pause, caret moved by
  anything other than typing). VERIFY WITH USER: 10x's grouping rule.
* `Selection { Position anchor, head; }`; `Editor` holds
  `std::vector<Selection> carets` (primary first). All editing commands
  operate on all carets, applying edits bottom-to-top and re-mapping the
  other carets through `TextBuffer::EditResult`. Commands: `insertText`,
  `backspace`, `del`, `moveCarets(direction, by word/line/page, extend)`,
  `addCaretAbove/Below`, `addCaretAtNextOccurrence`, `collapseToPrimary`,
  `columnSelect(rect)`.
* Acceptance: unit tests for multi-caret insert/delete with overlapping and
  adjacent carets; undo of a grouped word.

### A6 ∥ Find in workspace (text search engine)
* Files: new `src/search/TextSearch.{h,cpp}`, `src/app/Editor`.
* Parallel search over `Workspace::codeFiles()` (+ optional other files)
  with options: case, whole word, regex (`std::regex` is too slow; vendor
  PCRE2 or implement literal + simple-regex fast path with memchr/SIMD
  friendly loop; see Dependencies), include/exclude globs, open-documents
  override (search buffer text, not disk). Streaming results via callback
  batches; cancellation token; result count cap.
* `Editor::findInWorkspace(query, opts, onBatch, onDone)`; selecting a result
  goes through `gotoLocation` (constraint §1.3).
* Acceptance: Godot literal search for a common identifier completes in
  < 400 ms Release; unit tests for options; cancellation test.

### A7 ∥ CI, formatting, sanitizers
* Files: `.github/workflows/ci.yml`, `.clang-format`, `CMakePresets.json`.
* Matrix: ubuntu (gcc, clang, asan), windows (MSVC 2022 and clang-cl),
  macos (AppleClang). Install clangd in each (pip `clangd` package works
  everywhere). Run `tenfold-tests`. `clang-format --dry-run` check on
  changed files only.
* Acceptance: green CI on `main`.

### A8 ∥ Benchmark harness
* Files: `tools/bench.sh`, `tools/bench_workspace.py`, new CLI command
  `bench` in `src/cli/main.cpp` that prints machine-readable timings for
  scan / fast index / compdb / clangd start / first `def` / warm `def` /
  `sym` latency.
* Target workspaces: `examples/sample_project` and a `--large <path>`
  (document how to fetch Godot or LLVM shallow clones).
* Acceptance: `tools/bench.sh` produces a Markdown table; numbers recorded
  in `docs/BENCHMARKS.md`.

### A9 → A5 Engine completion: remaining clangd features
* Files: `src/lsp/LspClient.{h,cpp}`, `src/semantic/SemanticEngine.{h,cpp}`.
* Add: `textDocument/semanticTokens/full` and `/delta` (with legend
  decoding to a `std::vector<TokenSpan{line, col, len, type, modifiers}>`),
  `textDocument/codeAction` + `workspace/executeCommand` (include fix-its,
  clangd tweaks), `textDocument/inlayHint`, `textDocument/foldingRange`,
  `textDocument/documentHighlight`, `callHierarchy/incomingCalls|outgoingCalls`,
  `typeHierarchy/supertypes|subtypes`, `textDocument/formatting` and
  `rangeFormatting`, `workspace/didChangeWatchedFiles` (from A3).
* `SemanticEngine` exposes each; results are byte-column converted like the
  others. Code actions apply through `Document::replace` within an undo
  group.
* Acceptance: integration tests on `sample_project` for semantic tokens
  (the `Vec2f` alias is a `type` token), a code action that adds a missing
  include, and call hierarchy of `Shape::area`.

---

## 4. Phase B: graphical front end (the shell)

Goal: a native, GPU-rendered editor window that looks and feels like 10x
and drives `Editor`. Design first, then implement in layers so tasks are
independently testable.

### Stack decision (made; do not relitigate without a reason)
* **Windowing/input:** SDL3 (cross-platform, high-DPI, IME, clipboard, drag
  and drop). Vendored as a git submodule or fetched by CMake `FetchContent`.
* **Rendering:** our own renderer behind `ui/gfx/Renderer.h`
  (`drawRect`, `drawGlyphRun`, `drawLine`, `scissor`, `present`). First
  backend OpenGL 3.3 core (works everywhere today); second backend D3D11 on
  Windows (B9) for the lowest input latency and flip-model presentation.
* **Text:** FreeType glyph rasterization into an atlas (R8 for grey AA, RGB
  for subpixel on Windows), monospace-first layout (no shaping), ligature
  support deferred. Fallback font chain for symbols/CJK.
* **UI toolkit:** none. A small retained widget layer of our own
  (`ui/widgets`), because 10x's look is bespoke and immediate-mode toolkits
  fight with text editors. Keep it minimal: Panel, Splitter, TabStrip,
  TextView, ListPopup, InputLine, StatusBar, Tooltip, Dialog.
* **Frame loop:** event-driven; render only when dirty or animating; cap at
  display refresh; one `Editor::pump()` per frame.

### B1 Platform and renderer foundation
* Files: `src/ui/platform/{Window,Input,Clipboard}.{h,cpp}`,
  `src/ui/gfx/{Renderer.h, GlRenderer.cpp, Atlas.{h,cpp}, Font.{h,cpp}}`,
  `src/ui_app/main.cpp` (opens a window and draws a coloured quad + the
  sample file as text), CMake option `TENFOLD_BUILD_UI`.
* Acceptance: 60 fps rendering of a 10k-line file's visible region with
  < 2 ms CPU per frame; headless screenshot test (`--screenshot out.png`
  renders one frame offscreen with an EGL/OSMesa context or a hidden window
  and exits) used by later golden-image tests.

### B2 → B1 TextView widget
* Files: `src/ui/widgets/TextView.{h,cpp}`, `src/ui/view/{Viewport,Layout}.{h,cpp}`.
* Responsibilities: map `Document` lines to glyph runs with tab expansion,
  wrap off by default (VERIFY WITH USER whether 10x wraps), caret(s) and
  selections, current-line highlight, gutter with line numbers, vertical
  and horizontal scrolling with smooth scroll (VERIFY WITH USER), mouse
  selection (click, double-click word, triple-click line, drag, Alt-drag
  column), scrollbar with diagnostics/search markers, 90-column guide
  (setting), whitespace rendering (setting), bracket match highlight.
* Colouring: lexical from `CppLexer` immediately, upgraded to clangd
  semantic tokens (A9) when they arrive, so colours never flicker: paint
  lexical colour first, semantic overrides only change identifier colours.
* Acceptance: golden screenshots for 3 scenarios (plain file, selection +
  multi-caret, diagnostics squiggles); input tests through a scripted
  event feed.

### B3 → B1 Layout shell: tabs, split panels, status bar
* Files: `src/ui/widgets/{TabStrip,Splitter,Panel,StatusBar}.{h,cpp}`,
  `src/ui/shell/Workbench.{h,cpp}`.
* 10x behaviours to copy (VERIFY WITH USER on each): tabs per panel, drag
  tab between panels, split vertical/horizontal, close others, pinned tabs,
  recently-used tab ordering for Ctrl+Tab, dirty marker, status bar showing
  line:col, encoding, line ending, clangd state ("indexing 43%"), fast-index
  state, current workspace.
* Acceptance: layout serialized to `.tenfold/layout.json` and restored;
  screenshot tests.

### B4 → B2 Command system and key bindings
* Files: `src/ui/commands/{Command,CommandRegistry,KeyMap}.{h,cpp}`,
  `src/ui/settings/KeyBindings.{h,cpp}`, default bindings file
  `assets/keybindings.default.txt`.
* Every user action is a named command (`Editor.GotoDefinition`,
  `Nav.Back`, `Nav.Forward`, `Search.FindInWorkspace`, …) with a human name
  and optional argument. Chords supported (`Ctrl+K Ctrl+C`). Bindings are a
  plain text file; parse errors reported in the status bar, never fatal.
* Defaults for the two headline features: `Alt+Left`/`Alt+Right` and
  `Ctrl+-`/`Ctrl+Shift+-` for Back/Forward; mouse buttons 4/5 (back/forward
  thumb buttons) also map to Back/Forward. `F12` go to definition,
  `Ctrl+F12` declaration, `Shift+F12` references, `Alt+O` switch
  header/source, `Ctrl+,` symbol search, `Ctrl+P`/`Ctrl+O` open file.
  VERIFY WITH USER: the user's actual 10x key map; offer an importer for
  `KeyMappings.10x_settings` once its grammar is known.
* Acceptance: unit tests for the binding parser and chord resolution.

### B5 → B2, B4 Pickers and popups
* Files: `src/ui/widgets/{ListPopup,InputLine}.{h,cpp}`,
  `src/ui/features/{SymbolSearch,FileSearch,FindInFiles,CompletionPopup,HoverTooltip,SignatureHelp,DiagnosticsPanel}.{h,cpp}`.
* Symbol search: types into `Editor::findSymbols`; shows approximate
  results instantly (greyed "verifying" badge), swaps to authoritative when
  they arrive, workspace symbols first, kind icons, scope in dim text,
  preview of the target line on the right (VERIFY WITH USER whether 10x
  previews). Enter jumps (through `gotoLocation`).
* File search: fuzzy over `Workspace` file list using
  `FastIndex::fuzzyScore` on path components; recently used first.
* Find in files: input with options, results list grouped by file with
  context line; Enter jumps; `F8`/`Shift+F8` next/prev result.
* Completion: triggered on identifier chars and `.`/`->`/`::` ; filters
  client-side on the current word; shows detail/return type; docs on the
  side; Tab/Enter accept; snippets off (we asked clangd for no snippets).
* Hover tooltip after 400 ms dwell; signature help on `(` and `,`.
* Diagnostics panel listing `Editor::diagnostics` for open files and the
  whole workspace (clangd only reports open files; note this in the UI).
* Acceptance: screenshot tests; scripted-input tests for the picker
  filtering and the approximate→authoritative swap.

### B6 → B3 Navigation UI for the back button
* Files: `src/ui/features/NavigationUi.{h,cpp}`, toolbar buttons.
* Back/Forward toolbar buttons with enabled state from
  `NavigationHistory::canGoBack/Forward`; long-press or right-click shows
  the stack (`entries()` with labels; label is the symbol name recorded by
  `Editor::gotoLocation`), clicking an entry jumps to it. Status bar shows
  "← 3 | 1 →" counts (setting). "Go to last edit location" command
  (track last edit in `Editor`, add `Editor::lastEditLocation()`).
* Acceptance: scripted test: def, def, back, back, forward → caret positions
  match the engine tests.

### B7 → B2 Settings and themes
* Files: `src/ui/settings/{Settings,Theme}.{h,cpp}`,
  `assets/themes/*.txt`, `assets/settings.default.txt`.
* Text-file settings (key = value, comments with `#`), hot-reloaded via the
  file watcher (A3). Theme = named colours for UI and token kinds (lexical
  and semantic-token types/modifiers). Ship a dark theme resembling 10x's
  default (VERIFY WITH USER: provide screenshots of their 10x to match
  palette and font).
* Acceptance: parser tests; theme switch without restart.

### B8 → B5 Editing conveniences
* Auto-indent (brace-based, continuation lines), auto-close brackets and
  quotes (setting), toggle line/block comment, duplicate line, move line
  up/down, join lines, go to line, bookmarks (toggle/next/prev; list), word
  wrap toggle, find/replace in file with incremental highlight and regex,
  trim trailing whitespace on save (setting), encoding/line ending switch.
  VERIFY WITH USER which of these 10x has and their exact behaviour.
* Acceptance: unit tests on the engine side (`Editor` commands), scripted
  tests for the UI.

### B9 → B1 D3D11 renderer backend (Windows)
* Files: `src/ui/gfx/D3d11Renderer.cpp`, `WindowsSwapChain`.
* Flip-model swap chain, waitable object for low latency, DirectWrite is
  **not** used (keep FreeType for identical rendering across platforms;
  revisit if ClearType parity is requested).
* Acceptance: identical golden screenshots to the GL backend within a
  tolerance.

---

## 5. Phase C: 10x behaviours beyond editing

Each item begins with a VERIFY WITH USER step; implement the engine side
behind interfaces so the UI can be adjusted when the facts arrive.

### C1 Build integration
* `src/build/{BuildRunner,ErrorParser}.{h,cpp}`: run a configurable
  command (MSBuild/devenv for `.sln`, `cmake --build`, make, custom) with
  streaming output into a panel; parse MSVC (`file(line,col): error Cxxxx:`),
  GCC/Clang (`file:line:col: error:`) and NMake/MSBuild wrappers; `F4`/
  `Shift+F4` jump through errors via `gotoLocation`; squiggles for build
  errors merged with clangd diagnostics (different colour).
* Per-workspace build configurations (Debug/Release, platform) shared with
  `CompileCommands` so clangd and the build agree.

### C2 Debugger
* VERIFY WITH USER how 10x debugs (launch Visual Studio / RemedyBG /
  integrated). Default plan: launch an external debugger with the current
  file/line, plus a Debug Adapter Protocol client (reuses `JsonRpc`
  transport patterns) for an integrated experience later.

### C3 Version control
* Git first (status badges in tabs/file tree, diff gutter marks, blame
  tooltip, checkout/revert file). Perforce: `p4 edit` on first edit of a
  read-only file (VERIFY WITH USER the exact 10x Perforce flows).

### C4 Scripting
* VERIFY WITH USER whether they rely on 10x's Python API. If yes: embed
  CPython, expose a `tenfold` module mirroring the commands/registry from B4
  plus editor queries; otherwise defer indefinitely.

### C5 Workspace extras
* Unreal Engine: detect `.uproject`, generate compile commands from the
  UBT-generated `compile_commands.json` or by invoking UBT's
  `-mode=GenerateClangDatabase`.
* Importers: 10x `.10x` workspace files and settings (VERIFY WITH USER by
  obtaining sample files).

---

## 6. Phase D: release engineering

* D1 Packaging: ship `clangd` (LLVM release binaries) next to the editor on
  all platforms; `SemanticEngine` prefers the bundled one unless
  `clangdPath` is set. Installer/zip for Windows, `.dmg`, AppImage.
* D2 Crash handling: minidumps on Windows, backtrace on POSIX; clangd
  crash → automatic restart with exponential backoff (hook
  `LspClient::onDisconnect`; re-send `didOpen` for open documents).
* D3 Telemetry: none by default. Local `.tenfold/log.txt` with rotation.
* D4 Docs: user guide (keys, settings, workspace files), troubleshooting
  compile commands ("why is this symbol approximate?") with a diagnostics
  command that prints the compile command clangd uses for the current file.

---

## 7. Dependencies (approved)

| Library | Use | Vendoring |
|---|---|---|
| nlohmann/json 3.11.3 | already vendored | `third_party/nlohmann` |
| SDL3 | windowing/input | FetchContent, pinned tag |
| FreeType 2.13 | glyph rasterization | FetchContent or system |
| PCRE2 (optional) | regex in find-in-files | FetchContent; literal path must not need it |
| LLVM clangd binary | semantic engine | downloaded at package time, not built |

Anything else requires a plan amendment.

---

## 8. Risk register

| Risk | Mitigation |
|---|---|
| clangd first-parse latency on huge TUs (Unreal, Godot) | fast tier covers the wait; pre-warm by `didOpen`ing the N most recently used files at startup; persisted background index; consider `--pch-storage=disk` setting for low-RAM machines |
| MSVC projects on Windows need `--query-driver` and the exact `cl.exe` | A2 toolchain discovery via `vswhere`; expose in settings; diagnostics command (D4) |
| Heuristic compile commands produce false diagnostics on exotic projects | surface "compile flags: heuristic" in the status bar; one-click "disable diagnostics for heuristic mode" setting |
| 10x behaviours misremembered | every VERIFY WITH USER item blocks UI polish, not engine work; keep `docs/OPEN_QUESTIONS.md` current |
| Rendering text identically across GL/D3D | shared glyph atlas and layout code; golden-image tests with tolerance |
| Multi-caret edits corrupting history/LSP sync | all edits through `Document::replace`; property test: random multi-caret edit sequences keep `TextBuffer::text()` consistent with a reference implementation and all navigation entries within bounds |

---

## 9. Suggested execution order and sizing

| Wave | Tasks | Notes |
|---|---|---|
| 1 | A1, A2, A3, A4, A6, A7, A8 (∥) | pure engine; 7 Sonnet agents in parallel; orchestrator merges, runs CI |
| 2 | A5, A9, B1 (∥) | B1 is the first UI code; A5 defines the caret model B2 renders |
| 3 | B2, B3, B4 (∥ after B1) | first usable window at the end of this wave |
| 4 | B5, B6, B7 (∥) | the editor is now better than the CLI for daily use |
| 5 | B8, B9, C1 | polish + build integration |
| 6 | C2–C5, D1–D4 | gated on user answers |

A Sonnet task should be at most ~800 lines of new code plus tests; split
anything larger (B2 and B5 will each need two or three sub-tasks; the
orchestrator should split them along the file boundaries listed).

---

## 10. Definition of done for v1.0

* All Phase A and B tasks merged, CI green on Linux/Windows/macOS.
* `docs/FEATURE_PARITY.md` has no ⬜ rows in "The two fixes", "Workspace and
  files", "Code intelligence", "Editing", and "Shell" except those the user
  explicitly deferred.
* Benchmarks (A8) within the budgets in §1.7 on the reference machine.
* The user has used it for a week on their real codebase and the
  approximate→authoritative swap is never wrong in a way they notice
  (track reports in `docs/OPEN_QUESTIONS.md`).

---

## Benchmarks (keep updated via A8)

Current numbers, methodology and per-budget pass/fail live in `docs/BENCHMARKS.md`
(run `tools/bench.sh`). The table below is the original historical baseline, kept as is.

Reference machine: 4 cores, 15 GB RAM, Linux, GCC 13 Release build, clangd 22.

| Workspace | Launch → interactive | Launch → first TU parsed | `def` after parse | Fast index |
|---|---|---|---|---|
| `examples/sample_project` (3 files) | 0.09 s | ~1.5 s (`<memory>`, `<vector>` preamble) | < 10 ms | 2 ms |
| Godot, cold `.tenfold` (8755 files) | 2.4 s | 3.7 s | < 10 ms | 1.4 s, 507k symbols |
| Godot, warm | 0.2 s | 1.8 s | < 10 ms | 1.4 s (A4 will make this ~0.3 s) |

Observation worth preserving: Godot in heuristic compile-flag mode reports
`core/extension/gdextension_interface.gen.h: file not found` because that
header is generated by its build. Types derived from it degrade and clangd
falls back to index-based lookup for those symbols (two candidates for
`get_global_transform` instead of one). The fix is the real compile
database (Godot's SCons can emit one); the UI must make the "heuristic
flags" state visible so users know why (see risk register and D4).
