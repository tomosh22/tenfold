# Architecture

Tenfold is split into an engine (everything under `src/` except `cli/`) and
front ends. The engine never touches a window or a key binding; it exposes an
`Editor` model plus callbacks, and a front end renders that model on its own
thread.

```
 front end (GUI / CLI)
      │ commands, pump()
      ▼
   Editor ────────── NavigationHistory
      │ owns
      ├── Workspace (files, settings)
      ├── Document* (TextBuffer + undo + listeners)
      └── SemanticEngine
             ├── FastIndex  ← CppLexer            (tier 1, approximate, instant)
             └── LspClient  ← JsonRpc ← Process   (tier 2, clangd, authoritative)
                     ▲
                     └── CompileCommands (discover or generate compile_commands.json)
```

## Threading model

* The front end owns the **main thread**. All `Editor` methods are called on
  it, and all results come back on it: background work calls
  `Editor::post()`, and the front end drains the queue with `Editor::pump()`
  once per frame (or `waitFor()` in headless use).
* `JsonRpcConnection` runs one **reader thread** per language server. Every
  LSP response/notification handler runs there. `SemanticEngine` wraps these
  so the `Editor` only ever sees `post()`ed callbacks.
* `FastIndex::indexFiles` uses a **thread pool** (hardware concurrency) and
  is launched by `SemanticEngine::start()` on a background thread. Lookups
  take a shared lock, so the index is queryable while it is still filling.
* `FileWatcher` (`src/workspace/`) owns one **watcher thread** (inotify on
  Linux, `ReadDirectoryChangesW` on Windows, polling elsewhere or when the
  inotify watch limit is hit). It debounces events per path (200 ms) and
  calls back on that thread. The callback does only the thread-safe, cheap
  part itself (`SemanticEngine::fileChanged/fileRemoved`, one file re-lexed
  into the `FastIndex`) and `Editor::post()`s the rest: workspace file lists,
  navigation history, open-document checks (`Editor::onExternalChange`) and
  build-system refresh. Heavy follow-ups (compile-database regeneration, bulk
  re-index after a directory rename) run on a lazily started **engine worker
  thread**; a moved compile database is applied by `restartClangd()` on the
  main thread. Files open in a `Document` are never re-indexed from disk
  behind the user's back. Watching is on by default; disable it with
  `"watchFiles": false` in `.tenfold/workspace.json`.
* `SemanticEngine` has a small **timer thread** that implements the result
  budget (below).
* `SemanticEngine` also has a lazily started **symbol thread** (A10). It
  runs the fast-tier scan of `workspaceSymbols` so `Editor::findSymbols`
  returns immediately; the scan itself fans out over `std::thread`s (up to
  the hardware concurrency, max 8) that live only for the duration of one
  query. A newer `workspaceSymbols` call supersedes older ones (see below).

### What is thread-safe (A10)

