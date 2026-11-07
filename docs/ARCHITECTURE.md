# Architecture

Tenfold is split into an engine (everything under `src/` except `cli/`, `ui/` and `ui_app/`) and
front ends: `tenfold-cli` (`src/cli`) and the graphical `tenfold-ui` (`src/ui`, `src/ui_app`). The engine never touches a window or a key binding; it exposes an
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
  The inotify backend reads events late when the machine is loaded, so a path in
  an event may name a different directory by then. Each watch records its
  directory's inode; a directory-rename event moves the watches only when the
  watch under the old name is on the directory that moved (otherwise the moved
  directory is listed and watched under its new name), and paths a walk could not
  stat because a rename overtook it are looked up again when that rename arrives
  (`unwatched`, bounded). No per-event rescans.
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

### clangd crash recovery and the process crash handler (D2)

`LspClient`/`JsonRpcConnection` report an unexpected server exit through `onDisconnect`; at that moment every
request still in flight is completed with an error (`connection lost`) on the reader thread, and later requests fail
immediately, so nothing waits for a dead server. A write to the dead pipe cannot kill the editor either: POSIX
`Process::start` ignores `SIGPIPE` once (children get the default back).

`SemanticEngine` runs a supervisor thread (started by `start()`, joined first by `stop()`):

* The disconnect hook hands the dead client to the supervisor (never dropping the last reference on the reader
  thread). The supervisor detaches it (`clangd()` is null, so the fast tier answers alone, results stay
  Approximate), counts a crash, and waits `restartBackoffInitialMs` doubling per consecutive crash up to
  `restartBackoffMaxMs` (500 ms, 1, 2, 4 ... 30 s). The wait is a condition-variable wait that `stop()` and a manual
  restart interrupt.
* A crash after `restartHealthyMs` (60 s) of running starts the count again. The Nth consecutive crash
  (`restartMaxFastCrashes`, 5) or failed restart makes the engine give up: "clangd stopped: crashed 5 times".
* The new server is started and handshaken on the supervisor thread but not published. Re-sending the open documents
  (`didOpen` with the current text and version, then a semantic-tokens request, diagnostics follow) must happen on
  the thread that owns the `Document`s, so the supervisor hands `adoptPending` to the poster (`setPoster`; the Editor
  uses `post()`). Only after that is the client published; edits made in between are covered because the full text is
  sent at that moment. Without a poster (CLI, tests) it runs on the supervisor thread.
* `restartClangd()` is also the manual command (`Editor::restartClangd`): it cancels a pending automatic restart,
  resets the count and works after the engine gave up. Lock order: `lifeMu_` (client launches) -> `supMu_` ->
  `clientMu_`; none is held across a call into `LspClient`.
