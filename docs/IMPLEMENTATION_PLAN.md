# Tenfold implementation plan

**Audience.** An orchestrating agent (Opus) that will break this plan into
tasks for implementing agents (Sonnet), review their output, and integrate.
Everything an implementer needs is either in this file or in a file this
plan points at. Read `README.md`, `docs/ARCHITECTURE.md` and
`docs/FEATURE_PARITY.md` first; they describe what already exists.

**Status (2026-11).** This file started as the plan handed to the orchestrating agent. Waves 1 to 10 are done (§9);
§0 is the current state, and the task briefs in §3 to §6 are history. The only open work is the "next" row of §9.

**Product goal.** A C++ code editor with the behaviour and speed of the
10x editor (10xeditor.com), plus two fixes: (1) a Visual Studio-style
Navigate Backward/Forward history, and (2) code intelligence that is
IntelliSense-accurate (templates, `auto`, typedefs, macros, overloads) while
staying instant. Everything else should match 10x as closely as we can
observe it. 10x is closed source, so some behaviours must be verified
against a running copy by the user; those are flagged **VERIFY WITH USER**.

---

## 0. State of the repository (current)

Location: `github.com/tomosh22/tenfold`, built with CMake (C++20; presets in `CMakePresets.json`; GCC, Clang, MSVC,
clang-cl and AppleClang are in CI, `.github/workflows/ci.yml`). Phases A to D and the 10x commands of W8 are done and
merged; waves 1 to 10 of §9 are done except what the last row ("next") lists. The sections of this file below §1 are the
original task briefs and are kept as history: they say what each task was meant to do and why, not what the code looks
like today; for the current design read `docs/ARCHITECTURE.md`, for what matches 10x and Visual Studio
`docs/FEATURE_PARITY.md`, for decisions `docs/OPEN_QUESTIONS.md` and `docs/VS_BEHAVIOUR.md`.