| API | Thread-safety |
|---|---|
| `FastIndex::search`, `lookup`, `fileSymbols`, `fileCount`, `symbolCount`, `allIncludes`, `exportFiles`, `generation`, `prepareSearch` | Any thread, concurrently with each other and with mutations (shared lock). `search` may spawn worker threads itself and accepts a `cancelled()` predicate (polled between scan blocks, must be thread-safe) that aborts the scan and returns an empty vector. |
| `FastIndex::indexFiles*`, `importFiles`, `updateFile`, `removeFile`, `clear` | Any thread, mutually exclusive with everything else (unique lock). glibc's rwlock prefers readers, so a writer waits for a *continuous* stream of searches to pause; real queries are sparse, tests that hammer `search` in a loop should yield. |
| `FastIndex::fuzzyScore` | Pure, any thread. |
| `SemanticEngine::workspaceSymbols` | **Any thread, asynchronous.** Returns at once; callbacks run on the symbol thread, the timer thread or the clangd reader thread (not the caller's). A newer call supersedes every older one: the older scan is cancelled and its callbacks (approximate, authoritative, error) are never invoked. After `stop()` the callback fires once, inline, with an error. |
| `SemanticEngine::approximateSymbols`, `approximateDefinitions` (the latter reads a `Document`, so only on that document's thread) | Synchronous; `approximateSymbols` is safe on any thread but is the cost of a full scan (use it from benchmarks, tests, workers, not from the UI thread). |
| `SemanticEngine::fileChanged`, `fileRemoved`, `watchedFileChanged`, `indexFilesAsync`, `refreshCompileCommands`, `status`, `waitFor*`, `compileCommands`, `clangd` (A11: snapshot accessors, see below) | Any thread (as before). |
| Everything else on `SemanticEngine` (`goto*`, `hover`, `completion`, `didOpen/Change/Save/Close`, `restartClangd`, ...) | Editor thread, as before. `gotoDefinition` & co. compute their approximate tier synchronously from `FastIndex::lookup` (one hash lookup, `defFirstApproxMs` is ~0.03 ms on Godot), so they stay on the caller thread. `completion` without clangd calls `FastIndex::search` synchronously (now ~1-10 ms on Godot). |

### Fast-tier symbol search (A10)

`FastIndex::search` ranks by (score desc, name asc, insertion order) and keeps
only the best `limit` results in per-thread bounded heaps, merged at the end;
nothing proportional to the number of matches is materialised or sorted. Cheap
prefilters run before `fuzzyScore`: plain queries use a flat table of the name
nodes with a 32-bit case-insensitive character mask per name (built lazily,
rebuilt only when a name is added or erased, warmed by
`FastIndex::prepareSearch()` after indexing), qualified (`a::b`) queries scan
file by file with a per-symbol mask of `scope::name` (built lazily per file).
The scan is cut into blocks handed out by an atomic counter, which is also
where the `cancelled()` predicate is polled.

### Engine robustness (A11): ThreadSanitizer and the shared state it found

A10 found two pre-existing races with `-fsanitize=thread`; A11 fixed them at the source and added the
`linux-gcc-tsan` CI job (preset `tsan`, gcc, Debug, `-fsanitize=thread -O1`, the whole `tenfold-tests` suite,
`halt_on_error=1`, suppressions in `tsan.supp`, which is empty: every report was ours and was fixed).

| State | Was | Now |
|---|---|---|
| `SemanticEngine::compileCommands_` | a plain vector rewritten by the engine worker (`prepareCompileCommands`) while `compileCommands()` returned a reference to it on other threads | an immutable `shared_ptr<const vector<CompileCommand>>`, swapped under `mu_`. `compileCommands()` returns a snapshot (never null) that stays valid and unchanging while the worker publishes a newer one. |
| `SemanticEngine::clangd_` | a `unique_ptr` read by `clangd()` / `status()` / `toServer()` / the symbol thread while `restartClangd()` / `stop()` on another thread reset it | a `shared_ptr` guarded by the leaf mutex `clientMu_`; `clangd()` (initialised client or null) and `client()` return a snapshot. A request in flight keeps the old client alive until it completes; `restartClangd()` stops the old client (its pending requests fail fast) and publishes the new one only after it started. |
| `compdbPath_` / `compdbSource_` read in `startClangd` | read without `mu_` while the worker rewrote them | copied under `mu_` at the top of `startClangd`. |

`clientMu_` is never held across a call into `LspClient`, so it cannot take part in a lock cycle.

## The two-tier query policy

Goal: never miss, never block.

1. A navigation query (`gotoDefinition`, `workspaceSymbols`, ...) computes an
   **approximate** answer from the fast index immediately and sends the real
   request to clangd.
2. If clangd answers within `clangdBudgetMs` (250 ms default) the caller
   gets **one** callback, `Confidence::Authoritative`.
3. Otherwise the caller first gets the approximate answer
   (`Confidence::Approximate`, so the UI can show it greyed or with a
   "verifying…" hint) and later the authoritative one, which replaces it.
4. If clangd is not running or fails, the approximate answer is delivered
   with the error string, so the user still gets something.

   `workspaceSymbols` is the one query whose approximate tier is a scan of
   the whole index, so it is computed on the engine's symbol thread, not on
   the caller (A10): the budget clock (and the clangd request) start when
   that scan finishes (about 1-10 ms on Godot), and a superseded query never
   delivers anything. The single-name lookups behind `gotoDefinition` and
   friends cost well under a millisecond and stay synchronous.
5. While clangd's background index is incomplete, a definition answer that
   stops at a declaration is **augmented** with fast-index definitions of
   the same name (flagged Approximate), because clangd cannot see other
   translation units until it has indexed them. Once the index is complete
   clangd's answer is final.

References, hover, completion, signature help and rename have no
approximate tier worth having (a text search for references would mislead),
so they go straight to clangd; hover and completion degrade to fast-index
output only when clangd is absent.

`Editor::gotoDefinition` jumps automatically when there is exactly one
result and records both ends of the jump in the navigation history. With
several results it does not jump; the front end shows a picker.

## clangd-only features (A9)

Semantic tokens, inlay hints, folding ranges, document highlights, call and
type hierarchy, code actions (+ `workspace/executeCommand`) and formatting
have **no approximate tier** (constraint 1.4): `SemanticEngine` delivers
clangd's answer or an error string (`"clangd not running"`), never fabricated
data. Everything is converted to byte columns like the other queries and the
`Editor` wrappers deliver on the pump thread through `post()`.

* **Semantic tokens**: the first request per document is `/full`, later ones
  `/full/delta` against the stored result id (a rejected delta falls back to
  `/full`). The legend comes from the server (`tokenLegend()`); `TokenSpan`
  holds a legend index and a modifier bit set. With `Editor::onSemanticTokens`
  registered, tokens are requested automatically `semanticTokensDebounceMs`
  after didOpen / didChange and stale versions are dropped.
* **Applying edits**: `Editor::applyEdits` is the single place server edits
  enter documents. All files are opened first (as documents, nothing is
  written to disk), then each document gets ONE undo group
  (`beginGroup/endGroup`), applied bottom-to-top; carets of the current
  document follow. Code actions, formatting and the server's own
  `workspace/applyEdit` (sent while a tweak command runs) all go through it.
  `applyEdit` is answered `{applied:true}` immediately on the reader thread
  and applied later on the pump thread.
* **Hierarchy items** carry a byte-column `Location` of the name, so
  `Editor::gotoHierarchyItem` is a plain `gotoLocation` (navigation history).
* **Watched files**: `workspace/didChangeWatchedFiles` is sent from the
  Editor's FileWatcher events (created / changed / deleted, renames as
  delete + create) for files that are not open in a Document.

## Why clangd, and how it stays fast

clangd *is* the Clang frontend, so correctness on templates / `auto` /
aliases / macros / overload resolution is the compiler's, not ours. The
costs are first-parse latency and memory, and we attack them the way a
fast editor must:

* **Compile flags are always present.** `CompileCommands` finds an existing
  `compile_commands.json`; otherwise configures CMake into
  `.tenfold/cmake-build`; otherwise evaluates `.sln`/`.vcxproj` (MSBuild
  property expansion, conditions, imported `.props`, per-file metadata,
  `%(…)` merging) into `clang-cl` commands; otherwise emits heuristic
  commands with `-I` for every header directory. clangd also gets
  `fallbackFlags` for files that match nothing.
* **Background index** (`--background-index`) with on-disk persistence in
  `.cache/clangd`, so the second launch of a workspace is warm.
* **Preambles in memory** (`--pch-storage=memory`), `-j` = cores.
* **The fast index covers the gap.** It lexes hundreds of MB/s per core and
  needs no flags, so symbol search and approximate navigation are available
  within a second of opening even a huge workspace.
* **Incremental sync.** Edits go to clangd as ranged `didChange` deltas (we
  negotiate UTF-8 offsets so our byte columns are clangd's columns; UTF-16
  conversion exists for servers that insist on it).

## Build integration (C1, `src/build`)

* **`BuildRunner`** runs one build at a time through `tf::Process` with
  `ProcessOptions{mergeStderr, processTree}`: stderr shares the stdout pipe (one
  interleaved, ordered stream), the child leads its own process group (POSIX,
  `kill(-pgid)`) or is terminated through its job object (Windows
  `TerminateJobObject`), so cancelling also stops compilers spawned by
  make/msbuild. A reader thread splits the stream into lines and hands batches
  to `Editor::post()`; the done callback carries exit code, elapsed time and
  `cancelled`. Starting a build cancels the previous one (non-blocking; its
  done callback still fires, its late output is dropped). A build that leaves a
  background process holding the output pipe does not finish until that
  process exits or is cancelled.
* **Command selection** (`detectBuildCommand`): custom `buildCommand`
  (override or `.tenfold/workspace.json`, run via `sh -c` / `cmd /s /c`) >
  `.sln`/`.vcxproj` -> `msbuild <sln> /m /p:Configuration=<vsConfiguration>
  /p:Platform=<vsPlatform>` > CMake -> `cmake --build <dir> --config <cfg>` >
  Makefile -> `make -j<cores>`. The CMake directory is `buildDir` (setting or
  override), else the directory of the compile database clangd uses when it
  has a `CMakeCache.txt`, else `.tenfold/cmake-build`. Configuration and
  platform are the same settings the compile database generation uses, so
  clangd and the build agree. The build tree is not configured automatically.
* **`ErrorParser`** (stateful, one per build) recognises MSVC
  (`file(l,c): error C1234:`, column-less, `LINK : fatal error`, `x.obj :
  error LNK`), MSBuild decorations (`  1>` prefixes, `[proj.vcxproj]` suffix,
  duplicate summary lines), NMake `U1077`, GCC/Clang (`file:l:c: severity:`,
  `[-Wflag]` as code, driver errors, ANSI colours, Windows drive letters),
  `In file included from` chains (attached to the next error/warning as
  `notes`) and make (`make: *** ...`, `Makefile:12: ***`). Paths use `/`,
  relative ones resolve against the build's working directory; compare with
  `tf::pathKey`.
* **Editor**: `build()` clears `buildMessages()` / `buildDiagnostics()`; messages
  arrive on the pump thread while output streams. `nextBuildMessage()` /
  `prevBuildMessage()` wrap around errors and warnings that have a location
  (notes and location-less messages are skipped) and jump with `gotoLocation`,
  so Back returns and entries follow edits like any other. Build diagnostics
  are stored per file apart from clangd's (`buildDiagnostics(file)` vs
  `diagnostics(file)`), so a UI can colour them differently. Message line
  numbers are those of the build time; they are not remapped when the buffer is
  edited afterwards. Every output line goes through `tf::stripAnsiEscapes`
  (`src/build/AnsiStrip.*`: CSI colour codes and OSC hyperlinks from
  `-fdiagnostics-color` / `-fdiagnostics-urls`) before `ErrorParser` sees it,
  and the build panel uses the same helper to render lines; the `onLines`
  callback still receives the raw text. `setBuildCursor(index)` moves the F4
  cursor to a message (a click on a build-panel line), so the next F4 /
  Shift+F4 continues from there.