* Status: `SemanticEngine::clangdHealth()` / `onClangdHealth()`; the Editor posts the text ("clangd restarting
  (2)...", "clangd stopped: crashed 5 times", empty when fine) to `Options::onClangdStatus` and `clangdStatusText()`.

`src/core/CrashHandler` (`installCrashHandler`, called first in the CLI and GUI mains, never by the tests) handles the
editor process itself. Windows: `SetUnhandledExceptionFilter` writes a minidump with `MiniDumpWriteDump` (dbghelp.dll
loaded dynamically at install time) to `%LOCALAPPDATA%\tenfold\crashes`; `abort()` writes one too. POSIX:
SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT on an alternate stack write `crash-<pid>-<time>.txt` (signal, fault address,
`backtrace_symbols_fd` on glibc/macOS) to `$XDG_DATA_HOME/tenfold/crashes` (`~/Library/Application Support/tenfold/
crashes` on macOS) using only async-signal-safe calls (the path is formatted by hand, libgcc is preloaded by a dummy
`backtrace()` at install time), then reset the signal and re-raise it. Only the installing thread has the alternate
stack. Nothing is uploaded.

Tests (`tests/test_crash.cpp`) use `tenfold-test-helper` (`tests/test_helper_main.cpp`), a fake clangd that dies after
N requests (exit or SIGSEGV), on its first launches, or hangs on a method, and a child that installs the handler and
crashes.

### Local log file (D3)

`src/core/Log.{h,cpp}` is the only diagnostic channel: a local, rotated text log, no telemetry and no upload.

* Format: `2026-10-25T12:34:56.789Z I t3 clangd: message` (UTC with ms, level E/W/I/D, a small per-thread number,
  category). A multi-line message is one record whose continuation lines are indented four spaces. Categories in use:
  `clangd`, `index`, `vcs`, `build`, `watch`, `editor`, `log`; `debug` and `ui` are reserved for their subsystems.
* Where: `<workspace>/.tenfold/log.txt` once an `Editor` exists (its constructor calls `Log::setWorkspace`; the
  `.tenfold` directory is already excluded from scans and watching), else `<user data dir>/tenfold/log.txt` (same
  base directory as the crash reports). `Log::setWorkspace` flushes pending records to the old file and moves on.
  The file is created by the first record, so a disabled or silent run leaves nothing behind. `Editor::logFilePath()`
  gives the UI the path for a future "Open log" command; `tenfold-cli log [<root>] [--tail N]` prints it (path on
  stderr, lines on stdout; no root means the user-level log). It only reads.
* Rotation: `log.txt` -> `log.1.txt` -> `log.2.txt` (`LogConfig::keep` = 3 files, `maxBytes` = 5 MB each). Rotation
  happens at line boundaries, so a file never exceeds the limit (except for one single line larger than it). An existing
  file's size is picked up at open, so restarts continue where they left off.
* Level: `LogConfig::level` (Info by default); the environment variable `TENFOLD_LOG=error|warn|info|debug|off` wins
  over it (and over `Log::setLevel`). Off means no file at all. `Log::enabled(level)` is a single relaxed atomic load;
  `TF_LOG_DEBUG(cat, msg)` does not evaluate `msg` when filtered out.
* Threading: any thread may log. `write()` formats the record outside any lock, appends it to a buffer under a short
  mutex and returns. A writer thread drains the buffer every 500 ms, at once after an Error and when 64 KB are pending,
  doing all disk I/O under a separate `ioMu` (lock order: `ioMu` -> `mu`; the buffer lock is never held across I/O).
  `flush()`, `setWorkspace()`, `setFile()` and `shutdown()` are the only calls that touch the disk on the caller. If the
  disk stalls and 8 MB accumulate, further records are counted and a "dropped N" record follows later.
* Lifetime: `Log::init()` in the mains (CLI, GUI), `Log::shutdown()` flushes, joins the writer and is also registered
  with `atexit`. The state is leaked on purpose, and after `shutdown()` every call is a no-op, so logging from a static
  destructor cannot crash. `init` after `shutdown` works (tests).
* Wiring: the `Options::log` callbacks stay as they are (tests inject them). `Editor`'s constructor wraps them with
  `Log::tee(category, userCallback)`: the message goes to the log (category routed by prefix: `vcs: `, `fast index`,
  `file watcher: `, `build: `, `compile database`, `clangd`; Warn for "failed/could not/crashed", Error for
  "giving up") and then to the injected callback if any. The same wrapper is given to `SemanticEngine::Options::log`,
  `VcsManager::Options::log` and `FileWatcher`, so clangd start (with its path and the `clangd.log` stderr file), the
  compile-commands source, the active Visual Studio configuration, clangd crash/restart events (D2), build start/finish
  and the VCS provider all reach the file. The session header records the commit (`TENFOLD_GIT_COMMIT`, configure time),
  platform, pid and workspace root.
* Crash handler: `Log` tells `CrashHandler` the current path (`setCrashLogPath`, a fixed buffer); the POSIX report
  gets a `log: <path>` line, Windows gets a `crash-<pid>-<tick>.txt` next to the minidump. The handler does not flush the
  log, because that takes a mutex and allocates, which is not async-signal-safe; records from the last flush interval
  (at most about 500 ms, except Errors which are flushed immediately) can be missing after a crash.
* Not yet logging: `DebugSession` and `BuildRunner` make no `Log` calls of their own; the editor-level events around
  them (build start/finish) are logged, subsystem-internal ones are a follow-up. `FastIndex` logs the files it skips.

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

### Go to definition with a choice (W10-CHOOSE)

For a name that stands for more than one wanted thing, `SemanticEngine::gotoDefinition` assembles the authoritative
answer from a few more questions to clangd (all about the caret's own, open document) and returns *labelled* targets:
`SymbolLocation::label` ("class Widget", "constructor Widget(int)", "using-declaration", "typedef Bar → Foo"),
`preferred` (the chooser's default selection) and, on the fast tier only, `refinable`. Order: declaration first, entity
last. The text helpers (which `(` makes a name a construction, what an alias names, parameter types without names) are
pure functions in `src/semantic/DefChoice.{h,cpp}`.

1. `textDocument/definition` at the caret, as ever. A single answer with no special shape is returned unchanged.
2. **Using-declaration** (a result whose declaration is a `using ns::x;`): clangd already answers the declaration and the
   entity; they are labelled (`using-declaration`, `function ns::f(int)`), the entity is `preferred`.
3. **Alias** (the single result is a `typedef X Y;` / `using Y = X;` / alias template): the hops are followed up to the
   real entity. Hop 1 is `textDocument/typeDefinition` at the caret (one hop for clangd: `B3 → B2`); later hops ask
   clangd for `definition` at the aliased name inside the alias' declaration when that document is open in clangd, else
   look the name up in the fast index (taken only when exactly one workspace declaration has it; a library type like
   `std::vector` ends the chain). Cycles and more than 8 hops end it. The list is the aliases nearest first, then the
   entity (`preferred`); an alias of a builtin type is a single target (labelled; the front end shows the label).
4. **Constructor call** (the single result is a class, and the word is followed by a `(` / `{` that spells a
   construction: `T(..)`, `T{..}`, `T name(..)`, `new T(..)`, `: T(..)`; `defchoice::constructPoint`): `definition` at that
   `(` / `{` is the constructor clangd resolved overload resolution to (clangd's selection tree puts the construct
   expression there; `Widget(2, 3)` answers `Widget(int, int)`, a delegating or base mem-initializer likewise).
   * the answer is the class's own position: an **implicit** constructor, labelled `class T (implicit constructor)`,
     one target; no answer: aggregate initialisation, the class alone;
   * the answer is on the `using Base::Base;` of an **inherited** constructor: the call's signature is read from
     `textDocument/ast` (the `CXXConstructExpr`, e.g. `'void (int) noexcept(false)'`) and matched, parameter types
     without names, against the base class's constructors in the fast index (`constructor Base(int) (inherited)`); if that
     fails the using-declaration is the second target;
   * otherwise the constructor (checked to be one: the fast index says Constructor, or the word is the class name),
     moved to its out-of-line definition when the index knows exactly one for that signature (`preferConstructorDefinition`,
     so the answer does not depend on how far clangd's background index is).
   A result on the caret's own line (the caret is on a declaration) is never refined.

Two tiers: the fast tier answers the entity / class as a single result and cannot tell a construction or an alias use from
a plain use. A single approximate answer that is a `Typedef`, or a class/struct/union where `constructPoint` finds a
construction, is flagged `refinable`. `Editor::deliverDefinition` (the callback of `gotoDefinition` /
`gotoDefinitionAt`) does **not** jump on a lone refinable approximate answer: it passes it on (the UI shows `waiting for
clangd`) and waits. The authoritative answer then either has one target (jump, as for any single answer) or several (no
jump; the front end opens the chooser, once). If clangd fails or answers nothing, it jumps to the held-back approximate
answer. An approximate answer with an error (clangd off, held back) always jumps at once; the budget (250 ms) still
means that when clangd is quick there is only the authoritative callback. A using-declaration is not in the fast index,
so a name that is *only* a using-declaration is a plain single answer to the fast tier: with a slow clangd it jumps first
and the chooser follows at the authoritative answer (documented in OPEN_QUESTIONS).

The chooser is `NavResultPicker` (below). The `def` of the corpus format can judge the whole list and its default
(docs/TESTING.md, `def A = B C default C`).

### Navigation like Visual Studio (VS-NAV)

* **Go To Document on `#include`.** `SemanticEngine::gotoDefinition` and `gotoDeclaration` first ask `gotoInclude`: the
  caret line is read by `incnav::includeAt` (`src/semantic/IncludeNav.*`: `#`, `include` / `include_next` / `import`, a
  literal `"..."` or `<...>`, comments blanked; the caret may be anywhere from the `#` to just after the closing
  delimiter). The file is then resolved the way the compiler does (`incnav::resolveInclude` over `searchPathOf` of the
  includer's compile command: its own directory for `"..."`, `-iquote`, `-I`, `-isystem`, `-idirafter`, `/I`, `/imsvc`;
  for a file in no command, the directories of every command; for an include-only fragment, the command of its TU
  context). A hit is delivered **Authoritative at once** (no clangd round trip: it is a file-system fact) as one
  `SymbolLocation` at 0:0 with `extent` = the first line, and `augmentWithFastDefinitions` / the word-based lookups are
  skipped (the word would be `h`). A miss goes through `locationQueryWith` (the old `locationQuery` body with a given
  approximate tier): the approximate tier is the workspace file whose path ends with the spelling nearest to the includer
  (FastIndex::exportFiles), clangd's `definition` is the authoritative one (system headers). The Editor needs no change: a
  single location jumps through `gotoLocation`.
* **Find All References.** `findReferences` post-processes clangd's list (`classifyReferences`): `SymbolLocation::role`
  (`Definition` when the fast index has a symbol of that name on that line, else `incnav::classifyReference`: Read / Write /
  Other from the line text; every file is read once, open documents from their buffers) and the order Definition first,
  then by file path, line, column. `NavResultPicker` turns a list whose entries carry a role into grouped rows
  (`ListRow::header` headings that `ListPopup` never selects, hovers or accepts; `rowLoc_` maps rows to locations).
* **Miscellaneous files.** `SemanticEngine::isMiscFile` (compile commands snapshot + fast-index include graph through
  `compdb::filesReachableFrom`, cached per snapshot and fast-index generation) decides in the clangd diagnostics handler:
  with `showMisc_` false (setting `diagnostics.miscFiles`, `Editor::setMiscFileDiagnostics`) the handler delivers an empty
  list and keeps the real one in `miscHeld_`, which flipping the switch on delivers. No database or an unfinished fast
  index means "not misc". Diagnostics set through `Editor::setDiagnostics` and the build's are not filtered.
* **Preview switch.** `ListPopup::onPreviewToggle` adds the header switch (click on `previewToggleRect()`, Alt+P) and the
  pickers keep the state in `PickerEnv::preview` until `EditorApp::applySettings` reads `picker.preview` again.

### What the fast tier indexes, and how its answer is shaped (W9-FIX-C1)

The fast tier is `CppLexer` + the `Extractor` in `src/semantic/FastIndex.cpp`: no preprocessing, no types, no scopes
below class / namespace level. It records `Symbol{name, scope, kind, line, col, definition, isTemplate, arity}` and
answers by *word*: `approximateDefinitions` takes the identifier under the caret, looks the name up and shapes the list.

*Indexed* (besides namespaces, classes, functions, methods, variables, fields, enumerators, typedefs and aliases):

| construct | symbol |
|---|---|
| `#define NAME` / `#define NAME(a, b)` | `Macro` at the name (also behind `# define`, a `/* comment */` or a backslash-newline after `define`); `arity` is -1 for an object-like macro, the parameter count for a function-like one (which counts as a use only when a `(` follows). No `#if` evaluation: every branch is indexed. A macro defined further down the *same* file is not offered for an earlier line. |
| `friend T f(...) { ... }` in a class (hidden friend) | `Function` in the enclosing *namespace* scope (not the class). A plain `friend` declaration or `friend class X;` adds nothing. |
| `operator T()`, `operator+`, `operator()`, `operator new[]`, `operator""_km` | named canonically (`operator Meters`, `operator+`, `operator()`, `operator new[]`, `operator""_km`; a space only between two word-like tokens; `operatorFunctionName()` builds the same name from text), positioned on the `operator` keyword. A caret on a written `operator` word is looked up under that name. **Operator tokens themselves (`a + b`, `x[i]`, `os << v`) and literal suffixes (`2.5_km`) have no fast answer** (a punctuator is not a word; resolving it needs the operand types); clangd answers. |
| `typedef enum { A, B } T;` | the enumerators (scope = enclosing scope), `typedef enum Tag {` also the `Enum`; `typedef struct Tag {` the `Struct`; one `Typedef` per declarator (`} S, *SP;`). The members of a `typedef struct [Tag] { ... } Name;` body are indexed too (scope = the tag, else the first declarator name). |
| `~Name` in a class | named `~Name` (as `Name::~Name` out of line already was), kind `Constructor` (special member). |
| `int (name)(int)` | a function (parenthesised declarator). |

*Not indexed:* names declared through macro arguments (`DECLARE_CLASS(Gadget)`, `X(Red, 1)` in a `.def` list,
`SHAPE(Triangle, 3)`). Which argument is the declared name, and of which kind, is a property of the macro body; a rule
"first plain identifier of a namespace-scope macro invocation" would add a symbol for every `TEST(a, b)`,
`REGISTER(x)`, `static_assert`-like call in the workspace, answer them for unrelated words, and still miss
`DECLARE_ENUM(Hue, Warm, Cold)` (the second argument) and pasted names (`idx_##name`). The lexer-only tier has no
safe way to tell; clangd (which expands the macros) answers these. `.def` / `.inc` files are scanned like headers but
contain nothing the extractor recognises.

*Answer shaping* (`FastIndex::shapeAnswer`, applied after the lookup and the `A::B::` qualifier filter; the order of the
remaining matches is kept):

* **Prototype and definition.** `gotoDefinition` keeps the definition of a function / method / constructor / variable
  that has a prototype of the same *scope + name + arity* and drops that prototype; `gotoDeclaration` does the
  opposite (prototypes win over the definitions they pair with). With no partner (only a definition, or only a
  prototype, or a different arity) whatever exists is answered. `arity` is the parameter count (default arguments
  count, `(void)` is 0). Overloads of the same arity are not told apart by this step (the scope-aware narrowing below
  separates them by argument count, not by argument type). `static int x;` inside a class is a *declaration* (its
  definition is `int C::x = 0;` elsewhere) and pairs like a prototype; `static const int x = 1;`, `constexpr` and
  `inline` members stay definitions.
* **Class name.** A name that is a class / struct / union / enum / typedef is answered without its constructors. With
  the caret *on the name of a constructor* (declaration or definition) the constructors with that arity in that class
  are answered instead.
* **`~Name`.** The destructors (`~Name` lookup); a class with no declared destructor falls back to the class.
* **Typedef and tag.** `typedef struct node node;` plus `struct node {...}`: a use outside `struct` / `class` /
  `union` / `enum` answers the typedef, a name written after such a keyword (a `/* comment */` in between is skipped)
  answers the tag. Only a typedef and a tag of the *same scope* compete.
* Hover, type definition and implementation keep the unshaped (`Want::Any`) list apart from the three rules above.

The persisted store (`FastIndexStore`, format version 2) keeps the arity in bits 2-7 of the flags byte and, in a second
byte, `minArity + 1` (bits 0-5), `fileLocal` (bit 6) and `variadic` (bit 7); `kExtractorSalt` is 5 (the fingerprint also
covers a probe of the extractor output, so any change of what it emits invalidates old stores). `Symbol` is still 80
bytes (`SymbolKind` is a `uint8_t` enum, the new fields fill the padding).

### Scope-aware lookup in the fast tier (W11-FAST-SCOPE)

The by-name answer above is narrowed with what can be read off the caret's own file and the include graph, so that the
first jump is the entity C++ name lookup picks and not every function with that word. Everything is lexical: a rule
applies only when its evidence is clear, and **a rule that would leave no candidate is not applied** (the narrowed answer
is never emptier than the by-name one; clangd stays the authority and replaces it).

*Pieces* (`src/semantic/`):

* `ScopeScan.{h,cpp}`: `scanScopes(text)` lexes a file once into a `ScopeMap`: a tree of regions (global scope,
  `namespace` incl. `a::b`, anonymous and `extern "C"`; classes with their bases as written; function bodies with the
  declarator name as written, `Vec2::length_sq`; lambdas and blocks) with, per region, the parameters, locals and
  fields (`ScanVar`: name, declared type without cv / `*` / `&` / template arguments, first template argument, pointer,
  position), `using namespace` / `using ns::name` and `namespace x = y`. `auto x = new Foo(...)`, `= Foo(...)`,
  `= std::make_unique<Foo>(...)` give the variable a type; structured bindings, `for` / `if` / `catch` declarations and
  lambda parameters are locals. Brace initializers (`int x{1}`, `S() : a{1} {}`) are not function bodies. Bounded: texts over 512 KiB are not scanned, no
  recursion (10k nested braces are fine), fuzz target `scopescan`.
* `FastScope.{h,cpp}`: `narrowByScope(env, CaretInfo, matches)`, `findLocal`, `accessKindOf`, `callArgCount`,
  `constructionArgCount`, and `ScopeCache`. `SemanticEngine::scopeMapOf` builds a map from the open document's buffer
  (cache key: the document's version) or from disk (key: mtime and size of the index entry); at most 48 maps / 400k
  regions are kept, least recently used out.
* `FastIndex::reachableFrom(file, quoteDirs, angleDirs)`: the files the caret's file can see through `#include`,
  transitively. A spelling is resolved next to the includer, then in the compile command's include directories (`-iquote`,
  `-I`, `-isystem`, ...; `SemanticEngine::includeDirsOf`, through `incnav::searchPathOf`), then by path suffix against the
  indexed files (a basename table, `byBase_`, kept by `addLocked`). Cached per file until a file is added, removed or
  changes its `#include` list; a walk over more than 6000 files gives up (no restriction).

*Rules*, in the order `narrowByScope` applies them to the by-name candidates (after the `A::B::` qualifier filter, before
`shapeAnswer`):

1. **A local wins.** An unqualified word that names a parameter or local of the enclosing function (declared before the
   caret in an enclosing block, not after `.` / `->` / `::`) is answered with that declaration alone (`variable`,
   Approximate), whatever globals share the name.
2. **File-local entities.** `static` functions / variables at namespace scope (`Symbol::fileLocal`) and entities in an
   anonymous namespace are offered only in their own file, or from a header (which the caret's file may include); a
   file-local entity of the caret's file hides same-named entities of other files.
3. **Member access** `x.f` / `p->f` / `this->f`: only members (`Method`, `Field`) are candidates. When the receiver's
   declared type is known lexically (local, parameter, field of an enclosing or base class, global of this file;
   `a.b.c` chains through the fields' declared types; `unique_ptr<T>` / `shared_ptr<T>` with `->`; `vector<T>` with
   `[i]`; `auto x = Foo(...)`) the candidates are narrowed to that class, **the most derived class that has the member
   hides the same name in its bases** (`Derived d; d.own()`; `Base* b; b->own()` stays `Base::own`). Members of anonymous
   unions / structs count as members of the enclosing class.
4. **`::f`** means the global scope; **`Derived::f`** falls back to the bases of `Derived`, and a namespace alias
   (`namespace fs = std::filesystem; fs::path`) is expanded.
5. **Unqualified lookup** follows C++: the caret's chain of scopes, innermost first: the enclosing class (for an
   out-of-line `void A::B::f() {}` the class `A::B`, resolved against the namespaces around it), its bases generation by
   generation, the enclosing namespaces (each with the `using namespace` / `using ns::name` seen so far), the global scope.
   A candidate found in an inner scope hides outer ones; macros are exempt. When no candidate is in any scope of the chain
   (ADL, names from a header that is `using`-ed elsewhere) the candidates are left alone.
6. **Kinds.** After `struct` / `class` / `union` / `enum` only tags; `Name(` with a *function* of that name in the same
   scope as the class drops the class (C `stat`); explicit template arguments (`get<int>(x)`) keep the templates.
7. **Overloads by argument count.** The call's `(...)` is counted (`f(a, g(b, c))` = 2; angle brackets that may be
   template arguments make the count unknown): candidates need `minArity <= n <= arity` (variadic: `n >= minArity`).
   Overloads of equal arity stay (no argument types).
8. **Include graph.** Candidates in files the caret's file cannot see through `#include` are dropped when some candidate
   is visible; a definition in an invisible file stays when its prototype (same scope, name, arity) is visible, so the
   `.cpp` of a visible header is still offered.
9. **A written construction** (`Widget(2)`, `Widget w{1}`) still answers the class alone: the constructor is the
   authoritative tier's refinement (W10-CHOOSE, `refinable`), the fast tier does not add it.

Not done (stays a superset of clangd's answer, the chooser shows the candidates): overloads of the same arity (types,
cv / ref qualifiers), explicit and partial specialisations of a class template, alias chains (`using B3 = B2` answers
`B3`, clangd also the targets), `#if` branches (every branch is indexed; the compile command's defines are not
evaluated), names declared through macros, operator tokens, forward declarations of classes for `decl`.

Cost (`docs/STRESS_RESULTS.md`, "W11-FAST-SCOPE"): the scope map of a file is built on the first query in it (about
50 microseconds for a typical file) and reused until the document changes; a warm query adds about 2 microseconds to
the 2 microsecond by-name lookup. Index build time and memory are unchanged (`Symbol` is still 80 bytes; the persisted
store grows by one byte per symbol; the basename table is one pointer per file).

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

## Translation-unit context for included files, rename safety, index readiness (W9-FIX-B)

**The problem.** A file that is only ever `#include`d (`.tpp` `.ipp` `.inl` `.def`, an X-macro list, a unity-build part
`beta.cpp` included by `build_unity.cpp`) has no compile command and is not self-contained. When tenfold `didOpen`s it
(go to definition landed there, a rename edited it, the user opened it) clangd parses it **alone**: the names of the
header are undeclared, definitions resolve to nothing, and, worse, the open document's dynamic index replaces that file's
entries in the merged index (`MergedIndex` drops static symbols/references whose file the dynamic index owns), so find
references and rename lose every use inside the file and a rename silently leaves code that no longer compiles
(STRESS_RESULTS W9b F6/F7, W9c P-3/P-9/P-10/P-14, W9e 11/13/14).

**Choice.** Give the file the context of a translation unit that includes it, instead of not opening it. Not opening it
would keep the background index intact but leave queries *inside* the file with the fast tier only, and clangd cannot answer
for a file it has not been sent. clangd has no "parse this header as part of that TU" (its own heuristics for headers borrow
the flags of a similar file, never the text that precedes the `#include`), so tenfold reproduces the text:

1. `SemanticEngine::provideTuContext` runs before a document is sent to clangd. Candidates: no compile database entry, extension
   not one of `.h .hh .hpp .hxx .h++ .hp .cuh` and not extensionless (clangd's header heuristics keep those).
2. `tu::planContext` (`src/semantic/TuContext.*`, pure, unit tested) does a breadth-first search up the `#include` graph (includes
   as written, from the fast index; verified against the includer's current text, the editor buffer when open) to the nearest
   file that has a compile command: the shortest chain `main.cpp -> stack.h -> stack.tpp`. For every file of the chain it keeps
   the text **before** the `#include` line plus one `#endif` per `#if` still open: the "prefix". Line and column numbers of the
   prefix are those of the original. If the orphan itself includes a file of the chain (a self-contained `.inl`), it brings its
   own context and is left alone. Nothing includes it: left alone.
3. The prefixes are written to `<cache>/tu/<hash>-<name>.h` and clangd gets, via `workspace/didChangeConfiguration`
   (`compilationDatabaseChanges`, in memory, applies to the open document and to the background index), the root's command
   with the main file replaced by the orphan, `-x c++-header` (`-x c++` for `.cpp` unity parts), and `-include <prefix>
   -iquote <dir of the includer>` per chain file. A unity part therefore sees `build_unity.cpp`'s flags and, through the
   prefix, the earlier parts; `clang-cl` commands get `/FI` `/I` `/TP`. An X-macro list is parsed in the context of the
   **first** `#include` of it (the first expansion).
4. Everything clangd then reports in a prefix copy is mapped back to the original (`remapUri`: `fromServer`, `posFromServer`,
   `editsFromServer`, hierarchy items; identical locations and identical edits are de-duplicated), so users never see
   `.tenfold/tu`. The overrides are kept per file and re-sent to a restarted clangd before the documents are re-opened.

Limits: the prefix is built when the orphan is opened (an unsaved edit of the includer afterwards is picked up at the next
open); a file included from several places is parsed in the context of the first chain found (as clangd does for headers);
an `#include` inside a class or namespace leaves it open at the end of the orphan, clangd reports "expected }" there.
`SemanticEngine::tuContext(file)` returns what a file was given (tests, diagnostics).

**Rename safety.** `Editor::renameSymbol` asks `SemanticEngine::renameCoverage(oldName, edits, origin)`: a lexical scan (no
comments, strings or `#include` lines) for the old identifier in the edited files, the files that include them (3 levels, fast
index include graph) and the files they include (two levels; at most 300 files / 8 MB) that is not at the start of an edit.
If anything is found and the call was not forced, **nothing is applied**: the result has `needsConfirmation`, `missed`
(<= 20 places) and `missedTotal`. The UI (`RenameInput`) shows the first places in the status line and re-opens the box
("may miss N uses; Enter again to force"); a second Enter applies the edits (`renameSymbol(..., force=true)`). The corpus
runner forces and reports the places in the note of the rename line ("rename warning"). A same-named unrelated symbol is
reported too (the check errs on the side of asking).

**Request watchdog and the cause of "a rename that is never answered" (W9g G-2).** Reproduced with `pp-includes` run 12 times
in parallel on a loaded machine: after a `rename` the next request stays "queued" for good (about 1 run in 3 before the fix,
none in 12 after). Thread dump of the wedged clangd: no thread runs; the `ASTWorker` / preamble threads of several files wait
on one semaphore on the stack of clangd's `main` (the scheduler's concurrency barrier), whose permits are never released. The
trigger is on tenfold's side: a document is **closed while clangd is still building it**. A rename across files opens the files
it edits, edits, saves and closes them in a row (`Editor::applyRenameEdits`; the corpus restore does the same), and
`didClose` of a file whose update is still queued or running leaks a permit. `SemanticEngine::didClose` therefore holds the
`didClose` back until clangd reported the file `idle` (`closeWhenIdle`, polled every 25 ms, at most 30 s; the close is dropped
if the file was opened again meanwhile, the new `didOpen` is an update of the open document). The same hang happens with the
translation-unit context switched off, so it is not caused by it.

Independently, `SemanticEngine::Options::requestTimeoutMs` (25 s): definition, declaration, type definition, implementations,
references and rename that clangd has not answered are cancelled with `$/cancelRequest` and their callback gets "clangd did
not answer ... ; the request was cancelled"; a late reply is ignored. All callbacks are asynchronous, so the UI never waits.
`implementations` answers that are in-class declarations are replaced by the out-of-line definition of the same
function that the fast index knows (`preferDefinitions`; W9g G-1: clangd answers the declaration once the header is open).

**Index readiness.** What the investigation found, against clangd 22.1.1:

* The first `$/progress end` is not a barrier. It can precede the rebuild of the merged index that queries use (on a loaded
  machine a `workspace/symbol` made right after the report found a symbol 130 ms later), and another pass can start right
  after it. `waitForClangdIndex(timeout, quietMs = 300)` now needs one completed pass **and** 300 ms of continuous idle
  (the engine counts the `end` reports of the current clangd and remembers when it went idle).
* clangd's background index follows **no** disk change: `didChangeWatchedFiles` and `didSave` of a closed file re-index nothing
  and report nothing; a file is re-read when it is *opened* (silently, no progress events), and a header's entries only change
  when a translation unit that includes it is re-read. So there is no signal to wait for after a rename wrote files; the
  runner re-opens the closed files it restored and closes them again so that translation units catch up, and the corpora do
  not depend on a rewritten header being fresh (a name used by a later `rename` is never the one an earlier `rename` renamed).
* Most of what looked like index timing was **shadowing**: the `.def` / `.tpp` / unity part that a `def` line navigated into was
  opened standalone and its empty dynamic index removed the static entries declared in it (the delta-debugged W9c repro:
  `def use_op_mul` opens `ops.def`, then `sym "op_mul"` finds nothing). With the translation-unit context the repro is
  stable and the `#?` lines of `pp-xmacros` / `pp-includes` that were disabled for it are enabled again.
* Requests are answered in order per document, so a query after an `edit` sees the edit; the "dynamic index lags an edit"
  remark of W9d is the AST-then-index handoff inside clangd (a `workspace/symbol` right after `didChange`), not a tenfold race.

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
* **Rebuild and single-file builds (W8e)**: `BuildOverrides::kind` selects what `detectBuildCommand` returns. `Rebuild`
  (RebuildActiveWorkspace, `Build.Rebuild`): `rebuildCommand` (override argument or setting, a shell command) >
  `msbuild <sln> /t:Rebuild /m ...` > `cmake --build <dir> --config <cfg> --clean-first` > the shell command
  `make clean && make -j<cores>`; a custom `buildCommand` without a `rebuildCommand` is an error (nothing can clean it).
  `CurrentFile` (BuildCurrentFile, `Build.BuildCurrentFile`, `BuildOverrides::file`; `buildCommand` is ignored): a
  solution or lone `.vcxproj` -> `msbuild <owning project> /t:ClCompile /p:SelectedFiles=<file> /p:Configuration=..
  /p:Platform=..`, where the owning project is the first project of the solution whose `ClCompile Include` items name the
  file (read with `XmlLite`; wildcards and imported item groups are not expanded) and the configuration is the
  solution's mapping of the active one (`SolutionInfo::mapping`); otherwise the file's entry in the compile database
  (`CompileDatabase::find`): `ninja -C <entry directory> <object>` when that directory holds a `build.ninja` and the
  command has an output (`-o` / `/Fo`), else the entry's own argv run in its directory (the 10x documentation only says
  "Build the current file", so it is a real compile, not `-fsyntax-only`). A file without a project or entry is an
  error. Both run through the same `Editor::build`, so the Build panel, `buildMessages`, squiggles and F8 work unchanged.
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

## Debugger (C2a, `src/debug`)

The engine side of the Visual Studio debugger integration (OPEN_QUESTIONS #9). No window, render or COM code lives
here; the Visual Studio backend (C2b, `VsDebugger_win32.cpp`) and the UI (C2c) plug into it.

* **`Breakpoints`** is the store: one breakpoint per (file, 0-based line) with `enabled` and `condition`. It follows
  edits with the same `onLinesReplaced` / `onFileRenamed` contract and shifting rule as `Bookmarks` (the `Editor`
  forwards the document's line-change notification to both), is persisted in `.tenfold/breakpoints.json`
  (`{"version":1,"breakpoints":{"<relative path>":[{"line","enabled","condition"}]}}`, loaded with the first
  document or `start()`, saved when it changed and on `save()` / `shutdown()`), has a generation counter and a change
  handler (used by the session).
* **`Debugger`** is the abstract backend (`start`, `startWithoutDebugging`, `stop`, `restart`, `breakAll`, `stepOver`,
  `stepInto`, `stepOut`, `runToCursor`, `setBreakpoint`, `removeBreakpoint`, `connect(workspace + solution)` /
  `disconnect`, `poll()` returning a `DebugSnapshot {mode, currentStatement, breakpoints, lastError}`). Every call
  returns `DebugResult {ok, error}`; nothing throws across it. Modes: `Disconnected`, `Design`, `Running`, `Break`.
  `cancel()` is the only method that may be called from another thread (to unblock a call at shutdown).
* **`DebugSession`** (`Editor::debug()`) owns the backend and one **worker thread**. Threading rules:
  * The backend is only ever called on the worker, one call at a time (so a COM apartment is safe); the backend
    object is also destroyed on the worker thread.
  * Commands are queued from the main thread and return immediately; the worker also polls the backend, every
    100 ms while Running / Break and every 1000 ms otherwise (`setPollIntervals` for tests). A poll runs right after
    a command when the queue is empty. Snapshots are posted only when they differ from the last posted one.
  * Everything reaches the main thread through `Editor::post()`; state (`mode()`, `currentStatement()`,
    `lastError()`, `status()`, `generation()`) changes only inside `pump()`.
  * Shutdown (`Editor::shutdown()` / destructor) stops the worker, calls `Debugger::cancel()`, waits at most
    `setShutdownTimeout()` (3 s) and, if a backend call still does not return, **abandons** the thread: it owns its
    state through a `shared_ptr`, posts nothing (the poster is cleared) and tidies up when the call finally returns.
    The Editor never hangs on a stuck debugger.
* **Two-way breakpoint sync.** The session remembers what the backend was last told (`known_`). Any change of
  `Breakpoints` (toggle, condition, a moved line, a rename) is diffed against it and pushed as `removeBreakpoint`s
  first, then `setBreakpoint`s. A poll snapshot is applied to `Breakpoints` (the backend wins for state it reports)
  only if the worker had executed every breakpoint command the main thread had queued when the snapshot was taken, so
  a poll that overtook a push never undoes it; applying a snapshot is wrapped so the change handler does not push
  it back (no echo). A failed `setBreakpoint` is reported and forgotten (the breakpoint stays in tenfold and is retried
  with the next change); a `Disconnected` snapshot never deletes breakpoints. On connect everything in `Breakpoints`
  is pushed and breakpoints only the backend has are adopted.
* **Configuration and breakpoint sync (C2d).** Mirrors 10x's `VisualStudioSync` (workspace setting
  `debug.visualStudioSync`, default true). `DebugSession` calls the optional `Debugger::setActiveConfiguration(name,
  platform)` right before `start`, `startWithoutDebugging`, `restart` and `runToCursor` (not before steps or `break`),
  with `Workspace::activeVsConfiguration()` and only when the workspace has a solution. The Visual Studio backend finds
  the entry in `DTE.Solution.SolutionBuild.SolutionConfigurations` by `Name` + platform (case-insensitive) and
  calls `Activate()`. The items dispatch as the base `SolutionConfiguration`, which has no `PlatformName`, so the platform
  is read from `PlatformName` when present, else from the first `SolutionContexts` item, and if that fails too the
  `Build.SolutionConfigurations` / `Build.SolutionPlatforms` commands are the last resort; it does nothing while a debuggee exists (Visual Studio refuses to switch then) and when that
  configuration is already active; an unknown one is an error that lists what Visual Studio has. A failure never blocks
  the command: the command runs and the session shows the problem in `lastError()` (`DebugResult::warning` carries it
  on a successful command). With the setting off there is no configuration sync, no breakpoint push or adoption and no
  attach at startup; commands still work.
* **Attach-only connection (C2b2).** Breakpoints toggled in tenfold only reach the debugger once the session is
  connected, and a command connects implicitly (the Visual Studio backend may then launch devenv).
  `DebugWorkspaceInfo::launchIfMissing` (default true) switches that off: `connect()` then answers
  `DebugResult::noInstance()` (`notRunning`) when no instance has the solution open. `DebugSession::attachIfRunning()`
  connects that way and treats `notRunning` as nothing wrong: no `lastError`, nothing in `status()`, mode stays
  `Disconnected`. `Editor::start()` calls it once when the workspace setting `debug.attachAtStartup` (default true,
  `.tenfold/workspace.json`) is on and a backend factory exists. While nothing is attached (never was, or the
  instance went away: a `Disconnected` snapshot) and `Breakpoints` is not empty, the worker retries the attach-only
  connect by itself every `setAttachRetryMs()` (default 10 s; 0 = never), so opening the solution in Visual Studio
  later picks up tenfold's breakpoints (on success everything in `Breakpoints` is pushed as after any connect).
  Other problems of a retry are reported once. An explicit `disconnect()` stops the retries; commands launch as before.
* **Follow current statement.** Entering Break mode, or a new current statement while in Break, calls
  `Editor::gotoLocation({file, {line, 0}}, ..., "debugger")` (so Back returns) when
  `DebugSession::followCurrentStatement` (default true) is set. The statement is also exposed for the UI.
* **Backend creation.** `Editor::setDebuggerFactory(fn)` installs the factory (Windows: the VS backend); `connect()`
  (or the first command, which connects implicitly) calls it. Without a factory or `setBackend()` the session reports
  "no debugger backend on this platform".
* **`FakeDebugger`** (`src/debug/FakeDebugger.*`, compiled into `tenfold-tests` only) records calls with their thread,
  scripts mode transitions, injects backend-side breakpoint changes / errors / blocked calls. Tests:
  `tests/test_debug_breakpoints.cpp`, `tests/test_debug_session.cpp`.
### Visual Studio backend (C2b, Windows)

`src/debug/VsDebugger_win32.cpp` (factory `createVisualStudioDebugger()`, installed by `src/ui_app/main.cpp` on
`_WIN32` through `Editor::setDebuggerFactory`; other platforms keep "no debugger backend") implements `Debugger` on
Visual Studio's automation model (EnvDTE) with late-bound `IDispatch` only: no `#import`, no generated headers, so it
builds with MSVC, clang-cl and mingw-w64. COM headers appear only in the `_win32` files; the pure helpers (moniker
parsing, version order, path normalisation, line / mode conversion, HRESULT classes) live in `VsDteUtil.*` and are
unit-tested on every platform. `ComDispatch_win32.*` holds the RAII `Bstr` / `Variant` / `Dispatch` wrappers and the
Running Object Table enumeration.

* **Threading.** Every call, and the destructor, runs on the `DebugSession` worker. `connect()` calls
  `CoInitializeEx(COINIT_APARTMENTTHREADED)` and registers an `IMessageFilter`; the destructor releases the DTE
  object, unregisters the filter and calls `CoUninitialize`. The filter retries calls Visual Studio rejects as busy
  (`RPC_E_CALL_REJECTED`, `RPC_E_SERVERCALL_RETRYLATER`) every 100 ms for up to 15 s per call of a command. A
  `poll()` gets a window of 1 s for the whole poll (`VsBusyPolicy`, both values configurable through
  `createVisualStudioDebugger(policy)`; the decision is the pure `retryDelayMs`), so a Visual Studio stuck in a modal
  dialog cannot delay queued commands: the poll fails with "Visual Studio is busy" and the session keeps its last
  snapshot. A poll never reports a partial breakpoint list: a busy or dead answer while reading it fails the poll.
  `cancel()` sets an atomic flag that the filter and the devenv launch wait check, so shutdown never hangs.
* **Connecting.** The ROT is enumerated for monikers `!VisualStudio.DTE.<ver>:<pid>` (any version, newest first). The
  instance whose `Solution.FullName` equals the workspace solution (case-insensitive, separators, `.`/`..`, `\\?\`
  normalised) wins. Otherwise a new instance is created through COM automation (`CoCreateInstance` of the DTE
  ProgID, newest version first, then `VisualStudio.DTE`; on a helper thread so the 90 s launch bound and `cancel()`
  apply), made visible (`MainWindow.Visible`, `UserControl = true`: a normal window that stays open after tenfold
  disconnects) and told `Solution.Open(sln)`. Only if that fails is `devenv.exe "<sln>"` spawned (`vswhere.exe -latest
  -property productPath` under `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer`; `TENFOLD_DEVENV` overrides the
  path) and the ROT polled for up to 90 s until an instance has loaded that solution; that timeout's error lists the
  automation failure and every Visual Studio entry of the ROT with its `Solution.FullName`. Without a solution the newest running instance is used
  and `DebugSnapshot::lastError` carries a note saying so. A dead instance (`RPC_E_DISCONNECTED`,
  `RPC_S_SERVER_UNAVAILABLE`, ...) turns the next poll into `Disconnected`; the next command reconnects.
* **Commands.** start / continue `Debugger.Go(false)`; Ctrl+F5 `ExecuteCommand("Debug.StartWithoutDebugging")`;
  stop `Debugger.Stop(false)` (a success in design mode); restart `ExecuteCommand("Debug.Restart")`; break all
  `Debugger.Break(false)`; steps `StepOver/StepInto/StepOut(false)`; run to cursor opens the file at the line
  (`ItemOperations.OpenFile`, `TextSelection.GotoLine`) and then `ExecuteCommand("Debug.RunToCursor")` (that command
  works on the active editor's caret; `Debugger.RunToCursor` takes no position).
* **Breakpoints.** `Debugger.Breakpoints.Add(, File, Line, Column, Condition, ConditionType)` (lines 1-based in VS,
  0-based here), `Breakpoint.Enabled`, `Breakpoint.Delete`. A changed condition deletes and re-adds (it is read-only
  in EnvDTE). Idempotent; removing a missing one succeeds. Breakpoints without a file (function breakpoints) are
  ignored.
* **poll().** `CurrentMode` (1 design, 2 break, 3 run), `CurrentStackFrame.FileName` / `LineNumber` in Break mode (a
  frame without source gives no current statement; only when the frame cannot be read at all does it fall back to
  `ActiveDocument.FullName` + `Selection.CurrentLine`), and the whole breakpoint list.
* **Open in Visual Studio at the caret.** `Debugger::openInEditor(file, line, column)` is an optional virtual
  (default: not supported) implemented here. `DebugSession::openInEditor(file, line, column, done)` queues it on
  the worker like the other commands (connecting first) and `Editor::openInVisualStudio()` calls it at the primary
  caret of the current document; the UI command (C2c) binds to that.
* **Tests.** `tests/test_vs_debugger.cpp`: helpers everywhere; on Windows the wrappers against
  `Scripting.Dictionary`; `vsdte_integration` only with `TENFOLD_VS_DTE_TEST=1` (creates a tiny solution, launches
  devenv, breakpoint / start / step / VS-side breakpoint / stop, then `DTE.Quit`). The CI job `windows-vs-debugger`
  (non-blocking) runs it on `windows-2022`.


### Debugger UI (C2c)

Looks like Visual Studio; everything runs against whatever backend the session has (the UI tests use `FakeDebugger`,
compiled into `tenfold-ui-tests` too).

* **Glyph margin** (`TextView`, `paintGlyphMargin`): a lane one line height wide at the left edge of the gutter, left of
  the line numbers; `Layout::setGlyphLane` adds its width to `gutterWidth` (so `textOffset`, hit testing, the wrap
  width and the caret all move with it; another gutter strip between numbers and text, e.g. diff marks, adds its own
  term the same way). The lane exists while the Editor has a debugger backend (`DebugSession::hasBackend()`: always
  on Windows, where the factory is installed) or any breakpoint at all (workspace-wide, so the text does not jump when
  you switch tabs). Without either, the gutter is exactly what it was before, which is why the older goldens are
  unchanged. Glyphs are on the first visual row of a line only (B2c's row model): breakpoint = red disc
  (`debugBreakpoint`, built-in #E51400), disabled = ring, conditional = disc with a white plus (ring with a plus in the
  ring colour when also disabled); current statement = arrow in `debugCurrentArrow`, drawn over the disc. The bookmark
  flag keeps its small square but sits just right of the lane. Shapes are 1px-high rects (no circle primitive).
* **Click**: a left press in the lane toggles a breakpoint on that line (a click on any row of a wrapped line counts)
  through `Breakpoints::toggle` + `Editor::saveBreakpoints`; the glyph appears at once (optimistic), the session pushes
  `setBreakpoint` / `removeBreakpoint` to the backend and takes the backend's version back after `pump()` (the backend
  wins; a breakpoint it could not bind stays with the normal glyph and the error goes to the status bar). The press
  moves no caret and takes no focus.
* **Current statement**: only in Break mode, in every `TextView` whose document is the statement's file (not only the
  current one): the arrow plus a band over all rows of the line (`debugCurrentLine`, behind selection and text). The
  jump to it is the engine's (`gotoLocation`).
* **Repaint**: `syncState` compares `Breakpoints::generation()` and `DebugSession::generation()` with the last seen.
* **Commands** (`EditorCommands.cpp`, engine-level, named in the registry): `Debug.Start` F5, `Debug.StartWithoutDebugging`
  Ctrl+F5, `Debug.Stop` Shift+F5, `Debug.Restart` Ctrl+Shift+F5, `Debug.ToggleBreakpoint` F9,
  `Debug.ToggleBreakpointEnabled` Ctrl+F9, `Debug.DeleteAllBreakpoints` Ctrl+Shift+F9, `Debug.StepOver` F10,
  `Debug.StepInto` F11, `Debug.StepOut` Shift+F11, `Debug.RunToCursor` Ctrl+F10, `Debug.BreakAll` Ctrl+Alt+Break; and
  bound by `EditorApp`: `Debug.ShowBreakpoints` Ctrl+Alt+B, `Debug.EditCondition` (no key). Debug commands queue on
  the session and return at once (asynchronous). Ctrl+Alt+B used to be `View.ToggleBuildPanel`; that moved to
  Ctrl+Alt+O (Visual Studio's Output window key).
* **`BreakpointList`** (`src/ui/features`): modal input-less `ListPopup`, one row per breakpoint (`[x] path:line`,
  condition or line text; `[ ]` and dim when disabled). Enter jumps with `gotoLocation` (Back returns), Space toggles
  enabled, Delete removes; the list follows changes from the backend (`refresh()` from `EditorApp::tick`).
  `openCondition()` is the `Debug.EditCondition` input line (empty text removes the condition).
* **Status bar**: segment `debug` (declared after `build`) shows `DebugSession::status()` while connected or not
  Disconnected; empty otherwise. `StatusBar::setDebugging(true)` while the mode is Running or Break switches the bar to
  `statusBackgroundDebugging` / `statusTextDebugging` (vs-dark: #CA5100 / white; `dark` and `light` leave them
  transparent = unchanged look). A new `lastError` of the session is shown in the status bar message like other errors.
* **`Debug.OpenInVisualStudio`** (bound by `EditorApp`, no default key; 10x's `OpenCurrentFileInDebugger` is `Alt X`): `Editor::openInVisualStudio()` (current file at the caret in the connected /
  attached Visual Studio). Without a backend the status bar message says so; a backend failure arrives through the
  session's `lastError` and is shown like other debugger errors.
* **`ConfigurationPicker`** (`src/ui/features`, C5 UI; `Build.SelectConfiguration`, no default key): modal `ListPopup`
  with a filter line listing `Editor::vsConfigurations()` ("Debug|x64", ...), the active one badged "active" and
  preselected. Enter calls `Editor::selectVsConfiguration(name, &err, applied)`. Status-bar segment `config` (declared
  after `message`, click opens the picker) shows the active configuration, `Configuration: Release|x64 (reloading...)`
  until `applied` fires, and nothing for a workspace without a solution configuration. The list is read from the .sln
  files on construction, on opening the picker and after a switch, not per frame (a .sln added later appears after the
  first open of the picker).
## Version control (C3a, `src/vcs`)

Provider-neutral engine plus the Git provider; Perforce (C3b) and the UI (C3c) plug into it (both are done). Nothing in `src/vcs`
includes a UI header; it depends on `core` and `lsp/Process.h` (and on `app/Editor.h` in `VcsManager.cpp`, for
`post()`).

```
 Editor ── owns ── VcsManager ──(jobs)──▶ worker thread ── owns ── VcsProvider (GitProvider, PerforceProvider)
    ▲                    ▲                      │  child processes, line diff
    │ open/edit/save/    │ hunks(), fileStatus() │
    │ reload/close/      └──────── Editor::post() ◀┘  results: base text, status, hunks
    │ file events
```

* **`VcsProvider`** (`Vcs.h`): `detect`, `status(files)` (one `VcsStatus` per file: Unmodified, Modified, Added,
  Deleted, Renamed, Untracked, Ignored, CheckedOut, ReadOnlyNotCheckedOut, Conflicted, Unknown; an empty list means
  "the whole workspace, changed files only"), `baseText(file)` (the committed / have revision; nullopt without an
  error means there is no committed version), `revert(file)`, `beforeFirstEdit(file)` (no-op for Git; Perforce runs
  `p4 edit`). Every call blocks on a child process and is made from the manager's worker thread only. `runCommand`
  (`VcsExec.h`) runs a tool without a shell (arguments are passed as they are), captures stdout and stderr and
  enforces a timeout.
* **`GitProvider`**: `rev-parse --show-toplevel`, `status --porcelain=v2 -z -uall` (renames, untracked, conflicts),
  `show HEAD:<path>`, `restore --source=HEAD --staged --worktree` with `checkout HEAD --` as the fallback for old git.
  `GIT_LITERAL_PATHSPECS=1` and `--no-optional-locks` are always set (names with `*`, `[`, spaces or non-ASCII text
  work; a background status never takes the index lock away from the user's own git). A staged rename queried by path
  is recognised through `diff --cached -M`, because a pathspec hides the other half of the rename.
* **`LineDiff`**: Myers O(ND) on integer line ids after a common prefix / suffix trim. Texts are split like
  `TextBuffer::setText` (a CR before an LF is dropped), so the committed text of a CRLF checkout and an LF buffer
  (or the reverse, `core.autocrlf`) compare equal. The edit distance is capped (`DiffOptions::maxEditDistance`,
  3000); beyond it the changed middle part becomes one hunk. Hunks are `{baseStart, baseCount, newStart, newCount,
  kind}` with 0-based line indices; `hunkBaseLines` returns the deleted / replaced base lines for an inline display
  and `revertHunkEdit` the one `Document::replace` that restores them.
* **`VcsManager`** (`Editor::vcs()`): picks the first provider whose `detect()` accepts the workspace root
  (`Options::providers`; default Perforce, then Git, narrowed by the workspace setting below). Per open document it keeps the base lines (split
  on the worker, shared immutably), the last hunks and the document version they belong to. A new file in the index
  has an empty base (the whole file is one Added hunk); an untracked file has no base and no hunks.

### Perforce (C3b, `PerforceProvider`)

`p4` (or `vcs.p4Path`) is always run as `p4 -ztag ...` with the workspace directory as cwd, so P4CONFIG / P4CLIENT /
P4PORT / P4USER resolve exactly as in the user's shell. Paths are separate arguments with `% @ # *` escaped.

* **detect**: `p4 -ztag -F %clientRoot% info` (3 s timeout; `null` / `*unknown*` = no client) and `p4 -ztag where
  <dir>/...` (needs a `depotFile` that is not an exclusion). Any failure (no `p4`, server down, directory outside the
  client view) is a quick `false`, cached for 15 s per executable and directory so Git-only users with `p4` installed
  do not pay the timeout again.
* **status(files)**: one `p4 -ztag fstat -T depotFile,clientFile,headRev,haveRev,action,movedFile,unresolved,
  headAction,headType,type <files>` (100 files per call). `unresolved` is Conflicted; `action` edit / integrate /
  other is CheckedOut, add / branch Added, delete / move/delete Deleted, move/add Renamed (`movedFile` is mapped to a
  local path with `p4 where`); no `action`: no record is Untracked (Unknown when p4 says "not under client's root" /
  "not in client view" or the file does not exist), a head revision deleted is Untracked, otherwise read-only on disk
  is ReadOnlyNotCheckedOut and writable Unmodified. Empty list (workspace-wide): `p4 -ztag fstat -Ro ... <root>/...`
  (opened files only; unlike `p4 opened` it reports local paths). A writable, un-opened file whose content changed
  (for example a `+w` filetype) reads Unmodified: finding that needs `p4 reconcile`, which is not used.
* **baseText**: `p4 -ztag fstat` (type, action, haveRev), then `p4 print -q <file>#have`. Added / branched / moved-in
  files, files not in the depot and binary-ish types (`binary`, `ubinary`, `utf16`, `apple`, `resource`, `symlink`,
  with or without `+modifiers`) have no textual base (nullopt, empty error). Bytes are returned as printed; line
  endings are ignored by the diff as for Git.
* **revert**: refused with "not opened" unless `fstat` shows an `action`; then `p4 revert <file>`.
* **beforeFirstEdit (auto-checkout)**: in the depot and not opened (or opened but read-only) -> `p4 edit <file>`, then
  `fstat` again: the file must be opened AND writable on disk, the exit code alone is not trusted (an exclusive lock
  is a warning). The editor never changes file attributes itself. Failure returns false with p4's message
  (`VcsManager::onError`); the edit stays in the buffer. Files not in the depot, deleted at the head or not synced are
  left alone; `p4 add` is never run automatically (`PerforceProvider::add` exists for the UI to call).
* **Saving a read-only file**: `Editor::save` checks the file's write permission first and fails with
  "`<name>` is read-only (not checked out in Perforce; run p4 edit, or revert the edit)" instead of a bare "cannot
  write".

Provider choice: `Options::provider` (`auto` | `perforce` | `git` | `none`), filled from
`.tenfold/workspace.json` `{"vcs": {"provider": "...", "p4Path": "..."}}` (flat `"vcs.provider"` / `"vcs.p4Path"` keys
work too) when the caller left it at `auto`. `auto` tries Perforce first and falls back to Git when no client maps
the workspace or the server is down; `none` switches version control off. Tests use a fake `p4`
(`tests/fake_p4_main.cpp`, state in a JSON file); no real server is involved.

**History, print and p4v helpers (W8f).** `PerforceProvider::filelog` (`p4 -ztag filelog -L -m 200`, parsed by
`parseFilelog` from the numbered `rev0` / `change0` / `time0` / `user0` / `desc0` fields; a description with a blank line
splits the tagged record, so all records are merged), `printRevision` (`p4 print -q file#rev`) and `p4vTarget` (`p4 info` for
`serverAddress` / `userName` / `clientName`, `p4 where` for the depot path). `Vcs.h` is unchanged: `VcsManager::perforceHistory`
/ `perforceRevisionText` / `perforceP4vTarget` run on the worker through `withPerforce`, which `dynamic_cast`s the provider and
answers `ok=false` with "the workspace is not under Perforce" for any other; results come back through `post`. The fake `p4`
learned `filelog`, `print file#N` (per-file `"history"` entries) and `serverAddress` in `info`.

**VS-VCS additions (VS_BEHAVIOUR section 4).** `VcsFileInfo::outOfDate` (fstat `headRev > haveRev`, Perforce only) feeds the
yellow triangle; `PerforceProvider::haveRevision` / `VcsManager::perforceHaveRevision` give the history menu the workspace
revision. `VcsManager::probe(file)` (Perforce only, called by the explorer for every file row it paints) collects unknown
files and asks them in one batched `status()` after 60 ms on the worker; `refreshStatus` keeps the probed files' last answer
until it has re-asked them (no blinking). `Options::promptOnEdit` / `setPromptOnEdit` / `onEditPrompt` (VC5): when on, the
first edit of a `ReadOnlyNotCheckedOut` Perforce file calls the handler instead of `beforeFirstEdit`; its reply
(`CheckOut`, `CheckOutAlways`, `Leave`) runs `checkOut` or nothing. Defaults are unchanged: off, auto-checkout.

### Threading (VCS)

| Thread | Does |
|---|---|
| main (`Editor`) | `documentOpened/Changed/Saved/Reloaded/Closed` notifications, all queries (`hunks`, `fileStatus`, `generation`), `revertHunk` (synchronous and exact), the pumped result handlers |
| VCS worker (lazy, one per `Editor`, started by the first open document) | provider detection, child processes, the line diff, the debounce timers |

An edit bumps the document's sequence number and (re)arms a timer **on the worker** (`Options::debounceMs`, 150 ms).
When it fires it posts to the main thread, which copies the buffer lines (about 3 ms for 100k lines) and hands the
snapshot to the worker; the worker diffs and posts `{sequence, hunks}`. A result whose sequence or base no longer
matches is dropped (`Stats::staleDropped`), so a result never describes an older text than the one on screen.
Providers never see a `Document`, only paths and strings. `beforeFirstEdit` is queued to the worker the first time a
document becomes dirty (once per open document; an external reload or `revertFile` re-arms it) and may take as long
as `p4 edit` needs without blocking typing; a failure goes to `onError` and the edit is kept.
`revertFile` runs the provider revert on the worker and then reloads the open document with
`Editor::reloadFromDisk`, i.e. exactly like an external change (only the changed lines are replaced, navigation
history follows, the undo history is cleared). `revertHunk` applies one `replace` through `Editor::replaceRange` (or
through the Document for a background tab): one undo step.

Refresh triggers: open, save, reload, `refreshStatus()` (also re-reads the base of every open document; the UI should
call it when the window regains focus, because commits made outside the editor do not touch the files) and,
debounced (1 s), file-watcher events for files that are not open. `generation()` changes whenever anything a view
shows changed, `onChanged` says which file. `checkOut(file, done)` (C3c) runs `beforeFirstEdit` on the worker on demand
(explicit "check out" command) and then re-reads the file's status.

### Version control UI (C3c, `src/ui/features/{VcsUi,DiffTool,VcsState}.*`, `TextView`, `src/ui/view/RowBlocks.*`)

The UI is a client of `Editor::vcs()` only (hunks, base lines, `fileStatus`, `revertHunk` / `revertFile`, `onError`,
`generation()`), so the Perforce provider (C3b) needs nothing here.

* **Change strip** (`TextView::paintDiffStrip`, one self-contained helper plus `Layout::diffStripWidth/X`): a narrow
  bar between the line numbers and the text, drawn from `vcs().hunks()` for the visible rows only (binary search into
  the sorted hunks): added green, modified blue, and for deleted lines a small red wedge (1 px slices) centred on the
  boundary above the line that now follows them (below the last visible row at the bottom edge). A wrapped line's mark
  spans all its visual rows. The strip lies in the padding cell the gutter already reserves right of the numbers
  (numbers end one cell before `gutterWidth`), so it adds **no gutter width**: text does not move when version control
  becomes active and every existing golden stays byte-identical. The breakpoint lane (C2c) lives left of the numbers
  and widens `gutterWidth`; the strip is positioned from `gutterWidth`'s right edge, so the two do not interact. The
  scrollbar has three more marker lanes (`Lane::DiffAdded/Modified/Deleted`, a thin stripe at the left edge of the
  track, drawn under the others; at most 1024 marks per hunk). Marks are the engine's last result; during the
  debounce after an edit they sit where they were, then jump to the new answer.
* **Inline diff block**: clicking the strip, or `Vcs.ToggleInlineDiff` at the caret, opens a block under the hunk
  (`TextView::toggleInlineDiff`): a header row (what changed, a "Revert hunk" button, a close button) and the base
  lines on a red tint (`diffBlockBackground`, text `diffBlockText`, dimmed rather than struck), with a red mark in the
  strip column. Escape closes all blocks of the focused view (`EditorApp::runCommandId` intercepts
  `Editor.CollapseCarets` first), as does the close button; "Revert hunk" goes through `VcsManager::revertHunk` (one
  undo step) and the block goes with its hunk.
* **How the block meets `WrapLayout`**: it does not. `WrapLayout` keeps counting only document rows (its cost model,
  lazily scanned rows and estimates are untouched, and the Document never changes). A small value class,
  `RowBlocks`, maps *wrap rows* to *display rows*: block `i` precedes the wrap row after its anchor line (`at`) and
  occupies `rows` display rows, so `display(w) = w + rows of the blocks with at <= w` and `wrapRow(d)` is the inverse
  (or says "inside block i"). `TextView` applies it at exactly the places that turn rows into pixels: total height and
  the scrollbar's row count, `collectVisibleRows` (text rows get their display row, block rows are skipped), the
  current-line band, `caretRect`, `ensureCaretVisible`, `positionAt` (a click in a block hits the line above it), the
  scrollbar marker rows, the anchor kept at the top of the view and the horizontal-scroll width scan. Everything
  per-row after that (selections, squiggles, find marks, carets) keeps working on `RowRef::vrow`, which is now a display
  row. The map is rebuilt when the wrap generation, the blocks or the document version change; with no blocks every
  function is the identity and the extra cost per frame is a null check. A block is an *overlay that reserves rows*:
  the text below moves down by its height, nothing else about the layout changes. Opening or closing a block above the
  first visible line shifts the scroll offset by the same rows, so the text does not jump, and a new block is
  scrolled into view when its bottom would be below the window.
* **Lifetime**: a block is identified by its hunk's base side (`baseStart`, `baseCount`, kind), its size (`newCount`)
  and its base lines. Edits above it move its anchor line at once (Document change listener); when the recomputed hunks
  arrive (`VcsManager::generation()`) it is re-placed on the matching hunk, or removed when none matches (the hunk
  grew, shrank, was reverted or the base changed). Typing inside the hunk without changing its size keeps it.
* **Commands** (`VcsUi`; registered by `registerEditorCommands`, bound by `EditorApp`, unbound by default except 10x's Perforce chords `Control P, Control E` / `D` / `R` for `CheckOut` /
  `DiffFileExternal` / `RevertFile`): `Vcs.NextChange` / `Vcs.PrevChange` (jump to the next /
  previous hunk through `Editor::gotoLocation`, wrapping with a message), `Vcs.ToggleInlineDiff`, `Vcs.RevertHunk`,
  `Vcs.RevertFile` (a modal two-row `ListPopup`, "Discard all changes" / "Cancel", default on the first row, then
  `VcsManager::revertFile`), `Vcs.DiffFileExternal`, `Vcs.RefreshStatus`, `Vcs.CheckOut` (the explicit form of
  Perforce's auto-checkout: `VcsManager::checkOut`, a small additive engine call that runs the provider's
  `beforeFirstEdit` now and marks the open document as checked out so its first edit does not repeat it).
* **External diff tool** (`DiffTool`): writes the committed text (document's dominant line ending) and the buffer (a mixed buffer with its dominant ending, so the tool lists text differences only) to
  `<temp>/tenfold-diff/<run>/{base,current}/<file name>` (original name and extension), starts the tool through
  `tf::Process` and keeps the object (never killed on exit; on Windows the job object still ends the child with the
  editor) with `tick()` collecting it. `vcs.diffTool` empty = auto: p4merge from PATH (Windows: `p4merge.exe`, then
  `%ProgramFiles%\Perforce\p4merge.exe`); a name is searched in PATH, a path is used as it is; nothing found -> status
  bar message ("no diff tool found: install p4merge or set vcs.diffTool"). `vcs.diffToolArgs` (default
  `"{base}" "{current}"`) is split like a command line and then `{base} {current} {file} {baseTitle} {currentTitle}` are
  substituted per argument; for the default template and a tool called p4merge, `-nl <title> -nr <title>` are put in
  front, the titles being `<file>#have` (Perforce) or `<file>@HEAD` (Git) on the left and `<file> (unsaved)` (or
  `(working copy)` when the buffer is clean) on the right. A settings value in quotes loses its outer pair, so the default file doubles them and
  the splitter repairs the common `"{a}" "{b}"` mistake. Temp directories are removed when a tool that ran at least 3 s
  has exited; those of a tool that returned at once (a launcher handing over to a running instance) are swept after a
  day.
* **Perforce badges and the question (VS-VCS)**: `vcsMark(vcs, file)` (VcsState.h) is `vcsTabMark(status)` plus, for Perforce,
  OpenedForAdd / NotInDepot / OutOfDate; `ui/widgets/VcsBadge.h` holds `vcsMarkColor` (theme keys with fallbacks) and
  `drawVcsGlyph` (5 x 7 pixel-rectangle bitmaps: check, plus, ?, triangle) used by `TreeView` on the file icon; `TabStrip`
  keeps squares. The explorer hides the ReadOnly mark (every synced file is read-only). `VcsUi` shows the edit question as a
  modal three-row `ListPopup` (queued per file) and `PerforceUi` the revision action menu; diffs go through
  `VcsUi::launchDiff` (hook `PerforceUi::Hooks::diff`).
* **Status**: `Workbench::sync` sets a `TabStrip::VcsMark` per tab (`vcsTabMark`: a small square left of the title,
  filled for modified / added / checked out, outlined for Perforce read-only, theme keys `tabVcs*`) and the left
  status-bar segment `vcs` (`vcsStatusText`: "Git: modified", "P4: checked out", "P4: read-only — will check out on
  edit", ...; empty for clean / unknown files, so the bar is unchanged when there is nothing to say).
  `VcsManager::onError` messages ("Version control: ...", 12 s) appear in the `message` segment. A regained window focus
  (`InputType::FocusGained`) calls `refreshStatus()`.
* **Theme keys** (all optional, `Theme::orElse` fallbacks so `dark` / `light` still show marks; values for `vs-dark`):
  `diffAdded`, `diffModified`, `diffDeleted`, `diffBlockBackground`, `diffBlockText`, `diffBlockHeader`,
  `tabVcsModified`, `tabVcsAdded`, `tabVcsCheckedOut`, `tabVcsReadOnly`. Settings: `vcs.diffTool`, `vcs.diffToolArgs`.
* **Measured** (Release, llvmpipe, 1920x1080, 100k-line file with 5 hunks and an open inline block): keystroke-to-paint
  0.10 ms average, p99 0.14 ms (`vcs_perf_keystroke_to_paint_with_marks_100k_lines`; budget 8 ms).

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

`TextBuffer` stores lines without terminators, remembers each line's own terminator (LF, CRLF or CR; see "Encodings and
line endings"), and exposes `replace(Range, text)` returning the inserted range and
line delta (what undo, the navigation history and LSP incremental sync
need). Columns are UTF-8 byte offsets; `utf8ToUtf16Column` and the reverse
exist for protocol conversion. `Document` adds path, version, dirty flag,
undo/redo and change listeners.

**Encodings and line endings (W9-FIX-ENC).** Everything inside the editor is UTF-8: `TextBuffer`, the fast index,
the text sent to clangd, every JSON message. Files are converted at the edge.

* *Decoding* (`core/TextEncoding.h`, used by `Document::loadFromDisk`, `Editor::reloadFromDisk`, the fast index's
  file reads and `SemanticEngine::fileChanged`): valid UTF-8 (ASCII, with or without a BOM) is taken as is, and a
  UTF-8 BOM stays in line 1 (columns of line 1 count its three bytes, as clangd's do); a UTF-16 BOM followed by
  well-formed UTF-16 is decoded (the BOM is dropped from the text); anything else is **Windows-1252** (all 256
  bytes map to one code point each, the five undefined ones to the C1 controls), so *every* byte string loads, never
  throws, and converts back byte for byte. `Document::encoding()` records the result.
* *Saving* writes the buffer in `Document::encoding()` with every line's own terminator: an unchanged file, mixed or not,
  is written byte for byte, and an edited mixed file stays mixed. A character the encoding cannot hold is **refused** (W10-ENC, user decision of
  2026-11-06): `Document::saveToDisk` first calls `findUnencodable()` (`collectUnencodable` in `core/TextEncoding.h`
  scans every line; an `UnencodableReport` holds the first 200 characters as code point, text, line and byte column,
  and counts all). If it finds any, `saveToDisk` returns false with the sentence in `error` and the report in its
  `refused` argument, writes nothing, and the document stays dirty and in its encoding. UTF-8 never refuses; UTF-16
  refuses only bytes that are not UTF-8 (stray bytes, encoded lone surrogates); Windows-1252 refuses everything outside
  its repertoire. `encodeText` itself also fails on those. `Editor::save(doc, &error, &refused)`, `saveAs` and
  `saveAll(&error, &refusals)` pass the report up (`SaveRefusal` = file + report; `saveAll` saves the other documents
  and lists the refused ones), `applyRenameEdits` collects `RenameResult::refused`, and `replaceMatchesInFile`
  (closed files; it converts the replacement to Windows-1252 for such a file and writes nothing when it cannot)
  fills `ReplaceInFilesResult::refused`. The UI funnels every refusal through `PickerEnv::saveRefused`, which
  `EditorApp` turns into the status-bar message and `SaveRefusedDialog` (characters as text, `U+XXXX`, `line:column`,
  first 8 and "and M more", Enter goes there through `gotoLocation`); the save on close / quit keeps the document
  open (`UnsavedPrompt::saveAll` returns false after saving the rest). The old `saveNote()` and the silent switch to
  UTF-8 are gone.
* *Choosing the encoding (VS-ENC, docs/VS_BEHAVIOUR.md section 2).* `EncodingChoice` (`core/TextEncoding.h`: UTF-8, UTF-8
  with signature, UTF-16 LE/BE, Windows-1252) is what the pickers and the status-bar segment speak; `Encoding` stays
  the codec's. The UTF-8 signature is not an `Encoding`: it stays in the text of a file that was read with one (so
  line-1 columns are unchanged) and `Document::encodingChoice()` derives "with signature" from the text, or from
  `pendingSignature_` (a signature asked for on a document whose text has none; `saveToDisk` then writes EF BB BF in
  front, a new `utf-8-bom` document or Save with Encoding "UTF-8 with signature"). `Document::applyEncodingChoice`
  switches: dropping the signature is an ordinary undoable edit of line 1, the encoding itself is not an undo step.
  `Document::findUnencodable(EncodingChoice)` is the check for a target (a leading U+FEFF that the switch drops does not
  count; reported columns are the document's). `Editor::saveWithEncoding(doc, choice, optional<LineEnding>)` first runs
  that check and on any unencodable character returns the same refusal as `save` (nothing changes: encoding, text and
  line endings stay), otherwise applies the encoding and the line endings as one group and calls `save`; an untitled
  document only gets the encoding. `Editor::reopenWithEncoding(doc, choice)` shares `reloadWith` with `reloadFromDisk`:
  the file's bytes are decoded with `decodeForced` (no detection; UTF-16 must be well-formed, UTF-8 keeps stray bytes so
  nothing is lost on save), only the differing lines are replaced, then `Document::loadFromDisk(..., LoadOptions)` resets
  encoding, dirty flag and undo history. Detection takes `DetectOptions`: `editing().autoDetectUtf8` (off: BOM-less
  non-ASCII is not guessed to be UTF-8) and the fallback page. `fallbackEncoding(codePage)` is always Windows-1252, the
  only single-byte page the codec has; `systemAnsiCodePage()` is `GetACP()` on Windows (1252 elsewhere, overridable with
  `setAnsiCodePageForTesting`), and `Editor` logs once when it differs from 1252. The Fast index, clangd file reads and
  the corpus use the default detection. `Editor::newUntitled` applies `editing().defaultEncoding`. The UI is
  `EncodingPrompt` (`ui/features`): the Reopen list (a modified document goes through `UnsavedPrompt` first) and the
  Advanced Save Options list (one `ListPopup` that is reopened after each choice; Save writes through
  `Editor::saveWithEncoding`); `SaveRefusedDialog::saveWithEncoding` is its row for one refused file. `Workbench::
  refreshStatus` sets the "encoding" segment from `encodingLabel(doc.encodingChoice())`; `EditorApp` opens the menu
  (`statusMenuEntries("encoding")`) on a click. `.editorconfig` `charset` is not read (tenfold reads no `.editorconfig`).
* *Line endings (W10, like Visual Studio: docs/VS_BEHAVIOUR.md section 1).* A line ends at `\r\n`, `\n`, a lone `\r`,
  NEL (U+0085, `C2 85`), LS (U+2028, `E2 80 A8`) or PS (U+2029, `E2 80 A9`), exactly like Visual Studio's editor (user
  decision 2026-11-07). `LineEnding` is `{LF, CRLF, CR, NEL, LS, PS}`. One finder, `linebreak::BreakFinder`
  (`core/LineBreaks.h`: memchr for `\n`, and cached next positions of `\r`, `C2 85` and `E2 80 A8/A9`, so a text without
  them costs one extra pass), is shared by `TextBuffer::setText` / `replace()` / `replaceMany()` / `countBreaks` /
  `withLineEnding`, find-in-files (`TextSearch`'s `LineTracker`, `ReplaceInFiles`), `vcs::splitLines` (it uses
  `TextBuffer`), the multi-caret paste split, the corpus anchor scanner and the engine's closed-file line readers. The
  fast lexer (`CppLexer`) keeps clang's rules for comments, strings and directives (a `//` comment goes on past an LS)
  but counts NEL / LS / PS as line breaks in the line and column of every token (`place()`), so a symbol after one has the
  position the editor shows. A Windows-1252 file cannot contain them (its byte 0x85 decodes to U+2026) and such a
  document cannot be saved as Windows-1252 (`Document::findUnencodable` lists the terminators); a UTF-16 file can.
  **Every line keeps its own terminator.** Representation: `base_` (the ending of every line while the
  document is uniform: no per-line storage, the usual case) and, as soon as a second kind appears, `ends_`, one byte
  per line parallel to `lines_` (the last entry is unused: the last line has no terminator). `replace()` interprets the
  breaks of the inserted text literally (CRLF, LF, CR) and stores them; the new last line of an edit keeps the old
  last line's terminator, so joining two lines keeps the later one's. `lineEnding()` is the dominant ending (ties: LF,
  then CRLF, CR, NEL, LS, PS; cached counts), `mixedLineEndings()` tells whether the terminators differ, `textForDisk()` writes what is
  stored. `Document` records undo text with `textInRangeRaw` (real breaks), so undo restores a mixed file byte for
  byte; `Document::convertLineEndings(e)` rewrites only the terminators that differ, as one undo step (the load
  dialog's Yes, the status bar menu and Save with Line Endings all use it; the line structure does not change, so
  carets stay). The `Editor` decides which ending a *new* break gets: `newLineEndingAt(doc, line)` is Visual Studio's
  `ReplicateNewLineCharacter` (the terminator of the caret line; the last line has none, so the line before it; a document
  without any break uses `editing().defaultLineEnding`; `replicateNewLine=false` always does). Every Editor edit
  path (typing, Enter, line commands, LSP text edits, `replaceRange`) converts the breaks of its text with it
  (`withDocBreaks`, `applyCaretEdits`); text that is copied from the buffer (comment toggling, duplicate selection,
  wrap in brackets, case change) and pasted text (`insertText(text, literal=true)`, when `pasteKeepsLineBreaks`) is
  inserted literally. Copy puts `selectedTextRaw()` (real breaks) on the clipboard. `Editor::open` reports a document
  read from disk with mixed endings to `onInconsistentLineEndings` (setting `checkLineEndings`); the UI's
  `LineEndingPrompt` shows VS's dialog (`LineEndingDialog`, a modal widget built from the reusable `DropDown` and `CheckBox` widgets in `src/ui/widgets`; documents queued together share one dialog; the check box writes `editor.checkLineEndings = false` through `writeUserSetting`). `DropDown` paints its open list beyond its bounds, so `Widget::hitRect()` (default: the bounds) lets a widget take mouse events outside them. `.editorconfig` `end_of_line` is not read (tenfold reads no `.editorconfig`).
* *Text for clangd.* `didOpen` / full-sync / `reopenDocuments` send `TextBuffer::textForLsp()`: UTF-8 with the
  document's real line endings (CRLF lines are sent with CRLF, LF lines with LF, so for the usual files the buffer
  clangd parses is the file on disk; W9e finding 4), and incremental changes get their new text converted the same way
  (`TextBuffer::lspText`). A lone CR is sent as LF, because clangd's LSP layer finds lines at `\n` only (positions,
  edits) while clang's preprocessor also ends them at a lone `\r`. Line numbers and columns are identical for those
  endings. **NEL / LS / PS** are sent as the characters they are (clang and clangd do not end lines there: a `//`
  comment goes on past an LS), so for a document that has them clangd's lines are the editor's lines glued together at
  the separators and every position that crosses the boundary goes through `LspLineMap` (`core/LspLineMap.h`).
  *The map.* `LspLineMap::build(buffer)` returns null for a document without separators (the common case: no map, the
  editor's line is clangd's line and only the column is converted, as before). Otherwise it is an immutable snapshot of
  the *runs* (groups of consecutive editor lines that form one clangd line): per run its first editor line, its clangd
  line, the number of separators before it, and per editor line of a run its offset inside the clangd line in UTF-8 bytes
  and in UTF-16 units (the separator counts 1 unit, or its 2 / 3 bytes). Lines outside runs only move by the number of
  separators before them. `toServer(p)` (editor line, byte column -> clangd line, column) and `fromServer(q)` are binary
  searches over the runs plus the usual byte/UTF-16 column conversion of the one line; a column inside or after a
  separator clamps to the end of the editor line. The engine keeps the snapshot of what it last sent per open document
  (`SemanticEngine::lspMaps_`, updated in `didOpen` / `didChange` by `noteLspText`, one atomic load when no document has
  separators) and uses it for *every* position: `toServer(doc, pos)` for requests (definition, declaration, references,
  hover, completion, signature help, rename, highlights, hierarchies, code actions, formatting ranges, inlay hints),
  `serverPosition(file, pos)` for requests about a file by path, `posFromServer(uri, pos)` for every location, range,
  edit, diagnostic, symbol and folding range that comes back, and `decodeTokens` for semantic tokens (a token that spans
  a separator becomes one span per editor line). A file that is not open is read and decoded when it has separators
  (`diskSeparators`, cached by mtime and size; Windows-1252 and UTF-16 files never have any), and the engine's own line
  readers (`readLineOfFile`, `splitTextLines`, `classifyReferences`) split closed UTF-8 files at the editor's breaks.
  *Incremental changes.* With UTF-8 offsets the engine sends `didChange` ranges; a range is in the coordinates of the
  text before the edit, which the map (built from the text after it) does not describe, so a change that removes or
  inserts a separator (`Document::Change::separators`, set for a whole batch) or any change to a document that has
  separators is sent as a full-text change. Without separators nothing changes. The fast index is fed
  `TextBuffer::textForIndex()` (real breaks as `\n`, separators as characters) so it agrees with clang about comments and
  with the editor about lines.
* *Positions from clangd.* For an open document clangd saw the same UTF-8 text, so columns map directly (through
  `LspLineMap` when it has NEL / LS / PS, see above). For a
  Windows-1252 file that is not open, clangd reads the raw bytes from disk and its byte columns are converted
  (`SemanticEngine::posFromServer`, `diskEncoding`). Known gap: a Windows-1252 file that is open in tenfold but whose
  answer comes from clangd data computed from the disk bytes (a stale preamble or the background index) can report raw
  columns.
* *JSON.* `dumpSafe()` (`core/JsonUtil.h`) is `dump()` with the replace error handler; every dump of data that can
  hold user text (LSP messages, the clangd trace, bookmarks, breakpoints, `workspace.json`, the generated compile
  database, the layout, the corpus report) uses it. The documents sent to clangd are valid UTF-8 anyway; this is the
  last line of defence.
* *Fast lexer.* `CppLexer` skips a leading UTF-8 BOM, ends lines at a lone `\r`, and has a C keyword table
  (`isC`, chosen for `.c` files by `extractSymbols(..., isC)` / `isCSourceFile`): `class`, `delete`, `new`,
  `template`, `this`, `namespace` ... are identifiers there. The persisted fast-index fingerprint salt was bumped.

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

**Editing conveniences (B8, `src/app/EditorEditing.cpp`, `src/app/Bookmarks.*`).** Everything is an `Editor` method that
edits at every caret and forms ONE undo step; the behaviours that could differ from 10x live in `Editor::EditingOptions`
(`autoIndent`, `autoIndentBraces`, `formatBracesOnEnterSplit`, `autoCloseBrace`, `autoCloseBracket`, `autoCloseQuotes`,
`bracketSelectedText`, `detectIndent`, `indentUnit`, `trimTrailingWhitespace`, `commentBlockUsesLineComments`; the UI fills them from the `editor.*`
settings, OPEN_QUESTIONS #8). The struct's defaults are 10x's documented defaults (B8b: braces and brackets do not auto-close, tab
indent, no detection, selections are bracketed) ; `useEditorConfig` (off) lets `Editor::save` apply `.editorconfig` `trim_trailing_whitespace` / `insert_final_newline` (`core/EditorConfig.*`) and `textFileNames` (off) names new files `TextFileN.txt` (VS-EDIT); the engine tests pin the behaviours they test explicitly. Two entry
points exist for text: `insertText` gives the breaks of its text the document's line ending, unless it is a paste (`literal`), and `typeText` is what the view calls for typed
characters (auto-close per pair kind, typing over the closer that is already there and Backspace between an empty pair,
both only for a pair kind whose auto-close is on; wrapping a selection with `( [ {` per `bracketSelectedText`, with quotes
per `autoCloseQuotes`; `}` as the first non-blank character dedenting to the line of the `{` it closes; everything else
falls through to `insertText`, so the typing coalescer is unchanged: both go through `typeEdits`). `insertNewline` copies the indentation, adds one unit after a line ending in `{ ( [` or a binary
operator (`+ - * / % & | ^ = ? <<`, not `++ -- */`), returns one unit when a continued statement ends with `;`, splits
`{|}` into three lines (only with `formatBracesOnEnterSplit`) and trims the blanks around the break. `indentUnitFor(doc)` detects tabs vs. the most common
indentation step of the first 400 lines. Line commands (`indentLines/outdentLines`, `toggleLineComment`, `moveLines`,
`joinLines`, `deleteLines`, `trim on save`, `replaceAll`) compute a sorted batch and go through
`Editor::runLineEdits` -> `Document::replaceMany` (A5b's single-pass path), carrying carets with `mapThrough`
(positions inside a replaced range go to its start; at a pure insertion the start of a selection stays before it);
`toggleBlockComment`, `duplicate` and the typing commands are per-caret edits through `applyCaretEdits`, which now
accepts per-edit caret/anchor offsets inside the inserted text (`CaretEdit::headOffset/anchorOffset`). A selection that
ends at column 0 does not touch that last line. `findInDocument` / `replaceAll` run TextSearch's matcher on the buffer
text (a synthetic one-file `SearchInput` with an override), so literal / whole-word / PCRE2 semantics are identical to
find in files; regex group references in the replacement are not expanded.

**10x editing commands (W8a, `src/app/EditorEditing.cpp`).** Same rules as above (per caret, one undo step):
`makeSelectionUppercase/Lowercase` (ASCII letters; the length does not change, so the selections stay),
`commentLines/uncommentLines` and `toggleLineComment` share `lineComments(mode)` (a batch through `runLineEdits`),
`toggleComment` is 10x's `ToggleComment` (block comment only when `EditingOptions::commentBlockUsesLineComments` is
false and every caret has a selection, else line comments), `deleteToEndOfLine` (never joins lines), `insertLineAbove/
Below` (indentation copied with `autoIndent`, one unit more after `{ ( [`; carets sharing a line insert one line),
`cutLines` (returns the text, then `deleteLines`), `saveAll`. `selectCurrentWord` uses `TextBuffer::wordAt`. The scope
commands scan the buffer once for matched brackets (`bracketPairs`: comments, string and character literals skipped) and
pick, per selection, the smallest of {inside, with the brackets} of every enclosing pair that strictly contains it, so
repeated presses grow outward; `moveToMatchingBracket` jumps to the same side of the partner and fails when the caret is
not next to a (code) bracket. All of them select through `setSelections`, so the move lands in the navigation history.

**10x multi-cursor commands (W8d, `Editor.cpp` / `EditorEditing.cpp`).** `addCaretAtNextOccurrence` wraps the old step
(`addCaretAtNextOccurrenceStep`) and pushes `{document, version, carets before, carets after}` on `selectNextSteps_`
when the carets changed; `undoAddCaretAtNextOccurrence` pops one step and restores `before`, but only while the current
document, its `version()` and the caret set still equal the step's `after` (else the whole stack is dropped).
`selectAllOccurrences(find?)` runs `findInDocument` (the find bar's options, or the selection / word with a built option
set) and calls `setSelections` with the match at or after the primary first. `alignCarets` computes visual columns (tab
stops of `EditingOptions::tabWidth`, set from `editor.tabWidth`), sorts the carets by position, and builds one `CaretEdit`
per caret (an insertion of spaces for the first caret of a line that is left of the rightmost caret, an empty edit for
the others) for `applyCaretEdits`: one undo step. `adjustSelectionEnds(grow)` moves `range().end` of every selection by one
character (crossing line breaks when growing) and keeps the orientation; `addCaretAt(p)` inserts a primary caret.

**Navigation and bookmark variants (W8d).** `NavigationHistory::backInFile/forwardInFile(current)` scan for the nearest
entry of `current.file` in that direction and move the cursor straight to it (`Editor::backInFile/forwardInFile`, no
`gotoLocation`: like `back()` they add no entries); the live position is recorded (as `back()` does) only when a target
exists. `Bookmarks::nextInFile/prevInFile` wrap inside one file, `removeFile` clears one file; `Breakpoints::removeFile`
erases one file and runs the change handler once, so `DebugSession` syncs it to the backend like any other change.
`Editor::lastResultSource()` (set by `build` and `findInWorkspace`, and by `FindInFiles::start` for a replaced search
function) tells `EditorApp` whether `Search.NextResult` / `PrevResult` (F4 / Shift+F4) call `FindInFiles::step` or the
shared `EditorApp::stepBuildResult` (also behind `Build.NextError` / `PrevError`, F8).

`Bookmarks` keeps sorted line sets per file (key `pathKey`), moves them with the same rule as
`NavigationHistory::onLinesReplaced` (the Editor's document listener feeds both), is persisted as
`.tenfold/bookmarks.json` (`{"version":1,"bookmarks":{"<path relative to the root>":[line,...]}}`, 0-based lines; loaded
when the first document opens, saved on toggle, on `save()` and on shutdown) and navigates by (path, line) across files
with wrap-around; `gotoBookmark` / `gotoBookmarkItem` go through `gotoLocation`. `generation()` lets views repaint
their gutter flags.

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

## Visual Studio solution settings (`compdb::fromSolution`, C5)

The workspace's compiler settings come from the open `.sln` (OPEN_QUESTIONS #12), evaluated without MSBuild and
emitted as `clang-cl` commands, so clangd and the fast index see what `cl.exe` would. Evaluation is string-level
(backslashes, drive letters and UNC names behave the same on every host) and hermetic apart from the environment.

**Configurations.** `readSolution` parses the projects, `GlobalSection(SolutionConfigurationPlatforms)` (the
configuration list, file order) and `GlobalSection(ProjectConfigurationPlatforms)` (`.ActiveCfg` = the project's
configuration/platform for a solution configuration, e.g. `Debug|x86` -> `Debug|Win32`; `.Build.0` = built there).
`fromSolution` skips a project the solution does not map in the active configuration (no `.ActiveCfg` for it) and
evaluates the others with their mapped configuration/platform, **built or not**: a project with an `.ActiveCfg` but no
`.Build.0` line (not part of that configuration's *build*) still gets its own flags (W9-FIX-vs, P-16). Visual Studio's
IntelliSense evaluates such a project with the mapped project configuration, because `.Build.0` only controls what
`msbuild` compiles; the alternative (no entry) made clangd borrow another project's flags, which is never better. I
could not verify this on a Windows machine; the evidence is VS's documented behaviour (Configuration Manager "Build"
checkbox vs the configuration column) and that the alternative is arbitrary. `SolutionProjectConfiguration::build` still
records the `.Build.0` line for consumers that care (the build). A solution without those sections uses the
request as is. The active configuration is `Workspace::Settings::vsConfiguration` / `vsPlatform` (the existing
`.tenfold/workspace.json` keys, shared with the build): an exact match in the solution, else `Debug|x64`, else the
first listed configuration (`pickSolutionConfiguration`; the engine logs the fallback). API: `Workspace::
vsConfigurations()`, `activeVsConfiguration()`, `setVsConfiguration("Debug|x64")` (validates, saves);
`Editor::selectVsConfiguration(name, &err, applied)` additionally regenerates the configuration's compile database on
the engine worker and restarts clangd against it, calling `applied` on the pump thread; CLI: `tenfold-cli vsconfig
<root> [--set Debug|x64]`. A generated database is stamped (`compile_commands.stamp` next to it: source, configuration
and toolchain mode for a solution; `heuristic` for a heuristic one, which is how a warm start keeps reporting it, and a
cached heuristic database is dropped when a solution or CMake project appears), so a hand-edited `workspace.json` is not
served from a stale cache. `Workspace::saveSettings` merges into the existing
`workspace.json` (unknown keys survive), writes `workspace.json.tmp` and renames it, and refuses to touch a file that is
not a JSON object. Edits to `.sln`, `.vcxproj`, `.props`, `.targets` and `Directory.Build.*` trigger a refresh like
`compile_commands.json` does.

**One compile database and one clangd index per configuration (W9-FIX-vs, P-15).** clangd keeps its background index in
`<directory of the compile database it was started with>/.cache/clangd/index` (`--compile-commands-dir`), one index per
project, whatever the flags of each entry. With a single `.tenfold/compile_commands.json` the index therefore outlived a
configuration switch: the shards of the Debug translation units (and the declarations in `inc_Debug/cfg.h`, same USR as
the Release declaration) stayed in the index the restarted clangd loaded, so find-references and rename touched the
headers of configurations that were no longer active. Decision: a solution's database lives in
`.tenfold/vs/<configuration>-<hash>/compile_commands.json` (name sanitised, 8-hex hash of the exact name so
`Debug|x64` and `Debug_x64` never share), so each solution configuration has its own `.cache/clangd/index` and an index
never mixes two configurations. Alternatives weighed: (a) delete the index on every switch (wrong answers impossible,
but every switch re-indexes the whole project, which users do often and which is the slow part of a switch); (b) one
index plus filtering results by the active configuration (clangd offers no such hook; tenfold would have to
post-process every references/rename answer against the compile database, and rename edits come from clangd itself);
(c) the per-configuration directory (chosen): no clangd change, correct by construction, and switching back to a
configuration whose index is still valid reuses it (clangd re-indexes only files whose digest or compile command
changed; the regenerated database is byte-for-byte the same for an unchanged project, so a switch back is a clangd
restart plus a verification, not a re-index). Cost: disk, bounded by `SemanticEngine::Options::maxVsConfigIndexes`
(default 4): when a configuration is selected, the least recently used other directories (by the stamp's mtime, which is
touched on use) are deleted. The directory of the previous tenfold layout (`.tenfold/compile_commands.json` +
`.tenfold/.cache/clangd/index`, which mixed configurations) is removed the first time a solution database is made.
`Editor` does not watch the folders under `.tenfold/` (the regeneration would otherwise trigger itself).

*Open documents.* clangd parses every open document on its own and adds it to its dynamic index, which shadows the
background index. A header of the previous configuration that stays open in the editor (`inc_Debug/cfg.h` after
`go to definition` landed there) would put its declaration back into the answers of the new configuration even with a
clean index. At a configuration switch `SemanticEngine::reopenDocuments` therefore compares the include closure of the
old and the new configuration (`compdb::filesReachableFrom`: units plus `#include`s resolved next to the includer and
through the command's `-I`/`/I`/`-isystem`/`/imsvc` directories and forced includes, using the fast index's include
lists, no preprocessing) and **parks** an open document that the old configuration compiled or included and the new
one does not: it stays open in the editor but is not sent to the new clangd. Editing it or opening it again
(`Editor::open` on an open document, which is what go-to-definition and the tab picker do) sends it, so a header the
user works on is never left out for long. A document that neither configuration reaches (a header being written, a
file outside the project) is never parked. The check is an over-approximation (`#if` is ignored), so it can keep a
document that the new configuration does not really include, never park one that it does.

**Host standard library fallback (`.sln` off Windows).** The commands are `clang-cl` commands for MSVC, and clangd finds
MSVC's STL and the Windows SDK through `--query-driver` of `cl.exe` (below). Where no `cl.exe` can be found
(`msvcToolchain`, `VCToolsInstallDir`, `vswhere`; that is every Linux and macOS machine) `<vector>` and `<cstdio>`
resolved to nothing and every standard type was an error. The engine then asks `fromSolution` for the *host form*
(`VcxprojOptions::hostTarget`, `compdb::toHostStdlibArgs`): same driver (`clang-cl`), same defines, include
directories, forced includes, PCH forced include and options, but `--target=<host triple>` (clangd's Linux/macOS
driver then adds the host's libstdc++/libc++ and libc include paths, as for any `clang++` command; macOS also gets
`-isysroot` from `platformSystemFlags()`), `/std:c++N` written as `/clang:-std=c++N` (cl mode ignores `/std:` for a
non-Windows target), `-m32`/`-m64` and the Windows ARM64 target dropped (the host has one architecture and no 32-bit
standard library), `-fms-extensions` (so `__declspec`, `__int64`, `__stdcall` still parse) and
`/D_MSC_VER=1933` (the version clang-cl reports; code under `#ifdef _MSC_VER` keeps taking the MSVC branch). Tried and
rejected: `-fms-compatibility` on a Linux target (libstdc++ then fails on `__GCC_ATOMIC_TEST_AND_SET_TRUEVAL`), and the
GCC driver (`clang++`), which would need every `/` option of the project translated. Consequences: `sizeof(void*)`
and `_WIN64` follow the host/project defines, not the project's platform (a `Win32` configuration still gets no
`_WIN64`, but pointers are 8 bytes); Windows SDK headers (`<windows.h>`) are not available. The fallback is logged once
per session (`no MSVC toolchain (cl.exe) found: ...`), the stamp records `host:<triple>` as the mode (a changed
`msvcToolchain` regenerates), and `Options::forceHostStdlibFallback` forces it for tests. **On Windows nothing
changes**: the mode is decided at compile time (`_WIN32`) to be MSVC, the log line `cl.exe not found` stays, and
`tenfold-cli compdb` always writes the MSVC form.

**Evaluation order** follows MSBuild: pass 1 properties and imports in document order (environment variables are the
lowest-priority properties; `Directory.Build.props` is imported where `Microsoft.Cpp.Default.props` sits, or at the top
for projects without it, `Directory.Build.targets` after the body; the nearest file walking up from the project
directory is used, and chains through `$([MSBuild]::GetPathOfFileAbove(...))`); pass 2 `ItemDefinitionGroup`/`ClCompile`
metadata with the final property values; pass 3 `ClCompile` items (wildcards, item `Condition`, per-item metadata that
overrides the definition group, `ExcludedFromBuild` per file and configuration). `%(Name)` self references inherit the
earlier value. `$(MSBuildThisFile*)` is the file the element is written in. `Choose/When/Otherwise` pick one branch.
Property functions understood: `[MSBuild]::EnsureTrailingSlash`, `NormalizeDirectory`, `NormalizePath`,
`GetDirectoryNameOfFileAbove`, `GetPathOfFileAbove`, `[System.IO.Path]::Combine/GetFullPath/GetDirectoryName`.

**Anything that cannot be resolved removes only its own entry.** Unknown properties in values, `$(VC_IncludePath)`-style
toolchain macros, `$(Registry:...)`, other property functions and unknown `%(Metadata)` expand to a marker that makes
the include directory, define, forced include, option or file holding it be dropped (never a half-written `/I/include`).
In conditions an unknown property is empty, as in MSBuild. The toolset's own files (`$(VCTargetsPath)\Microsoft.Cpp*.props`)
are never imported: system headers are the toolchain's business (clangd `--query-driver`).

**Argv per source file** (`clang-cl`): `/std:c++NN` (`LanguageStandard`; `stdcpp23`/`stdcpplatest` -> `c++latest`;
unset -> `c++17`, a deliberate superset of cl's default so older code still parses) or `/std:c11|c17|clatest`
(`LanguageStandard_C`, C files); `/EHsc|/EHa|/EHs` (`ExceptionHandling` Sync/Async/SyncCThrow; unset = Sync like the
toolset's default; `false` = none); `/nologo`; `-m64`/`-m32`/`--target=aarch64-pc-windows-msvc` from the platform;
`/I` (`AdditionalIncludeDirectories`, then the project's `IncludePath`), `/imsvc` (`ExternalIncludePath`,
`AdditionalExternalIncludeDirectories`), `-Wno-system-headers`/`-Wsystem-headers` (`ExternalWarningLevel` Level0/>0);
`/D` (`PreprocessorDefinitions`, then implicit ones), `/U` (`UndefinePreprocessorDefinitions`, after every `/D`);
`/FI` (the precompiled header first, then `ForcedIncludeFiles`), `/GR-`, `/Zc:wchar_t-`, `/permissive-`
(`ConformanceMode`), `/Zc:preprocessor`, the runtime library (`/MD /MDd /MT /MTd`), `/arch:` and `/openmp`
(`EnableEnhancedInstructionSet`, `OpenMPSupport`), `AdditionalOptions` (Windows command-line splitting: backslashes
literal), and `/TC` or `/TP` (`CompileAs`, else by extension).

**Implicit defines (and why).** `_WIN32` (always; cl predefines it, clang-cl does not on a non-Windows host), `_WIN64`
(x64, ARM64), `_DEBUG` with `/MDd` `/MTd`, `_DLL` with `/MD` `/MDd`, `_MT` with any runtime library (cl defines all of
these from the switch), `UNICODE` + `_UNICODE` for `CharacterSet=Unicode`, `_MBCS` for `MultiByte` (the toolset adds
them). The runtime library defaults like the toolset's: `MultiThreadedDebugDLL` when `UseDebugLibraries` is true, else
`MultiThreadedDLL`. Not invented: `_CONSOLE`, `_WINDOWS`, `NDEBUG`, `WIN32` (only what the project lists).
`_MSC_VER`, `_MSVC_LANG`, `_M_X64`... are clangd's: its clang-cl mode and `--query-driver` set them. Duplicates are
collapsed; `VcxprojOptions::implicitDefines = false` turns the implicit ones off.

**Precompiled headers.** `PrecompiledHeader` Use/Create with a `PrecompiledHeaderFile` yields `/FI<header>` (resolved
next to the source, in the project directory or the include directories) and no `/Yu`/`/Yc`: with `/Yu` clang skips
everything in a file before the through header, so a source that never includes it vanishes from clangd, and clangd has
no `.pch` to load anyway. `NotUsing` adds nothing.

**Not honoured:** `Update=`/`Remove=` items, `.slnx`, non-C++ projects, `ItemDefinitionGroup` metadata for item types
other than `ClCompile`, `UndefineAllPreprocessorDefinitions`, `/Yu`-style pch semantics, `DisableSpecificWarnings` and
other warning switches (clangd's diagnostics are its own), `StructMemberAlignment`/`CallingConvention` (layout flags;
add through `AdditionalOptions` if wanted), `ProjectReference` include propagation, `PlatformToolset`-dependent defaults,
instance property functions (`$(x.Replace(...))`), numeric condition operators.

## Compile-command diagnostics (D4)

"Why is this symbol approximate?" is answered by `Editor::compileCommandInfo(path)`, which returns a plain
`Editor::CompileCommandInfo` and never blocks on clangd. It reads only snapshots that already exist:

* `SemanticEngine::status()`: `compdbSource` (`existing` | `cmake` | `vcxproj` | `heuristic` | `generated(cached)` |
  `none`), `compdbPath`, `compdbEntries`, the clangd executable / source / version, and whether clangd is initialised;
  `clangdStatusText()` for a restart message.
* `SemanticEngine::compileCommands()`: the immutable snapshot of the database. The entry for the file is found by
  `pathKey(weakly_canonical(...))`, so spelling, separators and (on Windows) case do not matter. `listed` is false for
  headers and for sources the database does not cover (a project excluded in the active configuration); then `argv`
  is empty, and the report says that clangd borrows a similar entry's command (its own choice, invisible here) or uses
  the fallback flags, which are exactly the `fallbackFlags` given to clangd: `-std=<cppStandard>` and `-I<root>`.
* `Workspace`: the first `.sln` of the root and `activeVsConfiguration()`; `vsConfigurationUsed` is true only when the
  source is `vcxproj` (a `compile_commands.json` or CMake takes precedence over the solution).
* `.tenfold/clangd-config/clangd/config.yaml`: the `CompileFlags.Add` list the engine writes in heuristic mode where
  clangd honours `XDG_CONFIG_HOME` (the shared `-I` flags). It is read whatever the current source is, because clangd
  reads it whatever the source is. Since P1 the engine rewrites it whenever its content would change (a freshly
  generated heuristic database writes the flags, every other source writes none), except for a cached heuristic
  database, which keeps the config written beside it.
* `Editor::document(path)` and `SemanticEngine::fileReady(path)` for "clangd has been given the file" and "clangd's file
  status is idle" (parsed). The report's confidence is `authoritative` (clangd running, file open and parsed),
  `approximate` (clangd not running, or still parsing the open file), or `not open`.

`CompileCommandInfo::text(width)` formats the report (wrapped at `width` columns when positive; the argv is quoted for
display). Users of it: `tenfold-cli why <root> <file> [--clangd p] [--no-clangd] [--wait ms]` (starts the engine, opens
the file, waits for clangd to parse it, prints `text(100)`), and the UI feature `CompileInfo`
(`src/ui/features/CompileInfo.*`, command `Help.ShowCompileCommand`, bound by `EditorApp` with the other pickers): a
modal input-less `ListPopup` with one row per line of `text(76)`; Escape closes. The command has no default key (there
is no command palette yet, so the user binds one). Tests: `tests/test_compile_info.cpp` (the Visual Studio `Multi`
fixture in two configurations, an existing `compile_commands.json`, a heuristic workspace, an unstarted editor, and a
real clangd; plus the check that the user guide's settings table matches `assets/settings.default.txt`) and
`tests/ui/test_compile_info_ui.cpp`.

## Terminal engine (W8f, `src/term`, part of `tenfold_core`)

The UI-agnostic half of the terminal panel (`NewTerminal`, `ShowTerminal`); the panel that renders it is described under "Terminal panel" below.
No third-party code. Three independent pieces:

* **`Pty`** runs a program in a pseudo-terminal. `PtyOptions{program, args, cwd, rows, cols, env}`; an empty program is
  `Pty::defaultShell()`: the `TerminalCommand` setting if given (10x default `cmd.exe`; `Pty::splitCommand` splits
  `cmd /c agent` style values), else `%ComSpec%` on Windows, `$SHELL` or `/bin/sh` on POSIX. The working directory is
  the workspace root (the caller passes it). One **reader thread** per `Pty` polls the master with a 25 ms timeout and
  calls `onData(bytes)`; when the program exits by itself the remaining output is delivered, then `onExit(code)` runs
  once (code is the exit status, `128 + signal` on POSIX). Both callbacks run on the reader thread: the owner posts
  them to the main thread (`Editor::post`) and feeds the bytes to a `VtScreen`. `write` is a blocking write of input
  (keystrokes, `VtScreen` replies); `resize` is `TIOCSWINSZ` / `ResizePseudoConsole`; `close()` (also the destructor)
  kills the tree, joins the reader and guarantees no callback runs afterwards (it is not reported as an exit).
  * POSIX (`Pty_posix.cpp`): `posix_openpt/grantpt/unlockpt`, the slave is opened by the parent, `fork`, in the child
    `setsid`, `TIOCSCTTY`, `dup2` onto 0-2, `SIGPIPE` and the signal mask reset, `chdir`, `environ` replaced by an
    array built before the fork (only async-signal-safe calls between `fork` and `exec`), `execvp`. `TERM=xterm-256color`
    and `COLORTERM=truecolor` are added unless the caller sets them. A close-on-exec error pipe reports exec/chdir
    failure to `start()`. Every descriptor (master, slave, error pipe) is close-on-exec (see D2 and commit d93f151): a
    child forked concurrently on another thread cannot inherit a terminal's master and keep it open. The child leads its
    own session, so `close()` sends `SIGHUP` to the process group, waits up to 300 ms, then `SIGKILL` to the group
    (also stragglers that outlived the shell).
  * Windows (`Pty_win32.cpp`): ConPTY. `CreatePseudoConsole`, `ResizePseudoConsole`, `ClosePseudoConsole` come from
    `GetProcAddress(kernel32)`, so the binary starts (and the mingw build compiles) without them and
    `Pty::available()` is false. Two non-inheritable anonymous pipes; `STARTUPINFOEX` with
    `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE`; `CreateProcess` with `bInheritHandles = FALSE` (the pseudo console supplies the
    standard handles, so concurrent `Process::start` calls cannot leak the pipe ends either way); the process is created
    suspended, assigned to a kill-on-close job object, then resumed. The reader polls (`PeekNamedPipe` plus a wait on the
    process handle) because ConPTY keeps the output pipe open after the child exits until `ClosePseudoConsole`;
    `close()` terminates the job, then calls `ClosePseudoConsole` while the reader is still draining the pipe.
* **`VtScreen`** is the screen model: `feed(bytes)` (any chunking), grid of `VtCell{cp, fg, bg, attrs, width}`, cursor,
  scroll region, scrollback (`std::deque` of rows, default 10000 lines, `setScrollbackLimit`), alternate screen, wide
  characters (width table, 2-cell characters store a width-0 right half), per-row soft-wrap flag. Colours are one
  `uint32_t` (`0` default, `0x01000000 | n` palette 0-255, `0x02000000 | rgb` truecolour); mapping to real colours and
  bold-as-bright is the renderer's decision. Replies to the program (`DSR`, `DA`) go through `setReplyCallback`, which the
  owner connects to `Pty::write`. Mode accessors for the panel: `bracketedPaste()`, `applicationCursorKeys()`,
  `mouseTracking()/mouseSgr()`, `focusReporting()`, `cursorVisible()/cursorShape()`, `title()`, `altScreen()`.
  * Rendering support: `generation()` increases whenever anything visible changes (cells, cursor, modes, title,
    size); `rowDirty(r)` / `allDirty()` / `clearDirty()` track the visible rows touched since the last repaint
    (scrolling dirties the whole region; entering/leaving the alternate screen and resizing dirty everything).
    Scrollback rows are immutable once pushed.
  * Resize does **not reflow**: columns are truncated (a wide character that would be cut is blanked) or padded with
    blanks, rows are appended at the bottom; when the height shrinks on the main screen and the cursor would fall off,
    the top lines move into the scrollback so the cursor line stays visible. The scroll region is reset and the program
    is expected to redraw after `SIGWINCH`. Scrollback lines keep the width they had (renderers pad or clip).
  * Robustness: a byte-at-a-time and a whole-buffer feed give identical screens (tested); unknown sequences (any CSI
    final, DCS/APC/PM/SOS strings, unknown OSC, ESC with intermediates) are consumed and ignored; `CAN`/`SUB` abort a
    sequence, `ESC` inside one restarts it; parameters beyond 32 and OSC payloads beyond 4096 bytes are dropped without
    losing sync; invalid UTF-8 becomes U+FFFD. Random-byte tests check the cursor/region invariants under ASan.
  * Throughput (Release, 50 MB of coloured `ls`-like output in 64 KiB chunks): see the number printed by the
    `vt_throughput` test; printable ASCII takes a fast path that writes a cell and marks the row dirty.
  * Supported sequences. C0: BS HT LF VT FF CR SO SI (BEL and the rest are ignored). ESC: `7 8 c D E M H = >`, `( )`
    with `0`/`B` (DEC special graphics on G0/G1), `Z`. CSI: `@ A B C D E F G H I L M P S T X Z ` a b c d e f g h l m
    n r s t u`, DECSCUSR (`SP q`), DECSTR (`! p`). SGR: 0-9, 21-29, 30-39, 40-49, 90-97, 100-107, 38/48 with `;5;n`
    `;2;r;g;b` and the colon forms (`38:2::r:g:b`, `4:n`), 58/59 (underline colour, consumed and ignored). Modes (set
    and reset): 4 (insert), private 1 (application cursor keys), 6 (origin), 7 (wrap), 25 (cursor), 47, 1047, 1048, 1049
    (alternate screen), 1000/1002/1003/1006 (mouse flags), 1004 (focus), 2004 (bracketed paste); other modes (3, 5, 12,
    1005, 1015, 9001 ...) are accepted and ignored. Reports: `CSI 5 n`, `CSI 6 n`, `CSI ? 6 n`, `CSI c`, `CSI > c`,
    `ESC Z`, `CSI 18 t`. OSC 0 and 2 (title); OSC 1, 7, 8, 9;9 and the rest are ignored. Not implemented: reflow,
    combining characters (dropped), grapheme clusters, selective erase, rectangle operations, DECALN, sixel/kitty graphics.
* **`TermKeys`** (`encodeTermKey`, `encodeTermPaste`) are pure functions mapping a `TermKeyEvent` (named key or UTF-8
  text, ctrl/alt/shift) to the bytes of xterm conventions: arrows/Home/End (`ESC [ x`, `ESC O x` in application cursor
  mode, `ESC [ 1 ; m x` with modifiers), Insert/Delete/PageUp/PageDown (`ESC [ n ~`), F1-F12, Ctrl+letter, Alt as an `ESC`
  prefix, Enter `CR`, Backspace `DEL`, Shift+Tab `ESC [ Z`. Pastes are wrapped in `ESC [ 200 ~ ... ESC [ 201 ~` when
  `bracketedPaste()` is on (embedded end markers removed), else line breaks become `CR`.

Threading: `Pty` owns one reader thread; `VtScreen` and the key functions are single-threaded and belong to whoever
consumes the posted data (the main thread). Lock order is trivial: the backend's `exitMu` is a leaf lock.


## Terminal panel (W8f, `src/ui/features/Terminal*`, `src/ui/widgets/TerminalView.*`)

The UI on top of the terminal engine. 10x commands `NewTerminal`, `ShowTerminal`, `ClaudeCodeTerminal`, `CursorTerminal` are
`Terminal.New`, `Terminal.Show`, `Terminal.NewClaude`, `Terminal.NewCursor` (aliases in `CommandNames.cpp`).

* **Hosting.** A terminal is a tab of an editor panel (`Workbench::addWidgetTab`): the tab strip, drag between panels, split
  drops, Ctrl+PageUp/Down, Ctrl+Tab, Control F4 and the tab's x all work on it. A widget tab has no `Document`: it is not
  in `layout.json`, DuplicatePanel/split do not copy it, `closeTab` destroys the widget without asking. Destroying a
  `TerminalView` destroys its `TerminalSession`, which kills the process tree (`Pty::close`).
* **`TerminalSession`** (UI-free): `Pty` + `VtScreen` + plumbing. The Pty reader thread only appends bytes to a mutex-guarded
  chunk queue and calls `Editor::post` (plus an optional `wake`, the SDL wake event) once per burst; the posted task feeds the
  `VtScreen` **on the main thread** with a 4 ms budget per call and re-posts itself while bytes remain, so a 50 MB burst costs a
  slice per UI iteration. The reader blocks above 8 MiB queued (backpressure). The `VtScreen` is never touched by two threads.
  Input (keys, paste, `VtScreen` replies) goes through a write queue served by a writer thread, because `Pty::write` blocks.
  The destructor stops the writer (`Pty::kill` first when it is stuck in a write), then `Pty::close`. The posted tasks hold a
  shared state whose `owner` pointer the destructor nulls, so tasks queued for a dead session do nothing. On exit the final
  screen stays and `[process exited with code N]` is written to it.
* **`TerminalView`** (widget): paints the cell grid (merged background rects, text runs at exact cell x), 16 ANSI colours from
  `Theme::terminalAnsi0..15`, 256-colour cube/grays computed, truecolour, bold (bright + double strike), dim, underline,
  strike, inverse, wide characters, DECSCUSR cursor shapes; the renderer repaints the whole frame, so `VtScreen` dirty rows are
  only cleared, not used. A custom `terminal.font`/`terminal.fontSize` uses one shared `TextRenderer` (`TerminalFont`) flushed in
  `paintOverlay`. Scrollback (wheel, Shift+PageUp/Down, scrollbar, keeps its place while output arrives, any key snaps to the
  bottom), selection (drag, word, line, Shift-extend) in absolute scrollback coordinates, clipboard, bracketed paste, focus
  reporting, SGR/X10 mouse reporting, alternate-screen wheel to arrow keys. Keys: `encodeTermKey`; Ctrl/Alt combinations from
  `KeyDown`, printable text from `Text` events (Ctrl+Alt = AltGr is left to `Text`).
* **Copy / paste (VS-TERM).** Visual Studio's terminal keys: `TerminalView::onKey` sends Ctrl+C (^C) and Ctrl+V (0x16) to the program
  and only copies on Ctrl+C when `ctrlCCopiesSelection` (`terminal.ctrlCCopiesSelection`) is set and a selection exists. Copy and paste
  are the commands `Terminal.Copy` / `Terminal.Paste` (key context `terminal`, which `EditorApp::activeContext` returns while a
  `TerminalView` is focused; defaults Ctrl+Shift+C, Ctrl+Insert / Ctrl+Shift+V, Shift+Insert in the default key map, so rebindable).
  A right click pastes. `Terminal.OpenAt <path>` (explorer menu "Open in Terminal") creates a shell in that folder (a file: its folder).
  The default shell (`terminal.command` empty) is `Pty::chooseTerminalShell` (pure: Windows pwsh.exe if on PATH else powershell.exe,
  else `$SHELL`, else `/bin/sh`; `Pty::defaultTerminalShell` supplies the real PATH).
* **Key routing.** While a terminal has the focus `EditorApp::handleEventImpl` sends a `KeyDown` straight to it unless a global
  binding starting with that stroke runs `Terminal.*`, `Workbench.*` (tabs, panels, close tab, focus/move/duplicate panel),
  `App.Quit`, `App.CommandPalette` or a window/explorer/zoom toggle (`EditorApp::terminalSkipsShell`).
* **`TerminalPanel`** (feature): creates views, resolves the command (`terminal.*` settings; off Windows `cmd.exe` = `$SHELL`
  / `/bin/sh`, a leading `cmd /c` dropped), the working directory (workspace root, else the current file's folder, else home),
  the initial size (a synchronous layout, so the program starts with the right size), cycling for `ShowTerminal`, tab titles
  (the program's OSC title) and `terminal.closeOnExit`. Tests: `tests/ui/test_w8f_term.cpp`.
* ConPTY (Windows) has still only been compile-tested; everything above `Pty` is platform neutral and draws through the same
  `gfx::Renderer` calls as every widget, so the D3D11 path needs nothing of its own.

## Corpus harness (W9a, `src/corpus`, part of `tenfold_core`)

The end-to-end correctness and speed check of the code intelligence: `tenfold-cli corpus <dir>` and the
`tenfold-corpus-tests` target run annotated projects (`tests/corpus/<name>/`, format and how-to in `docs/TESTING.md`)
against the real `Editor` / `SemanticEngine` / clangd.

* `corpus/Corpus.{h,cpp}` is pure: the anchor scanner (`/*@name*/token`), the `tenfold-expect.txt` parser (errors as
  `file:line: message`, all of them at once), the location comparison (`locMatchesAnchor`, `checkLocations`,
  `checkHover`, `checkRename`) and the latency statistics. It is unit-tested without clangd (`tests/test_corpus_parse.cpp`,
  in `tenfold-tests`).
* `corpus/Runner.{h,cpp}` copies the project to a scratch directory (rename, `edit` and `.tenfold/` never touch the
  repository), starts an `Editor`, waits for the fast index and walks the expectations in order. Queries go through the
  calls the UI makes: `Editor::gotoDefinition`, `gotoDeclaration`, `findReferences`, `findSymbols`, `hover`,
  `renameSymbol`, `selectVsConfiguration`, `open`, `replaceRange`, and `gotoTypeDefinition` / `findImplementations`.
* **Isolating the fast tier.** `SemanticEngine::Options::fastOnlyQueries` / `setFastOnlyQueries()` is the one hook the
  harness needed in the engine: a single `queryClient()` (null while the flag is on, else `clangd()`) replaces
  `clangd()` at the entry of `deliverWithBudget` (definition, declaration, type definition, implementations, workspace
  and document symbols), `findReferences`, `hover` and `rename`. With the flag on those paths behave exactly as when
  clangd is not running (Approximate answer plus the error "clangd not running") while clangd keeps parsing the
  document, so a `fast` check never races clangd. The flag is off in the product and for every final query; the runner
  flips it per query.
* **Final answer.** The first Authoritative callback (or any error). Before it the runner waits for the document's TU
  (`fileStatus idle`) and, once per project and after each `config`, for clangd's background index; that wait is not
  in the latency. A thread polls `fileReady` after the first document is opened to measure the time to the first TU.
* **Agreement.** At every `def` / `decl` line both tiers are queried and compared (same `file:line` set).
* **Report.** `Report::toJson()` (counts, per-kind fast/final latency median / p95 / max, agreement and every
  disagreement, start / fast index / first TU / clangd index / total time, peak RSS, one record per line); one text line
  per expectation while running. `exitCode()`: 0, 1 for a FAIL or XPASS, 2 for a format or setup error.
* CI: the `corpus` job builds `tenfold-corpus-tests` (Release) with clangd from pip, runs every corpus (blocking) and
  uploads `corpus-reports/*.json` as an artifact. The target is not in `ctest`.

## Portability

Everything is standard C++20 except the platform files behind small interfaces: `src/lsp/Process_posix.cpp` /
`Process_win32.cpp` (fork/exec and pipes; `CreateProcessW` with inherited pipes and a kill-on-close job object),
`src/term/Pty_posix.cpp` / `Pty_win32.cpp` (pty; ConPTY), `src/debug/VsDebugger_win32.cpp` (Visual Studio automation;
Windows only), the file watcher backends in `src/workspace/FileWatcher.cpp` and the platform parts of `src/core`
(`Platform`, `Trash`, `PathUtil`, `CrashHandler`). On Windows the editor ships `clangd.exe` (Packaging below) and uses
`--query-driver` or the `clang-cl` driver so MSVC system headers resolve; `CompileCommands::fromVcxproj` emits
`clang-cl` style commands.

## Packaging (D1)

**Layout** (same on every platform; `cmake/TenfoldPackage.cmake`):

```
tenfold-<ver>-<os>-<arch>/
  bin/tenfold-ui[.exe]  bin/tenfold-cli[.exe]  bin/clangd[.exe]
  lib/clang/<major>/include/...     clangd's builtin headers
  licenses/  THIRD_PARTY_NOTICES.md  LICENSE (when the repo has one)  LLVM-LICENSE.TXT
  clangd-version.txt
```

clangd locates its builtin headers at `../lib/clang/<major>/include` relative to its own binary, so keeping the LLVM
release's `bin/` + `lib/clang/` shape is what makes standard headers like `<stddef.h>` resolve; the two executables
simply sit next to it in `bin/`.

**Discovery order** (`resolveClangd` / `findClangd` in `src/workspace/Workspace.cpp`, used by
`SemanticEngine::launchClangd`): (1) the `clangdPath` workspace setting or `--clangd` (a path, or a name looked up on
PATH; if it does not resolve the start fails, there is no silent fallback), (2) `clangd` next to the running
executable (`<exe dir>/clangd`, then `<exe dir>/bin/clangd`), (3) `clangd` on PATH. The choice is logged
(`clangd: using <path> (setting|bundled|PATH)`) and exposed as `Status::clangdExe/clangdSource` (`status` in the CLI).

**clangd is downloaded, never built.** `tools/package/ClangdPin.cmake` pins the clangd release version (the clangd-only
`clangd-<windows|linux|mac>-<ver>.zip` assets of the clangd/clangd releases) and the SHA-256 of each asset.
`cmake --build <dir> --target fetch-clangd` (or `cmake -DDEST=<dir> -P tools/package/FetchClangd.cmake`) downloads,
verifies, and stages it in `<build>/clangd-stage`; `install()` then copies the stage (installing with nothing staged
is an error unless `-DTENFOLD_BUNDLE_CLANGD=OFF`). The pinned release can differ from the clangd the CI test jobs
install from PyPI; bump both deliberately.

**Packages** (CPack, `cpack` in the build directory): Windows `ZIP`; Linux `TGZ` plus `tools/package/appimage.sh`
(linuxdeploy, CI only); macOS `DragNDrop` (unsigned, best effort). No NSIS/WiX installer: that would need extra tools
on the runner. Code signing, auto-update and store packaging are out of scope. All runtime assets (default settings,
key bindings, themes) are embedded in the binary, so there are no asset files to install.

**Version stamping.** `project(tenfold VERSION ...)` plus the short git commit (`-DTENFOLD_GIT_COMMIT=...` overrides)
are written at configure time into `<build>/generated/tenfold_version.h`; `tenfold-cli --version`,
`tenfold-ui --version` and the GUI window title print `tenfold <ver> (<commit>)`.

**Smoke test.** `tools/package/smoke-test.sh <package>` unpacks to a temp dir, checks the version, then runs a
definition query on `examples/sample_project` with every directory containing a `clangd` removed from PATH and fails
unless the engine logged `(bundled)` and answered authoritatively. CI jobs `package-<platform>` (pushes to main and `v*` tags
only; the AppImage step is best effort) run build, fetch, cpack, smoke test and upload the artifacts per platform.

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
`TextView` (below). `Theme` holds the colours; B7 loads them from theme files (default `vs-dark`). The engine never
includes any of this.

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

### Context menus (W8f, `src/ui/widgets/ContextMenu.*`, `src/ui/features/ContextMenus.*`, `EditorApp`)

`ContextMenu` is a modal top-layer widget like `ListPopup`: `ContextMenu::open(root, menu, anchor, keyboard)` shows it
through `showModal` (previous focus restored on close). Items are `MenuItem`s (label, display-only shortcut, enabled,
separator, optional `sub` submenu, `action`); the geometry is computed on demand from the root's size (below the anchor
rectangle, above it when there is no room, clamped to the window; a submenu to the right of its parent item or to the
left). Keys: Up / Down / Home / End over the enabled items (wrapping), Right / Left open and close a submenu, Enter /
Space run, Escape closes the innermost menu, a typed letter jumps to the next enabled item starting with it. Mouse:
hover selects and opens a submenu, a left press runs the item (not the release: the menu opens under the pointer, so the
release of the opening right click must not hit an item), a press outside closes. The action runs after the menu has
closed.

Menus are data (`ContextMenus.cpp`): a list of `MenuEntry` (command name, label, optional "only on a version-control
change"). `buildMenu` turns it into items through two hooks that `EditorApp` fills (`buildCommandMenu`): an entry is
**left out** when its command is unknown, a 10x command tenfold lacks (`ShowInWorkspaceTree` until the tree exists) or
has no handler, **greyed out** when `Command::enabled` says no, and shows the first key binding in 10x notation
(`bindingFor`). A menu item never carries behaviour: it calls `runCommandId`. Separators that would lead, trail or repeat
are dropped.

`EditorApp::handleEventImpl` intercepts a right `MouseDown` (no modal open) and calls `openContextMenuAt`; the
`App.ShowContextMenu` command (10x `ShowContextMenu`, Shift F10 / AppsKey) calls `openContextMenuForFocus` and closes an
open menu instead when pressed again. Both go through `menuRequestFor(target, x, y, keyboard)`: first the **providers**
(`addContextMenuProvider`, newest first; `EditorApp` registers one for the workspace explorer's `TreeView`, `explorerMenu`,
using `TreeView::nodeAt` / `selectedNode`, and any other widget that owns a menu can add its own:
it gets the widget under the pointer, or the focused widget with `keyboard` set, and fills items plus an anchor), then
the built-ins found by walking up from the widget: a `TextView` (editor menu; with the mouse the view and panel become
active and the caret goes under the pointer unless it is inside a selection; from the keyboard the anchor is the caret
rectangle clamped into the view), a `TabStrip` (the clicked tab is activated first, because the tab commands act on the
active tab), a `StatusBar` segment (`StatusBar::keyAt`). Widgets that ignore the right button are untouched, so a right
click where no menu applies still reaches them (the navigation dropdown buttons).

Commands added for the menus (all thin over existing `Workbench` calls): `Workbench.CloseOtherTabs` (10x
`CloseAllOtherTabs`), `Workbench.CloseAllButPinned` (`CloseAllButPinnedTabs`), `Workbench.PinTab` / `TogglePinTab`,
`File.CopyPath` (`CopyActiveFilePath`) and `App.ShowContextMenu`. The two close commands skip the last view of a modified
document (a message says how many were kept) instead of opening one prompt per tab. Theme keys `menuBackground`,
`menuBorder`, `menuSelection`, `menuSeparator` are optional (popup colours otherwise).

### Pickers (`src/ui/features`, B5a)

All pickers are owned by `EditorApp` and share a `PickerEnv` (root, editor, `LineSource` for previews, the app clock).
While one is open the key context is `picker`, so the `editor` bindings (arrows, Enter, Escape) do not fire and the
keys reach the popup. *Symbol search* (`Search.SymbolSearch`, Ctrl+,): typing calls `Editor::findSymbols` on the main
thread (it returns at once: the fast tier runs on the engine's symbol thread since A10); the first
keystroke after a pause goes out at once, later ones at most every 25 ms (`tick` returns the wait); every request has a
sequence number and answers for an older one are dropped. An Approximate answer is shown greyed with "verifying..."
(or "approximate (reason)" when no authoritative answer will follow, as without clangd); the Authoritative one replaces
it and the selected symbol stays selected if it is still present (constraint 1.4). The right pane previews the target
line from the open Document (unsaved text) or from disk; setting `picker.preview` (default true, OPEN_QUESTIONS #6).
Enter -> `Editor::gotoLocation`, so it lands in navigation history, and `Workbench::sync` reveals the tab. *Open file*
(`File.OpenFile`, Control O / Shift Alt O): `FileSearch::rank` scores every workspace file with `FastIndex::fuzzyScore` on the
filename (weight 4) and the relative path, whitespace-separated tokens must all match, files from
`Workbench::recentFiles()` (MRU, persisted as `recent` in `layout.json`) get a decaying bonus and lead the empty-query
list; it opens with `Workbench::openFile`, a tab operation that is not a navigation jump. 8.7k Godot paths filter in
2-4 ms per keystroke. *Result list* (`NavResultPicker`): GotoDefinition/Declaration with several candidates and
FindReferences with any number show `file:line` plus the source line; the approximate list is swapped in place for the
authoritative one with the selection kept by location; stale answers of an older command are ignored.
*Chooser* (W10-CHOOSE): when the targets carry a `SymbolLocation::label` the same picker shows a compact chooser:
`ListPopup::setAnchor` places it just below the caret (above when there is no room; for the middle-click command at the
clicked symbol; `NavResultPicker::setAnchor` is set by `EditorApp::runCommandId`), one row per target (label, code line,
`file:line` right aligned), no more rows than targets, the `preferred` row selected. Enter / click go through
`Editor::gotoLocation` (history, Back returns), Escape closes without moving. A single labelled target is not a chooser:
the Editor has jumped and `EditorApp::showNavResults` puts the label in the status bar ("... (implicit constructor)"); a
held-back approximate answer shows `waiting for clangd`.

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

*Several result lists* (NewSearchPanel, W8e): 10x keeps independent search panels with their own results. tenfold keeps its
one modal picker and adds lists to it: `FindInFiles::openNew` parks the current list (`Set`: query line, options, matches,
summary; the last `kMaxKept` = 3) and opens an empty one; `cycleSet` (Alt+PageUp / Alt+PageDown in the picker) rotates a
kept list back in and parks the current one. F4 and the markers always follow the current list. The separate 10x
commands `ShowFindInFilesResults1-4` / `ToggleFindInFilesResultsPanel1-4` need result panels and stay unimplemented.

*Replace in files* (`ReplaceInFiles`, Ctrl+Shift+H, W8e): a second modal widget in `src/ui/features/ReplaceInFiles.*`
(not a `ListPopup`: it needs two input lines). Search is the same `Editor::findInWorkspace` with `FindInFiles::parse`'s
query syntax; the matches are a checklist (`Entry::checked`, file rows toggle all their matches) and each row shows the
line with the old text struck and the literal replacement after it. Enter calls `Editor::replaceInFiles(matches,
replacement)`: files that are open documents are edited with `applyEdits` (one undo group per document, the document
stays modified), the others go through `replaceMatchesInFile` (`src/search/ReplaceInFiles.*`: temporary file in the same
directory, permissions copied, renamed over the original; bytes outside the matches, including CRLF, are untouched). A
`SearchMatch` carries the text of its line as the search saw it, so `matchStillAt` skips a match whose line has changed
since (counted as `stale`). Writing closed files needs one confirmation (`confirming()`): the first Enter only shows how
many replacements go to how many files on disk. Regex group references are not expanded, as in the file replace bar.

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
no default key): modal list of the open documents' clangd diagnostics (clangd only reports open files, the status says
so) plus the last build's `Editor::buildDiagnostics`, labelled "build"; errors first, filter by text, live refresh,
Enter -> `gotoLocation`. Engine additions (additive): `Editor::hoverAt(doc, pos, done)` and
`Editor::signatureHelp(done)`.

### Paste history, key sequences, Perforce history (W8f, `src/ui/features/{PasteHistory,KeySequence,PerforceUi}.*`)

* **PasteHistory** (`Editor.PasteHistory`): a deque of strings, newest first, capped by `editor.pasteHistorySize`. EditorApp
  routes Copy, Cut and CutLine through `clipboardSetter()`, which records what it put on the clipboard (the history hook;
  `Editor` itself is untouched), and `FocusGained` calls `syncFromClipboard()`, which records a clipboard text that differs
  from the last one recorded or set. The popup is a `ListPopup`; accept calls the paste hook (`pasteText`, the same code as
  Paste: one line per caret when the counts match), moves the entry to the top and sets the system clipboard.
* **KeySequence** (`App.RecordKeySequence` / `App.PlaybackKeySequence`): steps are Command (tenfold id + argument), Text
  (adjacent typed text merged) and Key (a key press with no binding that the editor view took). `runCommandId` records
  after resolving the 10x name, only in the `editor` key context and never the two sequence commands or the palette
  command; `dispatchUnbound` (the path of `Text` events and unmatched keys to the widgets) records the other two. Playback
  runs commands through `runCommandId` and Text / Key steps through `RootWidget::dispatch`, inside
  `Document::beginGroup` / `endGroup` of the document current at the start (one undo step), then
  `Editor::setSelections` to end the typing coalescer. Recording shows a `recording` status segment (`REC`).
* **PerforceUi**: `history()` asks `VcsManager::perforceHistory`, then shows a `ListPopup` (rows from `Revision`, UTC dates);
  accept calls `perforceRevisionText`, writes `<temp>/tenfold-p4/<hash of path>/<stem>#<rev><ext>`, removes its write
  permission and opens it with `gotoLocation`. `showInP4v()` asks `perforceP4vTarget`, builds `-p -u -c -s` and starts the
  program through the new `OsLauncher::startProgram` (tests replace it); the program is `vcs.p4vPath` or `p4v`.

### 10x parser commands (W8c, `src/ui/features/{RenameInput,FunctionPicker,ParserCommands}.*`, `EditorApp::bindParserCommands`)

Eight 10x commands wire the existing engine features to 10x's default keys; none re-implements a feature.

* **RenameSymbol / SubmitRenameSymbol** (`Control R`, then `Enter`): `RenameInput` is a non-modal top-layer box under the
  symbol (a bordered widget holding an `InputLine`, pre-filled and selected, focused). While it is open
  `EditorApp::activeContext()` is `"popup"`, which is the context 10x's `Enter: SubmitRenameSymbol` binding lives in
  (CommandNames.cpp), so Enter never reaches the editor's `Editor.Enter`; Escape and a click outside cancel; Up/Down/PageUp/
  PageDown are swallowed by the box (they are `AutocompleteUp...` in the popup context and must not report "not implemented").
  The engine half is `Editor::renameSymbol(at, name, done)` over `SemanticEngine::rename` (clangd's `textDocument/rename`).
  **No approximate tier (constraint 1.4):** without a running clangd the command says so and opens nothing, and the engine
  call answers `clangd not running`. `Editor::applyRenameEdits` applies the WorkspaceEdit: every file is opened first (a
  failure to open one leaves all untouched), one undo group per document (`applyEdits`), documents that were open stay
  open and dirty, files that were not open are saved (with `trimTrailingWhitespace` switched off for the write, so only the
  renamed text changes) and closed again; a file that cannot be saved stays open and dirty and is named in the result.
  A rename is dropped when the document changed while clangd was thinking.
* **Autocomplete** (`Control Space`): `CompletionPopup::trigger()` is the same request as typing, anchored at the start of
  the word at the caret whatever its length; an empty or failed answer says so in the status bar (typed triggers stay
  silent). **ShowFunctionArgsInfo** (`Control Shift Space`): `SignatureHelp::trigger()` requests the call around the caret
  (the scan for the unmatched `(` of the typed path) and reports "not inside a function call" / clangd's error.
  **ShowSymbolInfo** (`Control K, Control I`): `HoverTooltip::showAtCaret()` fills the hover tooltip for the word at the
  caret (diagnostics first, then `Editor::hoverAt`, greyed and marked when the fast index answers).
* **FindFunction** (`Alt M`): `FunctionPicker`, a modal `ListPopup` over `Editor::documentSymbols` (new, current document;
  `SemanticEngine::documentSymbols` already delivered the fast tier first and clangd's list as the authoritative
  replacement). Only function-like symbols that are *defined* are listed (`isFunctionDefinition`): fast-tier symbols carry
  a `definition` flag; clangd's document symbols do not, so `SymbolLocation::extent` (the full range, additive) is
  inspected: a `{` before a `;` outside parentheses means a body. Source order, fuzzy filter on the qualified name, rows
  greyed with the `verifying...` / `approximate (why)` badge until clangd answers, selection kept on replacement. Enter
  is `Editor::gotoLocation` (navigation history).
* **GotoSymbolDefinitionUnderMouse** (`MouseMiddleButton`): `Editor::gotoDefinitionAt(doc, pos, done)` is `gotoDefinition`
  for a position other than the caret: the same two-tier query and `autoJump`, except that a single answer that is the
  asked-about spot itself does not jump. `TextView::identifierAt(x, y)` is the hit test (inside the text area, over the
  glyphs of an identifier, not over blank space after a line or a number). `EditorApp::handleEventImpl` runs the command
  only when that test succeeds; over a tab, the gutter, a picker or blank text the middle click is dispatched to the
  widgets exactly as before, so `TabStrip`'s middle-click close is untouched. The caret does not move and the answers share
  the `Editor.Goto*` result picker for several definitions. From the command palette it uses the last mouse position.
* **FormatSelection** (`Control K, Control F`): `Editor::formatRange` (clangd range formatting, one undo group). The range
  is the first non-empty selection (the primary one first); without a selection it is `formatScopeRange`: the innermost
  `{ }` block around the caret as whole lines from the line of the `{` to the line of the `}` (comments, string and
  character literals and preprocessor lines skipped), else the caret line. clangd only (no approximate formatting).
* **InsertPrevAutocomplete / InsertNextAutocomplete** are not implemented: their description does not say what is inserted.
  See FEATURE_PARITY.md.

## Commands and key bindings (`src/ui/commands`, `src/ui/settings/KeyBindings.*`, part of `tenfold_ui`)

Every user action is a named `Command` (`Editor.GotoDefinition`, `Nav.Back`, ...: id, human name, handler
`bool(const CommandContext&, string_view arg)`, optional `enabled()`), held in a `CommandRegistry` that can enumerate
(command palette) and `run(id, arg, ctx)`; the result distinguishes `UnknownCommand`, `Disabled`, `Unbound` and
`Failed`. `registerEditorCommands` registers what `Editor` can serve itself; UI-only commands (pickers, find in
workspace, MRU cycling, clipboard, `Workbench.*`, `Vcs.*`) are registered without a handler and report `Unbound` until the
front end calls `CommandRegistry::bind`. `KeyMap` is the binding table plus the chord state machine
(`feed(chord, activeContext, nowMs)` returns Matched / Pending with "Control K..." text / NoMatch; a stroke that breaks a
pending chord is swallowed, a pending chord expires after `timeoutMs`; context-scoped bindings beat `global`; a stroke
that is a complete binding and also the start of a longer one, as 10x's `Control S` / `Control S, Control S`, matches at
once and leaves the longer chord open, and a stroke that then does not continue it is an ordinary fresh stroke).

**Key bindings are 10x's** (OPEN_QUESTIONS #1, B4b): files are `.10x_settings` text, `<keys>: <command>`, chords of
space-separated modifiers (`Control Shift Alt`) and a key, a sequence separated by `, `, key and mouse-button names as 10x
writes them (`Comma`, `Cancel`, `AppsKey`, `MouseMiddleButton`, `MouseBackButton`, `MouseForwardButton`; `formatChord` is
the inverse). `parseKeyBindings` collects errors and duplicate warnings with line numbers and never throws; an empty value
unbinds, a Python statement loads as the pseudo command `Unsupported.Python` with a warning, the old `keys = Command` style
is an error pointing at the new format. `src/ui/commands/CommandNames.*` is the alias table: every one of 10x's commands
(`Commands.txt`) with its tenfold command id (empty = not implemented yet, then the binding keeps the 10x name and
`EditorApp::runCommandId` answers `not implemented yet: <Name>`), the key *context* its binding belongs in (`editor` for
caret and text editing commands, `popup` for the autocomplete list and rename keys, which `CompletionPopup` handles itself,
`global` otherwise; this is how 10x's duplicate `Up` / `PageUp` / `Enter` entries coexist and why the pickers keep their
arrow keys) and a note where the tenfold command only approximates the 10x one. `KeyBinding::command` is the resolved
tenfold id, `KeyBinding::written` the text as typed. 10x names match case-insensitively; tenfold ids stay valid; the
palette shows the first 10x name of a command (`displayName10x`). A later layer replaces earlier bindings of the same
keys in every context. Layers (`loadKeyBindings(KeyLoadOptions)`): embedded `assets/KeyMappings.default.10x_settings`
(generated into a header at configure time: 10x's default map verbatim, then a `# ===== tenfold additions` section),
an installed 10x's `%APPDATA%\10x\Settings\KeyMappings.10x_settings` (Windows; `EditorApp::setKeyImport10x` /
`setKeyImportFile`, setting `keys.import10x` once it is in the settings schema; only when the user dir is the real
user config dir), `<user config dir>/tenfold/KeyMappings.10x_settings`, `<workspace>/.tenfold/KeyMappings.10x_settings`.
A leftover `keybindings.txt` gives a warning. When a matched binding's command is not runnable (not implemented, no
handler) and the focus is not a text view, the focused widget gets the key first (`Alt R` in the find bar; since W8e
`ReplaceNextInFile` / `ReplaceAllInFile` exist, `EditorApp::widgetOwnsKey` gives `Alt R` to an open picker and to the
find-only bar, and to the commands when the bar shows its replace row), and an
unimplemented middle-button binding never swallows the click (tab close). `tests/fixtures/KeyMappings.10x.expected.txt`
handler) and the focus is not a text view, the focused widget gets the key first (`Alt R` in the find bar), and an
unimplemented middle-button binding never swallows the click (tab close). `Control MouseMiddleButton`
(`AddCursorAtMousePos`, `Editor.AddCaretAtMouse`) runs only when the pointer is over a `TextView` (`EditorApp::textViewAt`,
the pointer position is tracked from mouse events); elsewhere the click is dispatched to the widgets. `tests/fixtures/KeyMappings.10x.expected.txt`
is the key list generated from the 10x documentation; a test compares the defaults file against it.
`EditorApp` (below) wires `KeyMap::feed` into the app's event loop.
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
`Theme::darkDefault()`; a neutral placeholder, kept for the goldens), `light` and `vs-dark` (the application default,
`ui.theme = vs-dark`: Visual Studio 2022 Dark, B7b) are embedded; a theme name also
resolves to `.tenfold/themes/<name>.txt` or `<user dir>/themes/<name>.txt`. `SettingsService` loads both, watches the
user dir and `<workspace>/.tenfold` (if it exists) with `FileWatcher`, and on a change sets a flag (plus an optional
thread-safe `wake` callback); the UI thread calls `poll()`, which reloads, fires `onChanged` / `onThemeChanged`, and
`applyTo(RootWidget&, scale)` swaps the theme (repaint). `editorSettings()` gives TextView its subset in one call.
The optional colour keys (`editorBackground`, `currentLineFill`, `currentLineBorder`, `selectionActive`,
`selectionInactive`, `findHighlight`, `findHighlightCurrent`, `editorCaret`, `tabActiveText`, `tabInactiveText`,
`popupBackground`, `popupBorder`, and the debugger keys `debugBreakpoint`, `debugCurrentLine`, `debugCurrentArrow`,
`statusBackgroundDebugging`, `statusTextDebugging`) default to fully transparent, which means "paint the pre-B7b look" (`Theme::orElse`),
`popupBackground`, `popupBorder`, and the version-control keys `diffAdded`, `diffModified`, `diffDeleted`,
`diffBlockBackground`, `diffBlockText`, `diffBlockHeader`, `tabVcsModified`, `tabVcsAdded`, `tabVcsCheckedOut`,
`tabVcsReadOnly`, C3c) default to fully transparent, which means "paint the pre-B7b look" (`Theme::orElse`),
so `dark` and `light` and their goldens are unchanged; only `vs-dark` sets them. `vs-dark.txt` lists in its header the
values that are recalled rather than sampled. The `controlKeyword` token kind (if / for / return ..., W10-THEME) falls back to `keyword`; string-escape colours need a
lexer kind that does not exist yet and have no key. The debugger keys fall back to built-in colours (breakpoint #E51400, the dirty-marker
colour for the statement arrow and band); the two status bar keys have no fallback, so the bar only changes in themes
that set them (`vs-dark`).

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

#### Tabs, panels and view commands (W8b, `Workbench`, `Splitter`, `EditorApp`, `src/ui/features/{SaveAsPrompt,OsLauncher}.*`)

* **Panel geometry** (`Workbench::panelToward`): the neighbour of a panel in a direction is found from the laid-out
  rectangles (it re-lays out first, so a command right after another sees the new tree): among panels wholly beyond that
  edge that overlap the panel across it, the nearest, then the one with the most overlap, then layout order.
  `focusPanelToward` activates it; `movePanelToward` swaps the two panels' places (`swapPanels`: `Splitter::swapChildren`
  for siblings, else three `replaceChild` calls through a placeholder), so the layout serialises like any other tree.
  `duplicatePanel` is `splitPanel(..., duplicateAll)`: views of every tab, pinned flags and the active tab included.
  `stepTab` (wraps) and `moveActiveTab` (does not wrap, stays in its pinned / unpinned group) work on the panel's strip.
* **Focus mode** (`setFocusMode`): `Splitter::setSolo` gives one child the whole area and hides the other, the workbench
  sets it along the path to the active panel (`applyFocusMode`, also run from `onLayout` and after any change of the
  active panel or the tree), hides the status bar and top bar and treats the dock as closed. Ratios and tabs are not
  touched, so leaving the mode restores the window. Panel focus / move commands find no neighbour while it is on.
* **Zoom** (VS-EDIT, VS_BEHAVIOUR Z8): `EditorApp::zoomBy` keeps `fontZoomPt()` (-8..+40 points, session only) and calls
  the `rebuildFont` hook given to `attachSettings`. The hook builds the root's TextRenderer unzoomed and, while
  `fontZoomPt() != 0`, a second one at configured size + zoom (`makeEditorTextRenderer(..., app.fontZoomPt())`) that it
  gives to `RootWidget::setEditorTextRenderer`; a later `font.*` change keeps the zoom. `RootWidget::editorText()` is that
  renderer (the root's own when not zoomed); `TextView` (all metrics and its painting) and the terminal's default font
  use it, `RootWidget::render` flushes it after the root's, so tabs, status bar and menus keep their size. The status bar
  segment "zoom" shows the ratio of the two cell widths ("113 %") and a click resets. `EditorApp::handleEventImpl` turns
  Control + wheel over a `TextView` into `zoomBy(+-1)` (`editor.mouseWheelZoom`). **Fullscreen**: `EditorApp::WindowHooks::setFullscreen` (`Window::setFullscreen`, SDL borderless
  desktop fullscreen); without the hook the command only says so.
* **Untitled documents** (engine): `Document::untitled(path, lineEnding)` / `isUntitled()`; `Editor::newUntitled` picks
  "Untitled N" (`textFileNames`: "TextFileN.txt") under the workspace root (a name that is neither open nor on disk), keeps it out of clangd, version
  control and the disk hash; `Editor::save` refuses it; `Editor::saveAs(doc, path)` re-keys the same `Document` (history
  and carets stay), writes it and then announces it to clangd and version control. The workbench follows with
  `documentRenamed`; `noteRecent` and `layout.json` skip untitled tabs; `ExternalChanges` does not poll them.
  `File.Save` on an untitled document opens `SaveAsPrompt` (a ListPopup input with one row saying where the file goes or
  why not). `editor.autoNameUntitledFiles`: `EditorApp::updateUntitledNames` (every tick) derives the first word typed and
  sets a tab title override (`Workbench::setTitleOverride`) and the prompt's suggestion.
* **OS launches** (`OsLauncher`): `openUrl` and `showInFolder` through `tf::Process` (parked, reaped at the next launch);
  EditorApp holds one and tests replace it with `setLauncher`, so no browser or file manager starts in tests.
  `urlEncode` / `internetSearchUrl` are plain functions.

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
  bar), `View.ToggleBuildPanel` (Ctrl+Alt+O). Status bar segment `build` shows `build: <state>` after the first build
  and toggles the panel on click.
* **Squiggles**: `TextView` draws `Editor::buildDiagnostics(file)` under the clangd ones in `Theme::buildError` /
  `buildWarning` (theme keys `color.buildError`, `color.buildWarning`, both shipped themes), with scrollbar marks in the
  diagnostics lane (`Scrollbar::Lane::BuildError/BuildWarning`, drawn below clangd's). Notes are not drawn. A new build
  clears them (Editor). Line numbers are those of the build time (not remapped on edits).

#### Workspace explorer and left dock (W8f, `Workbench`, `src/ui/widgets/TreeView.*`, `src/ui/features/WorkspaceExplorer.*`)

* **Left dock** (`Workbench::addSidePanel / showSidePanel / hideSidePanel / toggleSidePanel / sideVisible / sideWidth`):
  the same machinery as the bottom dock, mirrored. The root's child is a horizontal `DockSplit` (now constructed with an
  orientation and a "dock first" flag) whose first child is the side `Dock` and whose second is the vertical bottom
  `DockSplit`; closed (the default), the dock gets a zero-width rectangle and the divider neither paints nor reacts, so
  nothing moves and every existing golden is unchanged. The side dock spans the area between the top bar and the status
  bar, the bottom dock only the editor column. `layout.json` gets an additive `"left": {"visible", "panel", "width"}`
  (written only once the dock was used; `width` is a fraction of the window width, 5-70 %); a file without it loads with the
  dock hidden, and a panel registered later (EditorApp adds "Explorer" after `restoreLayout`) picks up the restored state.
  Focus mode hides it like the bottom dock. `Workbench::lastCaretOf(file)` exposes the caret a tab had when it was left.
* **`TreeModel` / `TreeView`**: the model is flat (`TreeNode`: name, path, parent, kind, kids; nodes are added parent first,
  so a backwards sweep reaches every ancestor), with a path index keyed by `tf::pathKey`. The view keeps a flat row list
  (`node`, `depth`) that is rebuilt on expand / collapse / filter and paints only the rows inside the viewport (pixel
  scroll offset, a thin scroll bar thumb). Expansion is a byte per node; across a model swap it is carried by node *key*
  (names from the root), found by descending only through expanded nodes, so the cost follows what the user opened. The
  filter marks nodes whose name passes `FastIndex::fuzzyScore` for every space-separated token, propagates to ancestors in
  one backwards sweep and lists the marked nodes in tree order, everything expanded. Per-row decoration is a callback
  (`markOf`, called for visible file rows only). Theme keys (optional, `Theme::orElse`): `treeBackground`,
  `treeSelection` (focused), `treeSelectionInactive`, `treeFolder`; `vs-dark` sets the last three.
* **`WorkspaceExplorer`**: builds the model. Solution mode (`Workspace::solutionFiles().front()` lists at least one
  project): `readSolution` for the projects, a small pass over the `.sln` text for `NestedProjects` and
  `ProjectSection(SolutionItems)`, `xml::parseFile` for each project's item groups (every item with an `Include` except
  references and configurations; `$(ProjectDir)`, `$(MSBuildProjectDirectory)` and `$(SolutionDir)` are substituted,
  other `$(...)` skipped; `*`, `?` and `**` expanded) and for `<project>.vcxproj.filters` (Filter elements, each item's
  `<Filter>`). Files go under their filter (filters are split on `\`, declared-but-empty ones included), else, with no
  filters file, under the folders of their path relative to the project (leading `..` dropped). Folder mode: the three file
  lists of `Workspace` (so the scan's ignore rules and extension lists apply) split by directory; consecutive files of
  one directory reuse the last node. Both end with `sortAll` (containers first, case-insensitive) and `indexPaths`.
* **Refresh**: only while the panel is open. `EditorApp::tick` calls `WorkspaceExplorer::tick`, which looks (at most every
  300 ms; a skipped look asks for one more wake-up) at `Workspace::revision()` (a counter bumped by `scan` and by every
  incremental file-list change, i.e. the file watcher's events) and at a stamp of the size and mtime of the `.sln`, the
  projects and their `.filters`; a change arms a 250 ms debounce, then the model is rebuilt and handed to
  `TreeView::setModel`. The VCS marker follows `VcsManager::generation()` (repaint). Closing the panel drops the
  "built" flag, so reopening builds afresh and nothing is polled meanwhile.
* **Commands**: `View.ToggleWorkspaceExplorer` (10x `ToggleWorkspaceExplorer`, Control Shift E), `View.ShowInWorkspaceTree`
  (Control T), `View.ShowInWorkspaceTreeWithToggle` (unbound); setting `explorer.trackActiveFile` (10x
  TrackActiveFileInWorkspaceTree). Enter / double click: `Editor::gotoLocation` at the file's current caret (the open
  document's, else the tab's last one, else the start), `Workbench::sync`, keys back to the editor view.
* **Measured** (Release, `w8f_perf_50k_file_tree_open_scroll_filter`: 50 000 files in 400 x 5 x 25 directories, all
  folders expanded = 52 401 rows, mock font, recording renderer so GL submit is not included; budgets enforced with
  `TENFOLD_ENFORCE_PERF=1`): building the model from the sorted path list 36-58 ms; opening the panel (build + set model
  + all rows expanded + first paint) 38-60 ms against a 400 ms budget; a scroll frame 0.03-0.05 ms average, p99 0.09-0.13
  ms (8 ms budget; only the ~30 visible rows are touched); a filter keystroke (fuzzy match of 52k names, rebuild of the
  row list, paint) 1.7-3 ms worst of seven queries (100 ms budget); collapse all 0.1 ms.

Two small framework additions came with the editor view: an **overlay layer** (`Widget::paintOverlay`, run after the glyphs
are emitted, so carets draw over text at the cost of one extra rect batch) and **ticks** (`RootWidget::tick(nowMs)` calls
`Widget::onTick`, which returns the ms until it next wants a tick; the app waits at most that long, so caret blink and
smooth scrolling need no timer thread and an idle window sleeps).

### TextView (B2a, `src/ui/widgets/TextView.*`, `src/ui/view/{Viewport,Layout,WrapLayout}.*`)

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
event and tick, and the caret is scrolled into view. Settings: `smoothScroll` (default off) and `wordWrap` (default
on) per OPEN_QUESTIONS #4 (answered), `caretBlink`, `caretBlinkMs`, `highlightCurrentLine`, `wheelLines`.
Measured keystroke-to-paint (key event, edit, sync, layout, paint recording; 100k lines, 1920x1080) is ~0.1 ms with one
caret and ~0.35 ms with 1000 carets against the 8 ms budget (§1.7); the llvmpipe submit is not representative.

#### Soft word wrap (B2c, `src/ui/view/WrapLayout.*`)

(Version control, C3c: the change strip, the scrollbar change lanes and the inline diff block are in "Version control UI"
above. The block reserves *display rows* through `RowBlocks`; `WrapLayout` itself still only knows document rows, and
`RowRef::vrow` below is a display row whenever a block is open.)

`editor.wordWrap` (default **on**; toggle `View.ToggleWordWrap`, Alt+Z, for the session in every view; a change of the
setting in a file wins again) wraps lines at the view width. Everything the view did per line it now does per
*visual row*: the scroll range and the scrollbar (thumb, marker lanes: markers are converted from lines to rows, again
whenever the row map changed), hit testing (`positionAt`, `caretRect`), selection rects, the current-line highlight
(the caret's row), find marks, bracket match, whitespace dots, squiggles and carets. The gutter numbers a line on its
first row only (so does the bookmark flag). Horizontal scrolling is off while wrapping.

* **Rules** (`WrapLayout`): a row holds at most `cols` cells, where `cols` is the text width in cells minus one (the
  cell keeps the caret at the end of a full row, and one hanging space, inside the view). Cells are counted by
  `gfx::TextLayout` (tabs, wide characters, control characters) from the *start of the row*, so a tab on a continuation
  row is as wide as it would be on a line of its own. A row ends after the last space or tab that fits; one more space
  may hang past the edge; a word that does not fit on a row of its own is cut at the last cell that fits (a character
  wider than the row still gets a row). Continuation rows are indented by the leading whitespace of the line, at most
  half the width. A caret position on the boundary between two rows belongs to the later row; the last caret position
  of a row that is not the line's last is the start of its last cell (`rowLastCaret`), which is also what a click to the
  right of the row and `End` yield.
* **Cost model**: never a pass over the document. A line that fits one row is not stored at all (a cheap test: bytes
  times the larger of tab width and 2 against `cols`, else one column lookup); a line that wraps keeps its row starts,
  computed front to back only as far as somebody asked (rows on screen, the caret's row), plus a row count that is
  exact once the whole line was scanned and an estimate before (`ceil(bytes / width)`). Lines nobody asked about count
  as one row, so `totalRows()` / `firstRowOf(line)` are estimates for unseen lines and exact for the lines on screen,
  which `TextView` examines before every paint (`collectVisibleRows`). The text above the first visible line may have
  been counted as single rows; when looking at the rows on screen changes the first row of the top line (scrolling
  up into unseen lines, a jump), the view shifts by the difference (`Viewport::shiftY`) so the text stays put; a caret
  reveal first examines the lines that can share the screen with the caret. Scanning is a tight loop over printable
  ASCII (one byte, one cell) and falls back to `TextLayout` cells otherwise: about 40 ms to scan 50 MB of one line, once.
  An edit (`Document::Change`) erases the rows of the lines it replaces and shifts the entries after them; a one-line
  edit keeps the rows whose break was decided entirely before the edit (a row's break reads up to one cell past its
  width) and re-scans only from there, so typing at the end of a 50 MB line re-scans a few rows. A width or tab width
  change drops everything (the first visible line stays where it is).
* **Keys**: Up / Down / PageUp / PageDown step by visual row (`WrapLayout::nextRow` / `prevRow`, which look only at the
  rows they pass) and keep the x of the first move for as long as only vertical moves follow (per caret, checked by
  comparing the carets with what the view left behind); at the first / last row they go to the start / end of the
  document like `Editor::moveCarets`. Home / End go to the row's start / end first and to the line's on a second press.
  The Editor knows nothing about rows: `EditorApp::runCommandId` hands the focused view to the caret commands through
  `CommandContext::moveCarets` (`TextView::moveCaretsVisual`, false when wrap is off or the direction is not a row-wise
  one, then `Editor::moveCarets` runs as before). Add-caret-above / below, word and document moves stay line-based.
* **Measured** (Release, llvmpipe, 1920x1080, wrap on): keystroke-to-paint 0.1-0.2 ms on the 100k-line file (1000 carets: about
  0.6 ms), and 0.5-3 ms with one 50 MB line, almost all of it the Editor's own insert (`docs/BENCHMARKS.md`, B2c).

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

### Editing conveniences UI (B8, `src/ui/features/{FindReplace,GoToLine,BookmarkList}.*`)

* **FindReplace** (`Editor.Find` Ctrl+F, `Editor.Replace` Ctrl+H, `Editor.FindNext/FindPrev` F3 / Shift+F3): a
  non-modal top-layer bar at the top right of the editor with a find field, an optional replace field and the toggles
  case (Alt+C), whole word (Alt+W), regex (Alt+R). Typing searches synchronously through `Editor::findInDocument` and
  selects the first match at or after where the search started; every match is highlighted in the visible lines
  (`TextView::setFindMatches`, behind the text, the current one stronger) and fills the scrollbar's search lane in
  place of the find-in-files markers until the bar closes. Enter / Shift+Enter step (far moves go through
  `Editor::moveCursor`, so Back returns); in the replace field Enter replaces the selected match and moves on, Ctrl+Enter
  replaces all (one undo step); Tab switches fields; Escape closes and returns the focus to the view. The bar re-searches
  when the document changes (`tick`). Keys reach it because the focused InputLine bubbles unhandled keys to the bar.
  W8e: `Editor.ReplaceNextInFile` (Alt+R) / `Editor.ReplaceAllInFile` (Shift+Alt+R) call `FindReplace::replaceNext` /
  `replaceAllInFile` with the bar's last query, options and replacement even when it is closed or the editor has the
  focus: the first match at or after the caret (the selected match first, wrapping) is replaced and the next one
  selected; "all" is `Editor::replaceAll` (one undo step). Alt+E toggles regex as well, because Alt+R belongs to the
  commands while the replace row is shown.
* **W8a view commands** (bound in `EditorApp::bindEditingCommands`): `Editor.FindNextCurrentWord/FindPrevCurrentWord`
  (Ctrl+F3 / Ctrl+Shift+F3) call `FindReplace::stepCurrentWord`: the query becomes the selection or the word at the caret
  (whole word, case-sensitive), the bar's field follows, and `step` selects the next / previous match; the word is
  selected first so that "next" leaves it. `View.ToggleWhitespace` flips `TextView::setShowWhitespace` in every view and
  is kept as a session override (like `View.ToggleWordWrap`) until `editor.showWhitespace` changes.
  `Editor.ScrollLineUp/Down` (Ctrl+Up / Ctrl+Down) call `TextView::scrollLines(+-1)`: one display row, carets stay,
  except those that would leave the view, which move to the nearest fully visible row (same x; a selection keeps its
  anchor). `Editor.CutLine` (Shift+Delete) puts `Editor::cutLines()` on the clipboard; `File.SaveAll` reports a failing
  file in the status bar.
* **GoToLine** (`Editor.GotoLine`, Ctrl+G): a one-row modal ListPopup; `42` or `42:7`, Enter -> `Editor::gotoLine`
  (`gotoLocation`, so it is in the navigation history); numbers past the end clamp to the last line.
* **BookmarkList** (`Editor.ListBookmarks`, no default key since W8d: Ctrl+Shift+F2 is 10x's RemoveAllBookmarksFromFile): modal list of every bookmark (line text, `path:line`),
  the input filters, Enter -> `gotoBookmarkItem`. Toggle / next / previous (Ctrl+F2, F2, Shift+F2) are engine commands.
  TextView draws a small accent-coloured flag at the left edge of a bookmarked line's gutter (right of the breakpoint
  glyph lane when there is one, see "Debugger UI").
* **Settings** (`assets/settings.default.txt`, defaults = 10x's, B8b): `editor.indentUnit` (`tab` or a number of
  spaces, default `tab`), `editor.detectIndent` (false), `editor.autoIndent` (true), `editor.autoIndentBraces` (true),
  `editor.formatBracesOnEnterSplit` (false), `editor.autoAddCloseBrace` (false), `editor.autoAddCloseBracket` (false),
  `editor.autoCloseQuotes` (false, tenfold only), `editor.bracketSelectedText` (true), `editor.trimTrailingWhitespace`
  (false), `editor.commentBlockUsesLineComments` (true). `editor.wordWrap` is still only reserved: soft wrap is not implemented.
* Plain Tab inserts the indent unit (it used to insert a tab character); Shift+Tab outdents; a selection spanning several
  lines indents them. Files are decoded to UTF-8 on load and encoded again on save (see "Encodings and line endings"
  in the text model; the status bar shows the line ending, `CR` for classic Mac files; a click on it, `Editor.LineEndingLF` /
  `LineEndingCRLF` / `LineEndingCR` convert the document, `File.SaveWithLineEndings` converts and saves; the
  status bar also shows the encoding, whose click opens `File.ReopenWithEncoding` / `File.SaveWithEncoding`).

#### Code folding (W8f, `src/ui/view/FoldModel.*`, `WrapLayout`, `TextView`)

The 10x CollapseRegions commands. The model is split in three, each of which exists for another reason:

* **`FoldModel`** (pure value logic). `detect(lines)` scans a copy of the lines with the Highlighter's `lexLine`, so
  braces in strings, character literals and comments do not count: brace pairs spanning >= 2 lines, multi-line block
  comments (the line where `LexState::Block` begins to the line where it ends) and `#pragma region` / `#pragma
  endregion` pairs (a `Preprocessor` span at the start of a directive). `#if` / `#endif` are not regions: the 10x
  documentation names no region syntax at all (OPEN_QUESTIONS, "W8f detail"). One region per start line, the outermost.
  `detect(lines, Options)` takes Visual Studio's three C++ outlining options (`outline.enabled`, `outline.pragmaRegions`,
  `outline.statementBlocks`); a brace region carries `type` (the text before its `{`, or the previous line for Allman
  style, has namespace / class / struct / union / enum / `extern "C"` and no parenthesis), which `collapseToDefinitions()`
  and the statement-block option use (a statement block is a non-type brace region inside another non-type one).
  A region is `{start, end}`; collapsing it hides `[start + 1, end]`, so the closing line is inside the marker.
  `collapsed_` is the model's own sorted list, independent of the region set: edits move it (`onChange`: before the
  edit it stays, after it it shifts by `lineDelta`, touching any of its lines expands it), `setRegions` reconciles it with
  a fresh set by head line. `hidden()` merges the collapsed ranges into disjoint `WrapLayout::HiddenRange`s.
* **Staleness.** Regions are recomputed off the main thread: `TextView::foldPoll` (from `onTick`) starts a
  `std::thread` on a copy of the lines once typing paused for 150 ms and the set is stale; the result carries the
  document and version it was computed for and is applied only if both still match (`foldJob_`, joined in the
  destructor and when the document changes). Every edit patches the set in place (`FoldModel::onChange`: regions
  before the edit stay, after it shift, around it keep their head and move their end, touched ones go) and marks it
  not `fresh()`; the gutter glyphs use the patched set, the commands call `foldRefreshNow()` first when it is stale
  (a synchronous scan: about 40 ms for 100k lines, on a key press, never on a keystroke that types text).
* **`WrapLayout::setHidden`** is how hidden lines enter the row model, so RowBlocks, the scrollbar, hit testing,
  selection painting and the caret keys keep using one mapping. A hidden line has no rows. `hid_` holds the ranges with the
  count of hidden lines before each; `firstRowOf` = line - `hiddenBefore(line)` + the extra rows of the visible wrapped
  lines before it (`rebuild()` skips hidden entries and keeps `base_[i]`, the first row of each entry);
  `lineOfRow` maps a visible index back with `lineOfVisible`; `nextRow` / `prevRow` jump over a range;
  `totalRows` subtracts the hidden lines. Everything works with wrap on or off (with wrap off `moveCaretsVisual` takes
  over only the vertical keys, because `Editor::moveCarets` knows nothing about hidden lines). A hidden line maps to the
  row of the next visible line (`firstRowOf`), but scrollbar marks of hidden lines are moved to their head line.
  Cost: O(log ranges) per query, `rebuild()` is O(wrapped entries x log ranges) once per change.
* **Display.** After the head line's last row the view draws a marker box (`...}` when the head line ends in `{`, else
  `{...}`; `...*/` for comments; `...` for pragma regions; theme keys `foldMarkerBackground`, `foldMarkerText`), a click on it
  expands. With `editor.showFoldGlyphs` (default on, like VS's outlining margin) `Layout::setFoldLane` widens
  the gutter by one line height between the numbers and the change strip. `paintFoldGlyph` runs for every row: a boxed
  minus (open) or plus (collapsed) on a region start, a 1 px guide through the open regions (`glyphRegions_`, collected once
  per paint) and a tick on the last line (`foldGlyph`); a click on a box toggles. `HoverTooltip` shows the hidden code when the
  pointer rests on the marker (`foldLine_`). A collapsed region moves nothing else: the gutter,
  the text origin and the wrap width are the same with and without folds.
* **Carets and jumps.** After a collapse, carets and selection ends in hidden lines go to the end of the head line
  (`foldMoveCarets`). `syncState` opens every collapsed region that hides the head of any selection (go to line, find,
  build error, Left from below, undo), `FoldModel::onChange` opens one an edit touches (typing at the end of its head
  line included). Triple click on a collapsed head selects through the hidden lines. Inline diff blocks anchored in newly
  hidden lines are closed. Collapsing / expanding keeps the first visible line where it is (`Viewport::shiftY`).
* **Highlighter.** `prepareRuns` takes the visible lines as several runs: with a collapsed region between them the
  hidden lines are not lexed provisionally (a lookback before each run only).
* **Commands** (`EditorApp::bindEditingCommands`, `Editor.CollapseRegion` ... `Editor.ExpandAllRegionsRecursive`, 10x
  names in `CommandNames.cpp`): they act on the focused (else the current visible) `TextView`; outside C/C++ files they
  say so in the status bar. Semantics are in the user guide and OPEN_QUESTIONS.
* **Measured** (Release, llvmpipe, 1920x1080, wrap on, 100k lines with 1000 collapsed regions): see
  `foldview_perf_keystroke_to_paint_100k_lines_1000_collapsed_regions` (budget 8 ms; the numbers are printed by the test).

### EditorApp: input to commands (`src/ui/shell/EditorApp.*`, B-INT)

`EditorApp(root, editor, workbench*)` is the glue the window's event loop calls. `handleEvent(e, nowMs)` turns a KeyDown
(a lone modifier press is not a stroke) or an X1/X2 MouseDown into a `KeyChord`, picks the context (`editor` when the
focused widget is a TextView, else `global`), runs `KeyMap::feed` and `CommandRegistry::run`; everything else (text
input, the mouse, unbound keys) is dispatched to the widget tree. Bindings are `loadKeyBindings(workspace root, user
config dir)`. `tick(nowMs)` runs `Workbench::sync`, expires a stale chord (`KeyMap::tick`) and message, and ticks the
root; it returns the wait the main loop should use. The status bar has a `message` segment: the pending chord text
("Control K..."), the first key-binding diagnostic (`formatDiag`, "+N more", shown for 12 s), "not implemented yet:
<Command>" for a bound 10x command tenfold lacks, "<command> is not available
yet" for a bound command without a handler (Search.FindInWorkspace: B5b), a failed command, or an empty navigation
result. Symbol search, open file and the multi-result list are the B5a pickers above. The UI-only handlers bound
here: `App.Quit` (Control Q), `Workbench.CycleMruNext/Prev` (the cycle ends when a key event arrives without Ctrl),
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

### Safety and visibility (P2, `src/ui/features/{UnsavedPrompt,ExternalChanges,CommandPalette}.*`, `Workbench`, `EditorApp`)

* **Unsaved changes** (`UnsavedPrompt`): a modal `ListPopup` without an input (the revert confirmation's shape): title
  "Save changes to a.cpp?" and the rows Save / Don't Save / Cancel; with two or more dirty documents (VS-TABS T6) Visual
  Studio's list dialog "Save changes to the following items?": one toggle row per document (`ListRow::toggle`, "[x] src/a.cpp",
  Space / Enter / click flips it via `ListPopup::onToggle`), then Yes / No / Cancel (Yes starts selected). Yes saves the checked
  documents and reports `proceed(true)` when some were left unchecked, so the caller closes those without saving. Enter takes the first row, Escape
  and a click outside are Cancel. Save goes through `Editor::save` per document and stops at the first failure: the error
  ("Cannot save src/a.cpp: file is read-only ...") goes to the status bar, nothing proceeds, documents already written stay
  saved. Three entry points: `Workbench::requestCloseTab` (the tab's x and middle click through `Panel::onCloseRequested`,
  and `Workbench.CloseTab`) calls the `Workbench::confirmClose` hook EditorApp installs when the tab is the *last view* of
  a dirty document (`isLastViewOfDirty`; another view of it, or a clean document, closes at once) and continues with
  `closeTab(id, discard)`: Don't Save closes the document in the Editor too (`releaseDocument(path, discard)`), so no hidden
  dirty document is left behind; the programmatic `closeTab(id)` keeps its old meaning (dirty documents stay open).
  `EditorApp::requestQuit()` (App.Quit, and the window's close event: `src/ui_app/main.cpp` calls it instead of quitting
  directly) lists every dirty document of the Editor, tabs or not; `quitRequested()` turns true only when the user agreed
  or nothing is dirty. Setting `editor.confirmUnsaved` (default on) switches all three off; the tab close then parks the
  document as before. `File.Save` is bound in EditorApp to report the engine's reason instead of "Save failed". The bulk closes
  (`Workbench.CloseOtherTabs`, `CloseAllButPinned`, `CloseAllDocuments`, `ClosePanel`) go through `EditorApp::closeTabs`: the
  dirty documents whose every view is among the tabs to close are asked about at once, then every tab is closed with
  `closeTab(id, discard)`.
* **External changes** (`ExternalChanges`): owns the single `Editor::onExternalChange` slot (pump thread = UI thread).
  Policy from `editor.detectExternalChanges` / `editor.reloadUnlessUnsaved` and the optional legacy override
  `editor.reloadExternalChanges` (`ExternalChanges::resolvePolicy`; VS-TABS T9). Reload-unless-unsaved + clean document:
  `Editor::reloadFromDisk` at once (only the differing lines are replaced, so carets, scroll and history entries follow the
  engine's rules; a message names the file). Otherwise the file is queued and Visual Studio's prompt ("The file X has been modified outside of the source editor. Do you
  want to reload it?": Yes / Yes to All / No / No to All, No the default for a dirty document; Yes to All reloads the other
  queued clean files, No to All drops the queue) opens from `tick` when no other modal is up; a
  prompt that another popup closed (`closePickers`) is asked again, Escape means No. The engine notifies once per distinct
  on-disk content, so Keep does not nag. Deleted files are not reported by the engine (`checkExternalChange` ignores
  unreadable files), so `tick` polls `std::filesystem::exists` of every open document once a second:
  `Workbench::setFileMissing` appends " (deleted)" to the tab titles and `Workbench::refreshStatus` fills the `disk` segment
  ("deleted on disk"); the buffer stays and saving recreates the file.
* **Tab well** (VS-TABS; `TabStrip`, `Workbench`, `SwitchPanel`): `SwitchPanel` (Ctrl+Tab) is a modal `ListPopup` of the active
  panel's `TabStrip::mruOrder()`; `EditorApp` keeps `cycling_` while Ctrl is held and commits on the Ctrl release. The preview tab is
  `TabStrip::Tab::preview` (always the last tab, never pinned, not reorderable, drawn with `TextRenderer::drawTextItalic`, a
  synthetic slant: glyph quads cut into four bands shifted sideways) plus `Workbench::previewId_`. `Workbench::withPreview`
  runs a navigation (`Editor::gotoLocation` ...) and `sync()`; `revealDocument` makes the tab it creates the preview tab when
  `previewNext_` is set or the debugger is in Break mode at that file; `makePreview` closes the previous preview quietly (not
  "recently closed"); `sync()` promotes it when its document is dirty, `TabStrip::onDoubleClick`, `openFile` and the explorer's
  open promote it. `PickerEnv::bench` + `openResult` let result lists preview on a mouse click only.
* **clangd visibility**: `Workbench::refreshStatus` shows `Editor::clangdStatusText()` in the `clangd` segment when it is
  not empty (restarting / stopped), else the on/off/starting/indexing/ready text. The `flags` segment (declared after
  `clangd`; EditorApp binds its click to `Help.ShowCompileCommand`) reads `SemanticEngine::status().compdbSource` (a cheap
  snapshot, unlike `compileCommandInfo`, which matches every database entry) and shows "flags: heuristic" for `heuristic`
  and `generated(cached)` while clangd runs; with clangd off the `clangd: off` text already says everything is approximate
  (and the existing goldens, whose workspaces are heuristic, stay byte-identical). `Clangd.Restart` calls
  `Editor::restartClangd` (blocks for the handshake) and reports "clangd restarted" or the engine's error.
* **Command palette** (`CommandPalette`, App.CommandPalette): `ListPopup` with an input over `CommandRegistry::all()`
  restricted to commands with a handler, minus the palette and `Workbench.FocusPanel` (argument required; `Build.Build`
  stays and runs without one). Row: name (fuzzy-matched characters highlighted), id (dim), first key binding
  (`EditorApp::bindingFor`: global bindings first, then any context). Ranking: name or id, whichever matches tighter
  (`fuzzyMatchPositions`, blanks in the query ignored), ties by name; no query: by name. A command whose `enabled()` is
  false is dim and Enter reports it. Accept closes the popup first, then runs the command through `runCommandId`, so
  popups the command opens replace the palette cleanly and the key context is the one of the focused widget.