| Part | Where | State |
|---|---|---|
| Engine (text model, navigation history, LSP client, two-tier semantic engine, compile commands, workspace, search, build, debug, VCS, terminal) | `src/core`, `src/nav`, `src/lsp`, `src/semantic`, `src/workspace`, `src/search`, `src/build`, `src/debug`, `src/vcs`, `src/term`, `src/app` | done; Windows process spawning, ConPTY and the Visual Studio (EnvDTE) debugger backend are in |
| Headless front end | `src/cli` (`tenfold-cli`: REPL, `compdb`, `why`, `bench`, `corpus`, ...) | done |
| Graphical editor | `src/ui`, `src/ui_app` (`tenfold-ui`; SDL3, OpenGL 3.3 or Direct3D 11, FreeType) | done; opt-in at configure time (`-DTENFOLD_BUILD_UI=ON`), always built for packages |
| Packages | CI job `package-*`: Windows `.zip`, Linux `.tar.gz` / AppImage, macOS (best effort), pinned clangd bundled | done |
| Tests | `tenfold-tests` 718, `tenfold-ui-tests` 649, `tenfold-text-tests` 30, `tenfold-corpus-tests` 3 (over about 35 corpora in `tests/corpus`) on Linux; fuzz targets in `tests/fuzz` | see `docs/TESTING.md` |
| Scripting (C4) | not built | dropped by the user (OPEN_QUESTIONS #11) |

Performance numbers: `docs/BENCHMARKS.md`; stress results: `docs/STRESS_RESULTS.md`. The table in the last section of
this file is the original baseline (historical).

Known limitations (current):

* The heuristic symbol extractor of the fast tier is intentionally approximate; clangd is the authority. Its first
  answer is scope-aware (the caret's namespace / class chain, receiver types, file-local entities, argument count, the
  include graph; `docs/ARCHITECTURE.md`, "Scope-aware lookup in the fast tier"), follows `#if` / `#ifdef` / `#elif`
  branches by the compile command's defines and the unit's own `#define`s ("Preprocessor conditionals in the fast
  tier") and agrees with clangd's final answer in about 81% of the `def` / `decl` lines of the corpora
  (`docs/STRESS_RESULTS.md`, "W11-FAST-SCOPE" and "W12-FAST-PP"); the rest is overloads by parameter type, explicit
  specialisations, alias chains, operator tokens and macro-declared names.
* `TextBuffer` is a vector of lines (fine to hundreds of MB, not for multi-GB logs); lines of 500 kB or more are slow to
  highlight (see §9, "next").
* In heuristic compile-command mode on Linux the include directories shared by every file go into a clangd user config
  under `.tenfold/clangd-config`, and clangd is pointed at it via `XDG_CONFIG_HOME`; this hides the user's own clangd user
  config for that process (`.clangd` at the workspace root still works). On macOS and Windows the flags stay in the
  commands (`SemanticEngine.cpp`, `kFlagsViaConfig`).

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

## 2. Orchestration protocol (how the work was run)

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

## 3. Phase A: hardening and portability of the engine (historical briefs, done)

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

## 4. Phase B: graphical front end (the shell) (historical briefs, done)

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
  `assets/KeyMappings.default.10x_settings` (10x format and defaults, B4b).
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

### B2c → B2 Soft word wrap (OPEN_QUESTIONS #4 answered)
* Files: `src/ui/widgets/TextView.{h,cpp}`, `src/ui/view/{Viewport,Layout}.{h,cpp}` (a new
  `src/ui/view/WrapLayout.{h,cpp}` for the line ↔ visual-row map), `src/ui/view/Scrollbar.*`,
  `assets/settings.default.txt`.
* Wrap at the view width on word boundaries (break inside a word only when it does not fit a row), continuation
  rows indented like the first row's leading whitespace up to half the width; gutter number on the first row only.
  Caret Up/Down move by visual row keeping the x position, Home/End go to the row's start/end first, then the
  line's; mouse hit-testing, selection painting, squiggles, find markers and the scrollbar work in visual rows.
  `editor.wordWrap` defaults to **true**, `editor.smoothScroll` to **false**; toggle command `View.ToggleWordWrap`.
* Performance: wrap data computed lazily for visible lines plus an estimate for the rest (no full-document pass
  per keystroke); keystroke-to-paint ≤ 8 ms on the 100k-line and 50 MB single-line benchmarks.
* Acceptance: unit tests for the wrap layout (word boundary, long word, tabs, CJK double width, indent of
  continuation rows), scripted caret-movement tests, one golden with wrap on; existing goldens unchanged (they pin
  wrap off where the scenario requires it).

### B7b → B7 Visual Studio dark theme (OPEN_QUESTIONS #7 answered)
* Files: `assets/themes/vs-dark.txt` (new), `assets/settings.default.txt`, `src/ui/settings/Theme.{h,cpp}`,
  widget paint code only where a VS look needs a new theme key (tab accent line, status bar), goldens that use the
  application default theme.
* Palette: Visual Studio 2022 "Dark" for editor text (C++ token colours, current line, selection, line numbers,
  squiggles) and IDE chrome (tabs, document well, status bar, scrollbars, popups). Font fallbacks Cascadia Mono,
  Consolas at 10 pt (VS default). `ui.theme = vs-dark` becomes the default; `dark`/`light` stay available.
* Acceptance: theme parser test covering every key; golden for the default theme; no hard-coded colours added.

### B4b → B4 10x key bindings (OPEN_QUESTIONS #1 answered, blocked on material)
* Read 10x's key bindings settings file format and ship 10x's default bindings as the defaults, mapping 10x
  command names onto tenfold commands. Blocked until the default file and command list are available (no
  bindings from memory).

---

## 5. Phase C: 10x behaviours beyond editing (historical briefs, done except W9i; C4 dropped)

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

### C2 Debugger: Visual Studio integration (OPEN_QUESTIONS #9 answered)
Strong integration with the Visual Studio debugger: tenfold drives a Visual Studio instance through its automation
interface (EnvDTE over COM), so debugging happens in VS while the code stays open in tenfold.
* **C2a ∥ engine model.** `src/debug/{Breakpoints,Debugger,DebugSession}.{h,cpp}`: breakpoint store (per file and
  line, enabled flag, condition text; moves with edits like `Bookmarks`; persisted in `.tenfold/breakpoints.json`),
  the abstract `Debugger` backend interface (start / start without debugging / stop / restart / break / step over /
  into / out / run to cursor / set and remove breakpoints / query mode, current statement and breakpoints), a
  `DebugSession` on the `Editor` that owns a backend, runs it on its own worker thread and posts state changes
  (mode, current statement, breakpoints changed on the backend side) via `Editor::post()`, and a `FakeDebugger` for
  tests. The current statement is shown, and in break mode the editor jumps to it through `gotoLocation`.
* **C2b → C2a Visual Studio backend (Windows).** `src/debug/VsDebugger_win32.cpp`: late-bound `IDispatch` EnvDTE
  (no `#import`, builds with MSVC, clang-cl and mingw) on an STA worker thread; finds the running `devenv` whose
  `Solution.FullName` is the workspace solution through the Running Object Table, else launches `devenv <sln>` and
  waits for it to register; maps every `Debugger` call onto `DTE.Debugger` / `Breakpoints`; polls mode, current
  stack frame and the VS breakpoint list so breakpoints set in VS appear in tenfold; "Open in Visual Studio" at the
  caret. Integration test behind `TENFOLD_VS_DTE_TEST=1`, run by a non-blocking `windows-vs-debugger` CI job.
* **C2c → C2a UI.** Breakpoint glyphs in the gutter (click to toggle), current-statement arrow and line colour,
  `Debug.*` commands with Visual Studio's default keys (F5, Ctrl+F5, Shift+F5, Ctrl+Shift+F5, F9, F10, F11,
  Shift+F11, Ctrl+F10, Ctrl+Alt+Break), debugger state in the status bar, breakpoint list popup.

### C3 Version control (OPEN_QUESTIONS #10 answered)
* **C3a ∥ engine: Git and line diff.** `src/vcs/{Vcs,VcsManager,GitProvider,LineDiff}.{h,cpp}`: provider
  interface (detect, file status, base text, revert), Git via the `git` CLI (`status --porcelain=v2 -z`,
  `show HEAD:<path>`, `restore`/`checkout --`), a Myers line diff between base text and the live buffer giving
  added / modified / deleted hunks, recomputed off the main thread after edits (debounced) and posted back;
  per-file status for tabs; revert file and revert hunk through `Document::replace` (one undo step).
* **C3b → C3a Perforce.** `src/vcs/PerforceProvider.{h,cpp}`: detection (`p4 -ztag info` / `p4 where`, P4CONFIG),
  `fstat` status, base text from `p4 print -q <file>#have`, revert, and **auto-checkout**: the first edit of a
  read-only file under a Perforce client runs `p4 edit` asynchronously and makes the file writable (failure is
  reported, the edit is kept). Tests use a fake `p4` script on `PATH`.
* **C3c → C3a UI.** Diff marks in the gutter and scrollbar, inline diff of a hunk (deleted base lines shown as a
  read-only block under the change), revert hunk / revert file commands, VCS status in tabs and status bar,
  "Diff in p4merge" (or the configured external diff tool, `vcs.diffTool`) launching it on the base and the
  current text.

### C4 Scripting
* Dropped (OPEN_QUESTIONS #11: the Python API is not used). Do not implement.

### C5 ∥ Workspace settings from the Visual Studio solution (OPEN_QUESTIONS #12 answered)
* No `.10x` import. The workspace's settings come from its `.sln`: complete the `.sln`/`.vcxproj`/`.props`
  evaluation in `src/semantic/CompileCommands.cpp` so every compile command carries what MSBuild would pass to
  `cl.exe`: solution configuration → project configuration mapping (`ProjectConfigurationPlatforms`), defines and
  undefines, forced includes, include paths (also external/system include dirs), language standard (C and C++),
  conformance/permissive, exceptions, RTTI, character set (`UNICODE`/`_MBCS`), runtime library (`_DEBUG`,
  `_DLL`, `_MT`), per-file metadata overrides and `ExcludedFromBuild`, `Directory.Build.props/targets`, property
  sheets. Active solution configuration/platform selectable (workspace setting, command), default Debug|x64 or the
  first solution configuration. Unreal Engine `.uproject` support stays out of scope.

### W8 10x commands not yet implemented (after B4b)
B4b bound all 183 of 10x's default key bindings; 88 commands behind them are not implemented (table:
docs/FEATURE_PARITY.md, "10x commands not yet implemented"; source: the 10x documentation only). Each task implements its
group as real commands, sets the alias targets in `src/ui/commands/CommandNames.cpp` and adds tests; behaviour follows the
10x one-line descriptions, anything they leave open goes to OPEN_QUESTIONS with a default.
* **W8a ∥ editing commands:** SaveAll, CutLine, MakeSelectionUppercase / Lowercase, Commentline / Uncommentline,
  ToggleComment with CommentBlockUsesLineComments, DeleteToEndOfLine, InsertLine / InsertLineAfter, SelectCurrentWord,
  SelectCurrentScope, SelectCurrentBracketScope, MoveToMatchingBracket, ToggleWhitespace, ScrollOneLineUp / Down,
  FindInFilePrev/NextCurrentWord.
* **W8b ∥ tabs, panels, view:** Prev/NextPanelTab, MovePrev/NextPanelTab, MovePanel{Left,Right,Up,Down},
  MovePanelFocus{Left,Right,Up,Down}, DuplicatePanel(Left/Right), ZoomIn / ZoomOut, ToggleFullscreen, ToggleFocusMode,
  ToggleFullScreenFocusMode, NewTextFile (untitled documents, DefaultLineEnding), OpenFolderForActivefile,
  InternetSearch (InternetSearchCommand).
* **W8c ∥ parser commands (clangd):** RenameSymbol / SubmitRenameSymbol, Autocomplete, ShowFunctionArgsInfo,
  ShowSymbolInfo, FindFunction, GotoSymbolDefinitionUnderMouse, FormatSelection, InsertPrev/NextAutocomplete.
* **W8d → W8a multi-cursor and navigation:** UndoSelectNextMultiCursor, SelectAllOccurrences, AlignMultiCursor,
  MultiCursorMoveLeft / Right, AddCursorAtMousePos, Prev/NextLocationCurrentFile, RestoreRecentlyClosedFile,
  bookmark-in-file variants, RemoveAllBookmarksFromFile, RemoveAllBreakpointsFromFile, GotoNext/PrevResult over the last
  search or build.
* **W8e → W8a build and search:** RebuildActiveWorkspace, BuildCurrentFile, ReplaceInFiles, ReplaceNextInFile,
  ReplaceAllInFile, NewSearchPanel.
* **W8f → W8b larger features (one task each):** code folding (CollapseRegions group), workspace explorer tree
  (ToggleWorkspaceExplorer, ShowInWorkspaceTree), paste history, key-sequence recording / playback, Perforce history and
  ShowInP4v, terminal panel (NewTerminal, ShowTerminal), context menus (ShowContextMenu).
* **W8 status (all merged):** W8a-W8f are on main. All 183 default bindings run a tenfold command (W12-CMD made the four autocomplete list keys and
  InsertPrev/NextAutocomplete real commands; the latter from 10x's changelog, FEATURE_PARITY). W8f was split into folding,
  explorer tree, misc (paste history, key sequences, Perforce history / p4v), context menus (+ explorer menu), terminal
  engine and terminal panel. Windows-only paths still unverified on real Windows: ConPTY (CI tests), the Visual Studio
  configuration lookup (CI-W1 added retries and diagnostics), p4v / filelog against a real server (OPEN_QUESTIONS #17).

---

### W9 Extreme stress test: varied codebases (user request, after W8)

Goal: put tenfold's code intelligence, indexing, editing and UI through an extensive suite of widely varying codebases
(hand-written hard cases with known answers, generated pathological workspaces, fuzzed inputs, large generated
projects with known answers; real-world projects were dropped by the user) and fix what breaks. Every finding becomes a regression test. Correctness is judged against expected answers
written in the corpus (or by the generator); the fast tier is measured
by how often its first answer agrees with the final (authoritative) one.

**Corpus expectation format (shared by W9a and W9b-e).** A corpus project is a directory under `tests/corpus/<name>/`
with its sources (and its own `compile_commands.json`, `.sln` / `.vcxproj` or nothing, as the case requires) and a
`tenfold-expect.txt` at its root.
* *Anchors* are written in the source as a comment immediately before a token: `/*@name*/token`. The anchor's location
  is the first character of `token` (the token after the comment, on the same line). Names are `[A-Za-z0-9_.]+`, unique
  per project. Anchors must not change what the code means (they are comments).
* *Expectations*, one per line in `tenfold-expect.txt` (`#` comments, blank lines ignored):
  * `def A -> B` go to definition at A lands on anchor B (`-> B | C` = any of; `-> none` = no result).
  * `decl A -> B`, `typedef A -> B` (go to type definition), `impl A -> B C ...` (implementations, as a set).
  * `refs A = B C D ...` find references at A returns exactly these anchors (set; include the declaration only if listed);
    `refs A >= B C` = at least these.
  * `hover A ~ "text"` the hover contains the text. `sym "query" -> B` workspace symbol search ranks B first;
    `sym "query" ∋ B` = B is among the results.
  * `rename A newName` then the following indented lines `  B C ...` list every anchor whose token must have become
    `newName` (the harness renames on a scratch copy and checks).
  * `fast <expectation>` the same expectation for the fast tier only (before clangd answers); without `fast`, an
    expectation is checked on the final answer. `xfail <expectation> # reason` documents a known wrong answer (reported,
    not failed; it fails when it starts passing, so it gets promoted).
  * `config Name|Platform` switches the active Visual Studio configuration for the expectations that follow.
  * `open <file>` / `edit <file>:<line>:<col> "text"` edits an open document before the following expectations
    (tests answers on unsaved text).
* The runner (`tenfold-cli corpus <dir>` and the `tenfold-corpus-tests` target) prints one line per expectation and a
  JSON report: pass / fail / xfail counts, fast-tier agreement, latency per query kind (fast, final), time to first TU,
  indexing time, peak RSS.

* **W9a** corpus harness: the format above, the runner, the JSON report, a seed corpus (the cases of
  `examples/sample_project`), CI job running every corpus on every push.
* **W9b-W9e** (after W9a) hand-written hard-case corpora, one agent per area: (b) templates, overloads, specialisations,
  C++20 concepts / constraints, CTAD, variadic packs, operators, conversions; (c) preprocessor: macros that define
  symbols, token pasting, X-macros, include tricks, `#if` on Visual Studio configurations, force-included headers,
  same header compiled differently; (d) name lookup and OOP: namespaces, ADL, using-declarations / directives, inline
  and anonymous namespaces, nested / local classes, friends, virtual / override / final, CRTP, multiple and virtual
  inheritance, lambdas, ctors / dtors / conversions; (e) C and odd layouts: C99 / C11 projects, K&R definitions,
  function pointers, extern "C", unity builds, headers without extension, `.inl` / `.ipp`, duplicate file names in
  different folders, Unicode / spaces in paths, CRLF / BOM / Latin-1 files, symlinks.
* **W9f** pathological generated workspaces (a generator script plus tests): 100k-file trees, 1M-line files, 100k-char
  lines, minified one-line files, deep nesting, huge identifiers, binary / invalid UTF-8 files, symlink loops, files
  changing / deleted during indexing, truncated and unbalanced code. Nothing may crash, hang, or exceed the memory
  budget; the UI must stay responsive (frame budget) while indexing them.
* **W9g** (after W9a; replaces real-world projects, which the user dropped: no third-party code is fetched) large
  synthetic codebases from a generator that knows the ground truth: realistic, varied C++ projects of 1k-20k files
  (module structure, headers / sources, classes, templates, namespaces, macros, cross-file calls, a `.sln` /
  `.vcxproj` variant and a CMake / compile_commands variant), emitted with anchors and a `tenfold-expect.txt` in the
  W9a format by the generator itself (it knows every definition and use), plus a final-vs-clangd comparison mode.
  Small sizes run in CI, large sizes in a scheduled workflow.
* **W9h** fuzzing: mutation fuzzing (seeded from the corpora) of the fast lexer / symbol extractor, the `.sln` /
  `.vcxproj` / `.props` evaluator, the compile-database reader, the 10x settings / key-mapping parsers, the build
  output parser, the diff engine and the terminal parser, under ASan; every crash input is kept as a regression test.
* **W9i** (after W9g) editor-level stress on the largest generated projects: scripted UI sessions (hundreds of tabs, find /
  replace in files across the largest project, 10k carets, folding a huge file, the terminal flooding while indexing,
  switching configuration mid-index) measured against the frame budget and memory.
* Findings are fixed in follow-up tasks (one per root cause), each with the corpus entry or input that exposed it.

## 6. Phase D: release engineering (historical briefs, done: D1 packages, D2 crash handling and clangd restart, D3 local log, D4 user guide)

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

## 8. Risk register (as written at the start)

| Risk | Mitigation |
|---|---|
| clangd first-parse latency on huge TUs (Unreal, Godot) | fast tier covers the wait; pre-warm by `didOpen`ing the N most recently used files at startup; persisted background index; consider `--pch-storage=disk` setting for low-RAM machines |
| MSVC projects on Windows need `--query-driver` and the exact `cl.exe` | A2 toolchain discovery via `vswhere`; expose in settings; diagnostics command (D4) |
| Heuristic compile commands produce false diagnostics on exotic projects | surface "compile flags: heuristic" in the status bar; one-click "disable diagnostics for heuristic mode" setting |
| 10x behaviours misremembered | every VERIFY WITH USER item blocks UI polish, not engine work; keep `docs/OPEN_QUESTIONS.md` current |
| Rendering text identically across GL/D3D | shared glyph atlas and layout code; golden-image tests with tolerance |
| Multi-caret edits corrupting history/LSP sync | all edits through `Document::replace`; property test: random multi-caret edit sequences keep `TextBuffer::text()` consistent with a reference implementation and all navigation entries within bounds |

---

## 9. Execution order and sizing (waves 1 to 10 done except as marked; "next" is open)

| Wave | Tasks | Notes |
|---|---|---|
| 1 | A1, A2, A3, A4, A6, A7, A8 (∥) | pure engine; 7 Sonnet agents in parallel; orchestrator merges, runs CI |
| 2 | A5, A9, B1 (∥) | B1 is the first UI code; A5 defines the caret model B2 renders |
| 3 | B2, B3, B4 (∥ after B1) | first usable window at the end of this wave |
| 4 | B5, B6, B7 (∥) | the editor is now better than the CLI for daily use |
| 5 | B8, B9, C1 | polish + build integration |
| 6 | B2c, B7b, C2a, C3a, C5, D2 (∥) | user answers to #4, #7, #9, #10, #12 received |
| 7 | C2b, C2c (→ C2a), C3b, C3c (→ C3a), D1, D3 | D4 (user docs) last, after 7 |
| 8 | B4b, B8b, C2d; then W8a, W8b, W8c (∥), W8d, W8e, then W8f | 10x documentation available (key bindings, settings, VS sync) |
| 9 | W9a, W9f, W9h (∥); then W9b-W9e, W9g (∥, → W9a), W9i (→ W9g); then fixes | extreme stress test on varied codebases. Done: W9a-W9h and the W9-FIX tasks (fast index, encodings, translation-unit context, Visual Studio configurations, harness). **Not done: W9i** (editor-level stress) |
| 10 | W10-ENC, W10-CHOOSE, W10-LE, vs-research; then VS-TERM, VS-TABS, W10-THEME, W10-OUTLINE, VS-ENC, VS-TREE, VS-EDIT, VS-NAV, VS-VCS (∥); CI-W3, CI-W4, CHURN-2; LE-UNICODE, LE-DIALOG | user decisions of 2026-11-06 (OPEN_QUESTIONS, "User decisions of 2026-11-06"): refuse to save characters the encoding cannot hold (W10-ENC), a chooser for go to definition on aliases, using-declarations and constructor calls (W10-CHOOSE), line endings exactly like Visual Studio (W10-LE, LE-UNICODE with NEL / LS / PS as line breaks, LE-DIALOG the inconsistent line endings dialog) and everything else like Visual Studio. `docs/VS_BEHAVIOUR.md` is the research (with sources) behind: terminal keys (VS-TERM), tab well (VS-TABS), theme colours (W10-THEME), outlining (W10-OUTLINE), encodings in the status bar (VS-ENC), Solution Explorer (VS-TREE), editing options (VS-EDIT), navigation (VS-NAV) and version-control marks (VS-VCS). CI-W3 and CI-W4 fixed the Windows, macOS and sanitizer CI failures of the W9 tests (long paths, ASan timeouts, host stdlib, crash signal, pathological names); CHURN-2 made the Linux file watcher follow directories by inode so renames during a walk lose no files |
| next | not scheduled | a fast tier with overloads by argument type, alias chains and names declared through macro arguments (fast/final agreement is about 81%, `docs/STRESS_RESULTS.md`, "W12-FAST-PP"); workarounds for clangd answer gaps; W9i editor-level stress; Unreal Engine awareness (`.uproject`); the remaining 10x commands and settings (`docs/FEATURE_PARITY.md`). Done in W12-FAST-PP: a fast tier that follows preprocessor conditionals (per-symbol `#if` arms in the index, a three-valued `#if` evaluator, the compile command's `-D` / `-U` and certain predefined macros, forced includes, `#define` / `#undef` of the unit, `__has_include`; agreement 75% to 83% over 36 corpora; 81% after the stricter absent-macro rule of 2026-11-08, no fast answer lost; `docs/ARCHITECTURE.md`). Done in W11-FAST-SCOPE: a scope-aware fast tier (locals, file-local entities, member access by receiver type, scope chain with using-directives, argument counts, include reachability; agreement 49% to 75% over 36 corpora, `docs/ARCHITECTURE.md`). Done in W11-GUI-LSP: call / type hierarchy panel, Quick Actions (Ctrl+.) with the light bulb, inlay hints (`docs/USER_GUIDE.md`). Done in W11-PERF-LINES: performance of extreme single lines (W9f "Not fixed" 1 and 2): column anchors for lines of 2 KB or more and windowed, resumable lexing, so every pathological file is under the 8 ms keystroke-to-paint budget (average and p99, wrap on and off) and the budgets are enforced in CI (`docs/STRESS_RESULTS.md`, `docs/ARCHITECTURE.md` "Very long lines") |

A Sonnet task should be at most ~800 lines of new code plus tests; split
anything larger (B2 and B5 will each need two or three sub-tasks; the
orchestrator should split them along the file boundaries listed).

---

## 10. Definition of done for v1.0 (open: the "next" row of §9, and the user's week of daily use)

* All Phase A and B tasks merged, CI green on Linux/Windows/macOS.
* `docs/FEATURE_PARITY.md` has no ⬜ rows in "The two fixes", "Workspace and
  files", "Code intelligence", "Editing", and "Shell" except those the user
  explicitly deferred.
* Benchmarks (A8) within the budgets in §1.7 on the reference machine.
* The user has used it for a week on their real codebase and the
  approximate→authoritative swap is never wrong in a way they notice
  (track reports in `docs/OPEN_QUESTIONS.md`).

---

## Benchmarks (historical baseline)

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
