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

Two small framework additions came with the editor view: an **overlay layer** (`Widget::paintOverlay`, run after the glyphs
are emitted, so carets draw over text at the cost of one extra rect batch) and **ticks** (`RootWidget::tick(nowMs)` calls
`Widget::onTick`, which returns the ms until it next wants a tick; the app waits at most that long, so caret blink and
smooth scrolling need no timer thread and an idle window sleeps).

### TextView (B2a, `src/ui/widgets/TextView.*`, `src/ui/view/{Viewport,Layout}.*`)

The editor view over an `Editor&` and a `Document` (it follows `currentDocument()`, or is pinned to one document for
tabs; `activate()` makes a pinned view's document current via `gotoLocation`). `Viewport` holds the pixel scroll offsets
and the smooth-scroll animation (exponential approach, 60 ms); `Layout` maps lines/carets to pixels and a byte offset to x
and back by delegating to `gfx::TextLayout` (tabs, wide characters), so hit-testing and drawing cannot disagree. A frame
paints only the visible lines: current-line highlight and selection rects first, then the gutter numbers and line text
through the deferred glyph path, then caret bars in the overlay (about 3 GL draw calls). The view never mutates carets
or text: mouse clicks, drags (`setSelections`, Alt-drag `columnSelect`), double/triple click (word / line), shift-click,
Ctrl- and Alt-click (add a caret) and all keys call `Editor`, so undo groups, navigation history and LSP sync stay
correct. The keyboard map is one function (`TextView::applyBuiltinKey`) that B4's command system replaces. Changes made
elsewhere (another view, LSP edits, Back/Forward) are noticed by comparing document, version and selections on every
event and tick, and the caret is scrolled into view. Settings: `smoothScroll` (default on) and `wordWrap` (default off,
reserved, not implemented) per OPEN_QUESTIONS #4, `caretBlink`, `caretBlinkMs`, `highlightCurrentLine`, `wheelLines`.
Measured keystroke-to-paint (key event, edit, sync, layout, paint recording; 100k lines, 1920x1080) is ~0.1 ms with one
caret and ~0.35 ms with 1000 carets against the 8 ms budget (§1.7); the llvmpipe submit is not representative.