* **UI (C1-UI)**: see "Build panel and bottom dock" under the Workbench section.

## Navigation history

`NavigationHistory` is a deque of `{Location, Kind, label}` plus a cursor.

* `record(loc)` is called twice per jump by `Editor::gotoLocation`: the
  origin and the destination.
* `onCaretMoved(from, to)` records `from` only when the move is "far"
  (different file or more than `coalesceLines` lines).
* Entries within `coalesceLines` of the previous one in the same file
  replace it instead of stacking.
* `back(current)`: if we are at the live position it is pushed first so
  `forward` can return exactly there. Then the cursor moves one entry back.
* Recording while in the middle of the history truncates forward entries
  (browser semantics).
* `onLinesReplaced(file, start, end, newCount)` shifts entries below an edit
  and clamps entries inside a deleted block. `Editor` wires this to every
  `Document` change, so history survives editing.
* `onFileRenamed` / `onFileRemoved` keep entries valid across workspace
  changes.
* `Editor::goToHistoryEntry(i)` walks the cursor with `back()`/`forward()`
  until it sits on entry `i`; the jump adds no entries (leaving the live
  position records it once, exactly like `back()`). Coalescing keeps the
  label of an entry when the next jump's "from" record lands on the same line.
* `Editor::lastEditLocation()` is one tracked `Location`: the primary caret
  after the last edit (typing, deleting, undo/redo, `replaceRange`,
  `applyEdits` on the current document). It shifts with line insertions and
  deletions in its file by the same rule as history entries, also for changes
  that are not user edits (`reloadFromDisk`). `Nav.LastEditLocation`
  (Ctrl+Shift+Backspace) goes there through `gotoLocation`, so Back returns.

### Navigation UI (B6)

`src/ui/features/NavigationUi.{h,cpp}`: `EditorApp` creates it when it has a
`Workbench`. A thin toolbar (`Workbench::setTopBar`) above the panels holds
Back and Forward buttons whose enabled state is
`NavigationHistory::canGoBack/canGoForward`. A click runs the `Nav.Back` /
`Nav.Forward` commands through `EditorApp`'s command path (never the Editor),
so buttons, key bindings and the Mouse4/Mouse5 thumb buttons behave
identically. The tooltip ("Navigate Backward (Alt+Left)") is built from the
first keyboard binding in the live `KeyMap`. Long-press (400 ms, driven by
`EditorApp::tick`) or right-click opens a dropdown of `entries()` (Back: the
current entry and older ones, newest first; Forward: current and newer) with
the symbol label or `file:line`, the current entry marked; a click jumps with
`goToHistoryEntry`. The dropdown is a private minimal list drawn in the
overlay pass (it flushes its own text so editor text does not show through);
replace it with `ListPopup` (B5a). The status segment `navcounts` ("← 3 | 1 →")
follows `EditorApp::setNavStatusCounts` (the `nav.statusCounts` setting, default
off; call it from the settings wiring).

## Text model

`TextBuffer` stores lines without terminators, remembers CRLF vs LF for
saving, and exposes `replace(Range, text)` returning the inserted range and
line delta (what undo, the navigation history and LSP incremental sync
need). Columns are UTF-8 byte offsets; `utf8ToUtf16Column` and the reverse
exist for protocol conversion. `Document` adds path, version, dirty flag,
undo/redo and change listeners.

**Undo groups.** The undo stack holds *groups* of edits; `undo()`/`redo()`
revert or re-apply one whole group. `Document::beginGroup()/endGroup()` nest
(only the outermost pair delimits the step); outside a group every `replace()`
is its own step; an empty group leaves no step. A group also carries opaque
caret state (`std::vector<Selection>`): the caret set before the first edit
(restored by undo) and after the last (restored by redo), read back through
`restoredCarets()`. `beginGroup(carets, joinPrevious=true)` re-opens the most
recent step, which is how typing is coalesced.

