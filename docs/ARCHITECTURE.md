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
* Not yet logging: `DebugSession`/`BuildRunner`/`FastIndex` have no log callback of their own; the editor-level events
  around them (build start/finish) are logged, subsystem-internal ones are a follow-up.

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
## Version control (C3a, `src/vcs`)

Provider-neutral engine plus the Git provider; Perforce (C3b) and the UI (C3c) plug into it. Nothing in `src/vcs`
includes a UI header; it depends on `core` and `lsp/Process.h` (and on `app/Editor.h` in `VcsManager.cpp`, for
`post()`).

```
 Editor ── owns ── VcsManager ──(jobs)──▶ worker thread ── owns ── VcsProvider (GitProvider, later Perforce)
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
* **Commands** (`VcsUi`; registered by `registerEditorCommands`, bound by `EditorApp`, deliberately **unbound** in
  `keybindings.default.txt`, which lists commented examples): `Vcs.NextChange` / `Vcs.PrevChange` (jump to the next /
  previous hunk through `Editor::gotoLocation`, wrapping with a message), `Vcs.ToggleInlineDiff`, `Vcs.RevertHunk`,
  `Vcs.RevertFile` (a modal two-row `ListPopup`, "Discard all changes" / "Cancel", default on the first row, then
  `VcsManager::revertFile`), `Vcs.DiffFileExternal`, `Vcs.RefreshStatus`, `Vcs.CheckOut` (the explicit form of
  Perforce's auto-checkout: `VcsManager::checkOut`, a small additive engine call that runs the provider's
  `beforeFirstEdit` now and marks the open document as checked out so its first edit does not repeat it).
* **External diff tool** (`DiffTool`): writes the committed text (document's line ending) and the buffer to
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

**Editing conveniences (B8, `src/app/EditorEditing.cpp`, `src/app/Bookmarks.*`).** Everything is an `Editor` method that
edits at every caret and forms ONE undo step; the behaviours that could differ from 10x live in `Editor::EditingOptions`
(`autoIndent`, `autoClosePairs`, `detectIndent`, `indentUnit`, `trimTrailingWhitespace`; the UI fills them from the
`editor.*` settings, OPEN_QUESTIONS #8). Two entry points exist for text: `insertText` stays raw (paste, completion,
tests) and `typeText` is what the view calls for typed characters (auto-close `( [ { " '`, typing over the closer
that is already there, wrapping a selection, `}` as the first non-blank character dedenting to the line of the `{` it
closes; everything else falls through to `insertText`, so the typing coalescer is unchanged: both go through
`typeEdits`). `insertNewline` copies the indentation, adds one unit after a line ending in `{ ( [` or a binary
operator (`+ - * / % & | ^ = ? <<`, not `++ -- */`), returns one unit when a continued statement ends with `;`, splits
`{|}` into three lines and trims the blanks around the break. `indentUnitFor(doc)` detects tabs vs. the most common
indentation step of the first 400 lines. Line commands (`indentLines/outdentLines`, `toggleLineComment`, `moveLines`,
`joinLines`, `deleteLines`, `trim on save`, `replaceAll`) compute a sorted batch and go through
`Editor::runLineEdits` -> `Document::replaceMany` (A5b's single-pass path), carrying carets with `mapThrough`
(positions inside a replaced range go to its start; at a pure insertion the start of a selection stays before it);
`toggleBlockComment`, `duplicate` and the typing commands are per-caret edits through `applyCaretEdits`, which now
accepts per-edit caret/anchor offsets inside the inserted text (`CaretEdit::headOffset/anchorOffset`). A selection that
ends at column 0 does not touch that last line. `findInDocument` / `replaceAll` run TextSearch's matcher on the buffer
text (a synthetic one-file `SearchInput` with an override), so literal / whole-word / PCRE2 semantics are identical to
find in files; regex group references in the replacement are not expanded.

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
`fromSolution` skips a project the solution does not build in the active configuration (no `.Build.0`, or no mapping
at all) and evaluates the others with their mapped configuration/platform. A solution without those sections uses the
request as is. The active configuration is `Workspace::Settings::vsConfiguration` / `vsPlatform` (the existing
`.tenfold/workspace.json` keys, shared with the build): an exact match in the solution, else `Debug|x64`, else the
first listed configuration (`pickSolutionConfiguration`; the engine logs the fallback). API: `Workspace::
vsConfigurations()`, `activeVsConfiguration()`, `setVsConfiguration("Debug|x64")` (validates, saves);
`Editor::selectVsConfiguration(name, &err, applied)` additionally regenerates `.tenfold/compile_commands.json` on
the engine worker and restarts clangd (even though the database stays at the same path), calling `applied` on the
pump thread; CLI: `tenfold-cli vsconfig <root> [--set Debug|x64]`. The generated database is stamped
(`.tenfold/compile_commands.stamp`: source and configuration), so a hand-edited `workspace.json` is not served from a
stale cache. Edits to `.sln`, `.vcxproj`, `.props`, `.targets` and `Directory.Build.*` trigger a refresh like
`compile_commands.json` does.

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
workspace, MRU cycling, clipboard, `Workbench.*`, `Vcs.*`) are registered without a handler and report `Unbound` until the
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
values that are recalled rather than sampled. Control-flow keyword and string-escape colours need lexer kinds that do
not exist yet; they have no key. The debugger keys fall back to built-in colours (breakpoint #E51400, the dirty-marker
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
* **GoToLine** (`Editor.GotoLine`, Ctrl+G): a one-row modal ListPopup; `42` or `42:7`, Enter -> `Editor::gotoLine`
  (`gotoLocation`, so it is in the navigation history); numbers past the end clamp to the last line.
* **BookmarkList** (`Editor.ListBookmarks`, Ctrl+Shift+F2): modal list of every bookmark (line text, `path:line`),
  the input filters, Enter -> `gotoBookmarkItem`. Toggle / next / previous (Ctrl+F2, F2, Shift+F2) are engine commands.
  TextView draws a small accent-coloured flag at the left edge of a bookmarked line's gutter (right of the breakpoint
  glyph lane when there is one, see "Debugger UI").
* **Settings** (`assets/settings.default.txt`): `editor.indentUnit` (`tab` or a number of spaces, default 4),
  `editor.detectIndent` (true), `editor.autoIndent` (true), `editor.autoClosePairs` (true),
  `editor.trimTrailingWhitespace` (false). `editor.wordWrap` is still only reserved: soft wrap is not implemented.
* Plain Tab inserts the indent unit (it used to insert a tab character); Shift+Tab outdents; a selection spanning several
  lines indents them. Encoding is UTF-8 only (the status bar shows `UTF-8` and the line ending; `Editor.LineEndingLF` /
  `Editor.LineEndingCRLF` switch the line ending written on save). TODO: other encodings need detection and conversion
  that must not be invented without a decision.

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