**Carets and selections.** `src/core/Selection.h` defines
`Selection { anchor, head }` (empty = plain caret, `head < anchor` = reversed).
`Editor` owns `selections()`: a non-empty vector, primary first, the rest in
document order. `cursor()` is the primary caret's head, so single-caret code
keeps working. Invariant: no two selections overlap *or touch*; every command
that could make them meet merges them (`normalizeCarets`). Editing commands
(`insertText`, `backspace`, `del`) compute one edit per caret, apply them
bottom-to-top through `Document::replace` inside one undo group (so LSP
incremental sync, navigation-history line tracking and diagnostics see
ordinary edits), then remap every caret through the returned
`TextBuffer::EditResult`s. Vertical moves keep a per-caret preferred column
(counted in code points, so it never lands inside a multibyte sequence);
word motion uses CppLexer identifier characters. Caret moves feed
`NavigationHistory::onCaretMoved` with the primary caret only.

**Typing coalescer.** `Editor::insertText` with exactly one code point joins
the previous typing step while nothing else happened since: same document and
version, caret set unchanged, no selection being replaced, and no longer
than `undoGroupPauseMs` since the last keystroke. A typed whitespace
character joins the step and then closes it (so `hello world` is two steps:
`hello ` and `world`). Any caret command, undo/redo, backspace/delete,
multi-character insert (paste) or edit made behind the editor's back ends
the step. Policy is the setting `undoGrouping` (`"word"` default, or
`"char"` for one step per character) plus `undoGroupPauseMs` (default 1000)
in `.tenfold/workspace.json`; time comes from an injectable clock
(`Editor::Options::clock` / `setClock`, milliseconds). This is a default
pending OPEN_QUESTIONS #3.

The line vector is simple and plenty fast for files up to hundreds of MB;
a piece table can replace it later behind the same interface if needed.

## Front-end contract

A front end needs to:

1. Construct `Editor(root, options)` and call `start()`.
2. Call `pump()` every frame.
3. Render `currentDocument()->buffer()`, `selections()` (primary first;
   `cursor()` is the primary head), `diagnostics(file)`.
4. Map keys to `Editor` commands (`gotoDefinition`, `back`, `forward`,
   `findSymbols`, `insertText`, `backspace`, `del`, `moveCarets`,
   `addCaretAbove/Below`, `addCaretAtNextOccurrence`, `collapseToPrimary`,
   `columnSelect`, `setSelections` for mouse placement, `undo`/`redo`, ...).
   All carets live in the engine; every edit command is one undo step and
   goes through `Document::replace`. Call `setPageLines()` with the
   viewport height for PageUp/PageDown.
5. Show picker UIs for multi-result callbacks and completion lists.

`src/cli/main.cpp` is the reference implementation of this contract.

## Portability

Everything is standard C++20 except `src/lsp/Process.cpp` (fork/exec and
pipes). A Windows implementation needs `CreateProcess` with inherited
anonymous pipes behind the same five methods. On Windows the editor should
ship `clangd.exe` and use `--query-driver` or the `clang-cl` driver so MSVC
system headers resolve; `CompileCommands::fromVcxproj` already emits
`clang-cl` style commands.

## Text rendering (`src/ui/gfx`, target `tenfold_text`, option `TENFOLD_BUILD_UI`)

CPU-side and testable without a GPU: `Font` (FreeType 2.13, system package else FetchContent `VER-2-13-3`; light
hinting + grey AA, `RasterMode::Lcd` is only a hook; `FontStack` = primary + fallbacks, missing glyph -> notdef box)
-> `Atlas` (R8, shelf packer, 1px padding, doubles on growth without repacking, no eviction, dirty rects uploaded via
`Renderer::updateTexture` by `Atlas::flush`) -> `TextLayout` (monospace cells, no shaping: UTF-8 with U+FFFD for bad
bytes, tab stops, wide = 2 cells, zero-width marks overlay the previous cell, control chars drawn as `^X`; emits
`GlyphQuad`s grouped into colour runs from byte-range spans, and owns the byte <-> column <-> x mappings used for the
caret and mouse hit-testing, all derived from one cell iterator so they cannot disagree). Font family/size are
settings passed in; `discoverMonospaceFont` is only the fallback search. Layout of 120 x 150 visible cells with a warm
atlas takes about 0.1 ms.

## Renderer backends (`src/ui/gfx`, B9)

`Renderer` has two implementations with identical behaviour (batching, draw order, straight alpha blended in gamma
space, R8 coverage x colour / RGBA8 modulate, scissor stack rounding outwards, `readPixels` top-left origin,
offscreen mode): `GlRenderer` (OpenGL 3.3 core, every platform) and `D3d11Renderer` (Direct3D 11, Windows). Both
share the FreeType atlas: glyph rasterisation stays on FreeType, DirectWrite is not used.

* **Selection.** CMake option `TENFOLD_D3D11` (default ON on Windows) builds the D3D11 files and defines
  `TENFOLD_HAVE_D3D11`; `tenfold-ui --renderer=gl|d3d11` picks at run time (default `d3d11` on Windows, `gl`
  elsewhere). `ui::WindowOptions::openGl = false` creates the SDL window without `SDL_WINDOW_OPENGL`;
  `Window::nativeHandle()` returns the HWND (`SDL_PROP_WINDOW_WIN32_HWND_POINTER`).
* **Shaders.** HLSL source is embedded in `D3d11Renderer.cpp` and compiled at start-up with `D3DCompile`
  (`d3dcompiler_47.dll`, Windows 10+), so the build needs no fxc/dxc and CI needs no extra tool. Cost: a few ms at
  start-up. Two pixel shaders (coverage, modulate) instead of a per-draw flag.
* **Device.** Hardware adapter first, WARP (`D3D_DRIVER_TYPE_WARP`) as fallback; `TENFOLD_D3D11_WARP=1` forces WARP.
  Feature level 11_0 down to 10_0, no debug layer.
* **Swap chain** (`WindowsSwapChain`). `DXGI_SWAP_EFFECT_FLIP_DISCARD`, 2 buffers, R8G8B8A8_UNORM (non-sRGB), created
  with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` and `SetMaximumFrameLatency(1)`;
  `D3d11Renderer::beginFrame` blocks on the waitable object before recording, so at most one frame is queued ahead of
  the display. Without `IDXGISwapChain2` (Wine) it falls back to `IDXGIDevice1::SetMaximumFrameLatency(1)`.
  `endFrame()` only draws; `present()` flips (the GL equivalent is `Window::swap()`). A size change in `beginFrame`
  triggers `ResizeBuffers`. Offscreen mode (screenshots, tests; always on without an HWND) renders into an owned
  texture and `readPixels` copies through a staging texture.
* **Tests.** `RENDERER_TEST` (tests/ui/ui_test.h) registers each renderer test twice: `name` (GL) and `name_d3d11`
  (offscreen, WARP; skipped with a message off Windows). `tenfold-ui-tests d3d11` runs only the D3D11 ones. The
  D3D11 runs are compared against the GL goldens (b1_scene, b1_text) with per-channel <= 3 and <= 0.5 % of pixels
  (GL: 0.2 %), since WARP and Mesa rasterise triangle edges slightly differently.

## UI widgets (`src/ui/widgets`, part of `tenfold_ui`)

A small retained widget layer of our own (B3a). `Widget` has absolute-pixel bounds, parent/children, visibility,
`layout(RectF)`, `paint(PaintContext&)` and `onEvent(const InputEvent&) -> handled`. `RootWidget` owns the `Theme` and
the `TextRenderer`, routes input (deepest-widget hit-test with bubbling, mouse capture for drags, keyboard focus, hover),
and tracks dirtiness: widgets call `markDirty()`, the app polls `RootWidget::dirty()` and calls `render()` (event-driven
frames, no timers). Paint is two-pass by construction: `PaintContext::fill` draws rects immediately while
`PaintContext::text` only records glyph quads in `TextRenderer::drawText` (clipped on the CPU, grouped by colour); the
quads are emitted after the tree has painted, so a shell frame is one rect batch plus one glyph batch (2 GL draw calls,
~0.05 ms CPU for chrome + two text views). If the glyph atlas grows mid-frame the recorded UVs are stale and `render()`
repaints once. Widgets: `TabStrip` (pinned, dirty marker, close, MRU order, drag reorder / drag-out, overflow
scrolling), `Splitter`, `Panel` (strip + one content widget per tab), `StatusBar` (keyed, clickable segments), plus
`TextView` (below). `Theme` is a neutral dark placeholder until B7 loads it from
files. The engine never includes any of this.

### Popups: top layer, modal, `InputLine`, `ListPopup` (B5a)

A widget with `setTopLayer(true)` is skipped by the base paint pass and painted afterwards with its subtree as a second
layer: base rects, base glyphs, base overlay, then the popup's rects, the popup's glyphs (a second `flushText`) and its
overlay (the input caret). A popup therefore covers the editor's text for one extra rect batch plus one glyph batch per
colour (about 8 batches for an open picker). `RootWidget::showModal(w)` makes `w` a full-window top-layer child that
owns the keyboard and mouse until `closeModal()` (previous focus restored; closing from inside the modal's own handler
is safe, the widget is destroyed on the next dispatch/render; a second modal replaces the first). `InputLine` is a
single-line UTF-8 field (caret, selection, Home/End, Ctrl+Left/Right, Ctrl+Backspace/Delete word edits, clipboard,
mouse, double click = word); keys it does not own bubble to its parent. `ListPopup` = optional `InputLine` + rows
(icon column, primary text with fuzzy-matched characters highlighted, dim secondary text, right-aligned detail and
badge, `dim` rows for approximate results) + optional preview pane + a status label ("verifying..."). Keys: Up/Down/
PageUp/PageDown/Ctrl+Home/Ctrl+End (Home/End too without an input), Enter accepts, Escape closes; mouse: hover, click
accepts, click outside closes, wheel scrolls. Callbacks: `onQuery`, `onAccept`, `onSelection`, `onClosed`.

### Pickers (`src/ui/features`, B5a)

All pickers are owned by `EditorApp` and share a `PickerEnv` (root, editor, `LineSource` for previews, the app clock).
While one is open the key context is `picker`, so the `editor` bindings (arrows, Enter, Escape) do not fire and the
keys reach the popup. *Symbol search* (`Search.SymbolSearch`, Ctrl+,): typing calls `Editor::findSymbols` on the main
thread (the fast tier is synchronous until A10: 35-270 ms on Godot); the first
keystroke after a pause goes out at once, later ones at most every 25 ms (`tick` returns the wait); every request has a
sequence number and answers for an older one are dropped. An Approximate answer is shown greyed with "verifying..."
(or "approximate (reason)" when no authoritative answer will follow, as without clangd); the Authoritative one replaces
it and the selected symbol stays selected if it is still present (constraint 1.4). The right pane previews the target
line from the open Document (unsaved text) or from disk; setting `picker.preview` (default true, OPEN_QUESTIONS #6).
Enter -> `Editor::gotoLocation`, so it lands in navigation history, and `Workbench::sync` reveals the tab. *Open file*
(`File.OpenFile`, Ctrl+P / Ctrl+O): `FileSearch::rank` scores every workspace file with `FastIndex::fuzzyScore` on the
filename (weight 4) and the relative path, whitespace-separated tokens must all match, files from
`Workbench::recentFiles()` (MRU, persisted as `recent` in `layout.json`) get a decaying bonus and lead the empty-query
list; it opens with `Workbench::openFile`, a tab operation that is not a navigation jump. 8.7k Godot paths filter in
2-4 ms per keystroke. *Result list* (`NavResultPicker`): GotoDefinition/Declaration with several candidates and
FindReferences with any number show `file:line` plus the source line; the approximate list is swapped in place for the
authoritative one with the selection kept by location; stale answers of an older command are ignored.

### Search, completion, hover, signature help, diagnostics (`src/ui/features`, B5b)

*Find in files* (`FindInFiles`, Ctrl+Shift+F): a large **modal `ListPopup`**, not a docked bottom panel (decision: the
Workbench layout stays untouched, and a modal reuses the picker key context; a named bottom-panel slot for the build
output, C1-UI, was built separately: see "Build panel and bottom dock"). Typing searches live: after a 150 ms pause `Editor::findInWorkspace`
starts (a new query, a toggle or closing the popup cancels the running search and its queued batches are dropped by
sequence number); batches arrive through `post()` and rows are rebuilt at most every 60 ms: a header row per file (sorted
by path, the match count as badge) and one row per match with the trimmed context line, the match highlighted and the
line number. Status: `[Aa] W .*  N matches in M files (truncated) - searching...`. Toggles Alt+C (case), Alt+W (whole
word), Alt+R (regex); globs after ` :: ` (`foo :: *.cpp, !third_party`, `!` excludes). Enter / click ->
`Editor::gotoSearchMatch` (history). The results outlive the popup: F8 / Shift+F8 (`Search.NextResult/PrevResult`)
walk them with wrap-around, and every file with matches gets scrollbar markers (`TextView::setSearchMarkers`).
A single-line selection seeds the query.

*Non-modal popups* (`PopupBox`: a top-layer widget that keeps its own box inside the window, below its anchor or above
it when there is no room, never takes focus). `EditorApp::handleEvent` offers keys to them first (completion:
Up/Down/PageUp/PageDown, Tab/Enter accept, Escape; signature help: Escape), tells them about typed text and runs
`update()` after every event; they close when a picker opens. *Completion* (`CompletionPopup`) asks `Editor::complete`
on `.`, `->`, `::` and when a word reaches 2 characters (one request per word, later characters only refilter), keeps
the unfiltered answer, filters it client-side (prefix matches first, then fuzzy tightness, then `sortText`; matched
characters highlighted), shows the return type / detail right-aligned and documentation in a side pane. Answers older
than the latest request, or arriving after the caret left the word, are dropped. Without clangd the items are the
fast index's: greyed and marked "approximate" (constraint 1.4). Accept replaces the typed prefix with the insert text
as one undo step: at every caret when all carets are empty and share the same prefix (`setSelections` +
`insertText`), else at the primary caret only. Measured (mock font, Release): keystroke-to-paint with 5000 items open
about 1.1 ms (worst 1.4 ms) against the 8 ms budget. *Hover* (`HoverTooltip`): a mouse rest of 400 ms over an
identifier or a diagnostic range; diagnostics are shown at once (error / warning / info colours; the last build's
message for the spot follows with a dim "build" label, C1-UI), `Editor::hoverAt`
text is appended when it arrives (clangd markdown fences stripped); a move to another word, a key or a click dismisses
it. *Signature help* (`SignatureHelp`): `(` / `,` ask `Editor::signatureHelp` (clangd only); the active parameter is
counted locally from the top-level commas between the call's `(` and the caret, so it follows the caret without a
round trip, and the enclosing call is shown again when an inner one ends. *Diagnostics* (`DiagnosticsPanel`,
Ctrl+Shift+M): modal list of the open documents' clangd diagnostics (clangd only reports open files, the status says
so) plus the last build's `Editor::buildDiagnostics`, labelled "build"; errors first, filter by text, live refresh,
Enter -> `gotoLocation`. Engine additions (additive): `Editor::hoverAt(doc, pos, done)` and
`Editor::signatureHelp(done)`.

## Commands and key bindings (`src/ui/commands`, `src/ui/settings/KeyBindings.*`, part of `tenfold_ui`)

Every user action is a named `Command` (`Editor.GotoDefinition`, `Nav.Back`, ...: id, human name, handler
`bool(const CommandContext&, string_view arg)`, optional `enabled()`), held in a `CommandRegistry` that can enumerate
(command palette) and `run(id, arg, ctx)`; the result distinguishes `UnknownCommand`, `Disabled`, `Unbound` and
`Failed`. `registerEditorCommands` registers what `Editor` can serve itself; UI-only commands (pickers, find in
workspace, MRU cycling, clipboard, `Workbench.*`) are registered without a handler and report `Unbound` until the
front end calls `CommandRegistry::bind`. `KeyMap` is the binding table plus the chord state machine
(`feed(chord, activeContext, nowMs)` returns Matched / Pending with "Ctrl+K..." text / NoMatch; a stroke that breaks a
pending chord is swallowed, a pending chord expires after `timeoutMs`; context-scoped bindings beat `global`). Key
bindings are plain text, `[context] chord [chord] = Command.Id [argument]`, parsed by `parseKeyBindings` (errors and
duplicate warnings collected with line numbers, never fatal). Layers: embedded `assets/keybindings.default.txt`
(generated into a header at configure time), then `<user config dir>/tenfold/keybindings.txt`, then
`<workspace>/.tenfold/keybindings.txt`; a command of `-` removes a binding. The defaults are placeholders until the
user's 10x key map is known (OPEN_QUESTIONS #1). `EditorApp` (below) wires `KeyMap::feed` into the app's event loop.
## Settings and themes (`src/ui/settings/{Settings,Theme}.*`, B7)

Settings are `key = value` text files with dotted keys (`editor.tabWidth = 4`; no `[sections]`), `#` comments only as
the first non-blank character (colours contain `#`), types bool / int / float / string / colour (`#rrggbb[aa]`) / font
(`family-or-path[, size_pt]`, empty = unset). `settingsSchema()` lists the known keys; unknown keys, type errors and
malformed lines are `SettingsDiag`s (= `KeyBindingDiag`, same `formatDiag`) with file:line, never fatal. Layers:
embedded `assets/settings.default.txt` (CMake-generated header), `<user config dir>/settings.txt`,
`<workspace>/.tenfold/settings.txt`. `Settings::onChanged` receives exactly the set of keys whose value changed.
Undo grouping stays in `.tenfold/workspace.json` (A5). Themes use the same grammar with `color.<ui::Theme member>`
and `token.<kind>[.<modifier>]` keys (lexical kinds plus the clangd legend names); `ui::Theme::tokenColor` walks the
fallback chain (`method.static` -> `method` -> `function` -> `identifier` -> `color.text`). Built-in `dark` (reproduces
`Theme::darkDefault()`; a neutral placeholder, TODO until OPEN_QUESTIONS #7) and `light` are embedded; a theme name also
resolves to `.tenfold/themes/<name>.txt` or `<user dir>/themes/<name>.txt`. `SettingsService` loads both, watches the
user dir and `<workspace>/.tenfold` (if it exists) with `FileWatcher`, and on a change sets a flag (plus an optional
thread-safe `wake` callback); the UI thread calls `poll()`, which reloads, fires `onChanged` / `onThemeChanged`, and
`applyTo(RootWidget&, scale)` swaps the theme (repaint). `editorSettings()` gives TextView its subset in one call.

user's 10x key map is known (OPEN_QUESTIONS #1). `EditorApp` (below) wires it into the app.
### Workbench (`src/ui/shell`, B3b)

`Workbench` (a `RootWidget`) owns the editor layout: a tree of `Splitter`s and `Panel`s above a `StatusBar`, and the
map tab id -> file. A tab is a view onto an `Editor` `Document` (several tabs may show one document); the view widget
comes from `makeDocumentView(Editor&, Document&)` in `DocumentView.cpp`: a TextView pinned to the document.
Split duplicates the active tab into the new panel; dragging a tab (TabStrip drag-out, the mouse is then captured by the
workbench) onto a strip moves it, onto a content edge splits; closing a panel moves its tabs to the nearest neighbour.
Tab switches go through `Editor::activateDocument`, which does not touch navigation history; navigation through the
Editor (`gotoLocation`/`back`/`forward`) is picked up by `Workbench::sync()` (call once per frame / event batch), which
reveals the tab of the Editor's current document, refreshes dirty markers and the status bar (`pos`, `eol`,
`encoding`, `clangd`, `fastindex`, `workspace`). Ctrl+Tab is `cycleMru`/`endMruCycle`; key bindings are B4's.
Layout is saved to `<workspace>/.tenfold/layout.json` (split orientation + ratio, panels, tab paths relative to the
workspace, pinned flags, active tab, active panel) and restored on start; missing files are dropped and a corrupt file
yields the default single-panel layout. `tenfold-ui <workspace> [file]` runs the workbench.

#### Build panel and bottom dock (C1-UI, `Workbench`, `src/ui/features/BuildPanel.*`)

* **Bottom dock** (`Workbench::addBottomPanel / showBottomPanel / hideBottomPanel / toggleBottomPanel`): the panel tree
  lives in a `Body` widget inside a vertical `Splitter` subclass (`DockSplit`) whose second child is the `Dock`, a host
  for named panels of which one is visible. Closed (the default: the dock is hidden until a build or a toggle), the
  split gives the body everything and the divider neither paints nor reacts, so existing layouts and goldens are
  unchanged. The divider is the ordinary Splitter drag (double-click resets to 50 %; minimum 80 px above, 60 px dock).
  Panels draw their own headers; find in files and diagnostics could move in by registering a panel of their own
  (they stay modal popups for now). `layout.json` gets an additive key
  `"bottom": {"visible": bool, "panel": "Build", "height": 0.3}` (height = the dock's fraction of the area above the
  status bar); a file without it loads with the dock hidden, a restored state for a panel registered later (EditorApp
  adds "Build" after `restoreLayout`) is applied when the panel is added, wrong types in the key are ignored.
* **`BuildPanel`**: header with the state (`Building...`, `Succeeded in 3.2 s`, `Failed: 2 errors, 5 warnings`,
  `Failed (exit code N)`, `Failed to start: ...`, `Cancelled`) and Build / Cancel / close buttons; below it the output in
  the editor font, ANSI escape sequences stripped (`BuildPanel::clean`: CSI, OSC, `\r` progress overwrite, tabs
  expanded, long lines cut, 20000 lines kept). It follows the end of the output until the user scrolls up (wheel or
  scrollbar) and resumes at the end. The panel calls `Editor::build` itself (one output callback per build) and feeds
  the lines to its own `ErrorParser`, which finds the same messages as the Editor's: that pairs lines and messages.
  Lines with a message are coloured by severity (`buildError` / `buildWarning`, notes dim), those with a location are
  clickable (click or Enter on the selected line -> `Editor::gotoLocation`, so Back returns) and the scrollbar marks the
  error / warning lines. Up/Down/PageUp/PageDown/Home/End select, Escape closes.
* **Commands** (EditorApp): `Build.Build` (Ctrl+Shift+B; an argument is a custom shell command), `Build.Cancel`
  (Ctrl+Break, enabled while a build runs), `Build.NextError` / `Build.PrevError` (F4 / Shift+F4:
  `Editor::next/prevBuildMessage`, then the panel selects and scrolls to the line and the message goes to the status
  bar), `View.ToggleBuildPanel` (Ctrl+Alt+B). Status bar segment `build` shows `build: <state>` after the first build
  and toggles the panel on click.
* **Squiggles**: `TextView` draws `Editor::buildDiagnostics(file)` under the clangd ones in `Theme::buildError` /
  `buildWarning` (theme keys `color.buildError`, `color.buildWarning`, both shipped themes), with scrollbar marks in the
  diagnostics lane (`Scrollbar::Lane::BuildError/BuildWarning`, drawn below clangd's). Notes are not drawn. A new build
  clears them (Editor). Line numbers are those of the build time (not remapped on edits).

Two small framework additions came with the editor view: an **overlay layer** (`Widget::paintOverlay`, run after the glyphs
are emitted, so carets draw over text at the cost of one extra rect batch) and **ticks** (`RootWidget::tick(nowMs)` calls
`Widget::onTick`, which returns the ms until it next wants a tick; the app waits at most that long, so caret blink and
smooth scrolling need no timer thread and an idle window sleeps).

### TextView (B2a, `src/ui/widgets/TextView.*`, `src/ui/view/{Viewport,Layout}.*`)

The editor view over an `Editor&` and a `Document` (it follows `currentDocument()`, or is pinned to one document for
tabs; `activate()` makes a pinned view's document current via `Editor::activateDocument` with the view's remembered
caret, so a tab switch records no navigation history). `Viewport` holds the pixel scroll offsets
and the smooth-scroll animation (exponential approach, 60 ms); `Layout` maps lines/carets to pixels and a byte offset to x
and back by delegating to `gfx::TextLayout` (tabs, wide characters), so hit-testing and drawing cannot disagree. A frame
paints only the visible lines: current-line highlight and selection rects first, then the gutter numbers and line text
through the deferred glyph path, then caret bars in the overlay (about 3 GL draw calls). The view never mutates carets
or text: mouse clicks, drags (`setSelections`, Alt-drag `columnSelect`), double/triple click (word / line), shift-click,
Ctrl- and Alt-click (add a caret) and all keys call `Editor`, so undo groups, navigation history and LSP sync stay
correct. The view does not handle keys (`EditorApp` turns them into commands); it handles text input, the mouse and the wheel. Changes made
elsewhere (another view, LSP edits, Back/Forward) are noticed by comparing document, version and selections on every
event and tick, and the caret is scrolled into view. Settings: `smoothScroll` (default on) and `wordWrap` (default off,
reserved, not implemented) per OPEN_QUESTIONS #4, `caretBlink`, `caretBlinkMs`, `highlightCurrentLine`, `wheelLines`.
Measured keystroke-to-paint (key event, edit, sync, layout, paint recording; 100k lines, 1920x1080) is ~0.1 ms with one
caret and ~0.35 ms with 1000 carets against the 8 ms budget (§1.7); the llvmpipe submit is not representative.

#### Colouring, squiggles, scrollbar and decorations (B2b, `src/ui/view/{Highlighter,Scrollbar}.*`)

* **Colouring (constraint 1.4 on screen).** C/C++ files (by extension) get lexical colours at once: `Highlighter` scans
  a line at a time (`lexLine`: keywords via `CppLexer::isKeyword`, strings with encoding prefixes, raw strings, char
  literals, numbers with separators, `#` directives with continuations, `//` and `/* */`) into a per-line cache whose
  entries remember the state they were lexed with (block comment, raw string + delimiter, directive). An edit
  invalidates only the edited lines (the cache follows line inserts / removals through `Document::Change`); re-lexing
  walks forward and keeps every later line whose start state still matches, so closing a comment again costs nothing
  below. First paint never lexes the file: at most `kSyncBudget` (1500) lines are lexed per paint; when the exact
  prefix is further behind, the visible lines are lexed *provisionally* from `kLookback` (200) lines early in the "no
  open construct" state and `onTick` finishes the exact pass in `kSyncBudget` chunks (a repaint follows when it
  reaches the viewport; a block comment opened far above is the only thing that can change). Clangd semantic tokens
  (`Editor::onSemanticTokens`, wired in `EditorApp`, one set per view of the document) override **identifier** spans
  only, through `Theme::tokenColor(legend type, modifiers)`; types the theme does not know ("comment" for inactive
  code, "unknown") are ignored, so keyword / string / comment colours never change when tokens arrive. Spans are kept
  sorted per document and remapped through edits: a line delta shifts them, a one-line edit keeps the spans before it
  and shifts the ones after it on that line (touching spans drop), a multi-line edit drops the replaced lines. Colour
  spans reach `TextRenderer::drawSpans` (the B1b colour-run API; palette indices, one recorded batch per colour).
  Other file types are drawn in the text colour.
* **Squiggles** (`paintOverlay`): for every `Editor::diagnostics(file)` entry on a visible line a zig-zag of short
  line segments under the range (error / warning / info colours, one cell minimum). `Editor::setDiagnostics` (additive)
  feeds them without a language server (tests); the view notices a changed diagnostic list on its next tick. Hover is B5.
* **Scrollbar** (`Scrollbar`, a child widget at the right edge of the TextView, 1.5 cells wide): thumb with drag,
  click-to-page (90 % of the view), marker lanes left to right: search matches (`TextView::setSearchMarkers`, B5b),
  carets, diagnostics. Markers are 2 px, one per pixel row and lane. The wheel over the bar scrolls the view. The
  horizontal scrollbar is not built.
* **Decorations**: `editor.columnGuide` (1 px line at that column), `editor.showWhitespace` (dots / tab arrows as
  rects and lines, dimmed; first 4000 bytes of a line), bracket match for `()[]{}` of the primary caret (character
  after the caret first, then before it; brackets inside comments / strings are not brackets since only lexical
  Punct spans count; the partner is searched up to 1000 lines away on the exact lexical cache). `<>` is deliberately
  not matched: the lexer cannot tell templates from comparisons and a wrong highlight is worse than none.
* **Settings wiring**: `EditorApp::attachSettings(SettingsService&, displayScale, rebuildFont)` applies `editor.*`
  (`TextView::applySettings`; tab width goes to the shared `TextRenderer`) to every view in the tree on attach, on
  every change and (for views created later) on the next tick; a theme change goes through `SettingsService::applyTo`
  (the views re-derive their palette from `RootWidget::themeGeneration()`); settings and theme diagnostics use the same
  status-bar message as key-binding ones. `tenfold-ui` creates the service (watcher thread posts `Window::wake`, the
  loop calls `poll()`), builds the font with `makeEditorTextRenderer` (`font.editor`, then each `font.fallbacks` name
  in order, then the platform default; no hard-coded choice, OPEN_QUESTIONS #7) and rebuilds it on any `font.*`
  change (`RootWidget::setTextRenderer`).
* **Measured** (Release, llvmpipe, 100k lines, 1920x1080, one caret): keystroke-to-paint 0.12 ms plain vs 0.15 ms
  coloured (p99 below 0.4 ms); cold first paint 1-2 ms at the top (glyph atlas cold too), 0.5 ms when scrolled to line
  90000 (provisional), exact pass finished in the background in ~60 one-ms ticks.

### EditorApp: input to commands (`src/ui/shell/EditorApp.*`, B-INT)

`EditorApp(root, editor, workbench*)` is the glue the window's event loop calls. `handleEvent(e, nowMs)` turns a KeyDown
(a lone modifier press is not a stroke) or an X1/X2 MouseDown into a `KeyChord`, picks the context (`editor` when the
focused widget is a TextView, else `global`), runs `KeyMap::feed` and `CommandRegistry::run`; everything else (text
input, the mouse, unbound keys) is dispatched to the widget tree. Bindings are `loadKeyBindings(workspace root, user
config dir)`. `tick(nowMs)` runs `Workbench::sync`, expires a stale chord (`KeyMap::tick`) and message, and ticks the
root; it returns the wait the main loop should use. The status bar has a `message` segment: the pending chord text
("Ctrl+K..."), the first key-binding diagnostic (`formatDiag`, "+N more", shown for 12 s), "<command> is not available
yet" for a bound command without a handler (Search.FindInWorkspace: B5b), a failed command, or an empty navigation
result. Symbol search, open file and the multi-result list are the B5a pickers above. The UI-only handlers bound
here: `App.Quit` (Ctrl+Q), `Workbench.CycleMruNext/Prev` (the cycle ends when a key event arrives without Ctrl),
`Workbench.FocusPanel N` (1-based), `Search.SymbolSearch` (needs a started Editor), `File.OpenFile`, `Editor.Copy/Cut/Paste` and, in the engine-served set, `File.Save`. Multi-caret
clipboard: copy joins the selections with a line break (no selection: the caret lines); paste with exactly as many lines
as carets (a final line break does not count) inserts one line per caret in document order
(`Editor::insertTextPerCaret`), otherwise the whole text at every caret. The clipboard has an injectable backend
(`setClipboardBackend`) for tests. Escape is a binding (`[editor] Escape = Editor.CollapseCarets`), it never quits;
Ctrl+Q or closing the window quits, and shutdown is: `Workbench::saveLayout`, then `Editor::shutdown`. The Workbench
focuses the view of the active tab after every tab/panel switch or reveal, so keys always reach the visible TextView.

`tenfold-ui <workspace> [file]` is this editor. The single-file `EditorShell` of B2a was dropped (a Workbench with one
tab is the same thing); `--shell` (B3a widget demo, still key-driven through EditorApp) and `--demo` (renderer scene)
remain.
