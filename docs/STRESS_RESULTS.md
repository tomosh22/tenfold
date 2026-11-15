# Stress test results

Measurements and findings of the corpus, stress and fuzz work (waves 9 to 12). Each agent adds its own self-contained
section; the sections are **historical records** (numbers measured when the work was done, on the shared machine named in
each), and a table's "not fixed" means "not fixed by that work". Format and rules of the corpora: `docs/TESTING.md`.
Order: W9i (editor-level stress) first, then the fast-tier work newest first (FAST-OVL, FAST-DECL, SLN-FILES,
W12-FAST-PP-STRICT, W12-FAST-PP, W11-FAST-SCOPE, W9-FIX-C1), then the W9 sections.

**Where things stand now** (the latest measurement of each; details in the sections named):

* Fast/final agreement over the 36 corpora plus `vs-slnx`, with both FAST-DECL and FAST-OVL merged: **2625/2848 =
  92.17 %**, 0 `FAIL`, 0 `XPASS` (`tenfold-cli corpus --json`, Release, clangd 22.1.1, measured on the merge). The 67 lines
  more than the 2,781 before are the `fast` lines the two tasks added (FAST-DECL 13, FAST-OVL 54). Before both:
  2329/2781 = 83.75 % (after SLN-FILES; 2327/2779 before that). Each task alone: FAST-DECL 2439/2794 = 87.29 %, FAST-OVL
  2515/2835 = 88.71 % (sections below).
* Earlier, the 36 corpora alone: 2236/2688 = 83.2 % (W12-FAST-PP-STRICT; 49.2 % before
  W11-FAST-SCOPE, 74.9 % after it, 83.2 % after W12-FAST-PP). Every `final` expectation passes; no `FAIL` or `XPASS`.
* Very long lines: the keystroke-to-paint budget (8 ms) holds for every pathological file, wrap on and off, average and
  p99 (W11-PERF-LINES, in "Pathological workspaces (W9f)", bugs 8 and 9; the rows that were over budget are fixed).
* Editor-level stress on 20k-file projects (W9i): keystrokes are inside 8 ms at p50 in every scenario (10k carets, 300 tabs,
  folding a 120k-line file, a flooding terminal, typing while clangd indexes); seven findings fixed (clangd's background
  index never started without an open document, one whole-file re-index per caret, a crash and a 47 s UI block on a busy
  clangd, a 2.7 s freeze on a configuration switch, 300-tab frames, memory kept after closing a workspace); open follow-ups:
  replace in files blocks the UI for 20 s, find results freeze the list for 0.1 s, opening / closing a workspace blocks
  the UI thread for about a second ("W9i", unfixed items A to G).
* Still open: fast tier explicit and partial specialisations, a function's address taken (`&f`), and the alias-chain /
  using-declaration / constructor answers that the chooser keeps for clangd (the FAST-OVL and FAST-DECL sections;
  W11-FAST-SCOPE, "Where the rest is"); the W9i follow-ups ("W9i", unfixed items A to G); the design-change items 3 to 9 under "Not fixed" in the
  pathological section; the clangd answer gaps recorded in the W9b to W9e finding tables.

## W9i: editor-level stress on the largest generated projects (branch `task/W9I`)

Scripted editor sessions on a generated 20k-file project, through the real `AppHost` / `Workbench` / `EditorApp` with a
mock font and a null renderer (CPU cost of a frame, not of the GPU; soft wrap on), measured against the 8 ms
keystroke-to-paint budget (`docs/BENCHMARKS.md`) and memory. The harness, how to run it and the scenario list are in
`docs/TESTING.md` ("Editor stress (W9i)"): `tests/ui/test_stress_ui.cpp` (7 `stress_smoke_*` tests that always run on a
tiny tree, 9 `stress_ui_*` tests that need `TENFOLD_STRESS_DIR` / `TENFOLD_STRESS_VS_DIR`) and
`tools/stress/run_editor_stress.sh`.

**Method.** `gen_codebase.py --profile game|service --size large --seed 1 --variant cmake` (20,380 / 20,437 files, 6,981 /
6,753 translation units, 0.96 M / 0.77 M lines) for every scenario except the configuration switch, which uses
`--profile service --size medium --variant vs` (3,066 files, `.sln` with Debug|x64 and Release|x64). Release build
(gcc 13.3, `-O2`), clangd 22.1.1 (the pip launcher: the figures are those of the real `clangd` binary below it), real
clangd with its background index, a scratch copy of the generated data that is deleted afterwards (nothing is committed).
Every action is timed from the event to the end of the frame (`pump` + `tick` + `render`). Peak RSS of the editor is
`VmHWM` (reset per scenario; each test in its own process so the earlier scenarios do not count), of clangd a 100 ms
sampler over `/proc`. **Machine: 4 cores shared with three other agents building and running tests; the load average was
11 to 22 during every run (shown per scenario), so every latency is inflated by contention, p99 and max most of all.
Compare between rows of one run, treat a budget miss as real only when it repeats or has a cause named below.**

### Results

Game profile, 20,380 files (before fixes 5 and 6 below; the other scenarios are unaffected by them). Latencies in ms
(n actions; p50 / p99 / max):

| scenario | action | n | p50 | p99 | max |
|---|---|---:|---:|---:|---:|
| 300 tabs | open a file into a new tab | 300 | 93 | 301 | 505 |
| | switch to the next tab (Ctrl+Tab, Ctrl+PageDown) | 600 | 11.8 | 28 | 34 |
| | type a character with 300 tabs open | 100 | 1.9 | 12.0 | 12.0 |
| | close a tab | 300 | 1.0 | 22 | 29 |
| find / replace in files | find "Update" (7,346 matches) to all results | 1 | 451 | | |
| | frame while the results stream in | 8 | 53 | 125 | 125 |
| | next result | 40 | 0.3 | 0.5 | 0.5 |
| | **apply: 7,346 replacements, 60 open documents, 5,322 files rewritten** | 1 | **21,961** | | |
| | undo in an open document | 60 | 0.4 | 4.7 | 4.7 |
| 10k carets (120k-line file) | type a character | 40 | 5.2 | 67 | 67 |
| | Backspace / move / undo | 40 / 20 / 1 | 3.9 / 0.8 / 3.9 | 4.6 / 1.0 | |
| fold 120k lines | CollapseAll (first) / ExpandAll | 1 | 83 / 1.0 | | |
| | PageDown / PageUp, all collapsed | 80 | 0.16 | 4.3 | 4.3 |
| | toggle one region | 30 | 0.15 | 4.2 | 4.2 |
| | type, all collapsed / all expanded | 100 | 1.2 / 4.0 | 7.2 / 13.2 | |
| terminal flood (`yes`, 30 s) + indexing | type a character next to the terminal | 1,558 | 5.1 | 16.7 | 117 |
| | every frame (terminal painting) | 1,563 | 4.7 | 12.9 | 36 |
| type while the index builds (600 keys, then 240 s more) | type a character, clangd indexing | 14,157 | 0.69 | 5.2 | 29 |
| open / close the workspace x 6 | File > Open (scan + fast index, UI thread) | 6 | 1,628 | 1,746 | 1,746 |
| | Close Solution (UI thread) | 6 | 603 | 1,880 | 1,880 |
| configuration switch x 4 (service vs medium) | Enter in the picker | 4 | 6.4 | 14 | 14 |
| | type while the configuration reloads (97 keys) | 97 | 0.42 | 2,695 | 2,695 |
| open / close a vs solution x 6 | File > Open / Close Solution | 6 | 514 / 102 | 609 / 921 | |

Service profile, 20,437 files, after fixes 1 to 5 (fix 6, the tab lookup, is in the next table):

| scenario | action | n | p50 | p99 | max |
|---|---|---:|---:|---:|---:|
| 300 tabs | open / switch / type / close | 300 / 600 / 100 / 300 | 63 / 8.9 / 3.1 / 1.1 | 323 / 167 / 24 / 16 | 409 / 234 / 24 / 22 |
| find / replace | find "Update" (7,970 matches) to all results | 1 | 354 | | |
| | frame while the results stream in | 9 | 5.6 | 139 | 139 |
| | **apply: 7,970 replacements, 5,433 files rewritten** | 1 | **23,949** | | |
| 10k carets | type / Backspace / move | 40 / 40 / 20 | 7.9 / 7.2 / 0.8 | 31 / 9.5 / 6.0 | |
| fold 120k lines | type, all collapsed / expanded | 100 | 4.0 / 4.0 | 16 / 13 | |
| terminal flood + indexing | type a character | 1,794 | 3.8 | 13 | 107 |
| type while the index builds | type a character (14,622 keys over 240 s) | 14,622 | 0.63 | 5.0 | 84 |
| open / close the workspace x 6 | File > Open / Close Solution | 6 | 1,111 / 530 | 1,235 / 641 | |
| configuration switch x 4 | type while the configuration reloads (67 keys) | 67 | 0.39 | 998 | 998 |
| open / close a vs solution x 6 | File > Open / Close Solution | 6 | 377 / 77 | 450 / 311 | |

Game profile again after fixes 5 to 7 (same build as the service table, each test in its own process, load 4 to 15; ms):

| scenario | action | p50 | p99 | max |
|---|---|---:|---:|---:|
| 300 tabs | open a file into a new tab | 7.9 (was 93) | 208 | 457 |
| | switch to the next tab | 0.55 (was 11.8) | 102 | 180 |
| | type with 300 tabs open | 0.39 (was 1.9) | 15.9 | 15.9 |
| | close a tab | 0.19 (was 1.0) | 0.45 | 12 |
| fold 120k lines | type, all collapsed / expanded | 4.0 / 4.0 | 6.5 / 8.0 | |
| terminal flood | type a character (1,669 keys) | 4.9 | 12.4 | 292 |
| configuration switch | type while the configuration reloads | 0.43 | 1,266 | 1,266 |
| replace in files | apply (same 7,346 matches, 5,322 files) | 19,939 | | |
| 10k carets | type a character, five runs at load 5 to 19 | 4.7, 4.7, 5.2, 7.9, 7.9 | 11, 21, 31, 67 | |

One 10k-caret run (the first test after a 100 MiB copy, with another agent's sanitizer suite starting) measured 164 ms p50 for
the same keystroke; the next four runs did not repeat it (select-all 186 ms against 6 to 31 ms, undo 210 ms against 4 to 7 ms
in the same run: a machine hiccup, not the code).

Tab switching (service, `TENFOLD_STRESS_TABS`; same load class), p50 of one Ctrl+Tab or Ctrl+PageDown including its
frame: 30 tabs 0.39 ms, 300 tabs 7.65 ms before fix 6; 30 tabs 0.14 ms, 300 tabs 0.47 ms after it.

Memory (editor process peak RSS, clangd peak RSS), MiB:

| scenario | game editor / clangd | service editor / clangd |
|---|---|---|
| 300 tabs | 198 / 1,034 | 166 / 974 |
| find / replace in files | 231 / - | 172 / - |
| 10k carets | 364 / - (a process that had run other scenarios: see below) | 233 / - |
| fold 120k lines | 423 / - | 179 / - |
| terminal flood + indexing | 386 / 452 | 176 / 287 |
| type while the index builds (4 min) | 391 / 871 | 160 / 637 |
| open / close x 6 | 491 / 388 | 323 / 338 |
| configuration switch, vs medium | 387 / 134 | 67 / 207 (two clangd for a moment while the old one stops) |

The game figures above came from one process that ran all nine tests in turn (RSS is not returned to the system between
scenarios, so they include the earlier ones); the service figures are one process per test. **Budgets (§1.7): the editor
stays below 500 MiB in every scenario (the game "open / close" peak of 491 MiB is the closest: two workspaces' fast
indexes overlap while one is replaced by the next), 160 to 230 MiB for the editing scenarios.** clangd with the background
index of a 20k-file project reaches 0.9 to 1.0 GiB (not budgeted). No scenario leaks threads (2 threads after every
close, 5 to 10 while a workspace is open) or clangd processes (none survives the close of its workspace; checked after
every cycle).

### Findings

Fixed in this branch (each with a regression test; "before" figures are from the first runs on the same project):

| # | symptom | root cause | fix | test |
|---|---|---|---|---|
| 1 | `tenfold-cli bench <ws> --wait-index 600000` on the 20k-file game project waited the whole 10 minutes with the machine idle: clangd never indexed ("clangdIndexPercent" never reached 100) | clangd loads the compile database, and starts its background index, only when the first document is opened; `bench` without `--file` (and the editor started with no tab) opens none, so clangd sat at 11 MiB and 0 % CPU and never sent `$/progress`. See "The clangd index investigation" below | `SemanticEngine::kickBackgroundIndex`: when clangd starts (or restarts) with no document open the engine sends `didOpen` + `didClose` of the first existing file of the compile database (its diagnostics are dropped); logs a line when no file of the database exists | `semantic_clangd_background_index_starts_without_an_open_document` (real clangd; fails without the fix: the wait times out) |
| 2 | typing with many carets: one keystroke with 3,000 carets in a 6,000-line file did not finish in 4 minutes (killed); 10k carets in a 120k-line file 12 ms p50 after the first half of the fix, 26 ms p99 | `Document::applyBatch` sends one change event per caret (last edit first) and every listener did its whole work for each: `SemanticEngine::didChange` re-lexed the whole file into the fast index, rebuilt the line map, signalled the token thread and sent one LSP message, `Editor` ran three position trackers and the version control hook. N carets = N whole-file re-lexes per keystroke | `Document::Change::pending` (events still to come in the batch); the engine decides at the first event, collects the incremental changes into one `didChange` and re-indexes / maps / schedules tokens once, at the last event; the trackers skip an edit that stays on one line; the version-control hook runs once | `multicaret_batch_events_count_down_their_pending_events`, `multicaret_typing_reindexes_the_fast_index_once_per_keystroke`, `stress_smoke_many_carets_typing` (3,000 carets: 0.5 s for the whole scenario) |
| 3 | **crash** (`std::system_error: Invalid argument`, `terminate`) when the editor closed while clangd was busy, reproduced every time with 60 tabs open and clangd indexing | `LspClient::stop()` sent `shutdown` with a handler that captured a mutex, condition variable and flag **on the stack**, waited 2 s and returned; clangd busy indexing had not answered, the handler stayed pending and the connection ran it (while failing the pending requests) on a destroyed mutex | the wait state is a `shared_ptr` shared with the handler | `stop_with_a_server_that_never_answers_shutdown_is_safe_and_bounded` (fake server that never answers `shutdown`; ASan reports the old code as stack-use-after-scope) |
| 4 | **the UI thread blocked for 33 to 47 s** closing a tab (3 of 3 runs of 300 tabs while clangd indexed; the harness's "slow step" lines) | `JsonRpcConnection::send` wrote to clangd's stdin pipe synchronously from the thread that edits (the UI thread) and from the reader thread (answers to the server's requests). A clangd that does not read for a while (a busy main thread while it indexes 7k files) fills the 64 KiB pipe; `didClose` sat in `write()` (gdb: `Process::write` under `LspClient::didClose` under `Workbench::closeTab`) until clangd read again. It could also deadlock the reader thread against clangd's own output | `JsonRpcConnection` queues outgoing frames and a writer thread writes them (order kept; `stop()` drains the queue, then closes the pipe, or terminates the server when its stdin is stuck; a warning at 64 MiB queued) | `jsonrpc_send_does_not_block_on_a_server_that_does_not_read` (12 MiB to a process that never reads: 0.55 s), `jsonrpc_queued_messages_reach_a_reading_server_in_order_before_stop_closes_the_pipe` |
| 5 | switching the Visual Studio configuration froze the window for 2.7 s while clangd was indexing (one keystroke took 2,695 ms of a 97-key run) | `SemanticEngine::restartClangd` (called from the posted continuation of the configuration refresh, on the UI thread) stopped the old server in place: up to 2 s for its `shutdown` answer plus up to 1.5 s for it to exit | the old server is stopped on a thread of its own (`retireClient`; the engine's `stop()` joins them); the new server starts at once | `manual_restart_does_not_wait_for_the_old_server_to_shut_down`; the same scenario after the fix: 998 ms worst key (the remaining second is the new server's `initialize` handshake, see below) |
| 6 | 300 tabs: switching tabs 7.65 ms p50 (0.39 ms with 30 tabs), 12 ms with 300 tabs on the game project, over the frame budget, and the same cost on every idle frame | `Workbench::sync` (every frame) asks `Editor::document(path)` for each tab, and each lookup canonicalised the path with `weakly_canonical` (stat / readlink chains: gdb samples show `fstatat64` / `realpath` under `Editor::document` under `Workbench::sync`). 300 tabs = about 300 x 25 us per frame | the key of a spelling is remembered for 2 s (`docKey`) | `editor_document_lookup_by_path_is_cheap_and_still_resolves_every_spelling`; after: 0.47 ms p50 with 300 tabs |
| 7 | closing the workspace did not give the memory back: RSS after each of 14 open / close cycles of the service project 164, 231, 276, 285, 308, 340, 346, 351, 351, 355, 377, 382, 441, 403 MiB in one run, 169 to 198 (flat) in the next; game 386 to 467 | allocator retention, not a leak: the same process after `malloc_trim(0)` was at 18 to 50 MiB. The fast index and the documents of the closed workspace are freed, but glibc keeps the pages in the arenas of the indexing threads | `AppHost::teardown` calls `malloc_trim(0)` (glibc) after the workspace is gone; after: 19, 19, 31, 36, 35, 27, 26, 29 MiB over 8 cycles | the growth check of `scenarioOpenClose` (RSS after the last close below 1.5 x the second + 64 MiB, thread count flat, no clangd left) in `stress_ui_open_close_*`; `stress_smoke_open_close_workspace_cycles` runs the same bookkeeping on a small tree |

Not fixed (each is larger than a contained change or needs a design decision; root cause, evidence and proposed fix so that
follow-ups can be scheduled):

| # | symptom and evidence | root cause | proposed fix |
|---|---|---|---|
| A | **replace in files blocks the UI thread for 22 to 24 s**: 7,346 / 7,970 replacements in 5,322 / 5,433 closed files plus 60 open documents (game / service, load 20; the frame after Enter took that long) | `ReplaceInFiles::apply` calls `Editor::replaceInFiles` synchronously; each closed file is read, decoded, edited and rewritten atomically one after the other (about 4 ms a file here, mostly the temp file + rename) | run the closed-file rewrites on the engine's worker with a progress message in the status bar and one result callback (the open documents stay on the UI thread: they are one undo group each); write 4 files at a time. The result must still report `stale` / `refused` (`Editor::ReplaceInFilesResult`) |
| B | find in files over 20k files: 7.3k to 8k matches in 0.35 to 0.45 s, but **the frames while the results stream in took 53 ms p50 / 125 ms max (game, 8 frames) and 5.6 / 139 ms (service, 9 frames)**: the list freezes for about 0.1 s, far over the 8 ms frame budget | `FindInFiles::onBatch` marks the list dirty and `rebuild()` re-sorts and rebuilds every popup row from scratch (`rebuildMs_` spaces them but each one is O(all matches)) | append the rows of a batch (matches arrive sorted per file) instead of rebuilding, and cap a rebuild at the visible rows (the popup is a virtual list of 22 rows) |
| C | opening a workspace blocks the UI thread: 1.1 to 1.6 s at 20k files, 0.4 to 0.5 s at 3k (File > Open of the same project in the same window, 6 times); closing it 0.5 to 1.9 s at 20k files (`Close Solution`) | `Editor::start()` runs the workspace scan, the compile database and the fast index synchronously (the "interactive in 3 s" budget of §1.7 holds, 1.0 to 1.6 s); closing stops clangd synchronously (the `shutdown` wait is the 0.5 to 1.9 s) | open: move the scan / fast index behind the first frame (the status bar already shows indexing); close: `AppHost::teardown` can hand clangd to the retire thread of fix 5 as long as the process is reaped before the next open (the leak check of the harness asserts that) |
| D | the configuration switch still froze the UI for 1.0 s (worst key of 67; load 14) after fix 5 | `restartClangd` starts the new clangd synchronously: `LspClient::start` waits for the `initialize` answer (about 0.3 s unloaded) and `reopenDocuments` sends the open documents | start the new server on the retire/worker thread and swap it in with `adoptPending` (the crash-recovery path already does exactly that, `adoptPending`) |
| E | undoing a very long typing run takes 1.9 to 2.2 s (14,000 keystrokes typed at 15 ms intervals are one undo group; the "slow step" at the end of "type while indexing") | `Document::undo` applies the group's batches one by one and every batch is its own event batch: one whole-file re-lex and one `didChange` per keystroke of the run (the same shape as fix 2, across batches instead of across carets) | give `Document::Change::pending` a group scope in `undo` / `redo` (count the events of all batches of the group), so the engine re-indexes once |
| F | 120k-line file: typing p99 13 to 16 ms (p50 4 ms with everything expanded, 1.2 to 4.0 ms collapsed) at load 19 | not isolated under that load; samples show the lazy fast re-index of a file over 1 MiB (`kLazyReindexBytes`) running on the UI thread 300 ms after the last key (`Editor::pump` -> `FastIndex::updateFile` -> the lexer) and `FastIndex::search` (thread create + join) under `SemanticEngine::completion` on the keystroke path | repeat on a quiet machine; move the lazy re-index of big files to a worker (the lexer works on a copy of the text); the completion request belongs to the completion popup work (`CompletionPopup*`, not touched here) |
| G | opening a file into a tab p50 63 to 93 ms at 300 tabs (30 to 39 ms at 30 tabs, 14 files of the 300 over 200 ms) | not isolated: file read + decode, `didOpen`, the tab strip layout and the first paint of a 40 KB file under load 15 to 20 | measure on a quiet machine; the p50 does not grow with the tab count after fix 6 |

### The clangd index investigation ("`--wait-index 600000` waited 10 minutes at load 0")

Project: `gen_codebase.py <dir> --profile game --size large --seed 1 --variant cmake --jobs 4` (20,380 files, 6,981 translation
units). What was checked, in order:

1. **Is clangd started with `--background-index`?** Yes: `SemanticEngine::launchClangd` passes `--background-index`
   (and the generated `config.yaml` has `Index: Background: Build`), `--compile-commands-dir=<dir of the database>` and
   `-j=max(2, cores)`. The launcher on this machine (`/usr/local/bin/clangd`) is the pip package's Python script; the
   real binary is `.../dist-packages/clangd/data/bin/clangd` (the first thing to know when reading `ps`).
2. **Does tenfold see the progress?** Yes: `LspClient` advertises `window.workDoneProgress`, answers
   `window/workDoneProgress/create`, and turns the `backgroundIndexProgress` notifications `begin` / `report` / `end` into
   `IndexProgress{active, percentage}`; `waitForClangdIndex` returns once an `end` arrived (percentage 100, not active). A
   raw LSP session (a 60-line Python client, not committed) shows clangd sending `begin`, then `report` with growing
   percentages (5 %, 13 %, 18 %, 25 % within 20 s on this machine) as soon as one file is opened.
3. **Does the index run on such a project?** Yes, once a file is opened. `tenfold-cli bench <ws> --file app/app_0.cpp:1:1
   --wait-index 280000` (load 20 to 26 from other jobs): clangd RSS 0.75 GiB after 90 s, `clangdIndexPercent` 100 after
   275 s (`clangdIndexWaitMs` 275,100), `defFirstMs` 1.4.
4. **Without a file it never starts.** The same run without `--file`: clangd at 11 MiB RSS and 0 % CPU for as long as it was
   watched (70 s, then 10 minutes in the original report); no `$/progress` at all, so `waitForClangdIndex` never saw an
   `end` and ran into its timeout. A raw LSP session confirms it and shows what does start it: `didChangeWatchedFiles` for
   `compile_commands.json` does nothing; `didOpen` of any file does (and `didOpen` + `didClose` is enough). The reason is
   clangd's: it reads the compile database lazily, for the first file it is asked about, and the background indexer is
   fed from that load.
5. **The generated `compile_commands.json` is not usable as it stands.** The `cmake` variant writes `@CORPUS_ROOT@` into
   `directory`, `file` and every `-I`; only `tenfold-cli corpus` (and this harness) replaces it with the real path. With the
   raw directory every command names a directory that does not exist; clangd cannot find any file: with a file open its
   progress `begin` / `end` come within seconds having indexed nothing (RSS 64 MiB), so the symptom there is a fast, empty index. (This is data hygiene, not a product bug; the engine now logs it, see below.)

**Verdict: a tenfold bug (item 1 of the findings), plus a test-data pitfall.** tenfold passed the right flags and
tracked progress correctly; what was missing is anything that makes clangd load the database in a workspace with no
open document: the editor opened empty, a workspace restored without tabs, and `tenfold-cli bench` / `wait-index` without
`--file` all left the background index unstarted for ever, the symptom being a status bar without "indexing" and
`--wait-index` time-outs. Fixed by `kickBackgroundIndex`. After the fix, the same command without `--file`
(`tenfold-cli bench <ws copy with the placeholder replaced> --wait-index 100000`) shows clangd indexing (92 % after
100 s on the loaded machine; `clangdIndexPercent` 92). When none of the first 64 files of the database exists (the
placeholder case) the engine logs `clangd: none of the first 64 files of the compile database exists (first: ...):
clangd's background index cannot start` instead of waiting silently.

**How long the index really takes.** 20k files, 7k translation units, `-j4`, load 12 to 26: 275 s (game, with `--file`), over
240 s without finishing in two "type while indexing" runs (game: 14,157 keystrokes; service: 14,622 keystrokes, index not done
after 4 min), clangd RSS 0.87 to 1.03 GiB. W9g measured the same project at more than 120 s on a quieter machine (G-5): a first
index of a 20k-file project takes minutes on four cores, and `--wait-index` needs a timeout of that order, plus a file.

Other observations (no change made):

* **Budget status.** At p50 every keystroke scenario is inside 8 ms (10k carets 4.7 to 7.9, fold 1.2 to 4.0, terminal
  flood 3.8 to 5.1, tabs 0.4 to 3.1, typing while indexing 0.45 to 0.7). At p99 the ones inside 8 ms in the loaded runs are
  typing while clangd indexes (5.0 to 6.5 ms, 29,000 keystrokes) and typing in the collapsed 120k-line file (6.5 ms); the others
  were over (10k carets 11 to 67, terminal flood 12 to 17, 300 tabs 12 to 24, fold expanded 8 to 16) on a machine at load
  11 to 22 and 4 cores: those are p99s of 40 to 1,700 keystrokes under contention and the p50s sit at or below the budget, so
  they are reported, not claimed as code misses. The ones with a cause named above are the fixed ones (fix 2, 6) and rows
  A, B, C, D, E of the table of unfixed items (the actions that took 0.1 s to 24 s: none of them is a keystroke).
* **The terminal flood has a recurring hitch**: the worst frame of the 30 s flood was 117, 107 and 292 ms in three runs
  (p99 12 to 17 ms), at load 5 to 20. Not isolated (candidates: the scrollback growing, or a burst of engine results in
  `Editor::pump` while indexing); the flood alone (`term_perf_50mb_burst_keeps_frames_short`) holds 100 ms.
* **Typing calls `FastIndex::search` on the UI thread**: gdb samples of the 10k-caret run show `SemanticEngine::completion`
  -> `FastIndex::search` creating and joining its scan threads on every keystroke. That is the completion popup's
  approximate tier (`complperf` area: `CompletionPopup*`); not touched here.
* **The fast index finished before the first keystroke** in every "type while the index builds" run: `Editor::start`
  returns when the lexer index is built (1.0 to 1.6 s at 20k files, on the UI thread, row C), so the only background
  indexing left to type against is clangd's, which is what the 14,000-keystroke series covers (p99 5.0 to 5.2 ms, max 29 to 84 ms).
* clangd's background index of a 20k-file project did not finish within 4 minutes of typing in either run (load 14 to 22);
  its RSS was 0.87 and 0.64 GiB then. The harness records `clangd index finished = 0` rather than waiting longer.

### Sanitizer and release runs of this branch

Release (gcc 13.3): `tenfold-tests` 795 tests, `tenfold-ui-tests` 776 tests under `xvfb-run -a`, all pass. Debug +
AddressSanitizer + UBSan (gcc; `-fsanitize=address,undefined`, leak detection off): the same two suites, 795 and 776 tests, no
sanitizer report; the stress smoke tests (7) and the new engine tests run in both. A suite started with `nohup` from a
non-interactive shell has SIGINT ignored in every child, which makes the two `term_ctrl_c_*` tests (a `trap` waiting for
Ctrl+C in the terminal's shell) fail with a time-out; they pass when SIGINT has its default action (`xvfb-run` from a normal
shell, or `signal.signal(SIGINT, SIG_DFL)` before `exec`): an environment property, not a W9i change.

### Reproducing

```sh
tools/stress/run_editor_stress.sh game large      # about 30 minutes on 4 cores; service: `... service large`
UI_TESTS=build-ui/tenfold-ui-tests TENFOLD_STRESS_DIR=<generated cmake dir> TENFOLD_STRESS_VS_DIR=<generated vs dir> \
  TENFOLD_STRESS_NOCOPY=1 xvfb-run -a build-ui/tenfold-ui-tests stress_ui_tabs
```

`TENFOLD_ENFORCE_PERF=1` turns the 8 ms p99 checks into failures; do that on a quiet machine only (the p99 of 40 keystrokes
is its slowest key).

## FAST-OVL: overloads by argument type and operator tokens in the fast tier (branch `task/FAST-OVL`)

User request: "the fast-index gaps need closing". This work closes two of the gap classes recorded under W11-FAST-SCOPE
and W12-FAST-PP: (a) overloads of equal arity that differ in parameter types or in `const` / `&` / `&&` qualification,
(b) operator tokens (`+`, `==`, `<<`, `[]`, `()`, `->`, `++`, a unary `-`, user-defined literals). Design and rules:
`docs/ARCHITECTURE.md`, "Overloads and operators in the fast tier". Only the fast tier changed: clangd's answers and every
`final` expectation are untouched (all of them still pass). **No on-disk format changed** (the persisted index stays at
format 3 / salt 6): a function's parameter types are read from the declaration in the file's text when a call with
several candidates is asked about, not stored per symbol, so index build time, memory and the store are exactly as before.

**Method.** `tenfold-cli corpus tests/corpus/<name> --json` for each of the 37 corpora, Release, a 4-core box shared with
three other agents and their builds (load average 11 to 15), clangd 22.1.1, `--timeout 900` for `vs-configs` /
`vs-slnx` (300 s for the others); the JSON's `fastAgreement` (`def` and `decl` lines, `file:line` sets equal, `xfail`,
`-> none` and failing lines included). *Before* = the binary built from `main` at `75d7eec`. FAST-OVL promoted 14
expectation lines (`xfail` lines that now pass: `overloads` 4, `operators` 2, `lookup-adl` 2, `gen-service` 2, `templates-basic`,
`gen-mixed`, `lambdas-bindings`, `name-collisions` 1 each) and added 54 `fast def` lines to `overloads` (22), `operators` (29)
and `templates-basic` (3); each duplicates a `def` line of the same anchor and so counts as one more agreement query. So that
the two columns count the same lines, *before* is the `main` binary run over the final expectation files of those
corpora (2330/2835) and, in the sentence after the table, the new binary is also run over the original files.

| corpus | before | after |
|---|---|---|
| `c-basic` | 93/109 (85 %) | 93/109 (85 %) |
| `c-heuristic` | 11/18 (61 %) | 11/18 (61 %) |
| `c-mixed-cpp` | 36/39 (92 %) | 36/39 (92 %) |
| `c-vcxproj` | 15/18 (83 %) | 15/18 (83 %) |
| `encodings` | 123/123 (100 %) | 123/123 (100 %) |
| `gen-game` | 236/255 (93 %) | 236/255 (93 %) |
| `gen-mixed` | 228/243 (94 %) | 231/243 (95 %) |
| `gen-service` | 234/258 (91 %) | 239/258 (93 %) |
| `harness` | 7/8 (88 %) | 8/8 (100 %) |
| `lambdas-bindings` | 87/98 (89 %) | 92/98 (94 %) |
| `layout-dupnames` | 38/38 (100 %) | 38/38 (100 %) |
| `layout-exts` | 46/50 (92 %) | 46/50 (92 %) |
| `layout-unity` | 26/26 (100 %) | 26/26 (100 %) |
| `lookup-adl` | 35/59 (59 %) | 50/59 (85 %) |
| `lookup-namespaces` | 48/66 (73 %) | 48/66 (73 %) |
| `name-collisions` | 108/114 (95 %) | 110/114 (96 %) |
| `oop-ctors` | 108/136 (79 %) | 108/136 (79 %) |
| `oop-virtual` | 76/82 (93 %) | 76/82 (93 %) |
| `operators` | 9/87 (10 %) | 86/87 (99 %) |
| `overloads` | 27/98 (28 %) | 85/98 (87 %) |
| `paths-unicode` | 70/70 (100 %) | 70/70 (100 %) |
| `pp-includes` | 71/74 (96 %) | 71/74 (96 %) |
| `pp-macros-std` | 12/21 (57 %) | 12/21 (57 %) |
| `pp-macros` | 68/104 (65 %) | 68/104 (65 %) |
| `pp-xmacros` | 15/38 (39 %) | 15/38 (39 %) |
| `seed` | 16/24 (67 %) | 16/24 (67 %) |
| `symlinks` | 4/6 (67 %) | 4/6 (67 %) |
| `templates-basic` | 67/99 (68 %) | 84/99 (85 %) |
| `templates-config` | 11/21 (52 %) | 11/21 (52 %) |
| `templates-cxx20` | 41/58 (71 %) | 41/58 (71 %) |
| `templates-specialisation` | 39/70 (56 %) | 41/70 (59 %) |
| `unicode-separators` | 16/16 (100 %) | 16/16 (100 %) |
| `vs-configs` | 91/91 (100 %) | 91/91 (100 %) |
| `vs-nav` | 15/15 (100 %) | 15/15 (100 %) |
| `vs-props` | 96/96 (100 %) | 96/96 (100 %) |
| `vs-slnx` | 91/91 (100 %) | 91/91 (100 %) |
| `vs-std` | 16/16 (100 %) | 16/16 (100 %) |
| **all 37** | **2330/2835 (82.19 %)** | **2515/2835 (88.71 %)** |

Over the original 2,781 lines (the new binary on the original expectation files of the four corpora: `overloads` 63/76,
`operators` 57/58, `templates-basic` 81/96, `lookup-adl` 50/59): **2329/2781 (83.75 %) -> 2461/2781 (88.49 %)**, 132 of the
452 disagreeing lines closed by this work; 320 remain (by shape: 117 "fast is a superset of clangd's answer", 112 "fast has
no answer", 48 disjoint, 39 subset, 4 overlap; the per-corpus tables above).

**No fast answer lost.** For every one of the 2,835 lines: no line that agreed before disagrees now (0), and no line where
clangd's answer was inside the fast answer before is missing from it now (0), both checked by comparing the `file:line`
sets of the before and after reports (`fastAgreement.disagreements`). Two earlier versions of the work did lose lines
and were fixed before the final runs: a prototype and its definition (`template <class T> T Vec2<T>::length_sq() const`
against the in-class declaration) were taken for overloads and the "non-template beats template" tie-break dropped the
definition (the two are now compared only when their parameter types differ); and `T&&` in an out-of-line member of a class
template was read as a forwarding reference (it is bound to the class's `T`, which the receiver's `Stack<int>` supplies).
0 `FAIL`, 0 `XPASS` in the final runs; no expectation was weakened (the 14 promoted lines became normal `fast` lines).

**What closed, by cause.**

| cause | lines | corpora |
|---|---|---|
| literal / declared / constructed argument types (`conv(1)` / `conv(1.5)` / `conv("s")`, `pick(&x)`, `log("x")`) | about 40 | `overloads`, `gen-*`, `templates-basic` |
| cv / ref qualified members (`buf.who()` against `Buf().who()`, `cbuf.at(0)`, `put(std::move(b))`) | about 20 | `overloads`, `lambdas-bindings`, `name-collisions` |
| `push(const T&)` / `push(T&&)` of a class template through `Stack<int> s` | 10 | `templates-basic` |
| operator tokens (binary / unary / `[]` / `()` / `->` / `++` / rewritten comparisons / literal suffixes) | 48 | `operators`, `lookup-adl` |
| argument-dependent lookup (`draw(c)` with `shape::Circle c`) | 15 | `lookup-adl` |
| a definition keeps its prototype's default-argument count (`log("x")`) | 1 | `overloads` |

Still open in `overloads` (13 lines): a function's address taken (`&area` takes the target type's overload), a constructor
call (`Bag(3)`: kept class-only by design, W10 `refinable`), a using-declaration that brings base overloads into a derived
class (`b::f(1)`, `d.f(1)`: FAST-DECL's area); in `operators`: `decl` of a friend operator declared inside its class (the
index holds friend *definitions* only).

**Latency** (`SemanticEngine::approximateDefinitions`, `Want::Definition`, a small driver around the engine with the
clangd client off, Release, `.tenfold` removed before each run; the driver samples up to 25 call sites (identifiers followed
by `(`) in each of 250 random files of the generated `game` profile at `--size large`, 20,380 files, 253,387 symbols; 2,194
queries, 3 runs of each binary alternated on a loaded box, so read the ranges as noise-limited; the new binary answers 2,289
locations where the old one answered 2,373):

| | p50 | p90 | p99 | first query in a file (p50 / p90) |
|---|---|---|---|---|
| `game` large, before | 17 - 25 us | 239 - 323 us | 606 - 1,103 us | 154 - 214 us / 620 - 864 us |
| `game` large, after | 22 - 28 us | 250 - 320 us | 705 - 4,262 us | 181 - 211 us / 647 - 870 us |
| overload-heavy synthetic, before | 58 - 84 us | 66 - 104 us | 96 - 223 us | 166 - 263 us / 203 - 290 us |
| overload-heavy synthetic, after | 156 - 202 us | 203 - 341 us | 4.3 - 5.2 ms | 280 - 463 us / 437 us - 9.3 ms |

The overload-heavy case is the worst one: 82 overloads of one name with the same arity (`f(T0, int)` ... `f(T39*, int)`,
`f(int, int)`, `f(double, int)`, `f(const char*, int)`, `f(const std::string&, int)`), 1,456 sampled calls in 20 files with
class-typed arguments: the answer shrinks from 74 locations to about 6 per query and the median costs about 100 us more
(lexing the call, typing the arguments, scoring 82 candidates against them; the p99 is the first query of each file
reading and parsing the 82 declarations, which are then cached). A call with a unique name, or a function declared in a
header and defined in a source file (two candidates that are not overloads of each other), skips the new step with
one pass over the candidates, which is why the ordinary workload moves by a few microseconds only.

**Index build time, memory, store, symbol search** (same generated `game` large workspace, `tenfold-cli bench --no-clangd
--runs 5`, `.tenfold` removed before each run, 3 runs of each binary alternated; the box was loaded, 4 cores shared with
the three other agents): `fastIndexMs` 1.2 - 2.5 s before, 1.8 - 2.3 s after (the ranges overlap; the extractor, the
persisted store and `FastIndex` are not touched), peak RSS 160 - 170 MiB before and 160 - 165 MiB after, symbol search
(`Shape`, `area`) 3 - 20 ms before and 1 - 6 ms after (the budget is 50 ms). The per-file state added by the new code is
the text of the files a query with overloads has asked about (`SigCache`: at most 24 files and 6 MiB, least recently used
out) and the parsed declarations of the candidates; the scope maps (`ScopeScan`) keep two more bytes per variable and a few
flags.

**Tests.** `tests/test_fast_overload.cpp`, 8 tests: the shape of declared types (`const`, `*` depth, `&` / `&&`, builtin
words, `auto` from a literal / `new`), literal types by the standard's rules, parameter lists read from declarations
(qualifiers, defaults, `static`, templates, operators, wrong or past-the-end positions, garbage and a 100,000-parenthesis
input), and three engine-level tests that ask the real engine with clangd off: overload selections by literals, declared
types, fields, casts, `nullptr`, enumerators, receivers, `std::move`, templates and explicit template arguments (and
that missing evidence keeps the whole set; 46 questions), 26 operator-token questions (binary / unary / subscript / call by arity / increment /
`->` / `<<` / enumerations / literal suffixes / rewritten and reversed comparisons, and that builtin operators, comments,
strings and chained results get no answer), and argument-dependent lookup (4 questions). Fuzz target `overload` (the parser paths:
parameter lists, expressions, operator tokens, over any text: 96 sampled carets each; `tests/fuzz/regressions/overload`
with four inputs). The first 20-second run under ASan + UBSan found a heap-buffer-overflow (an operator after a postfix
expression the reader cannot start, `"x" [0] + 1`: the left operand's start was `npos`), fixed with its input kept; three
seeds of 40 s each after the fix: 10,070 executions, no crash, hang or slow input (the slowest 432 ms in a sanitizer build,
96 carets per input). Suites on the final tree: `tenfold-tests` 795 (787 before + 8) in Release and in a gcc ASan + UBSan
Debug build; `tenfold-ui-tests` 760 (`xvfb-run -a`, unchanged: 759 pass in a full run on the loaded box, the one timing-dependent test that fails, `vsvcs_prompt_on_edit_do_not_ask_again_row_stops_the_questions`, passes on its own; a run started from a background shell fails the two terminal Ctrl-C tests because the shell leaves SIGINT ignored in its children, so that run restored it) and `tenfold-text-tests` 30; `tenfold-corpus-tests` 3 (all corpora, `TENFOLD_CORPUS_TIMEOUT=300`: 5,381 `PASS` and 131 `XFAIL` lines, 0 `FAIL`, 0 `XPASS`).

## FAST-DECL: names declared through macros, X-macro lists, forward declarations, inline namespaces (branch `task/FAST-DECL`)

User request: "the fast-index gaps need closing". Scope of this task: (a) names declared through macros and macro arguments
(`pp-macros`, `pp-xmacros`, generated `macros_*.h`) and (b) alias chains, using-declarations, specialisations and forward
declarations for `decl`. Design and rules: `docs/ARCHITECTURE.md`, "Names declared through macros, forward declarations and
inline namespaces (FAST-DECL)". Only the fast tier changed; clangd's answers and every `final` expectation are untouched.

**What changed in the fast answer.** (a) Function-like and identifier-bodied object-like `#define`s and the macro calls at
declaration position are recorded; a call is expanded (arguments, `#` / `##` / `__VA_ARGS__` / `__VA_OPT__`, nested and
passed macros, `__LINE__`, bounded by depth 8 / 64 expansions / 4,096 tokens / a work budget per file, braces must
balance) and the declaration extractor runs over the result, so `DECLARE_CLASS(Gadget)` declares `Gadget` where the
argument is written, a name only the macro's body spells where that `#define` is, a pasted name at the call. X-macro lists
(`#define X(..)` directly followed by `#include "list.def"`, also inside an enumerator list) are read with the macros defined
before the `#include`; the list files are indexed as hidden entries. `decl` on a class answers its forward declaration,
and `inline namespace v1` is transparent for a qualifier. (b) *Not changed on purpose:* the alias chain (`using B3 = B2`),
using-declaration and constructor answers of go to definition: the fast tier answers the single `refinable` alias and the
front end holds it back until clangd has answered, so the chooser of the decision of 2026-11-06 is shown once, by the front
end; listing the chain from the fast tier would put a chooser on screen that clangd then replaces (about 15 `def` lines, in
`lookup-namespaces`, `overloads`, `oop-ctors`, stay different from the final answer by that decision). *Not done:* explicit
and partial specialisations (31 lines of `templates-specialisation`; the scope of a member does not carry the template
arguments).

**Method.** As W12-FAST-PP / SLN-FILES: `tenfold-cli corpus tests/corpus/<name> --json` for each of the 37 corpora, Release,
shared 4-core box with other builds running, clangd 22.1.1, `--timeout 900` for `vs-configs` / `vs-slnx`, 300 s for the
others; the JSON's `fastAgreement`. *Before* = the binary built from `main` at `75d7eec` (2329/2781), *after* = this branch's
binary on the final tree. The after column has 13 more lines than the before column because 13 `fast` lines were added to
`pp-macros` (10), `name-collisions` (2) and `oop-virtual` (1) (all 13 agree; on the unchanged expectation files the
agreement is **2426/2781 = 87.23 %**, measured with the binary before the last changes).

| | before (`75d7eec`) | after |
|---|---|---|
| 36 corpora + `vs-slnx` | **2329/2781 = 83.75 %** | **2439/2794 = 87.29 %** |

| corpus | before | after |
|---|---|---|
| `c-basic` | 93/109 (85 %) | 93/109 (85 %) |
| `c-heuristic` | 11/18 (61 %) | 11/18 (61 %) |
| `c-mixed-cpp` | 36/39 (92 %) | 36/39 (92 %) |
| `c-vcxproj` | 15/18 (83 %) | 15/18 (83 %) |
| `encodings` | 123/123 (100 %) | 123/123 (100 %) |
| `gen-game` | 236/255 (93 %) | 245/255 (96 %) |
| `gen-mixed` | 228/243 (94 %) | 237/243 (98 %) |
| `gen-service` | 234/258 (91 %) | 243/258 (94 %) |
| `harness` | 7/8 (88 %) | 7/8 (88 %) |
| `lambdas-bindings` | 87/98 (89 %) | 87/98 (89 %) |
| `layout-dupnames` | 38/38 (100 %) | 38/38 (100 %) |
| `layout-exts` | 46/50 (92 %) | 46/50 (92 %) |
| `layout-unity` | 26/26 (100 %) | 26/26 (100 %) |
| `lookup-adl` | 35/59 (59 %) | 36/59 (61 %) |
| `lookup-namespaces` | 48/66 (73 %) | 53/66 (80 %) |
| `name-collisions` | 108/114 (95 %) | 112/116 (97 %) |
| `oop-ctors` | 108/136 (79 %) | 108/136 (79 %) |
| `oop-virtual` | 76/82 (93 %) | 79/83 (95 %) |
| `operators` | 9/58 (16 %) | 9/58 (16 %) |
| `overloads` | 26/76 (34 %) | 26/76 (34 %) |
| `paths-unicode` | 70/70 (100 %) | 70/70 (100 %) |
| `pp-includes` | 71/74 (96 %) | 71/74 (96 %) |
| `pp-macros-std` | 12/21 (57 %) | 18/21 (86 %) |
| `pp-macros` | 68/104 (65 %) | 107/114 (94 %) |
| `pp-xmacros` | 15/38 (39 %) | 38/38 (100 %) |
| `seed` | 16/24 (67 %) | 18/24 (75 %) |
| `symlinks` | 4/6 (67 %) | 4/6 (67 %) |
| `templates-basic` | 67/96 (70 %) | 67/96 (70 %) |
| `templates-config` | 11/21 (52 %) | 11/21 (52 %) |
| `templates-cxx20` | 41/58 (71 %) | 41/58 (71 %) |
| `templates-specialisation` | 39/70 (56 %) | 39/70 (56 %) |
| `unicode-separators` | 16/16 (100 %) | 16/16 (100 %) |
| `vs-configs` | 91/91 (100 %) | 91/91 (100 %) |
| `vs-nav` | 15/15 (100 %) | 15/15 (100 %) |
| `vs-props` | 96/96 (100 %) | 96/96 (100 %) |
| `vs-slnx` | 91/91 (100 %) | 91/91 (100 %) |
| `vs-std` | 16/16 (100 %) | 16/16 (100 %) |

**No fast answer lost.** For every line of the 37 corpora: no line that agreed before disagrees now (0; the lines of the
three corpora with added expectations were compared by query and final answer, not by line number), and the fast answer of
every line that still disagrees is unchanged except where it now is the final answer. 0 `FAIL`, 0 `XPASS` after the
promotions below; the CLI run counts 5,349 `PASS` and 122 `XFAIL` lines.

Where the rest is (355 of 2,794 lines disagree; by shape, before in brackets: 183 "fast is a superset of clangd's answer"
[186], 80 "fast has no answer" [167], 46 disjoint [53], 39 subset [39], 4 overlap [4], 3 "final has none" [3]). What the
macro work removed: `pp-macros` 36 -> 7 disagreements, `pp-xmacros` 23 -> 0, `pp-macros-std` 9 -> 3, and the `macros_*.h`
lines of `gen-game` / `gen-mixed` / `gen-service` (9 each). What is left of the "no data in the index" class: the aliases and
using-declarations above, specialisations, operator tokens (`operators` 48, FAST-OVL), overloads by type. The macro lines
that remain: a function-like macro and a function of the same name (`clamp01`), `SCALE` redefined between two headers
(needs the order of the includes), a conditional `#define` chain (`use_mode_fn`).

**Expectations.** 23 `xfail` lines of the fast tier that now pass became normal lines (`pp-macros` 4, `pp-xmacros` 4 + 2
`rename` lines, `pp-macros-std` 2, `lookup-namespaces` 2, `gen-game` 3, `gen-mixed` 3, `gen-service` 3); the two `rename`
lines (`use_Red_1` in `pp-xmacros`) passed because the index now knows the `Red` token of `colors.def`, which clangd's rename
leaves out. `seed`'s `fast def use_get_w -> none` is now `-> Box_get_w` (the macro `GEO_GETTER` is expanded). 13 `fast` lines
were added (the macro-declared names of `pp-macros`; `fast decl` for a forward declaration in `name-collisions` and
`oop-virtual`). Nothing was weakened; `final` lines are unchanged.

**Cost** (generated `game` profile, `--size large --seed 1 --variant cmake`: 20,380 files, 6,981 translation units, 253,387
symbols, 603 `macros_*.h` headers, 1,510 macro templates, 1,191 macro calls that expand to 1,191 symbols, 930 X-macro
includes, 1,053 forward declarations; `tenfold-cli bench --no-clangd`, 5 alternating runs of each binary on a loaded box,
`.tenfold` removed before each cold run; minimum / median):

| | before | after |
|---|---|---|
| fast index build, cold (`fastIndexMs`) | 1,004 / 1,063 ms | 849 / 1,093 ms |
| fast index complete, cold (`fastIndexDoneMs`) | 1,231 / 1,288 ms | 1,143 / 1,379 ms |
| fast index, warm (store loaded) | 301 / 367 ms | 348 / 432 ms |
| symbol search `Shape` / `area` (`symFast[..]Ms`) | 18.9 / 2.0 ms (min) | 14.0 / 3.2 ms (min) |
| peak RSS | 156.2 MiB | 163.5 MiB (+4.7 %) |
| store (`.tenfold/fastindex.bin`) | 9.59 MB | 9.92 MB (+3.5 %) |

The cold numbers overlap within the noise of a box that was also building (the extraction adds a directive scan of the
`#define` lines and a call record per typeless `NAME(`; the multi-threaded build absorbs it). The warm index is about 15 %
slower to start: the expansion layer is rebuilt after the store is loaded (an expansion is never persisted, so a changed
macro cannot leave a stale answer) and costs 50 ms of import time for the 1,191 calls here (`FastIndex::importFiles`, one
thread: 89 ms -> 141 ms in a driver around the store). Memory: +2.6 MiB are the expansion entries (measured with
`setExpandMacros(false)` against `true` over the loaded store), the rest the three new vectors of every `FileSymbols`
(88 bytes x 20,380 files) and the macro records. Incremental updates (the same 20,380-file index): the edit of a file with
macro calls 0.7 ms, of a file whose macros did not change 0.5 ms, of a macro definition 19 ms (every call is expanded again,
1,191 here), a new file 21 ms (the include graph changed, so every call is resolved again). `Symbol` is still 80 bytes
(`forward` sits in padding).

**Store.** `FastIndexStore` format 4, extractor salt 7: bit 7 of a symbol's kind byte is `forward`, and after each file's
preprocessor section comes the macro section (templates with token positions, calls, X-macro includes). Self-contained
and commented in `FastIndexStore.h` / `.cpp` ("FAST-DECL") so that it can be merged with another format change; an old
store is rejected (format mismatch) and rebuilt.

**Tests.** `tests/test_macro_decl.cpp` (20 tests): the extractor's records (templates with positions, calls in namespaces
and classes, the typed-declarator call, X-macro includes in and out of enums, a `#define` in a function body); the expander
(declared names and their positions, `##` / `#` / variadics / `__VA_OPT__` / `__LINE__`, nested and passed macros,
self-recursion, mutual recursion, exponential growth, depth, unbalanced scopes, the work budget, wrong argument count); the
index layer (a macro in an included header, change / removal of the caller and of the macro file, two different
definitions are not guessed between, `#define` / `#undef` order inside one file, X-macro lists with an enum, an unscoped
enum and a function list, the list changing and going); forward declarations and inline namespaces; the store round trip
with every payload byte flipped and truncations; four engine tests (names through macros, a macro call under `#ifdef`, `decl`
of a class, `api::call` through an inline namespace). `symsearch_ranking_equivalent_sample_project` switches the expansion
layer off for its oracle (`FastIndex::setExpandMacros`); `semantic_clangd_resolves_what_the_fast_parser_cannot` now expects the
fast tier to land on `GEO_GETTER(double, w)`. Fuzz target `macroexp` (records, store round trip, expansion under default and
tiny limits, an index over the input and its halves) with six regression inputs under `tests/fuzz/regressions/macroexp/`;
30 s of it in Release (239,000 executions) and 60 s under ASan + UBSan (13,900) found nothing; all 31 regression inputs replay.

Suites on the final tree: `tenfold-tests` 807 (787 + 20) in Release and in a gcc ASan + UBSan Debug build with leak
detection; `tenfold-corpus-tests` with `TENFOLD_CORPUS_TIMEOUT=300` (3 tests); `tenfold-ui-tests` 760 and `tenfold-text-tests` 30
under `xvfb-run -a` (unchanged).

## SLN-FILES: a solution workspace is the files of its projects (branch `task/SLN-FILES`)

User decision of 2026-11-15 (`docs/OPEN_QUESTIONS.md`, `docs/VS_BEHAVIOUR.md` section 19): in a solution workspace the file
lists are the files of the projects (wherever they are), the solution items and the additional workspace folders; the fast
tier additionally indexes the headers those files `#include` (resolved with the compile commands' include directories, not
listed anywhere; caps and exclusions in `docs/ARCHITECTURE.md`, "Workspace kinds"). Folder, CMake and compile-database
workspaces are unchanged.

**Agreement (method as W12-FAST-PP: `tenfold-cli corpus tests/corpus/<name> --json` for each of the 37 corpora, Release,
shared `-j3` box, clangd 22.1.1, `--timeout 900` for `vs-configs`; the JSON's `fastAgreement`, `file:line` sets compared line
by line between the binary built from `8c6c2b5` and this branch).**

| | before (8c6c2b5) | after |
|---|---|---|
| 36 corpora + `vs-slnx` | **2327/2779 = 83.74 %** (2236/2688 + `vs-slnx` 91/91) | **2329/2781 = 83.75 %** |

Every corpus has the same agreement as before except `vs-props` (94/94 -> 96/96: two added `fast def` lines, below). No line
that agreed before disagrees now, none that disagreed changed its answer (`fastAgreement.disagreements` identical for all 37),
and the `vs-configs` / `vs-slnx` figures are unchanged (91/91 each; 183 s each, as before). 0 `FAIL`, 0 `XPASS`. The eight
solution-workspace corpora (`c-vcxproj`, `gen-service`, `seed`, `templates-config`, `vs-configs`, `vs-props`, `vs-slnx`,
`vs-std`) and the 29 others behave identically otherwise.

**Changed corpus lines (all in `tests/corpus/vs-props/tenfold-expect.txt`, commented there with the decision).** The headers
`engine/special_inc/special_only.h` and `engine/features_Release/feature.h` are in no project (only `#include`d through
include directories), so a loose file is no longer an answer of the fast `sym`:
`fast sym "special_only" ∋ special_only_fn` became `-> none`; `fast sym "feature_impl" ∋ feature_debug feature_release`
became `∋ feature_debug` (`features_Debug/feature.h` is a Debug item of `engine.vcxproj`). Coverage is kept by two new lines,
`fast def use_special_only -> special_only_fn` and `fast def use_feature_impl -> feature_debug`: the fast definition still
reaches those headers because the fast index reads the headers the workspace includes. The final (clangd) `sym` lines are
unchanged. The first version of the header step missed forced includes (`/FI`) as files to index and lost `vs-std`'s
`def use_forced_size` (16/16 -> 15/16); fixed (seeds are indexed too) and re-measured 16/16.

**Performance** (generated `service` profile, `--variant vs`, 20,635 files, 196 projects, 6,753 translation units;
`tenfold-cli bench --no-clangd`, store removed before each run, 3 runs each, a loaded 4-core box):

| | before | after |
|---|---|---|
| workspace scan (`scanMs`; the after figure reads every project and each of its 2 configurations) | 53-87 ms | 350-390 ms |
| compile database | 1.4-1.6 s | 1.4-1.6 s |
| `Editor::start` returns | 1.4-1.7 s | 1.8-2.0 s |
| fast index complete (`fastIndexDoneMs`) | 1.4-1.7 s | 3.7 s |
| peak RSS | 280-296 MiB | 304-318 MiB |

The workspace files are indexed as before (about 0.7 s); the rest of the difference is the header step: it waits for the
compile commands, then walks the `#include` graph of 6,753 units in 196 include-directory sets (about 1.9 s here; 3.5 s before
the candidate paths were joined by string concatenation and the units' include sets were walked on several threads; this
generated code includes only its own headers, so the step finds 1 new file). `fastBaseDone` (the workspace files) is
available at the old time; `waitForFastIndex` now also waits for the header step.

## W12-FAST-PP-STRICT: absent macros are unknown unless the project mentions them (branch `task/W12-FAST-PP-STRICT`)

User decision of 2026-11-08 (`docs/OPEN_QUESTIONS.md`): an absent macro is "not defined" only when the project itself
mentions it: a `-D` / `-U` in **any** configuration's compile commands (every configuration and platform of the Visual
Studio solutions, derived from the projects without switching the active one) or a `#define` in an indexed file; every
other absent name is Unknown. The list of platform / standard-library names and the unindexed-include flag of the first
version are removed (`TuEnv::initialState`, `SemanticEngine::flagVocabulary`; `docs/ARCHITECTURE.md`). Same method as
W12-FAST-PP; the table is over the same corpus files.

| corpus | a08635c | strict, active configuration only | strict, all configurations |
|---|---|---|---|
| `c-vcxproj` | 15/18 | 13/18 | 15/18 |
| `seed` | 16/24 | 14/24 | 16/24 |
| `templates-config` | 11/21 | 4/21 | 11/21 |
| `vs-configs` | 91/91 | 79/91 | 91/91 |
| `vs-props` | 94/94 | 70/94 | 94/94 |
| `vs-std` | 16/16 | 15/16 | 16/16 |
| **all 36** | **2236/2688 (83.2 %)** | **2186/2688 (81.3 %)** | **2236/2688 (83.2 %)** |

(The strict-active-only column was measured with two extra `fast` lines in `vs-props` and `templates-config`, 2188/2690.)
With the names of every configuration all 50 lines that the active-only rule lost are back, and the result equals
a08635c line for line: the corpora's switches (`LEVEL_9`, `TC_FAST`, `PROPS_DEBUG`, `SEED_DEBUG`, `C_DEBUG`,
`STDAPP_DEBUG`) are all passed by some configuration. What the strict rule costs is therefore not corpus lines but
guesses: `#ifdef MSG_NOSIGNAL` after `#include <sys/socket.h>` keeps both definitions, because no command or project
file mentions the name. The two `xfail` lines of the active-only step are gone (the `vs-props` and `templates-config`
expectation files are as on main); 0 `FAIL`, 0 `XPASS`.

Cost: deriving the names of every configuration is one `fromSolution` per configuration and platform, once per solution
state: about 3 ms each for `vs-configs` and `vs-props` (4 configurations: 15 ms including process start), kept until a
`.sln` / `.vcxproj` changes or an explicit refresh. Workspaces from CMake or a `compile_commands.json` take the
single-database path of the first version (the large generated workspace is unchanged).

Tests: `MSG_NOSIGNAL` (unit and engine level); `-DENABLE_PROFILER` passed by another unit's command is Undefined where
it is absent; a name `#define`d only in an indexed header is a project macro; a solution with Debug and Release where
`-DTURBO` comes from a `.props` file for Release only: with Debug active `TURBO` is Undefined (one branch), after the
switch to Release it is defined, and back.

## W12-FAST-PP: the fast tier follows preprocessor conditionals (branch `task/W12-FAST-PP`)

Design and rules: `docs/ARCHITECTURE.md`, "Preprocessor conditionals in the fast tier". Every `#if` / `#ifdef` / `#elif` /
`#else` arm used to be indexed and offered equally; the extractor now records the arm of each symbol and the query
evaluates it in the caret's translation unit with the compile command's `-D` / `-U` and certain predefined macros, the
forced includes, the unit's `#define` / `#undef` and `__has_include`. A candidate is dropped only when its arm is
certainly inactive, and a filter that would leave nothing is not applied. Only the fast tier changed; clangd's answers
and every `final` expectation are untouched (all of them still pass).

**Method.** As in W11-FAST-SCOPE: `tenfold-cli corpus tests/corpus/<name> --json` for each of the 36 corpora, Release,
`-j3` box (shared with other builds), clangd 22.1.1, `--timeout 900` for `vs-configs` (300 s for the others); the JSON's
`fastAgreement` (`def` and `decl` lines, `file:line` sets equal, `xfail`, `-> none` and failing lines included).
*Before* = the binary built from `main` at `8e4d83d` (2014/2689 = 74.9 %, the W11 figure, reproduced), *after* = this
branch. The query count after is one lower: `vs-props` had the same `fast def use_level_value` twice (see Expectations).

| corpus | before | after |
|---|---|---|
| `c-basic` | 93/109 (85 %) | 93/109 (85 %) |
| `c-heuristic` | 11/18 (61 %) | 11/18 (61 %) |
| `c-mixed-cpp` | 36/39 (92 %) | 36/39 (92 %) |
| `c-vcxproj` | 11/18 (61 %) | 15/18 (83 %) |
| `encodings` | 123/123 (100 %) | 123/123 (100 %) |
| `gen-game` | 236/255 (93 %) | 236/255 (93 %) |
| `gen-mixed` | 228/243 (94 %) | 228/243 (94 %) |
| `gen-service` | 234/258 (91 %) | 234/258 (91 %) |
| `harness` | 7/8 (88 %) | 7/8 (88 %) |
| `lambdas-bindings` | 87/98 (89 %) | 87/98 (89 %) |
| `layout-dupnames` | 36/38 (95 %) | 38/38 (100 %) |
| `layout-exts` | 46/50 (92 %) | 46/50 (92 %) |
| `layout-unity` | 26/26 (100 %) | 26/26 (100 %) |
| `lookup-adl` | 35/59 (59 %) | 35/59 (59 %) |
| `lookup-namespaces` | 48/66 (73 %) | 48/66 (73 %) |
| `name-collisions` | 106/114 (93 %) | 108/114 (95 %) |
| `oop-ctors` | 108/136 (79 %) | 108/136 (79 %) |
| `oop-virtual` | 76/82 (93 %) | 76/82 (93 %) |
| `operators` | 9/58 (16 %) | 9/58 (16 %) |
| `overloads` | 26/76 (34 %) | 26/76 (34 %) |
| `paths-unicode` | 70/70 (100 %) | 70/70 (100 %) |
| `pp-includes` | 54/74 (73 %) | 71/74 (96 %) |
| `pp-macros` | 53/104 (51 %) | 68/104 (65 %) |
| `pp-macros-std` | 6/21 (29 %) | 12/21 (57 %) |
| `pp-xmacros` | 15/38 (39 %) | 15/38 (39 %) |
| `seed` | 13/24 (54 %) | 16/24 (67 %) |
| `symlinks` | 4/6 (67 %) | 4/6 (67 %) |
| `templates-basic` | 67/96 (70 %) | 67/96 (70 %) |
| `templates-config` | 1/21 (5 %) | 11/21 (52 %) |
| `templates-cxx20` | 41/58 (71 %) | 41/58 (71 %) |
| `templates-specialisation` | 39/70 (56 %) | 39/70 (56 %) |
| `unicode-separators` | 16/16 (100 %) | 16/16 (100 %) |
| `vs-configs` | 21/91 (23 %) | 91/91 (100 %) |
| `vs-nav` | 15/15 (100 %) | 15/15 (100 %) |
| `vs-props` | 4/95 (4 %) | 94/94 (100 %) |
| `vs-std` | 13/16 (81 %) | 16/16 (100 %) |
| **all 36** | **2014/2689 (74.9 %)** | **2236/2688 (83.2 %)** |

The gain came in three steps (the same corpora run after each): the conditional arms with the command line's defines,
predefined macros and the unit's own directives 2014 -> 2194 (`vs-props`, `vs-configs`, `templates-config`, `pp-macros`,
`c-vcxproj`, `seed`, `vs-std`); forced includes (`-include` / `/FI`), `-DX=Y` chains, `/MDd` -> `_DEBUG`,
`__has_include` against the file system, a macro redefined in the caret's own file, and the include-visibility fix
below 2194 -> 2234; angle-bracket includes that skip the includer's directory 2234 -> 2236. Two of the changes are
not about `#if` at all but showed up in the same lines: `FastIndex::reachableFrom` now treats `<x>` and `"x"`
differently (the index records which was written), and a *visible definition* no longer keeps other, invisible
definitions of the same function alive (`inc_Debug/cfg.h` and `inc_Release/cfg.h`, `plat_x64/arch.h` and
`plat_Win32/arch.h`: they used to pair with each other); this moved `layout-dupnames` 36 -> 38, `name-collisions`
106 -> 108 and the `dup.h` lines of `pp-includes`.

**No fast answer lost.** For every one of the 2,688 lines: no line that agreed before disagrees now (0), and no line
where clangd's answer was inside the fast answer before is missing from it now (0); both checked by comparing the
`file:line` sets of the before and after reports (`fastAgreement.disagreements`).

Where the rest is (452 of 2,688 lines disagree; by shape, before in brackets: 186 "fast is a superset of clangd's
answer" [409], 167 "fast has no answer" [167], 53 disjoint [53], 39 subset [39], 4 overlap [4], 3 "final has none"
[3]; every other shape is unchanged, so the gain is all removed over-inclusion):

| cause | lines (about) | why it stays |
|---|---|---|
| `#if` / configuration branches | 3 | `__cpp_lib_optional` (a library feature-test macro: unknown without the standard library's headers, `pp-macros-std` 2), `#include_next` (`pp-includes` 1). Was about 190 |
| operator tokens, macros, names declared through macro arguments (`operators` 49, `pp-macros` 36, `pp-xmacros` 23, generated `macros_*.h`) | 170 | by design: a punctuator is not a word, a macro-declared name needs the macro body |
| overloads of equal arity (parameter types, cv / ref qualifiers) | about 100 | the index keeps counts, not types |
| explicit and partial specialisations, alias chains, using-declarations, forward declarations for `decl` | about 80 | no data in the index |
| a class used as a construction (`Widget w{1}`): clangd answers class and constructor | about 30 | kept class-only on purpose (W10 `refinable`) |

**Expectations.** 13 `xfail` lines (fast `def`) that now pass became normal expectations (the runner reported them as
`XPASS`): `name-collisions` 1, `pp-includes` 4, `pp-macros` 4, `pp-macros-std` 1, `vs-configs` 2, `vs-props` 1. Four
`fast` lines that listed every branch as alternatives now name the one branch the configuration selects
(`vs-props` `use_level_value` -> `level_three` and `use_sheet_kind` -> `sheet_debug`, `templates-config` `use_run_int` ->
`Policy_safe_run`, `vs-configs` `use_log_core` -> `log_fancy`); the `vs-props` duplicate of `use_level_value` that the
promotion created was removed. None was weakened; `final` lines are unchanged. The final binary ran all 36 corpora twice with no `FAIL` or `XPASS`:
once through the CLI (the table above) and once through `tenfold-corpus-tests` (`TENFOLD_CORPUS_TIMEOUT=300`: 3 tests,
5,160 `PASS` and 145 `XFAIL` lines); the five-runs-in-a-row stability bar of docs/TESTING.md was not repeated.

**Latency** (`approximateDefinitions` with `Want::Definition`, 25 sampled identifier uses that the index knows in each of
250 random files after the file was opened, a small driver around the engine with the clangd client off, Release,
`.tenfold` removed before each run; generated `game` and `mixed` at `--size medium` (3,068 / 3,082 files, 26,043 /
23,003 symbols) and `service` at `--size large` (20,437 files, 178,816 symbols), each with a `compile_commands.json`
(so the conditions are evaluated); `cond` is a synthetic worst case, 40 translation units over 400 headers that hold 30
`#if` / `#elif` / `#else` chains of the same function each (440 files, 48,000 symbols, one symbol in two sits in an
arm). The ranges are 3 runs each on a machine shared with other builds; the driver samples differently from W11's, so
compare only before with after. The before column is `main` at `8e4d83d`):

| | p50 | p90 | p99 | max | first query in a file (p50 / p90) |
|---|---|---|---|---|---|
| `game` medium, before | 21.0 - 22.9 us | 72 - 75 us | 301 - 362 us | 8.2 - 8.4 ms | 54 - 63 us / 255 - 296 us |
| `game` medium, after | 21.3 - 23.1 us | 75 - 78 us | 339 - 511 us | 8.1 - 14.5 ms | 57 - 68 us / 265 - 318 us |
| `mixed` medium, before | 13.1 - 16.8 us | 65 - 83 us | 210 - 294 us | 4.4 - 8.2 ms | 41 - 62 us / 207 - 266 us |
| `mixed` medium, after | 14.2 - 15.6 us | 69 - 75 us | 279 - 301 us | 10.5 - 12.2 ms | 50 - 61 us / 234 - 259 us |
| `service` large, before | 91 - 98 us | 11.3 - 11.7 ms | 15.4 - 17.1 ms | 27.0 - 30.1 ms | 225 - 311 us / 10.8 - 12.3 ms |
| `service` large, after | 92 - 98 us | 10.8 - 12.5 ms | 16.0 - 17.2 ms | 34.3 - 42.8 ms | 246 - 327 us / 10.8 - 12.7 ms |
| `cond`, before | 2.4 - 2.5 us | 3.0 - 4.2 us | 132 - 147 us | 1.9 - 6.0 ms | 117 - 125 us / 318 - 341 us |
| `cond`, after | 5.2 us | 6.4 - 7.0 us | 199 - 235 us | 0.7 - 4.5 ms | 184 - 188 us / 353 - 422 us |

A query whose candidates sit in no arm (the generated workspaces have include guards only) costs nothing extra: the
check is one binary search per candidate in an empty table. A query with conditional candidates reads the translation
unit's environment: its first use after the index changed (a saved or edited file, a new compile database) builds it
(the files of the unit and their `#define` / `#undef` records: 4 ms for a unit of a few thousand files; the reverse
include table that finds the unit of a header with no compile command, built once per change of the include graph:
about 20 ms on the 20,000-file workspace), which is the `max` column of `game`, `mixed` and `service`; later queries
evaluate an arm in about a microsecond (`cond`: +2.8 us at p50, 1,000 queries, most of them conditional). The corpus
runner's own `latencyMs` (small projects, every first query included): `def.fast` mean of medians 0.033 ms -> 0.035 ms,
mean p95 0.48 ms -> 0.34 ms, max 19 ms -> 64 ms (one cold build of a 3,000-file unit in `gen-service` on a loaded
machine); `decl.fast` 0.044 -> 0.032 ms. clangd's `final` latencies are unchanged within noise.

**Index build time, memory, store** (same workspaces; build time: `FastIndex::indexFiles` over every source file with one
thread, 2 - 3 runs, loaded machine; RSS: Editor start including the fast index; store: `.tenfold/fastindex.bin`):

| workspace | build before | build after | RSS before | RSS after | store before | store after |
|---|---|---|---|---|---|---|
| `game` medium | 93 - 118 ms | 102 - 140 ms | 25.5 - 25.6 MiB | 26.3 - 26.5 MiB | 1.04 MB | 1.07 MB |
| `mixed` medium | 87 - 91 ms | 87 - 91 ms | 24.5 - 24.6 MiB | 25.3 - 25.7 MiB | 1.00 MB | 1.06 MB |
| `service` large | 640 - 794 ms | 686 - 857 ms | 145.8 MiB | 149.4 - 149.8 MiB | 7.34 MB | 7.59 MB |
| `cond` (worst case) | | | 17.6 - 17.8 MiB | 20.8 - 21.1 MiB | 1.26 MB | 2.17 MB |

The extraction pass over the directive tokens costs about 5 - 10 % of the single-threaded index time (a string copy per
`#define` / `#if`; the multi-threaded build of the large workspace stays under a second); memory grows by 2.6 - 3.5 %
for ordinary code (a few vectors per file and one record per `#define` / `#if` arm; `sizeof(Symbol)` is still 80 bytes)
and by 18 % in the synthetic case where every second symbol sits in an arm; the store grows by 3.4 - 6.1 % (one `u32` per
file, the arms, the symbol-arm pairs and the macro records) and by 72 % in that case. The corpus runner's peak RSS (largest
of the 36 projects) is 16.4 -> 16.7 MiB.

**Tests.** `tests/test_pp_cond.cpp` (35 tests: evaluator truth tables for literals, operators, signed / unsigned rules,
`defined`, macros; three-valued cases; malformed, deeply nested and very long expressions; directive parsing; the
extractor's arms, include guards, unbalanced and 100,000-deep input, a 20,000-arm `#elif` chain; the store round trip and
rejection of inconsistent arm tables with every byte flipped; compile-command macros (`-D` / `-U` forms and order,
`/D` spellings, response files, forced includes, predefined macros per driver / target, `_DEBUG`, `-DCHAIN=VAL`); the
translation-unit evaluator (flags decide branches, value comparisons and `#elif` chains, the absence rules, `#define` /
`#undef` in the same file and in other files, include-guard `#else`, macro bodies expanded where used, cycles, forced
includes, `__has_include`); angle / quoted reachability and foreign includes; engine-level narrowing with the compile
command's defines (two translation units, one header), a caret in a header with no command, a definition judged in its
own translation unit, no compile command (every branch stays), a macro redefined in the caret's file, forced includes
and `-DX=Y` values, and a Visual Studio configuration switch Debug -> Release -> Debug), the 13 promoted corpus lines,
fuzz target `ppcond` (and `indexstore` now also runs the evaluator on accepted stores). 60 s runs of `lexer`, `extract`,
`indexstore`, `scopescan` and `ppcond` under ASan + UBSan with coverage feedback found nothing (`ppcond` also with
seeds 2 and 3: 67,000 - 69,000 executions each, 7,400 edges); all 20 regression inputs replay.
Suites on the final tree: `tenfold-tests` 718 (683 before + 35) in Release, in a gcc ASan + UBSan Debug build with leak
detection, and in Release with `PATH=/bin` (no clangd); `tenfold-ui-tests` 640 (`xvfb-run -a`); `tenfold-text-tests` 30;
`tenfold-corpus-tests` 3 (all corpora); no failures.

## W11-FAST-SCOPE: scope-aware fast tier (branch `task/W11-FAST-SCOPE`)

Design and rules: `docs/ARCHITECTURE.md`, "Scope-aware lookup in the fast tier". The fast tier used to answer by word;
it now narrows the by-name candidates with the caret's lexical scope (`ScopeScan`), the declared type of a receiver, the
caret's namespace / class chain with using-directives and base classes, `static` / anonymous-namespace entities, the
call's argument count, the include graph (with the compile command's include directories) and locals. Only the fast
tier changed; clangd's answers and every `final` expectation are untouched (all of them still pass).

**Method.** `tenfold-cli corpus tests/corpus/<name> --json` for every corpus, Release, `-j3` box, clangd 22.1.1; the
JSON's `fastAgreement` (`def` and `decl` lines, `file:line` sets equal, `xfail`, `-> none` and failing lines included;
a property of the engine, not of the expectations). *Before* = the binary built from `main` at `bf14c58`, *after* = this
branch. `vs-configs` needs `--timeout 900` (its `config` switches reparse the project; at the default 180 s the runner
stops asking at line 168 in both builds). The corpus set is the 36 projects in `tests/corpus` today (the W9-FIX-C1 figure
809/1853 = 43.7 % was over 29).

| corpus | before | after |
|---|---|---|
| `c-basic` | 64/109 (59 %) | 93/109 (85 %) |
| `c-heuristic` | 10/18 (56 %) | 11/18 (61 %) |
| `c-mixed-cpp` | 30/39 (77 %) | 36/39 (92 %) |
| `c-vcxproj` | 11/18 (61 %) | 11/18 (61 %) |
| `encodings` | 113/123 (92 %) | 123/123 (100 %) |
| `gen-game` | 154/255 (60 %) | 236/255 (93 %) |
| `gen-mixed` | 173/243 (71 %) | 228/243 (94 %) |
| `gen-service` | 135/258 (52 %) | 234/258 (91 %) |
| `harness` | 7/8 (88 %) | 7/8 (88 %) |
| `lambdas-bindings` | 32/98 (33 %) | 87/98 (89 %) |
| `layout-dupnames` | 12/38 (32 %) | 36/38 (95 %) |
| `layout-exts` | 46/50 (92 %) | 46/50 (92 %) |
| `layout-unity` | 26/26 (100 %) | 26/26 (100 %) |
| `lookup-adl` | 19/59 (32 %) | 35/59 (59 %) |
| `lookup-namespaces` | 14/66 (21 %) | 48/66 (73 %) |
| `name-collisions` | 7/114 (6 %) | 106/114 (93 %) |
| `oop-ctors` | 58/136 (43 %) | 108/136 (79 %) |
| `oop-virtual` | 18/82 (22 %) | 76/82 (93 %) |
| `operators` | 9/58 (16 %) | 9/58 (16 %) |
| `overloads` | 4/76 (5 %) | 26/76 (34 %) |
| `paths-unicode` | 50/70 (71 %) | 70/70 (100 %) |
| `pp-includes` | 49/74 (66 %) | 54/74 (73 %) |
| `pp-macros-std` | 6/21 (29 %) | 6/21 (29 %) |
| `pp-macros` | 49/104 (47 %) | 53/104 (51 %) |
| `pp-xmacros` | 14/38 (37 %) | 15/38 (39 %) |
| `seed` | 11/24 (46 %) | 13/24 (54 %) |
| `symlinks` | 4/6 (67 %) | 4/6 (67 %) |
| `templates-basic` | 57/96 (59 %) | 67/96 (70 %) |
| `templates-config` | 1/21 (5 %) | 1/21 (5 %) |
| `templates-cxx20` | 41/58 (71 %) | 41/58 (71 %) |
| `templates-specialisation` | 30/70 (43 %) | 39/70 (56 %) |
| `unicode-separators` | 16/16 (100 %) | 16/16 (100 %) |
| `vs-configs` | 21/91 (23 %) | 21/91 (23 %) |
| `vs-nav` | 14/15 (93 %) | 15/15 (100 %) |
| `vs-props` | 4/95 (4 %) | 4/95 (4 %) |
| `vs-std` | 13/16 (81 %) | 13/16 (81 %) |
| **all 36** | **1322/2689 (49.2 %)** | **2014/2689 (74.9 %)** |

Where the rest is (409 of the 675 remaining disagreements are "fast is a superset of clangd's answer", 167 "fast has no
answer", 53 disjoint, 39 subset, 7 other; before: 986 / 228 / 95 / 43 / 15):

| cause | lines (about) | why it stays |
|---|---|---|
| `#if` / configuration branches (`vs-props` 91, `vs-configs` 70, `pp-includes`, `templates-config` 20) | 190 | every branch is indexed; evaluating the compile command's defines per symbol would need a conditional tag in the persisted index (done by W12-FAST-PP, above: about 3 lines remain) |
| operator tokens, macros, names declared through macro arguments (`operators` 49, `pp-macros` 51, `pp-xmacros` 23, generated `macros_*.h`) | 170 | by design: a punctuator is not a word, a macro-declared name needs the macro body |
| overloads of equal arity (parameter types, cv / ref qualifiers, `push(const T&)` / `push(T&&)`) | about 100 | the index keeps counts, not types |
| explicit and partial specialisations, alias chains (`using B3 = B2`: clangd also answers the targets), using-declarations (not indexed), forward declarations for `decl` | about 80 | no data in the index |
| a class used as a construction (`Widget w{1}`): clangd answers class and constructor | about 30 | kept class-only on purpose: the W10 `refinable` design waits for clangd, and expectations such as `fast def use_Pair -> Pair` encode it |

**Expectations.** 161 `xfail` lines (fast `def` / `decl`) that now pass became normal expectations (the runner reported
them as `XPASS`); no expectation was weakened. One previously passing `fast` line regressed during the work
(`templates-basic` `fast def use_Pair -> Pair`, after the fast tier had started to add the constructor of a written
construction); the code, not the line, was changed back. The generator (`tools/stress/gen_codebase.py`) now writes a plain
`fast def` for a name with several definitions instead of an `xfail`; `regen_committed.sh` keeps what is still wrong as
`xfail` with a fast-tier reason (`xfail_from_report.py`).

**Latency** (`approximateDefinitions` with `Want::Definition`, 25 sampled identifier uses in each of 150 to 300 random
files, a small driver around the engine, generated `game` and `mixed` profiles at `--size medium` (3,068 files, 26,043 /
23,213 symbols) and `service` at `--size large`; the ranges are 3 runs each; the before column is `bf14c58`):

| | p50 | p90 | p99 | max | first query in a file (p50) |
|---|---|---|---|---|---|
| `game` medium, before | 2.2 - 2.5 us | 0.41 - 0.56 ms | 3.9 - 4.9 ms | 7.9 - 16.4 ms | 3.0 - 3.6 us |
| `game` medium, after | 3.9 - 4.6 us | 0.21 - 0.34 ms | 1.2 - 1.5 ms | 5.2 - 10.4 ms | 47 - 50 us |
| `mixed` medium, before | 1.4 us | 0.09 ms | 2.8 - 3.5 ms | 6.0 - 10.0 ms | 2.1 us |
| `mixed` medium, after | 2.2 - 2.5 us | 0.04 - 0.06 ms | 0.8 ms | 2.1 - 8.4 ms | 40 - 44 us |
| `service` large (20,423 files), before | 4.1 - 4.5 us | 36 ms | 40 - 42 ms | 56 - 61 ms | 4.2 us |
| `service` large, after | 6.0 - 6.3 us | 8.8 - 8.9 ms | 11.9 - 12.6 ms | 19 - 21 ms | 65 us |

The median grows by about 2 us (a warm query reads the scope map and filters a handful of candidates); the first query
in a file pays the one-off scan of that file (about 45 us for a few kB; 20 to 60 ms only for files near the 512 KiB scan
limit, bigger files skip the scan). The tail is better because the narrowing removes thousands of candidates before the
answer is built (the `gx` namespace of the generated code has 2,940 declarations; the include graph keeps the visible
ones). The corpus runner's own `latencyMs` (small projects, every first query included): `def.fast` mean of medians
0.020 ms -> 0.052 ms, mean p95 0.18 ms -> 0.40 ms, max 12 ms -> 18 ms; `decl.fast` 0.021 -> 0.034 ms. clangd's `final`
latencies are unchanged within noise.

**Index build time and memory** (same workspaces, `.tenfold` removed, Editor start including the fast index, 3 runs; the
scope maps are built on demand, not at index time):

| workspace | build before | build after | RSS before | RSS after |
|---|---|---|---|---|
| `game` medium (3,068 files, 26,043 symbols) | 53 - 73 ms | 56 - 62 ms | 23.7 MiB | 24.1 - 24.3 MiB |
| `service` large (20,423 files, 186,225 symbols, 98 MB) | 394 - 463 ms | 391 - 402 ms | 139.4 - 139.6 MiB | 141.0 - 141.3 MiB |
| `fastindex.bin`, large workspace | 7.24 MB | 7.43 MB | | |

(+1.2 % RSS: the file-name table of the include resolver; `sizeof(Symbol)` is still 80 bytes; +1 byte per symbol in the
store.)

**Pathological workspaces** (`tools/stress/gen_pathological.py --scale asan`; `tree` 4,881 files, `bigfiles`, `nesting`,
`truncated`): index build and RSS unchanged (`nesting`, 72,050 symbols: 37.2 -> 37.4 MiB; `bigfiles` 17.4 MiB both). The
first query in a file costs the scan: `nesting` (scopes 3,000 deep, files up to 640 kB) up to 46 ms once per file; the
largest scanned `bigfiles` file (a 1 MB line of 1M tokens) took 105 ms before the limit was lowered to 512 KiB. Files over
the limit and code nested more than 48 scopes deep fall back to the by-name answer. The new `scopescan` fuzz target found a
quadratic case (a truncated source with an unclosed `(` followed by many `{` took 0.93 s for 12 kB because every brace was
matched to the end of the file); brackets are now matched once per file (0.06 s), and the input is kept in
`tests/fuzz/regressions/scopescan/`.

**Tests.** `tests/test_fastscope.cpp` (scope scan; extractor `static` / default arguments; store round trip of the new
fields; include reachability with include directories; an engine test: `static` functions of two files, namespace
overloads by argument count, members through declared types, base-class hiding, qualifiers, a local hiding a function),
the 161 promoted corpus lines, fuzz target `scopescan`; 60 s runs of `scopescan`, `lexer`, `extract` and `indexstore`
under ASan + UBSan found nothing else.

## W9-FIX-C1: fast-tier indexing gaps and answer shaping

Fixes in `src/semantic/FastIndex.*` and the approximate-answer path of `SemanticEngine` (design: `docs/ARCHITECTURE.md`,
"What the fast tier indexes, and how its answer is shaped"). `fast def`/`decl` agreement with clangd over the 29 corpora of
the time (harness summary lines, `def` and `decl` lines): **398/1841 (21.6%) before, 809/1853 (43.7%) after** (later: 49.2 %, 74.9 %, 83.2 % over 36 corpora, see the sections above); 31 `xfail` lines
were promoted to normal expectations (30 that `tenfold-cli` reported as `XPASS` plus `use_clamp01_paren`), and
fast-tier lines were added for declaration pairing, the tag after `struct`, the destructor, constructors and written
`operator` names.

| finding | status | what changed |
|---|---|---|
| W9d 4 hidden friends (`lookup-adl:use_dot`, `fast sym "dot"`; W9b F2 `overloads` `fast sym "draw"`) | fixed | `friend T f(...) { }` inside a class is a `Function` of the enclosing namespace; plain friend declarations and `friend class X;` are not symbols |
| W9d 5 conversion operators (`oop-ctors:use_Feet_to_Meters`) | fixed | `operator T` is named `operator T` (it was glued to `operatorT`); a caret on a written `operator` word looks that name up. Operator functions get the same canonical names (`operator+`, `operator()`, `operator new[]`, `operator""_km`) |
| W9d 6 / W9b F3 class name vs constructors / destructor (`oop-virtual:use_Engine`, `oop-ctors:use_dtor_call`, `templates-basic:use_Stack/Pair/Wrap`, `layout-exts:use_Vec`) | fixed | a class name answers the class; `~Name` answers the destructors (an in-class destructor is now named `~Name`, as the out-of-line one was); the caret on a constructor name answers the constructors of that arity |
| W9c P-1 / W9e 6 macros (`pp-macros`, `c-basic`, `c-mixed-cpp`, `encodings`) | fixed | the extractor already indexed `#define`, but only when the name directly followed `define`: a comment (the corpus anchors), a line continuation or `# define` hid it. Function-like macros carry their parameter count and are offered only where a `(` follows; a macro defined further down the same file is not offered for an earlier line (`pp-macros:use_late_before`) |
| W9e 7 enumerators of `typedef enum { A, B } T;` | fixed | `typedef` bodies are parsed for `enum` (enumerators, tag) and `struct`/`union` tags; one `Typedef` per declarator |
| W9e 9 `typedef struct node node;` | fixed | a use outside `struct`/`class`/`union`/`enum` answers the typedef, after the keyword the tag |
| W9e 12 prototype and definition both answered | fixed | `Symbol::arity`; "go to definition" drops prototypes that have a definition of the same scope, name and arity, "go to declaration" the reverse |
| W9c P-3 `int (clamp01)(int v)` | fixed | parenthesised declarator after a type |
| W9c P-2 names declared through macro arguments / `.def` lists | not fixed, by design | see the ARCHITECTURE section: no safe lexical rule for which macro argument is the declared name and of what kind |
| W9d 1-3, W9b F1/F5, W9c P-4, W9e 10 (name-only lookup, locals, scopes, overloads, `#if`) | not fixed here | scope-aware resolution was part 2: scopes, locals, receivers and argument counts by W11-FAST-SCOPE, `#if` branches by W12-FAST-PP; overloads of the same arity (by type) and explicit specialisations are still answered by name |
| W9b F4 operator tokens (`a + b`) and literal suffixes (`2.5_km`) | by design | a punctuator is not a word; the fast tier has no answer, clangd does |
| `typedef struct { int x; } P;` members | not fixed here, fixed later | members of a typedef'd struct body were not indexed (the tag/typedef name was); they are now (scope = the tag, else the first declarator name; `docs/ARCHITECTURE.md`) |

Performance (extractor only, Release, one thread, best of 15 runs; `tools/stress/gen_pathological.py` `bigfiles`
at `--scale asan --knob big_lines=300000`): this repository's `src` + `tests` + `third_party` (653 files, 4.9 MB) 47.4
-> 47.0 MB/s, the pathological `million_lines.cpp` + `minified.cpp` + `minified.js.h` (10.8 MB) 58.0 -> 56.6 MB/s;
symbols 23,787 -> 24,121 (macros behind comments, hidden friends, typedef enumerators) and 37,527 -> 37,527;
`sizeof(Symbol)` stays 80 bytes (`arity` fits in the padding), peak RSS of the benchmark process unchanged (15 / 74
MiB). The difference is within run-to-run noise (about 3%).

## Translation-unit context, rename safety, index readiness (W9-FIX-B, branch `task/W9-FIX-tu-context`)

Design and limits: `docs/ARCHITECTURE.md`, "Translation-unit context for included files, rename safety, index readiness".

| finding | state | how |
|---|---|---|
| W9b F6 / F7, W9c P-9, W9e 13 / 14 (`.tpp` `.ipp` `.inl` opened standalone: definitions resolve to nothing, references vanish from the merged index, rename leaves code that no longer compiles) | **fixed** | an opened include-only file gets the compile command of the translation unit that includes it plus the includer chain's text up to the `#include` (`SemanticEngine::provideTuContext`, `src/semantic/TuContext.*`); `templates-basic` 7 and `layout-exts` 7 `xfail` lines now pass and are normal expectations |
| W9e 11 (unity parts `beta.cpp` / `gamma.cpp`) | **fixed for 16 of 20 lines** | the part is compiled with the unity file's flags and the earlier parts through the prefix; 4 `refs` / `rename` lines (`g_counter`, `scaled_def`) are still wrong, cause not identified, kept as `xfail` |
| W9c P-3 (`pp-xmacros` `sym "op_mul"` after three `def`s; `sym "Triangle"` / `"Hexagon"`; `rename Color Hue`, `use_Red_1`, `use_Triangle`), P-10 | **cause found and fixed**: shadowing, not timing: the `def` opened `ops.def` standalone and its empty dynamic index removed the static entries declared in it | `sym` lines and `rename use_Triangle Tri` are normal expectations again; `rename Color Hue` and `rename use_Red_1 Crimson` are `xfail` (clangd misses the `Color::` spellings inside `#define X` bodies and the `Red` token in the `.def`) and the new rename-safety check reports exactly those places |
| W9c `pp-includes` disabled lines (`rename next_b`, `use_size`, `use_top`, `sym "FastOnly"` / `"SlowOnly"`) | `rename next_b`, `use_top`, `use_size`, `sym "FastOnly"` pass; `sym "SlowOnly"` and `refs next_b` are `xfail` with their real reason; `decl use_dup_*` stay disabled (clangd answers any of three merged declarations) | `stack_members.inl` is included inside a class body: such fragments are not sent to clangd (they cannot be parsed to a complete class and would shadow the index) |
| W9d lookup-namespaces rename flake under load, W9a "`waitForClangdIndex` is not a perfect barrier" | **barrier fixed, flake not reproduced** | `waitForClangdIndex` = one finished pass + 300 ms idle (see ARCHITECTURE); clangd never re-indexes on disk changes, so the harness cannot wait for that |
| rename leaves broken code silently | **fixed** | `SemanticEngine::renameCoverage`; `Editor::renameSymbol` holds the edits (`needsConfirmation`) until forced; the UI asks for a second Enter; the corpus runner reports `rename warning` |
| W9g G-2: rename request never answered | **fixed (cause found)** | clangd's scheduler wedges when a document is closed while it is still being built (open, edit, save, close in a row, as every cross-file rename does); `didClose` now waits for `idle` (`SemanticEngine::closeWhenIdle`). Reproduced with `pp-includes` x12 in parallel (5 of 12 stuck, 0 of 12 after). Also: every user-facing request has a watchdog (`Options::requestTimeoutMs`, 25 s) that cancels with `$/cancelRequest` and answers with an error. Not checked on the 3k-file generated project |
| W9g G-1: `impl` answers the declaration after the header was opened | **mitigated** | `preferDefinitions` swaps a declaration for the out-of-line definition the fast index knows |

Verification: `tenfold-corpus-tests` (all 29 corpora) 5 consecutive Release runs on a machine with load average 16-48: no `FAIL`
and no `XPASS` in the last 5 (an earlier set of 5 showed the pre-fix wedge twice and one `decl` flake, `c-basic:52`, whose
expectation now accepts the tag's forward declaration or its definition); `tenfold-tests` Release 550/550 (one unrelated
timing flake, `pathological_churn_while_indexing`, passed alone); `tenfold-ui-tests` Release and gcc ASan+UBSan 471/471;
ASan `tenfold-tests` 549/550 (`crash_handler_writes_report` fails in this build only: compiled with
`-fno-sanitize-recover=undefined` UBSan aborts the crash helper before the handler writes its report; it passes in a build
without that flag, and has nothing to do with this change).

## Corpus findings: name lookup and OOP (W9d)

Six hand-written projects under `tests/corpus/`, expectations written from the C++ rules (what a correct IDE must answer),
not from tenfold's output. 1080 expectations in total; every kind is used (`def decl typedef impl refs hover sym rename`,
`fast`, `xfail`, `open`/`edit`; `config` is not used, it needs a Visual Studio project and is about configurations, not
lookup). Every project has its own `compile_commands.json` (`@CORPUS_ROOT@`, `-std=c++20`).

| project | what it stresses | expectations (pass / xfail) | fast==final on def/decl | runtime (Release) |
|---|---|---|---|---|
| `lookup-namespaces` | nested and `a::b::c` namespaces, namespace aliases, inline namespace versioning, anonymous namespaces and `static` functions with equal names in two TUs, using-declarations (header), using-directives (function scope), unsaved edit | 116 (96 / 20) | 8/59 | 1-2 s |
| `lookup-adl` | ADL for free functions and operators, hidden friends, the `using lib::swap; swap(a,b)` idiom, derived-hides-base, `using Base::f`, member-hides-global, local-hides-member, friends declared in a class and defined later, using-directive reached by a qualified name, unsaved edit | 118 (106 / 12) | 4/59 | 1-2 s |
| `oop-virtual` | pure/virtual/override/final, interfaces with several implementations across files, multiple inheritance (one override implementing two interfaces), virtual-inheritance diamond, covariant returns, calls through base pointers/references, member pointers, pimpl (`Impl` twice), `this->` in dependent bases, CRTP, unsaved new override | 193 (172 / 21) | 10/82 | 2-5 s |
| `oop-ctors` | default/explicit/delegating/copy/move/inherited (`using Base::Base`) constructors, destructors (`~T`, explicit call), conversion operators and implicit conversions to user types, enum vs enum class with equal enumerators, bit-fields, unions (named and anonymous), statics declared in class and defined in a .cpp, nested and local classes named `Node`, range-for over a custom container | 234 (220 / 14) | 15/123 | 2-6 s |
| `lambdas-bindings` | captures (copy, ref, init-capture, `[=]`, `[this]`, `[*this]`), generic lambdas, IIFE, lambdas in `std::function`/`auto`, parameter hiding an outer variable, structured bindings named like members, `std::optional`/`std::variant`/`std::visit` with `overloaded`, member and ADL `begin`/`end`, unsaved edit | 200 (188 / 12) | 22/98 | 25-35 s (parsing `<functional>`, `<variant>`, `<map>`) |
| `name-collisions` | `Node`, `Impl`, `get`, `size`, `Id`, `i`, `n` in many namespaces/classes/TUs, forward declarations in several headers, uses through incomplete types, `friend class Impl` naming a different class per namespace, a function named like a struct (`struct Stat` / `Stat()`), global `log` vs `log_ns::log`, same loop variables in neighbouring functions | 219 (196 / 23) | 7/114 | 3-4 s |

Five consecutive Release runs of `tenfold-corpus-tests` and one ASan+UBSan run: PASS and XFAIL only, counts identical in every
run. The only order-sensitive spot is documented in "Determinism" below.

### Findings

"Fault" says whose behaviour is wrong; where clangd is involved it was queried directly over LSP (clangd 22.1.1, all files
open, background index settled) to separate clangd from tenfold's handling of the answer. In no case was tenfold's own
handling of a clangd answer the cause: every wrong final answer below is already wrong in clangd.

| # | expectation (project) | expected | actual | tier | fault | minimal repro |
|---|---|---|---|---|---|---|
| 1 (narrowed by W11-FAST-SCOPE) | every `xfail fast def` in all six projects (e.g. `lookup-namespaces:use_inner_value`, `name-collisions:use_a_Node_get`, `lookup-adl:use_draw_circle`) | the one declaration C++ lookup picks | every same-named definition in the workspace (all namespaces, classes, overloads, forward declarations) | fast | fast tier (design: name-only lexer index, no scope, receiver type, ADL or overload step) | `namespace a{int f();} namespace b{int f();} int g(){return b::f();}` : `fast def` on `f` returns both |
| 2 (fixed by W11-FAST-SCOPE) | `lambdas-bindings:use_cap_base`, `use_param_n`, `use_outer_n`, `use_sb_x`, `use_init_y`, `use_e_loop`, `use_lam_x`, `use_sb_inner_y`, `edit_use_b` | the local/parameter/capture/binding | none | fast | fast tier: locals, parameters, captures, init-captures, structured bindings and range-for variables are not indexed | `auto [x,y]=p; return x;` : `fast def` on `x` is empty |
| 3 (fixed by W11-FAST-SCOPE) | `name-collisions:use_i_a1`, `use_param_i` | loop variable `i` / parameter `i` | the unrelated member `Holder::i` | fast | fast tier: with no local scope a same-named member of another class is answered, which is worse than no answer | `int sum(int n){for(int i=0;i<n;++i);} struct H{int i;};` : `fast def` on the `i` in the loop gives `H::i` |
| 4 (fixed) | `lookup-adl:use_dot`, `fast sym "dot"` | the hidden friend `dot` | none (no symbol, no definition) | fast | fast tier: friend functions defined inside a class are not extracted | `struct V{ friend int dot(V,V){return 0;} };` : `fast sym "dot"` is empty |
| 5 (fixed) | `oop-ctors:use_Feet_to_Meters` | the conversion operator | none | fast | fast tier: conversion operators (`operator T`) are not indexed | `struct F{ operator int() const; }; f.operator int();` |
| 6 (fixed) | `oop-virtual:use_Engine`, `oop-ctors:use_dtor_call` | the class `Engine`; the destructor `~Widget` | the class plus all constructors and the destructor | fast | fast tier: the word `Engine`/`Widget` is looked up, so class, ctors and dtor are conflated; `~Widget` is not distinguished from `Widget` | `class E{E(); ~E();}; E e;` : `fast def` on the type gives 3 results |
| 7 | `lookup-adl:refs hf_dot`, `refs hf_plus`, `rename hf_dot` | references include the ADL call `dot(a,b)` / `a + b`; rename changes it | only the declaration; rename leaves the call (broken code) | final | clangd (index has no references to hidden friends; `definition` works) | `struct V{friend int dot(V,V){return 0;}}; int f(V a){return dot(a,a);}` : references/rename of `dot` |
| 8 | `lookup-namespaces:refs outer_Node` (exact), `rename outer_Node` | the use `app::Node n;` (through `namespace app{using outer::Node;}`) is a reference and is renamed | the use through the using-declaration is missing, rename leaves it (the code no longer compiles) | final | clangd (functions through a using-declaration work, classes do not) | `namespace o{struct N{};} namespace a{using o::N;} a::N n;` : rename `o::N` |
| 9 | `lookup-namespaces:typedef var_n` | the class `outer::Node` | the class and additionally the using-declaration | final | clangd | same as 8, `typeDefinition` on `n` |
| 10 | `oop-virtual:impl Shape_area =`, `impl Shape_name =`, `impl Shape_clone =`, `impl Shape =`, `impl Animal =` | all overriders / all derived classes, including indirect ones (`Square::area` overrides `Rect::area` which overrides `Shape::area`; `Duck : Walker : virtual Animal`) | only direct ones | final | clangd (`findImplementations` is one level) | `struct S{virtual void f();}; struct R:S{void f()override;}; struct Q:R{void f()override;};` : implementations of `S::f` lacks `Q::f` |
| 11 | `oop-virtual:rename Readable_size` | renaming `Readable::size` also renames `Writable::size` and `NullStream::size`, because `Buffer::size` overrides both | `Writable::size`/`NullStream::size` and their calls are kept; `Buffer::size` no longer overrides `Writable::size` | final | clangd | `struct R{virtual int size()const=0;}; struct W{virtual int size()const=0;}; struct B:R,W{int size()const override;};` : rename `R::size` |
| 12 | `oop-ctors:refs List_begin = List_begin range_list` | the implicit `begin()`/`end()` of a range-for count as references (location convention: the range expression; debatable) | not reported | final | clangd (debatable) | `for (auto& n : list)` : references of `List::begin` |

Observations that are not wrong answers but shape the expectations (all pass):

* Go to definition on a name introduced by a using-declaration answers the using-declaration **and** the entity (accepted
  as `using_x | entity`). `def` on the type name of a constructor call (`Widget(2)`, `new Widget(5)`, a mem-initializer) answers the class, not the
  constructor (accepted as `Widget | Widget_ctor_int`). Both choices are open questions, see below.
* `refs` on a virtual function answers the whole override family up and down one level, with calls through the base; the
  corpora use `>=` for those. `refs` on conversion operators and converting constructors does include the *implicit* use sites.
* Workspace symbols answer the *definition* location when a symbol has one (`sym "call" ∋ v1_call_def`), and match on the
  unqualified name unless the query is scope-qualified (`sym "deep::er::est::make"`; `sym "est_make"` finds nothing).
* Hover on a member names only the immediate class (`In Node`), not the namespace: `a::Node::get` and `b::Node::get` can be told apart only by the
  "provided by" file.
* clangd rejects a rename that collides with another symbol (`invalid name: conflict with the symbol ...`), good.
* `rename` on an `operator==` declaration answers "no symbol at the given location" (clangd refuses to rename operators); not asserted.

### Determinism

* Cross-TU answers (definition in another TU, `impl`, `refs`) need clangd's background index; the runner waits for it before
  the first final query, and all six projects gave identical counts in 5 + 1 runs.
* After an `edit`, clangd's dynamic index takes a moment to learn the new text. A `impl` asked **immediately** after inserting a new
  override does not see it. `oop-virtual` therefore asks `impl ... edit_area` after other queries; verified by LSP that the
  override is returned once the open document has been indexed. If this ever flakes, move those three lines further down.
* `lambdas-bindings` takes 25-35 s because three std headers are parsed by clangd on a loaded machine; the other projects take
  1-6 s. Well under the 60 s per-project budget.

### Open questions

1. What should go to definition answer for a name brought in by a using-declaration: the entity only, or both (clangd)? Accepted both. (Answered by W10-CHOOSE: a chooser lists the using-declaration and the entity, the entity preselected.)
2. What should go to definition answer on the type name in a constructor call: the class or the selected constructor? Accepted both;
   VS and CLion jump to the constructor. (Answered by W10-CHOOSE: the resolved constructor is offered with the class.)
3. Are the implicit `begin()`/`end()` of a range-for references to those functions? Pinned only as an `xfail`.
4. Is "references of an overriding function include the base's declaration and calls through the base" the wanted behaviour?
   Pinned loosely (`>=`).

## Corpus findings: preprocessor and Visual Studio configurations (W9c)

Six corpus projects under `tests/corpus/`. Expected answers come from C/C++ preprocessor and MSBuild semantics (what a
correct IDE answers), not from what tenfold or clangd return today. Where they differ the correct expectation stays and
is marked `xfail ... # reason`; every `xfail` below is a finding.

| project | build description | expectations | pass | xfail | fast/final agreement | runtime (Release) |
|---|---|---:|---:|---:|---:|---:|
| `pp-macros` | `compile_commands.json`, std-free: object/function-like macros, declare-by-macro (classes, functions, enums, fields, properties), `##`, `#`, macros calling macros, variadic + `__VA_OPT__`, `#define`/`#undef`/redefinition order, `#if`/`#elif`/`defined`/`__has_include`/`-D` | 188 | 172 | 16 | 22 / 104 | 1.5 s |
| `pp-macros-std` | `compile_commands.json` with libstdc++ (the CMake variant of the macro cases; `<vector> <map> <string> <cassert> <optional>`) | 49 | 42 | 7 | 4 / 21 | 11-13 s |
| `pp-xmacros` | `compile_commands.json`: X-macros from `.def` files and list macros (enum + switch + table + pasted index enum, generated functions, generated classes) | 82 | 72 | 10 | 14 / 38 | 1-2 s |
| `pp-includes` | `compile_commands.json` (relative paths, other `directory`): guards vs `#pragma once`, one file via several spellings, same header name in 3 directories, `#include_next`, `.inl`, extensionless header, macro-computed includes, `__has_include`, one header under two macro states in two TUs, `-include`, `-D/-U` order, an unguarded header included twice | 150 | 134 | 16 | 43 / 74 | 2-4 s |
| `vs-configs` | `.sln` + 2 `.vcxproj`, std-free: Debug\|x64, Release\|x64, Debug\|x86 (-> Win32), custom Profile\|x64; per-config `PreprocessorDefinitions`, `ForcedIncludeFiles`, `$(Configuration)`/`$(Platform)` in include dirs, `ExcludedFromBuild`, implicit `_DEBUG`/`_WIN64`/`_MSC_VER`, a header shared by both projects | 151 | 149 | 2 | 15 / 91 | 2-3 s |
| `vs-props` | `.sln` + 2 `.vcxproj` + `.props`: sheet imported by two relative spellings, `Import Condition="Exists(...)"` on `$(Configuration).props`, properties overridden after the sheet (item definitions see final values), `Directory.Build.props`, `$(SolutionName)`/`$(ProjectName)`/`$(MSBuildThisFileDirectory)`, conditional `ItemGroup`, `gen\*.cpp` wildcard, per-file metadata, ActiveCfg-only mapping | 134 | 133 | 1 | 4 / 95 | 2-3 s |
| `vs-std` (W9-FIX-vs, Linux/macOS only) | `.sln` + `.vcxproj` that uses the standard library (`<string>`, `<vector>`, `<map>`, `<optional>`, `<memory>`, `<iostream>`) through the host-library fallback; forced include, project defines, `_MSC_VER` branch, Debug\|x64 / Release\|x64 / Debug\|x86 | 30 | 30 | 0 | 7 / 16 | 20-25 s |
| total | | 751 | 687 | 64 | | |

(Counts include the `fast` lines. 50 further expectations are disabled with `#?` in the expectation files, see "Not
deterministic" below.) `config` lines are used for the configuration switches (one block per configuration).

Everything that is right is worth saying too: the whole MSBuild evaluation chain held up. Per-configuration defines and
include directories, `$(Configuration)`/`$(Platform)` inside paths, force-included headers, `ExcludedFromBuild`, a
property overridden after the sheet that imports it, `Directory.Build.props`, the solution's `x86 -> Win32` and custom
configuration mapping, implicit `_DEBUG`/`_WIN64` derived from `UseDebugLibraries`/platform, wildcards and per-file
metadata all produced the right clang-cl command (`tenfold-cli compdb` checked by hand) and the right go-to-definition
inside `#if` blocks after every `config` switch. Clangd handled macro names, nested/variadic macros, `##` names,
declare-by-macro, `#undef` order and `__has_include` correctly in the large majority of lines (172 of 188 in `pp-macros`).

Verification: each project passed `TENFOLD_CORPUS_ONLY=<name> ./build-rel/tenfold-corpus-tests` 5 times in a row
(Release; 30 runs, PASS + XFAIL only) and once under ASan+UBSan (g++ Debug, `-fsanitize=address,undefined`; no
sanitizer report; `pp-macros-std` 13 s, the others 4-6 s); the full `tenfold-corpus-tests` (seed + the six) passes in
39 s.

### Findings

Tier: **fast** = lexer index, **final** = clangd answer. Fault: **clangd**, **tenfold** (solution evaluation / compile
commands / configuration switch / waiting / navigation), **harness** (W9a runner). "clean session" = clangd started on the
same project with only the stated file opened and a settle time of 8 s (my own LSP probe), "editor session" = the
corpus runner.

| id | expectation (project:anchor) | expected | actual | tier | at fault | minimal repro |
|---|---|---|---|---|---|---|
| P-1 (fixed) | `pp-macros` `fast def use_max_items` (and every other macro: 68 of 82 fast/final disagreements) | the `#define` | no answer | fast | tenfold fast tier | any `#define X` + use of `X` before clangd is ready |
| P-2 (not fixed, by design) | `pp-macros` `fast def use_Gadget`, `use_compute_b`, `use_warm`; `pp-xmacros` `fast def use_Red_1/use_op_add/use_Triangle`; `pp-macros-std` `fast def use_Color_names/use_Red` | the declaring macro argument / invocation | no answer | fast | tenfold fast tier | names declared through macro arguments (`DECLARE_CLASS(Gadget)`), `.def` lists (not scanned), `##` |
| P-3 (fixed) | `pp-macros` `fast def use_clamp01_paren` | `int (clamp01)(int v)` | no answer | fast | tenfold fast tier | parenthesised declarator is not a declaration for the lexer |
| P-4 (fixed by W12-FAST-PP, 3 lines remain) | `pp-macros` `fast def use_ver_fn`, `use_nested_fn`, `use_fx`; `vs-configs`/`vs-props` every `fast def` into `#if` branches; `pp-includes` `use_dup_*`, `use_branch_forced` | the one active branch | every branch (3-4 candidates) | fast | tenfold fast tier (by design: no `#if`, no `-D`, no include-dir model) | `#if A int f(); #else int f(); #endif` |
| P-5 | `pp-macros` `def/hover/refs` at `(clamp01)(5)` and `= clamp01;` | the function `clamp01` | the macro `clamp01` (`macro clamp01 provided by "basic.h"`) | final | clangd | `#define f(x) x` + `int (f)(int); int (*p)(int) = f;` then def on `f` in `p = f` |
| P-6 | `pp-macros` `refs ADD3`; `pp-xmacros` `refs DECL_FIELD` | include the use `APPLY(ADD3, ...)` / `FIELDS(DECL_FIELD)` | that use is missing | final | clangd | a macro name passed as an argument and expanded when the replacement list is rescanned |
| P-7 | `pp-macros-std` `refs Color` at `DEFINE_ENUM(Color, ...)` (`Name` is also used in `Name##_names`) | the enum and its uses | empty, rename: "no symbol at the given location" | final | clangd | declare-by-macro where the argument is used both directly and as a `##` operand |
| P-8 | `pp-xmacros` `rename use_width w` (member declared once in `FIELDS(X)`) | edit the use and the token in the list macro | edits only the use: the code stops compiling | final | clangd | member declared through a list macro |
| P-9 (fixed, W9-FIX-B) | `pp-xmacros` last block: `open include/colors.def` then `rename use_Red_1`; `pp-includes` (disabled `#?` lines) `def use_size` landing in `stack_members.inl`, then `rename use_size length` | edit the declaration token in the `.def` / `.inl` and the use | edits the use only, silently | final | clangd (standalone parse of a file that has no TU + its dynamic index shadows the background index) and tenfold (go-to-definition navigates into the file and opens it, which sends `didOpen` to clangd) | `rename use_size length` alone: both edits; after `def use_size -> Stack_size` landed in the `.inl`: one edit |
| P-10 (fixed in part, W9-FIX-B: see section) | `pp-xmacros` `refs xc_Red/xc_Green/xs_Triangle` asked at the token inside the `.def` | uses of the enumerator / class | empty | final | clangd / tenfold: a `.def` is never given a compile command, headers are inferred only by extension | any query located in a file that is only `#include`d and not a header extension |
| P-11 | `pp-includes` `refs dup_value_src/inc1/inc2`, `rename use_dup_reversed`, `sym "dup_value"` | per include path: only that header's `dup_value()` and its own uses | all three declarations and every use; `sym` keeps one location | final | clangd (declarations in three headers have one USR) | same inline function in `src/dup.h`, `inc1/dup.h`, `inc2/dup.h` chosen by `-I` order; `decl` on these is additionally non-deterministic (disabled) |
| P-12 | `pp-includes` `sym "impl_mode"`, `refs mode_fast_impl/mode_slow_impl/SlowOnly`; `pp-macros-std` `refs trace_off`, `sym "trace_name"`, `sym "checks_enabled"` | both macro-state variants of a header | only the variant of the first TU (`sym "SlowOnly"` finds nothing at all); refs asked at an inactive declaration: empty | final | clangd (background index indexes a header once; a header position is answered in the first includer) | `mode/mode.h` with `#ifdef MODE_FAST`, two TUs |
| P-13 | `pp-includes` `def use_next_b_in_a` (position inside `inc1/next.h`, which uses `#include_next <next.h>`) | `next_b` in `inc2/next.h` | `next_a` in `inc1/next.h` | final | clangd (the header is parsed standalone as a main file: `#include_next` loses its place) | `#include_next` in a header, go-to-definition inside it |
| P-14 | `pp-includes` `rename use_box_double box_dbl`, `sym "box_double"` | also the token in `#define BOX_FN box_double`; symbol found | use only; symbol missing while `multi.cpp` is open (found when another file is open) | final | clangd (dynamic index of the open TU hides the static symbol whose name is spelled in an object-like macro) | name of a function given by an object-like macro and the header included twice |
| P-15 (**fixed**, W9-FIX-vs) | `vs-configs` `refs cfg_release/cfg_debug/cfg_profile`, `refs arch_win32/arch_x64`, `rename use_cfg_value` after the first `config` | only the declaration compiled in the active configuration | the declarations of every configuration visited earlier (stale background index, same USR) as well; `rename` edits the other configurations' headers | final | tenfold (configuration switch restarts clangd but keeps one `.cache/clangd/index`; and an open header of the previous configuration stays in clangd's dynamic index) + clangd (USR). Fix: one compile database + index directory per configuration (`.tenfold/vs/<configuration>/`), open headers only the old configuration includes are parked; see ARCHITECTURE.md "One compile database and one clangd index per configuration". The 8 `xfail` lines are plain expectations now. | `config Debug\|x64`, `config Release\|x64`, `refs` on `cfg_value` |
| P-16 (**fixed**, W9-FIX-vs) | `vs-props` Debug\|x86 `def use_level_plugin`, `use_pproj_marker`, `use_pbuild_marker`, `use_pextra_marker` | plugin evaluated as its mapped `Debug\|x64` (flags of plugin.vcxproj) | no compile command; clangd borrows the engine's flags (`LEVEL_9`, `PROJ_EXTRA`, no `PLUGIN_BUILD`) | final | tenfold (solution evaluation: a project with `ActiveCfg` but no `Build.0` in the active solution configuration is dropped from the compile commands) | Fix: a project with an `ActiveCfg` mapping is evaluated with the mapped configuration whether or not `.Build.0` is present (`fromSolution`); 4 `xfail` lines promoted. Repro was: solution maps `Debug\|x86` to `Debug\|x64` for `plugin` without a `.Build.0` line; open `plugin.cpp` |

Evidence for fault assignment (P-15 turned out to have two parts: the shared `.cache/clangd/index`, and the headers of the previous configuration that were still open documents; P-16's behaviour of VS itself is still unverified on Windows, see the open questions): P-1..P-4 are the answers `tenfold-cli corpus` prints for `fast` lines. P-5..P-8, P-10,
P-11, P-12 (refs), P-13 and P-14 were also reproduced against a clean clangd 22.1.1 (`textDocument/references`,
`rename`, `workspace/symbol`, `definition` on a copy of the project) with the same wrong answer. P-9 and P-15 are only
visible in a session with history (see below) and were not reproduced in a clean session. P-16 was checked with
`tenfold-cli compdb tests/corpus/vs-props/vsprops.sln --config Debug --platform x86` (no `plugin.cpp` command).

### Not deterministic (disabled lines, harness/tenfold waiting)

Answers that need clangd's background index or dynamic/static index merging are **not stable between a clean session
and the corpus runner, nor between runs**. They are disabled in the expectation files with `#? ... # DISABLED: reason`
(and listed here) so the corpora are deterministic; the correct expectations are kept in the file as comments.

* `pp-xmacros`: `sym "op_mul"`, `sym "Triangle"`, `sym "Hexagon"`, `rename use_Red_1 Crimson`, `rename use_Triangle Tri`,
  `rename Color Hue`, `refs use_Red_1`. A clean clangd answers all of them correctly (rename Color Hue edits all 8
  spellings including `Color::n` in the `#define X` bodies). In the runner they fail 8/8 once the file contains
  go-to-definition lines that land in `ops.def` and `fields.h` plus a query in a second translation unit; the
  delta-debugged minimum is `def use_op_mul` (lands in ops.def), `def use_fn_member`, `def use_height`, then
  `sym "op_mul" ∋ xox_mul` (none). With any one of the three removed it passes. Earlier in the same work the failing set
  changed from run to run (different lines on consecutive runs) while `timingMs.clangdIndex` ranged from 211 ms to
  1585 ms for identical runs, so a race between the runner's wait for the background index and the index is also
  involved (suspect: `SemanticEngine::waitForClangdIndex` returns at the first `end` progress and the machine is loaded;
  `tenfold-cli --trace` shows a single `begin..end` batch for one TU set, so this is not proven).
* `pp-includes`: `decl use_dup_angle/use_dup_reversed` (inc1 vs inc2 vary between runs: clangd picks one of the merged
  declarations), `refs next_b` and `rename next_b`, `rename use_size`, `rename use_top` (see P-9), `sym "FastOnly"`,
  `sym "SlowOnly"` (flaky XPASS).
* `vs-configs`: `decl use_core_level`, `refs level_*`, `refs log_plain`, `refs dbg_helper_decl`, `sym "core_level"`,
  `sym "cfg_value"`, `sym "arch_name"`, `sym "dbg_forced"`, `def use_word_t` / `def use_word_app` in Debug\|x86
  (clangd sometimes answers with the index definition of the same-USR typedef from the other configuration instead of
  the AST's: 4 of 6 runs) - flaky after a configuration switch.
* `vs-props`: `sym "feature_impl"` ranks `features_Debug/feature.h` first in Release (history).

Further harness limitations met while writing the corpora (W9a, not fixed here):

* `rename`/`refs` directly after a `fast` line act on the previous line's cursor or an unsynchronised document (observed:
  renamed `color_table` instead of the target, "no symbol at the given location"). Every rename block here is preceded
  by a non-fast query.
* `rename` needs at least one indented anchor, so "the rename must change nothing" (pasted names such as `count_`
  that cannot be renamed consistently) cannot be expressed. Three such cases are kept as comments.
* `tenfold-cli compdb tests/corpus/x/x.sln` without `-o` writes `compile_commands.json` next to the solution, and a
  `compile_commands.json` at a corpus root outranks the `.sln` in the runner: easy to commit by accident (not tenfold's
  fault, but a trap for corpus authors).

### Open questions and assumptions

* **Where should go-to-definition of a name produced by a macro land?** Assumed: the place where the identifier is
  spelled in the source (a macro argument), or, when it only exists by pasting or comes from the macro's replacement
  list, the macro invocation that produced it; where both are defensible both are accepted (`A | B`). clangd does the
  first; I did not find a case where it did something else.
* **Should find-references / rename on a symbol that has different declarations in different configurations touch the
  other configurations?** Expected here: only what the active configuration compiles (P-15). A user who wants the
  other configurations renamed would argue for the opposite; the point of the finding is that the answer depends on
  which configurations were visited earlier, which is wrong either way.
* **Is a project with `ActiveCfg` but without `Build.0` evaluated by Visual Studio for IntelliSense?** I believe yes
  (IntelliSense follows the active project configuration mapping, "Build" only controls compilation); not verified on
  Windows. If wrong, P-16 is not a finding (the borrowed flags would still be arbitrary). W9-FIX-vs implemented it
  (evaluate with the mapped configuration) and found no evidence to the contrary.
* **Sources of a project not part of the active configuration / `ExcludedFromBuild` file:** what should workspace
  symbol search and navigation do? Only the fast tier is asserted (`fast sym "dbg_only_free"` finds it in every
  configuration); the final-tier answer was not asserted because I am unsure what VS does.
* `_MSC_VER` is defined (clang-cl default) and `compiler_kind()` expects the MSVC branch; assumed correct for the VS route.
* `.sln` mode is std-free because no MSVC/SDK headers exist on Linux; the macro cases that use the standard library are
  in `pp-macros-std` as a `compile_commands.json` project (which `docs/TESTING.md` of this branch does not mention: the
  section about `.sln` workspaces on Linux does not exist there; the std-free rule is inferred from the task text and
  from the seed). **Fixed (W9-FIX-vs):** off Windows, `.sln` mode now falls back to the host's standard library
  (`--target=<host>`, defines/includes/forced includes kept), `tests/corpus/vs-std` is a `.sln` corpus that uses
  `<vector>`/`<string>`/`<map>`/`<optional>`, and `docs/TESTING.md` documents the rule. The older `.sln` corpora stay
  std-free on purpose (same meaning on Windows).
* Anchors inside a `#define` body or macro argument are written in comments before the token; they never alter the
  preprocessing. The scratchpad tools used for the clean-session probes are not part of the repository.

## Corpus findings: C and unusual layouts (W9e)

Nine hand-written projects under `tests/corpus/` for C code, unusual file layouts and awkward paths / encodings. Expected answers come
from the C and C++ rules (what a correct IDE must answer), not from tenfold's output. 955 expectations; every kind is used (`def decl
typedef impl refs hover sym rename`, `fast`, `xfail`, `open`, `edit`, `config`). Each project has a `compile_commands.json`
(`@CORPUS_ROOT@`) except `c-heuristic` (no project information on purpose) and `c-vcxproj` (`.sln` / `.vcxproj`).
`encodings/.gitattributes` is `* -text` so that CRLF, CR-only, BOM and Latin-1 bytes survive a checkout (the repository default is
`text=auto eol=lf`); `git ls-files --eol` shows `i/crlf`, `i/-text` and `i/mixed` for them.

| project | what it stresses | expectations (pass / xfail) | fast==final on def/decl | runtime (Release) |
|---|---|---|---|---|
| `c-basic` | C11 compiled as C: `struct node` + `typedef ... node`, opaque handle (`struct handle` defined in one .c), typedef'd anonymous struct / union / enum, enum flags, flexible array member, K&R definitions, tentative definitions, `extern` of one variable in two headers, `static` functions and variables with equal names in two files, tables of function pointers with designated initialisers, callbacks, `_Generic`, function-like macros | 185 (169 / 16) | 8/106 | 2-3 s |
| `c-mixed-cpp` | one header with `extern "C"` guards shared by C (`-std=c11`) and C++ (`-std=c++17`) units, C calling a function implemented in C++ and back, a C++ class (virtual interface, RAII) around an opaque C handle, `impl` | 73 (68 / 5) | 6/39 | 4-6 s |
| `c-heuristic` | a C project with no `compile_commands.json`: tenfold must choose C by itself (`class`, `new`, `delete`, `template`, `restrict`, `_Bool`, VLAs, `int a[static n]` are only valid in C); a header opened on its own | 33 (31 / 2) | 2/18 | 0.5 s |
| `c-vcxproj` | `.sln` / `.vcxproj` with `.c` default, a `.c` file with `CompileAsCpp`, a `.cpp` file with `CompileAsC`, `config Debug|Release` changing a declaration | 37 (37 / 0) | 2/18 | 1 s |
| `layout-unity` | a unity build: only `build_unity.cpp` (which `#include`s `alpha.cpp`, `beta.cpp`, `gamma.cpp`) is in the compile database | 45 (25 / 20) | 7/26 | 1-3 s |
| `layout-exts` | `.hh .hxx .inl .ipp .tpp` headers, a header with no extension, `.cxx` / `.cc` sources, a 3000-line generated file, a 29000-character line with anchors thousands of columns in | 95 (84 / 11) | 24/50 | 5-8 s |
| `layout-dupnames` | `a/util.h` and `b/util.h` (both included by one TU), `a/util.cpp` / `b/util.cpp`, a global `class Engine` in two unrelated sub-projects, `leaf.h` at depth 12 and depth 1 | 84 (72 / 12) | 7/38 | 2-5 s |
| `paths-unicode` | spaces, accents, CJK, emoji, parentheses, `% # + & = [ ] { } ' @ !` and a 181-character relative path in directory and file names (10 directories) | 160 (160 / 0) | 20/70 | 3-6 s |
| `encodings` | CRLF, CR-only, mixed LF/CRLF with no final newline, UTF-8 BOM (anchors on line 1), BOM + CRLF, Latin-1, tabs before anchors, multi-byte text (emoji = 4 bytes / 2 UTF-16 units) before anchors on the same line, non-ASCII identifiers (`données::Größe::漢字`), U+2028 / U+0085 / form feed / vertical tab, unsaved edits (`edit`) in CRLF / BOM / multi-byte files | 243 (225 / 18) | 36/107 | 3-5 s |

The low fast==final rates are one cause: for a function with a prototype the fast tier answers the prototype and the definition (two
results), clangd the definition only (see finding 12). Everything under `tests/corpus/` besides these nine was written by other agents.

Verification: five consecutive Release runs of `tenfold-corpus-tests` (`TENFOLD_CORPUS_ONLY=<project>`, nine projects, 45 runs): PASS and XFAIL only,
identical counts in every run, after one expectation was made deterministic (below). One ASan+UBSan pass over all nine projects (the ASan
`tenfold-cli corpus <dir>` binary of the integration worktree, same runner and same `src/`, since the corpus runner takes any directory): no
sanitizer report; counts as in Release in 9 of 12 runs of `encodings` and in the single run of every other project. In the other 3 runs of
`encodings` under ASan on a loaded machine one clangd-dependent line failed (`sym "fn_ünï"` returned nothing, or `rename id_kanji` missed the use in
`use.cpp`): index freshness, not seen in 10 Release runs or in 6 ASan runs of the identifier section alone (see Determinism).

**Fixed by W9-FIX-ENC (commit b16ebdd)**: findings 1, 2, 3, 4, 5, 8 and harness findings H1, H2 (marked "(fixed)" in the tables below; the text of each row
is the state before the fix). 1: files are decoded to UTF-8 on load (Windows-1252 / UTF-16 detection, `core/TextEncoding.h`) and encoded again on save, clangd
and the fast index see the same UTF-8 text, and every `dump()` of user text is `dumpSafe()` (docs/ARCHITECTURE.md, "Encodings and line endings"); the
Latin-1 files now take `def` / `decl` / `fast def` / `rename` in `encodings`. 4: clangd is sent the document's real line endings (`textForLsp()`). 2, 3: a
lone CR ends a line in `TextBuffer`, the fast lexer, find-in-files and the anchor scanner; CR-only documents are sent to clangd with LF because clangd's
LSP layer finds lines at `\n` only. 5: the lexer skips the BOM. 8: `.c` files use a C keyword table. H1: `copyTree` copies links as links, the scanner
skips them (new corpus `symlinks`, Linux only). H2: an exception is that line's FAIL. The corresponding `xfail` lines of `encodings` and `c-heuristic`
are normal expectations now. New observation from `symlinks`: the fast index follows a symlinked *file* (a definition is listed under both names; `xfail`).

### Findings

"Fault" names whose behaviour is wrong. Where clangd is involved it was also queried directly over LSP (clangd 22.1.1, same compile
database, offset encoding utf-8) to tell clangd from tenfold's handling. "Tier" is the tier of the expectation.

| # | expectation (project) | expected | actual | tier | fault | minimal repro |
|---|---|---|---|---|---|---|
| 1 (fixed) | not expressible as an `xfail`: the runner aborts. Any `def` / `decl` / `fast def` (the agreement check asks the final answer) / `rename` whose result file, or any query whose anchor file, is not valid UTF-8 (`encodings/src/latin1.*`) | the answer | `terminate called after throwing nlohmann::json type_error 316: invalid UTF-8 byte at index 60: 0xF6` | final | **tenfold**: `JsonRpcConnection::send` calls `msg.dump()` with the default error handler; `Editor::open` -> `SemanticEngine::didOpen` -> `LspClient::didOpen` sends the buffer text raw. `tenfold-cli` prints `error: [json.exception.type_error.316]...` for `open`; nothing in `Editor` / `EditorApp` catches it, so the editor terminates when a file with Latin-1 / Shift-JIS bytes is opened (or jumped into) while clangd runs; the restart path (`SemanticEngine.cpp:709`) re-sends all open documents the same way | `printf 'const char* s = "Gr\xf6\xdfe";\nint f();\n' > a.cpp`, open it with clangd running |
| 2 (fixed) | `encodings`: all 11 `cr_*` lines (`def`/`decl`/`refs`/`rename`/`sym` on `cronly.c` / `cronly.h`) | answers on the real lines | answers land on other lines (`src/cronly.c:4:19` for the anchor the file has on its line 1), `rename` fails with `Line value is out of range (2)` | final | **tenfold**: `TextBuffer` ends lines at `\n` only, so a CR-only file is one line; clang and clangd also end a line at a lone `\r`, so every position sent to / received from clangd disagrees (the harness splits at `\n` too, i.e. agrees with tenfold, not with the compiler) | file whose lines end in `\r` only |
| 3 (fixed) | `encodings`: `fast def` / `fast sym` for the CR-only files | the C declarations | none | fast | **tenfold** fast lexer: the whole file is one line starting with `#ifndef` / `#include`, so nothing in it is indexed | same |
| 4 (fixed) | `encodings:refs CRLF_MAX` (macro defined in a CRLF header, with a `\` continuation) | definition + both uses | only the definition | final | **tenfold**: `TextBuffer::text()` drops every `\r`, so `didOpen` sends clangd text that differs from the file; with that header open clangd stops reporting references of the macro (raw clangd with the original CRLF text: complete; with the `\r\n` replaced by `\n`: only the definition). Functions and fields defined in the same file are not affected | open `crlf.h` (CRLF) with a `#define M(a) \` continued macro, find references of `M` |
| 5 (fixed) | `encodings:fast def use_bom_v`, `use_bomns_inner`, `use_bomns_A`, `fast sym "inner_bom"`, `"Kind"` | members / namespace contents / enumerators declared by the first token of a BOM file | none (the scope's own name is found, not what is inside it) | fast | **tenfold** fast lexer: the UTF-8 BOM is not skipped, so on line 1 it glues to the first keyword (`\xEF\xBB\xBFnamespace`) and the declaration is mis-parsed. Final answers are right: byte columns agree between harness, tenfold (the BOM stays in the first line) and clangd | file = BOM + `struct S { int v; };` |
| 6 (fixed) | `c-basic:fast def use_SQUARE`, `use_LIMIT`; `c-mixed-cpp:fast def use_version`; `encodings:fast def use_MAKRO` | the `#define` | none | fast | **tenfold** fast index: macros are not indexed at all (no `sym` either) | `#define M 1` then `fast sym "M"` |
| 7 (fixed) | `c-basic:fast def use_F_READ` | the enumerator | none | fast | **tenfold** fast index: enumerators of `typedef enum { A, B } T;` (anonymous enum) are not indexed; enumerators of a named enum are | `typedef enum { A, B } T;` |
| 8 (fixed) | `c-heuristic:fast def use_class`, `use_delete` | the C function named `class` / `delete` | none | fast | **tenfold** fast lexer: C++ keywords are keywords even in a `.c` file. (Language choice itself is right: heuristic `.c` entries get `clang -std=c11`; the final tier passes all 26 other lines.) | `int class(int); int x = class(1);` in a `.c` file |
| 9 (fixed) | `c-basic:fast def use_node_td_main` | typedef `node` | typedef and struct tag `node` | fast | **tenfold** fast index (word-based; cannot tell `struct node` from `node`) | `typedef struct node node;` |
| 10 (narrowed by W11-FAST-SCOPE / W12-FAST-PP) | `c-basic:fast def use_helper_a`, `use_helper_b`, `use_counter_a`; `layout-dupnames:fast def use_Engine1/2`, `use_a_helper`, `use_b_helper`, `use_a_Settings`, `use_shallow_leaf`; `c-mixed-cpp:fast def use_Codec_run` | the entity in this file / namespace / sub-project | every same-named entity in the workspace | fast | **tenfold** fast index (name only, no scope, translation unit or include path); same as finding 1 of W9d | two `static int helper(int)` in two .c files |
| 11 (fixed, W9-FIX-B), 16 of 20 lines | `layout-unity`: 20 lines in `beta.cpp` / `gamma.cpp` (`def`, `hover`, `refs`, `rename`) | resolved as in the unity TU: `def use_bump_beta -> bump_def` etc.; rename of `scaled` / `g_counter` / `Item::weight` renames all parts | no result / not renamed in `beta.cpp`, `gamma.cpp`; references list only `alpha.cpp`; rename leaves code that no longer compiles in the unity TU | final | **clangd and tenfold**: only `build_unity.cpp` is in the database, so clangd parses the included `.cpp` files standalone (names declared by an earlier part are undeclared) and, even with `beta.cpp` closed, its index has no references from the parts (raw clangd and `tenfold-cli` agree). tenfold could map an `#include`d source that has no database entry to the includer's command (the same problem exists for any non-self-contained source) | `unity.cpp` includes `a.cpp` then `b.cpp`; `b.cpp` calls what `a.cpp` defines |
| 12 (fixed) | every `fast def use_f` of a function with a prototype and a definition (e.g. `c-basic:fast def use_list_push`; 9 `xfail` lines in `c-basic`, 3 in `c-mixed-cpp`) | the definition | prototype and definition | fast | **tenfold** fast index lists the prototype as a definition (accepted as `def \| decl` in the other projects, listed as xfail in `c-basic` and `c-mixed-cpp` to keep the correct expectation) | `int f(); int f(){return 0;} int g(){return f();}` |
| 13 (fixed, W9-FIX-B) | `layout-exts`: `def use_dot_in_len2`, `use_x_in_dot`, `use_y_in_dot` (in `vec.tpp`), `def use_state_in_step` (`engine.ipp`) | resolved as in the including TU | no result (`vec.tpp:4:11`, the namespace, for the first three) | final | **clangd**: `.tpp` / `.ipp` files that need the class from the header that includes them are parsed standalone when open. Tenfold passes the file with its borrowed command, which is all it can do today | `types.hh` includes `vec.tpp` at its end; open `vec.tpp`, go to definition of `x` |
| 14 (fixed, W9-FIX-B) | `layout-exts:refs Vec_x`, `refs Vec_y`, `refs Engine_state_field`, `refs Engine_step_decl >= ... Engine_step_def`, **`rename Engine_state_field state`** | all uses incl. those in the `.tpp` / `.ipp`; rename changes them | the uses in `vec.tpp` / `engine.ipp` are missing; rename leaves `state_ += v` in `engine.ipp`, which no longer compiles | final | **clangd**: the standalone parse of the open `.ipp` replaces that file's references in the dynamic index (not the cause for closed files: `refs` with `engine.ipp` closed is complete). Dangerous for rename | `Engine::step` defined in `engine.ipp`, rename `state_` with `engine.ipp` open |
| 15 | `layout-exts:sym "far_sum"` | the symbol at its real column (~6100) | column 4096 | final | **clangd**: index positions keep the column in 12 bits, so a symbol on a line longer than 4095 columns is clamped (the fast tier is right) | `int a[]={...6000 numbers...}; int far_sum();` on one line |
| 16 | `layout-dupnames:refs Engine1`, `refs Engine2`, `refs Engine1_id`, `refs Engine2_id`, `rename Engine1_id`, `sym "Engine" ∋ both` | the entity of this sub-project | references of the other sub-project's `Engine` / `Engine::id_` too; rename changes both; symbol search returns one of the two (which one varies between runs) | final | **clangd** (two global classes with equal names have one USR; not distinguishable from one entity). A workspace with two unrelated sub-projects reports each other's uses | `sub1/engine.h` and `sub2/engine.h`, both `class Engine { int id_; }` |
| 17 | `layout-exts:decl use_scale` (flaky, accepts `scale_decl \| scale_def`) | the declaration in `types.hh` | the inline definition in `scale.inl` in about 2 of 5 runs, the declaration otherwise | final | **clangd** (depends on whether the standalone parse of the open `scale.inl` is already in the dynamic index) | declaration in `types.hh`, `inline` definition in `scale.inl`, both open |
| 18 | `c-basic:sym "g_counter"` | the definition with the initialiser | the first tentative definition (`int g_counter;`) | final | **clangd** (a tentative definition counts as a definition for its indexer). `def` answers any of the three; accepted, see open questions | `int g; int g; int g = 100;` |

Harness findings (not tenfold):

| # | what | detail |
|---|---|---|
| H1 (fixed) | symlinks cannot be put in a corpus | `Runner::copyTree` computes `fs::relative(it->path(), from)`, which resolves symlinks, so a symlinked directory is copied as a symlink named like its **target** (`copy_symlink` fails with "File exists") and the link itself never appears in the scratch copy; for a symlinked **file** `scanAnchors` uses `is_regular_file` (follows links) so the target's anchors are found twice and the corpus fails with `duplicate anchor`. Result: no corpus can contain a symlink. Checked by hand instead: a symlinked directory inside a workspace (`link_lib -> real_lib`, header included through the link) works end to end: clangd answers the real path, the workspace scan does not follow the link (no duplicates), `refs` is complete in both directions |
| H2 (fixed) | an exception in the engine aborts the whole run | the runner does not catch exceptions from `Editor` calls (`terminate called ...`), so a single throwing line takes all following lines and the JSON report with it, and the `fast def` agreement check (`flushAgreement`) silently performs a `def` that jumps into (and opens) the target file, which can crash a run for a line that is only `fast` |
| H3 | line model | `scanAnchors` splits at `\n` (and drops one `\r`): CR-only files are one line and U+2028 / U+0085 are not separators; this matched `TextBuffer`, not clang. (Since 2026-11-07 `TextBuffer` and the scanner both end lines at CR, NEL, LS and PS like Visual Studio; the `unicode-separators` project and `encodings/seps.h` cover it.) A BOM is kept in the line text, so byte columns on line 1 include its three bytes: this agrees with tenfold and clangd (utf-8), so the BOM case noted as "not handled yet" in `docs/TESTING.md` §13 works as is: `encodings` has anchors on line 1 after a BOM in `.h`, `.cpp` and BOM + CRLF files, all passing |
| H4 | what opens a file | `def`, `decl`, `fast def` (through the agreement check) and `rename` open the files they land in or edit, and every query opens the file of its anchor. With finding 1 that makes any expectation that touches a Latin-1 file in one of those ways impossible; `encodings` keeps only `refs`, `hover`, `sym` and `fast sym` for the Latin-1 files, all asked from anchors in UTF-8 files |

What passes and is therefore worth recording: UTF-8 byte columns survive all of tabs, multi-byte text before anchors (emoji, CJK, accents, on
the same line), CRLF, mixed endings, a last line without newline, BOM on line 1, U+2028 / NEL / form feed / vertical tab, non-ASCII
identifiers (`def`, `refs`, `rename`, `sym`, `hover`, and the fast tier), `# % + & = [ ] { } ' @ ! ( )`, spaces, CJK and emoji in
directory and file names (including rename edits written back through percent-encoded URIs), a 181-character relative path, and
unsaved `edit`s in CRLF / BOM / multi-byte documents. Language and flags for `.c` files are right in all three modes (compile database,
`.vcxproj` incl. `CompileAs`, heuristic).

### Determinism

* Cross-TU answers and `refs` wait on clangd's background index (the runner waits); five consecutive Release runs and one ASan run gave identical counts
  after one expectation was made deterministic (`layout-exts:decl use_scale`, finding 17).
* Under ASan on a heavily loaded machine (load average 12 on 4 cores) `encodings` showed a flaky `sym "fn_ünï"` (empty answer) or `rename id_kanji` (misses
  `use.cpp`) in 3 of 12 runs; the cause looks like clangd's background index not yet holding the main TU's shard when the runner's single "index idle" wait
  returned. Not reproduced in Release. If it recurs in CI, move the "non-ASCII identifiers" section further down or add a `refs` that needs `use.cpp` before it.
* `layout-dupnames:sym "Engine"` returns `Engine1` or `Engine2` depending on indexing order; it is an `xfail` in both cases.
* Findings 11, 13 and 14 depend on the runner having opened the dependent files before the query (as every `def` does); with those files closed, `refs` is complete.

### Open questions

1. A header that is not self-contained (`.tpp`, `.ipp`, parts of a unity build) is parsed standalone by clangd. Should tenfold supply the includer's
   command / preamble (`-include`) for files that are not in the database and are `#include`d by one that is? Rename in the presence of such files is unsafe today (finding 14).
2. What should a file with invalid UTF-8 look like to the user: decode as Latin-1 / the system code page for display and send UTF-8 to clangd (and
   write the original bytes back), or keep bytes and replace invalid sequences in the LSP copy? Either way the editor must not terminate.
3. Should CR-only files (classic Mac) be supported, i.e. should `TextBuffer` split at a lone `\r`? Clang does.
4. Which of the three tentative definitions (`int g; int g; int g = 100;`) is "the" definition for go to definition? clangd answers any of them
   (accepted); an IDE could prefer the one with the initialiser.
5. Go to definition through a symlinked directory answers the real path (clangd); is that what users expect, or the path as included?

## Pathological workspaces (W9f)

Generator `tools/stress/gen_pathological.py`, driver `tools/stress/run_pathological.py`, regression tests
`tests/test_pathological.cpp` and `tests/ui/test_pathological_ui.cpp`; how to run them: `docs/TESTING.md`, section
"Pathological workspaces".

> **PROVISIONAL: measured under load.** The runs below were made on the shared 4-core, 15 GB machine while three other
> agents were compiling (load average 10 to 20). Wall-clock numbers are upper bounds; memory numbers are not affected.
> Re-run `python3 tools/stress/run_pathological.py --build build-rel --scale full --clangd` on a quiet machine to replace them.

Environment: Linux, GCC 13 Release (`-O3`), clangd 22.1.1, seed 1, `--scale full` (about 350 MB on disk in total; the
driver deletes each scenario after its steps). Memory is the peak RSS of the largest process (sampled every 50 ms);
"CLI RSS" is the editor's own `VmHWM` as reported by `tenfold-cli bench`.

### Summary

* No crash, no hang, no sanitizer report in any scenario after the fixes below: 9 problems were fixed (7 by W9f, the two
  per-line costs by W11-PERF-LINES), each with a regression test; of the nine items listed under "Not fixed" with
  reproducers two were fixed later and seven remain.
* Before the fixes the stress runs found: an out-of-memory kill (12 GB, `open` of a symlink to `/dev/zero`), a hang (`open` of
  a FIFO), an exception out of `Editor::open` (invalid UTF-8 with clangd running), O(depth^2) memory for nested scopes
  (330 MB for a 180 KB file), 65 ms to 2.5 s per keystroke in large documents, a stale workspace file list after
  directory renames under churn, 60 ms per keystroke at the end of a 1 MB line, and 3.4 GB for a 200 MB source file.

### Scenario table (Release, `--scale full`, fast tier unless noted)

| Scenario | Step | Result | Wall s | Peak RSS MiB | Key timings (ms) |
|---|---|---|---|---|---|
| tree (100k files: 10k flat, 60 deep, 2k dirs; 47 MiB) | bench | pass | 9.6 | 483 (CLI 495) | scan 562, fast index 3445, start 7387 (cold; includes the heuristic compile-flag step), def 0.02, sym up to 72 |
| | repl | pass | 6.7 | 478 | open / def / refs / sym / grep all answered |
| bigfiles (1M-line C++, 200 MB text, 1 to 10 MB lines, minified; 270 MiB) | bench | pass | 5.5 | 544 (CLI 567) | fast index 3235 (the 1M-line file alone: 1.1 s quiet), sym up to 132 under load (26 quiet) |
| | repl | pass | 5.9 | 528 | goto end, insert, def, refs, grep needle in a 200 MB file |
| | editor | pass | 9.8 | 667 | open, scroll to end, type, undo / redo, find, replace all + undo on each giant file |
| nesting (10k nested braces / namespaces / templates / structs, 64k-char identifiers, 100k declarations, 50k-term macro; 15 MiB) | bench | pass | 9.2 | 372 (CLI 265; the rest is the index being saved at shutdown) | fast index 2487, sym up to 276 (see Not fixed 8) |
| | repl | pass | 7.7 | 368 | |
| | editor | pass | 10.9 | 368 | select scope / matching bracket / def at depth 10000 |
| encodings (binary, invalid UTF-8, UTF-16/32, Latin-1, NULs, CR-only; 5 MiB) | bench | pass | 2.1 | 45 | |
| | repl | pass | 1.3 | 68 | |
| | clangd | pass | 20.9 | 41 | clangd takes 20 s to give up on the NUL-heavy files |
| | repl+clangd | pass | 106.3 | 212 | final answers for garbage files wait for clangd (up to 30 s each), the approximate ones come at once |
| fsodd (loops, outside links, chmod 000 as `nobody`, odd names, 4000-byte path) | bench | pass | 1.3 | 22 | |
| | repl | pass | 0.2 | 16 | |
| truncated (cuts, files ending mid-construct) | bench / repl | pass | 1.4 / 0.3 | 22 / 18 | |
| | clangd / repl+clangd | pass | 22.0 / 2.2 | 283 / 117 | |
| mixed (valid project + junk, `compile_commands.json`) | bench / repl | pass | 0.1 / 0.0 | 6 / 6 | |
| | clangd / repl+clangd | pass | 1.9 / 2.0 | 81 / 92 | `def` of `s->area()` lands in `shape.h` |
| churn (8 s of create / modify / rename / delete / file<->dir while a REPL queries) | churn | pass | 9.1 | 9 | |

Note on RSS: the editor's peak includes the persisted fast index being serialised at shutdown (`nesting`: 265 MiB at the end of the
bench, 372 MiB peak). `bigfiles` reaches 567 MiB in the CLI's own figure: the fast-index extractor holds all tokens of the
40 MB / 1M-line file, about 12 bytes of RSS per source byte (budget 500 MB is for a Godot-sized tree; 21x the disk size of that
file alone, below the 4 GB and 10x-the-workspace flags).

Unit-level versions (`tenfold-tests pathological_`, 15 tests, about 12 s) and UI versions (`tenfold-ui-tests
pathological_ui`, 5 tests, about 20 s) pass in Release and under ASan + UBSan. See the end of this file for the sanitizer runs.

### UI frame budget (Release, mock font, 1280x720, `tests/ui/test_pathological_ui.cpp`)

Keystroke-to-paint (budget 8 ms), caret at the end of the document, 60 keystrokes after 5 warm-up keys. Each cell is
average / p99 in ms, the median of 5 runs. Before = `bf14c58`, after = W11-PERF-LINES, runs interleaved on the same machine
(4 cores, llvmpipe, shared with other builds: load average 5 to 6, so the spikes of 4 ms and 8 ms that a loaded scheduler
adds to single keys show in the p99 of some runs; the best of the 5 runs is within 0.1 ms of the median for all
"after" cells):

| File | wrap off, before | wrap off, after | wrap on, before | wrap on, after |
|---|---|---|---|---|
| 300k-line C++ file | 0.62 / 1.08 | 0.76 / 1.07 | 0.66 / 3.03 | 0.65 / 0.91 |
| 100k-line text file | 0.17 / 0.30 | 0.17 / 0.27 | 0.20 / 0.47 | 0.21 / 0.37 |
| 1 MB single-line C++ expression | **16.9 / 19.3** | 0.04 / 0.08 | **16.3 / 20.8** | 0.51 / 1.19 |
| 2 MB single-line comment | 0.32 / 0.38 | 0.03 / 0.06 | 0.20 / 0.30 | 0.20 / 0.33 |
| 1 MB single-line string | 0.82 / 0.99 | 0.03 / 0.06 | 0.86 / 0.98 | 0.25 / 0.43 |
| 550 kB minified C++ line | **9.6 / 14.7** | 0.06 / 0.09 | **9.3 / 13.0** | 0.36 / 0.43 |
| 2 MB line of wide characters | **34.4 / 42.9** | 0.04 / 0.06 | 0.28 / 0.49 | 0.33 / 0.38 |

Every cell is now a `PERF_CHECK` on both numbers (8 ms times `TENFOLD_PERF_SCALE`), and CI runs them enforced with a scale
of 2 (`docs/BENCHMARKS.md`). Open to first frame: under 55 ms for the 300k-line file, 1 to 20 ms for the one-line files (the
1 MB expression was 63 ms: its line is now lexed once, with resume points, instead of its spans being built too). PageDown
x 20 and Ctrl+End are below 1 ms each. The 1000-caret line of `textview_perf_keystroke_to_paint_100k_lines` (engine multi-caret
edit, not a long line) has an average of 2 to 4 ms but a p99 of 4 to 18 ms that follows the machine load; it is outside
this work (its `PERF_CHECK` is on the average).

Where the time went (callgrind of the 1 MB expression, wrap off): 70% `Highlighter::advance` / `lexLine` (the whole line re-lexed
for each key), 19% `Highlighter::colorSpans` (1M colour spans copied), the rest in `TextRenderer`; for the wide-character line
63% `TextLayout::columnForByte` from `WrapLayout::xOf` (caret x and scroll-to-caret, a decode and range lookup of every
character before the caret) and 36% `layoutLine` walking from byte 0 to the first visible cell; for minified C++ with wrap on
the same lexing plus the bracket match walking every token of the line to find the partner of the `}` before the caret.

### Bugs found and fixed (each has a regression test)

1. **`open` of a device file or FIFO ate all memory / hung.** A symlink `x.cpp -> /dev/zero` (or `/dev/urandom`) made
   `Document::loadFromDisk` read forever: `std::bad_alloc` after 12 GB (the driver's guard killed it). A FIFO blocked
   `open` forever. Fix: `readRegularFile()` (`src/core/PathUtil.cpp`) opens non-blocking, `fstat`s, refuses anything but a
   regular file, and reads straight into a string of the right size (also halves the peak of loading a 200 MB file). Used by
   `Document::loadFromDisk`, `Editor::reloadFromDisk`, the open-document hash, the fast index file reader and
   `SemanticEngine::fileChanged`. Test: `pathological_non_regular_files_are_refused`.
2. **Deeply nested scopes cost O(depth^2) memory and time in the fast index.** Every symbol stores its full qualified scope;
   10k nested `struct S{ ...` was 333 MB / 2.1 s for a 180 KB file, 10k nested namespaces 365 MB. Fix: `scopeName()` keeps the
   innermost 64 components (`FastIndex.cpp`). Now 13 MB / 38 ms. Test: `pathological_nested_scopes_cost_linear_memory`.
3. **Every keystroke re-lexed the whole open document, whatever its type.** `SemanticEngine::didChange` copied the text and
   ran the C++ symbol extractor synchronously (about 75 MB/s: 65 ms per key in a 5 MB log, 2.5 s in a 200 MB one), and sent
   `.txt` / `.json` / `.md` documents to clangd as C++. Fix: only sources and headers are indexed and synced; code
   documents of 1 MB or more are re-indexed 300 ms after typing pauses, on the owning thread (`SemanticEngine.cpp`). Tests:
   `pathological_large_non_code_documents_skip_the_fast_index`, `pathological_edits_in_large_code_documents_reindex_lazily`.
4. **Invalid UTF-8 in a document threw out of `Editor::open` when clangd was running.** `JsonRpcConnection::send` used
   `Json::dump()`, which throws `type_error.316` on a Latin-1 comment or a binary file with a `.cpp` extension; the UI would
   have terminated. Fix: invalid bytes are sent as U+FFFD (`JsonRpc.cpp`). Test:
   `pathological_invalid_utf8_documents_open_and_edit_with_a_language_server` (fake clangd).
5. **The workspace file list went stale under churn.** The watcher's debouncer is keyed by path, so after a directory
   rename events for files below it could be delivered under the old name after the rename was handled, leaving stale
   entries and missing the files at the new location (5 + 5 wrong entries after 16k operations in 2.5 s). Fix:
   `FileWatcher::note` re-keys pending events below a renamed directory; `Editor::handleFileEvent` ignores a
   created/modified event whose path no longer is a regular file. Test: `pathological_churn_while_indexing`.
6. **A line scrolled far right laid out every glyph from its start.** `TextRenderer::drawSpans` passed scroll 0 to
   `layoutLine`, so the caret at the end of a 1 MB line (wrap off) emitted 1M quads per frame only for `record()` to clip
   them (60 to 80 ms per keystroke); `TextLayout::columnForByte` and `layoutLine` also walked every cell of the prefix. Fix:
   lay out only the clip, and skip a printable-ASCII prefix with a vectorisable scan (`TextRenderer.cpp`, `TextLayout.cpp`).
   Tests: the `pathological_ui_one_line_files` numbers above (80 ms to 0.3 ms for a 2 MB comment line).
   (The remaining per-line costs were fixed later: items 8 and 9.)

7. **A 200 MB source file took 3.4 GB and 20 s to index.** The symbol extractor keeps every token of the file in memory
   (about 16 bytes of RSS per source byte, times the indexing threads). Fix: a source above 64 MiB is not indexed
   (`extractSymbols` returns nothing; the file still opens and edits). Test: `pathological_huge_source_files_are_not_indexed`.
   Not a substitute for a streaming lexer, see "Not fixed" 9.

8. **(W11-PERF-LINES) The highlighter re-lexed the whole line, and built its colour spans, on every keystroke.** A C++ line of
   500 kB or more cost 10 to 20 ms per key (and 1M `ColorSpan`s were copied); the bracket match walked all its tokens.
   Fix: a line of 2 KB or more is lexed by windows from resume points (`LexResume`, every 2 KB, also inside string literals), an edit
   re-lexes from the last point before it and stops where the scanner re-joins the old points (`Highlighter`,
   `docs/ARCHITECTURE.md` "Very long lines"); the bracket search takes 8 KB windows and stops after 64 KB of long lines (the one
   place where a long line answers differently from a full scan: a partner farther than 64 KB away is not highlighted).
   Tests: `long_lines_highlighter_*` (windows equal a full `lexLine` through edits that open and close strings, comments, raw
   strings and continuations; only a few KB are re-lexed after typing), `long_lines_view_bracket_match_on_a_long_line`.
9. **(W11-PERF-LINES) Column mapping was O(line) for lines with non-ASCII text.** The caret x, scroll-to-caret, hit testing and
   the first visible cell of a 1M-character line of CJK / accented text with wrap off took 35 ms per key. Fix: `LineAnchors`
   keeps a cell boundary and its column every 1 KB of a line of 2 KB or more, built lazily and kept through edits after
   them; every `TextLayout` mapping and `layoutLine` / `drawSpans` take an optional start `Anchor` (`WrapLayout` owns the
   cache, so caret, selection, squiggle, find-highlight and mouse geometry all use it). Tests: `long_lines_text_layout_*`,
   `long_lines_anchors_*` (150 KB of wide characters, combining marks, tabs, control characters and invalid UTF-8, edited at the start,
   middle and end), `long_lines_view_geometry_*` (caret rect, hit test, scroll, clicks against a plain scan, wrap on and off).

### Not fixed (need a design change), with reproducers

1. and 2. **(both fixed by W11-PERF-LINES, see bugs 8 and 9)** The highlighter re-lexed the whole line on every keystroke (1) and the column mapping was O(line) (2).
   `FoldModel::detect` still lexes whole lines, but off the main thread on a copy.
3. **Typing into a 50 MB single line costs about 8 ms in the buffer itself** (already noted in `BENCHMARKS.md`; unchanged).
4. **Per-keystroke clangd full sync.** With a server that does not speak utf-8 offsets `SemanticEngine::didChange` copies
   the whole document (`didChangeFull`) on every key; not measured (found by reading the code; clangd negotiates utf-8 offsets, where the sync is incremental).
   A server limited to utf-16 offsets would get the whole text on every key.
5. **Cold start of 100k files.** `startReturnMs` 4 to 8 s (budget 3 s for Godot-sized trees, 8k files) and the editor's RSS
   is 480 to 530 MB (budget 500 MB): the scan takes 0.4 to 0.6 s, the fast index 4 s under load (1.6 s quiet) and heuristic
   compile-flag generation waits for it. A 100k-file workspace is outside the budget's reference size; recorded, not a defect.
6. **clangd itself stalls on garbage:** files with 100 kB of NUL bytes or 10k-deep nesting take minutes to parse
   (`bench` waits up to 300 s for the TU); tenfold stays responsive (approximate answers arrive at once, the final callback
   comes when clangd answers or after the 30 s the REPL waits). Reproduce: `python3 tools/stress/run_pathological.py
   --build build-rel --scenario encodings --clangd`.
7. **File symlinks that point outside the workspace are indexed and searched** (`link_secret.cpp -> /elsewhere/secret.cpp`
   shows up in find-in-files); directory symlinks are not followed, so there are no loops. Probably intended; recorded.
8. **Fast-tier symbol search is 84 ms for a query with 30k near matches in a 640k-symbol index** (`var_77`, under load;
   budget 50 ms for the reference workspace).
9. **Fast-index memory is proportional to the largest source file** (about 12 bytes of RSS per byte up to the 64 MiB cut: the
   1M-line, 40 MB file takes 500 MB). Needs a streaming lexer / extractor. Reproduce:
   `python3 tools/stress/gen_pathological.py --out /tmp/p --scale full --scenario bigfiles` and `tenfold-cli bench /tmp/p/bigfiles --no-clangd`.

### Sanitizer runs

(At the time of W9f: the suite had 508 + 467 tests then; today's counts are in `docs/TESTING.md`.)

Build: GCC 13, Debug, `-fsanitize=address,undefined`, `TENFOLD_BUILD_UI=ON`; `ASAN_OPTIONS=detect_leaks=1`.

* `tenfold-tests` (all 508 tests, including the 15 `pathological_` ones at 1/5 size) and `xvfb-run -a tenfold-ui-tests` (467
  tests, including the 5 `pathological_ui` ones at 1/12 size): 0 failed, no ASan / UBSan / LSan report.
* Driver, `--scale asan` (about 1/20 of the full sizes), every scenario and the `mixed` / `truncated` clangd steps: all pass, no
  sanitizer report on any stderr. RSS is not judged (shadow memory and the quarantine inflate it; only the 4 GiB rule applies):

| Scenario | bench s | repl s | editor s | peak RSS MiB |
|---|---|---|---|---|
| tree (5000 files) | 10.0 | 3.4 | - | 491 |
| bigfiles | 10.7 | 14.7 | 32.9 | 488 |
| nesting | 6.1 | 4.9 | 11.5 | 456 |
| encodings | 8.7 | 13.2 | - | 253 |
| fsodd (as `nobody`) | 1.7 | 0.5 | - | 106 |
| truncated (also with clangd) | 1.9 | 0.4 | - | 279 |
| mixed (also with clangd) | 0.4 | 0.3 | - | 104 |
| churn | - | 9.4 | - | 304 |

* The only sanitizer report during the work was in the new test itself (a `std::string(literal, n)` with a miscounted length in
  `pathological_binary_and_oddly_encoded_files`, a global-buffer-overflow read); fixed in the test.
* Unrelated flake seen in the full UI suite under load (Release and not touched by this work):
  `term_scrollback_wheel_shift_page_keys_and_snap_to_bottom` (`tests/ui/test_w8f_term.cpp:812`, a pty echo race: 3 of 5 runs
  fail while the machine is loaded, 0 of 1 when it is quiet). It passed in the ASan run.

## Corpus findings: templates and overloads (W9b)

Six corpus projects under `tests/corpus/`, 649 expectations in total (Release, 5+ consecutive runs of
`tenfold-corpus-tests`, no flake; once under ASan+UBSan). All use the standard library (libstdc++) except
`templates-config`, which is `.sln`-mode and std-free.

| project | what it stresses | expectations (pass / xfail) | fast/final agreement | runtime (Release, loaded 4-core box) |
|---|---|---|---|---|
| `overloads` | overloads by arity, cv/ref qualifiers, default args, conversions, `0`/`nullptr`, initializer lists, ctor overloads, using-declarations, ADL, hidden friends, virtual overloads, function-vs-template ties, unsaved `edit`s | 132 (121 / 11) | 2/76 (3 %) | 9 - 14 s |
| `operators` | member / non-member / friend operators, `()` `[]` `->` `<=>`, conversion operators, `++`/`--` pre/post, C++20 rewritten `!=`/reversed `==`, enum operators, user-defined literals | 87 (80 / 7) | 2/54 (4 %) | 8 - 11 s |
| `templates-basic` | class/function templates, out-of-line members in `.tpp`/`.inl`, dependent names (`typename`/`template`), variadics and folds, CTAD and guides, alias/variable templates, template template params, `a<b<c>>`, chained calls, `auto`/`decltype`/`consteval` | 163 (148 / 15) | 39/94 (41 %) | 24 - 33 s |
| `templates-specialisation` | full/partial specialisation of class and variable templates, function specialisation vs overload, explicit instantiation, member specialisation, SFINAE/`enable_if`, tag dispatch, CRTP, virtuals of a class template | 126 (117 / 9) | 23/70 (33 %) | 13 - 19 s |
| `templates-cxx20` | concepts, requires-clauses, abbreviated templates, constraint-ordered overloads, constrained partial specialisation and members, NTTP class types, `constinit`, aggregate CTAD, designated initialisers, `using enum` | 104 (98 / 6) | 35/58 (60 %) | 20 - 28 s |
| `templates-config` | `config Debug\|x64` / `Release\|x64` selecting different specialisations and constrained overloads (std-free, `.sln`) | 37 (35 / 2) | 1/21 (5 %) | 1 - 2.5 s |

Every expectation kind is used: `def`, `decl`, `typedef`, `impl`, `refs` (`=` and `>=`), `hover`, `sym` (`->`, `∋`),
`rename`, `fast`, `xfail`, `config`, `open`, `edit`.

Fast/final agreement is low by construction here, not because the fast tier is mostly wrong: the runner compares the
`file:line` sets, so a fast answer that lists declaration **and** definition (or every overload) never equals the single
location clangd returns. In `operators` 52 of the 54 queries are on operator tokens, for which the fast tier has no
answer. In `templates-specialisation` 42 of the 47 disagreements are "fast is a superset of final", 5 are real
differences (explicit specialisations: the lexer index only knows the primary template's members).

### Findings

"Who" = the layer at fault. *fast tier* = the lexer index (`FastIndex`) / its use for the approximate answer; *tenfold* =
tenfold's handling (documents opened, waiting, position mapping); *clangd* = clangd's own answer (reproduced by
talking to clangd 22.1.1 directly, `--background-index`, no tenfold in between); *harness* = the corpus runner.

| # | expectation (project:anchor) | expected | actual | tier | who | minimal repro |
|---|---|---|---|---|---|---|
| F1 (not fixed: overloads by argument type, specialisations) | `fast def use_ar1`, `use_who_r`, `use_conv_char`, `use_pick_p`, `use_b_f_dbl`, `use_c_f` (overloads); `use_kind_ptr`, `use_name_int`, `use_circle_area` (specialisation); `use_describe_num` (cxx20); `use_vmax_base` (basic) | the one overload / the one `b::f` | every function with that word (up to 9 results), qualifier and arguments ignored | fast | fast tier (by design: word lookup) | `int area(); int area(int);` ... `area(3)` |
| F2 (fixed) | `fast def use_draw_sprite`, `fast sym "draw"` (overloads) | the inline hidden friend `friend int draw(Sprite s) {...}` | not in the index: the fast tier answers with the *other* `draw` | fast | fast tier (lexer index does not record friend function definitions inside a class) | `struct S { friend int draw(S) { return 0; } };` then `sym "draw"` |
| F3 (fixed) | `fast def use_Stack`, `use_Pair`, `use_Wrap` (basic) | the class | class **plus** its constructor/destructor declarations (same word) | fast | fast tier | `template<class T> struct Pair { Pair(T); };` click `Pair` |
| F4 (by design) | `fast def use_plus`, `use_lit_km` (operators) | the operator function | nothing (an operator token or a literal suffix is not a word) | fast | fast tier (inherent) | `a + b` |
| F5 | agreement in `templates-specialisation` (5 queries), e.g. `use_swap_int`, `use_convert_bool`, `use_id_v_int` | the explicit specialisation | the primary template | fast | fast tier | `template<> void swap_it<int>(int&, int&)` |
| F6 (fixed, W9-FIX-B) | `templates-basic`: after `stack.tpp` is open (`templates-cxx20` has the same shape with `functions.tpp`, whose lines were moved after everything else), `def use_data_in_top`, `use_made_in_push`, `use_push_in_all`, `refs Stack_top_decl`, `refs Stack_map_decl`, `rename Stack_push_all_decl`, `rename Stack_top_c_def` | the definitions in `stack.tpp` are found, references and renames include `stack.tpp` | clangd parses the non-self-contained `stack.tpp` **standalone** (names undeclared): `def` answers the `namespace tb` line, the open document replaces the index shard so every reference inside it disappears, `rename` skips it or is refused ("symbol is not a supported kind") | final | **clangd** (non-self-contained header parsed alone) **and tenfold** (it `didOpen`s the file as soon as it is navigated to, or a rename edits it, with no compile context for it) | `stack.h` ends with `#include "stack.tpp"` and `stack.tpp` has no includes; jump to a definition in `stack.tpp`, then find references of a member declared in `stack.h` |
| F7 (fixed, W9-FIX-B) | `templates-basic` run flake (seen once in 6 runs before the corpus was reordered): `refs Stack_map_decl` lost `stack.tpp` | stable answer | answer depends on whether clangd finished the standalone parse of the just-opened `stack.tpp` when the next query arrives | final | tenfold/harness: `ensureReady` waits only for the *anchor's* file, not for documents a rename or navigation opened as a side effect; underlying cause F6 | `rename` of a member defined in a `.tpp`, then `refs` of another member |
| F8 | `refs describe_num / describe_sized`, `rename describe_num` (cxx20); `decl use_cost_dbl`, `refs cost_small` (config); `refs Holder_num_kind`, `refs Holder_ptr_kind` (cxx20) | one overload / one partial specialisation | all constrained overloads and all constrained partial specialisations are **one symbol**: refs include the calls of the other overloads, rename renames them, `declaration` returns the first | final | **clangd** (clang USR ignores type-constraints and requires-clauses) | `int f(const Numeric auto&); int f(const Container auto&);` rename the first |
| F9 | `refs vec_plus`, `refs vec_plus_def` (operators); `refs draw_sprite_friend` (overloads) | decl + definition + the call | friend declaration and the namespace-scope definition are different symbols; the call is attached to one only; the ADL call of a hidden friend is not a reference at all | final | **clangd** (index) | `struct V { friend V operator+(V,V); }; V operator+(V a, V b){...}` refs on the friend |
| F10 | `refs vec_cmp` (`<`, `>=`), `refs id_eq` (`!=`, reversed `==`) (operators) | the rewritten uses are references to `operator<=>` / `operator==` | not reported (go to definition on them works) | final | **clangd** (references of rewritten operator expressions) | `a != b` with only `operator==` |
| F11 | `impl speak_int`, `impl use_speak_int` (overloads) | all overriders (`Dog`, `Puppy`) | only the direct overrider (`Dog`) | final | **clangd** (relation index is one level) | `A::f` virtual, `B : A` overrides, `C : B` overrides |
| F12 | `impl Task_run`, `impl use_task_run`, `impl use_task_name` (specialisation) | overriders in classes derived from `Task<int>` | nothing (empty, no error) | final | **clangd** (no override relations for members of class templates) | `template<class T> struct Task { virtual T run()=0; }; struct I : Task<int> { int run() override; };` |
| F13 | `def inst_convert_float`, `def inst_checked_int`, `refs Registry` (specialisation) | the template; references include the names in explicit instantiations | no result for `template std::string convert<float>(float);`; `template class Registry<int>;` is not listed as a reference | final | **clangd** | `template int f<int>(int);` |
| F14 | `refs args_param`, `refs Ts_list` (basic) | `sizeof...(args)` / `sizeof...(Ts)` are references | missing (def at the same spot works) | final | **clangd** | `template<class... Ts> int n(Ts... a){ return sizeof...(a); }` refs of `a` |
| F15 | `def dep_rebind_type` (basic) | `Box<T>::Rebind<int>::type` -> `Rebind::type` | none | final | **clangd** (resolves one dependent step only) | `typename Box<T>::template Rebind<int>::type r;` |
| F16 | `refs lit_km` (operators) | reference at the literal | reported at the `_km` suffix inside the literal (for int literals at the literal start) | final | **clangd** (inconsistent) + **harness** (an anchor cannot point into a token) | `2.5_km` |
| F17 | unary `~` (operators, `use_flags_not`) | the query is made at `~` | the runner skips `~` for every `~identifier` (meant for destructors) and queries `f` of `~f` | harness | **harness** (`Runner.cpp`: `'~'` is skipped unconditionally); workaround `~(f)` | `/*@a*/~f` with a `def` expectation |

Behaviour that is not a bug but pinned down so it cannot regress silently (all pass):

* A click on `Bag(3)`, `Bag{3}`, `new Bag(7)` answers the *class*; the chosen constructor is only visible in `refs`
  (clangd and tenfold agree; the variable name of `Bag b(3);` answers the variable).
* `def`/`decl` through a using-declaration (`b::f(1)`, `Derived::f`) answer the target **and** the using-declaration
  (accepted as `A | B` in the corpus; open question 2).
* `sym` returns the definition of a function when there is one (`area` -> `arity.cpp`, not `arity.h`).
* `rename` of one overload renames only that overload (plain functions, member functions): correct. For a dependent
  call such as `half(v)` inside a template, clangd renames it with the overload that is being renamed (judgement, listed).
* `hover` on a type name in `Pair p{1, 2.0};` shows the template, not `Pair<int, double>`; the deduced type is on the
  variable (corpus hovers the variable).

### Non-determinism and how the corpora avoid it

* Cross-TU answers (definitions in `src/*.cpp`, uses in `src/main.cpp`) come from clangd's background index; the runner
  waits for it once per project. No flake was left in 6 consecutive Release runs per project.
* F6/F7 made the order of lines matter in `templates-basic` and `templates-cxx20`: every check that needs `stack.tpp`'s
  references comes **before** any line that navigates into or renames inside the non-self-contained file; then an
  explicit `open include/tb/stack.tpp` (the runner waits until clangd has parsed it) precedes the `xfail`s that document
  the damage. `queue.h`/`queue.inl` is the same shape with a self-contained `.inl` and keeps working when open.
  Do not reorder those blocks.

### Open questions

1. Should `fast def` on a class name include constructors (F3) and should a fast `def` offer both declaration and
   definition? The corpus expects the single class / the definition-or-declaration set, so these show as `xfail`.
2. Using-declarations: is "target + the using-declaration" the answer tenfold wants to show (F-note above)? Accepted
   as either today.
3. Dependent calls (`describe(v)` in a template): the corpus accepts any candidate of the overload set.
4. `Bag(3)` landing on the class: should tenfold map a construct expression to the selected constructor? clangd never
   does; only `refs` shows it. (Answered by W10-CHOOSE: tenfold asks clangd for the constructor at the `(` and offers it.)
5. F6 (done by W9-FIX-B, translation-unit context): what should tenfold do about `.tpp`/`.inl`? Options: do not `didOpen` such files into clangd without a
   containing-TU context, or give them a compile command that includes the owning header first.

### Assumptions

* clangd 22.1.1 from `PATH`, libstdc++ 13 headers, `-std=c++20`.
* Where the C++ rules leave a choice (specialisation vs primary on a type name, rewritten candidates for implicit
  `operator==`, which declaration a using-declaration shows) the corpus line carries a comment and accepts what clangd
  shows today when it is a defensible reading; those are the lines marked "judgement".
* Sources are never reformatted (`tests/corpus/.clang-format` disables it).

## Fuzzing (W9h)

Mutation fuzzing of every parser that reads untrusted or user-edited input, under gcc 13.3 AddressSanitizer + UBSan
(`-fno-sanitize-recover=undefined`, `-O1`), with edge-coverage feedback (`-fsanitize-coverage=trace-pc`). The fuzzer, the
21 harnesses of the time (24 today: `scopescan`, `ppcond` and `linemap` came later) and how to run them are described in `docs/TESTING.md` ("Fuzzing"); the regression inputs are in
`tests/fuzz/regressions/`.

Machine: 4 cores shared with three other agents (load average 10-15 during the runs), so the execution rates are lower
than on an idle machine and the time per target is shorter than planned (5-25 min instead of 10-20). Two budget runs
per target for the most complex ones (`extract`, `msbuild`, `jsonrpc`, `vtscreen`: seeds 1 and 2), one for the others.
Every budget run below was made on the fixed code; the crashes were found in short exploratory runs (5-20 s each) while
the harnesses were being written, which is why the table can show a finding next to a long run without one.

### Results per target

"Edges" is the number of distinct code edges (hashed into a 64 K map) the corpus reached; slow inputs are the ones that
stayed over 100x the median time per KiB of their size class in two re-runs (see `docs/TESTING.md`).

| target | fuzzing time | executions | edges reached | slow inputs reported | crashes / hangs found |
|---|---|---|---|---|---|
| `lexer` | 10 min | 700,052 | 564 | 0 | none |
| `extract` | 25 min (2 runs) | 320,499 | 2807 | 0 | 1 invariant |
| `indexstore` | 5 min | 1,537,547 | 961 | 0 | none |
| `xml` | 8 min | 1,717,833 | 1028 | 0 | 1 exception |
| `msbuild` | 25 min (2 runs) | 129,515 | 6496 | 0 | 1 exception |
| `msbuildexpr` | 8 min | 1,000,920 | 1753 | 0 | none |
| `compdb` | 5 min | 439,030 | 2967 | 0 | 1 exception |
| `compdbcmd` | 5 min | 302,966 | 1462 | 0 | none |
| `jsonrpc` | 20 min (2 runs) | 151,512 | 4022 | 420 | 1 exception (reader thread) |
| `buildoutput` | 8 min | 911,334 | 1627 | 0 | 1 invariant |
| `gitstatus` | 5 min | 1,354,736 | 319 | 0 | none |
| `p4output` | 5 min | 764,934 | 857 | 0 | none |
| `linediff` | 8 min | 1,072,139 | 920 | 0 | none |
| `vtscreen` | 20 min (2 runs) | 483,045 | 1885 | 2935 | 1 slow loop |
| `search` | 7 min | 22,395 | 2147 | 12 | 1 hang + 1 super-linear |
| `statefiles` | 6 min | 189,930 | 5602 | 0 | 1 exception |
| `keybindings` | 6 min | 103,118 | 2277 | 0 | none |
| `settings` | 6 min | 93,481 | 1151 | 0 | none |
| `theme` | 6 min | 111,413 | 1038 | 0 | none |
| `layout` | 10 min | 157,051 | 4242 | 0 | none |
| `explorer` | 8 min | 359,816 | 3144 | 45 | 1 hang |
| **total** | **206 min** | **11,923,266** | | | |

The "found" column counts what the whole campaign found in that target (all fixed, see below); no budget run ended in
a crash or hang. Per-run output lines (`RESULT ...`) come from `tests/fuzz/run_all.sh`; the 30-second CI run of
all 21 targets (seed 1) passed on the final code (1.34 million executions in total, about 11 minutes).

### Findings

Eleven root causes, all fixed (the unfixed observations are listed after the table). Each has a regression input under
`tests/fuzz/regressions/<target>/`, replayed by `fuzz_regression_inputs_replay` (`tenfold-tests`) /
`fuzz_ui_regression_inputs_replay` (`tenfold-ui-tests`).

| # | target | what happened | root cause | fix | regression input |
|---|---|---|---|---|---|
| 1 | `extract` | a symbol with an empty name was indexed (`a a::,`): the fast index held a nameless entry that shows up in the persisted store and in symbol lists | `Extractor::emit` accepted the empty name of a declarator cut off after `::` | `emit` ignores an empty name (`src/semantic/FastIndex.cpp`) | `extract/empty-name-after-scope.txt` |
| 2 | `xml` | `std::invalid_argument` / `std::out_of_range` out of `xml::parse` for `&#zz;`, `&#99999999999;`; `ent[1]` read past the entity for `&#;`. `.vcxproj` / `.props` / `.filters` are parsed with it, so one bad character reference in a project file aborted solution loading | `std::stoul` on unvalidated text | `std::from_chars`, a reference that is not a number or above U+10FFFF stays as written (`src/semantic/XmlLite.cpp`) | `xml/bad-character-references.xml` |
| 3 | `compdb` | `nlohmann::json::type_error` out of `CompileDatabase::load` for `[1,2,3]`, `{"directory":5}`, `"arguments":[1]`, `"command":[]` | `value()` / `get<std::string>()` throw on a wrong type | entries that are not objects and fields of the wrong type are skipped (`src/semantic/CompileCommands.cpp`) | `compdb/entries-not-objects.json`, `compdb/field-types.json` |
| 4 | `buildoutput` | a line count above `INT_MAX` in a `In file included from a.h:3:` chain became negative (`4294967295` -> `-1`) | `std::atoi` wraps; every other number in `ErrorParser` goes through the saturating `parseInt` | the include chain uses `parseInt` (`src/build/ErrorParser.cpp`) | `buildoutput/include-chain-line-overflow.txt` |
| 5 | `jsonrpc` | `nlohmann::json::type_error` thrown on the **reader thread** (process terminate) by a response whose `"error"` is not an object (`{"id":1,"error":0}`), a request whose `"method"` is not a string, or any handler that reads a malformed message with `value()` / `get<>()` (the LspClient converters do) | `dispatch()` assumed the shapes; nothing caught exceptions on the reader thread | `error` / `method` are read leniently; `processBuffer` catches an exception of a handler, reports it to the trace callback and drops the message (`src/lsp/JsonRpc.cpp`) | `jsonrpc/error-not-an-object.bin`, `jsonrpc/garbled-frames-and-fields.bin` |
| 5b | `jsonrpc` | found while writing the harness, not by a run: `Content-Length: -5` was read by `strtoul` as 2^64-5, a value near 2^64 made `bodyStart + len` wrap, and a huge length made the reader wait for bytes that never come | unchecked `strtoul` | digits only, `strtoull` with ERANGE check, claims above 1 GiB are a garbled header and are skipped (`src/lsp/JsonRpc.cpp`) | same file as 5 |
| 6 | `statefiles` | `type_error` out of the `Editor` constructor for a `.tenfold/workspace.json` with `"sourceExtensions":[0]`, `"cppStandard":5`, `"runCMake":"x"` ... | `get<std::vector<std::string>>()` / `value(key, default)` throw on a wrong type | wrong-typed values are ignored (`src/workspace/Workspace.cpp`) | `statefiles/workspace-wrong-types.json` |
| 7 | `msbuild` | `std::filesystem::filesystem_error` ("File name too long") out of `projectsInSolution` for a `.sln` project path with a component over 255 bytes | `fs::weakly_canonical(p)` without an `error_code` | the path is kept as written when it cannot be canonicalised (`src/semantic/CompileCommands.cpp`) | `msbuild/sln-project-path-too-long.txt`, `msbuild/sln-project-name-component-over-255.txt` |
| 8 | `vtscreen` | `CSI 999999999 I` (tab) and `CSI 999999999 Z` (back tab) spin ~1e6 iterations each (parameters are clamped at 10^6): 100 such sequences (2.4 KB) take 4 s under ASan, 12 sequences ~25 ms optimised | the loops ran to the count after the cursor had reached the last / first column | stop at the edge (`src/term/VtScreen.cpp`) | `vtscreen/tab-and-backtab-with-huge-count.bin` |
| 9 | `search` | **hang** (> 10 s) in the include / exclude glob matcher: `**/**/**/**/**/**/**/**/**/**/**/**/x` against a 30-segment path; and two cubic cases (`**/*.c\R` against a 1.4 KB path took 560 ms; a 100-character `*?*?*?...` pattern against a 700-character text took 316 million instructions) | `globRec` retried every later `*` group at every text position (exponential in the number of groups) and `pathMatchesGlob` repeats the whole match for every directory prefix | memo of failed (pattern, text) states, plus the text range a `*` / `**` group has already failed on, only for patterns with two or more `*` groups so the common `*.cpp` filter does not allocate; the harness compares the result with the old matcher on small inputs (`src/search/TextSearch.cpp`; `fuzz_glob_matcher_agrees_with_the_reference` compares 300000 random small cases with the old matcher) | `search/glob-many-doublestar-groups.txt`, `search/glob-doublestar-quadratic-per-prefix.txt` |
| 10 | `explorer` | **hang**: a `.vcxproj` item `<ClCompile Include="/**/x.cpp"/>` (an absolute `**` wildcard) made the explorer walk the whole file system (`/proc`, `/sys` ...) without limit when the project was opened | `globExpand` bounded the number of results but not the number of directories read | a budget of 100000 directories and 1.5 s per pattern (`src/ui/features/WorkspaceExplorer.cpp`) | `explorer/absolute-doublestar-include-walks-the-file-system.txt` (replay takes ~1.5 s: it runs into the time budget on purpose) |

### Not fixed

* **`vtscreen`: `CSI n b` (REP) is a 10^4x amplifier.** A 11-byte `ESC [ 999999 b` writes up to `kMaxRep` = 100000 cells
  (about 1.8 ms optimised, ~40 ms under ASan); a stream of them costs ~160 us per byte, so a program flooding the
  terminal panel with REP can freeze the UI thread. Bounded per sequence, so not a hang, and it is the documented limit
  (`kMaxRep`); not touched. Reproducer: the byte string `x` followed by `ESC [ 999999 b` repeated
  1000 times, fed to `VtScreen` (as a `vtscreen` input: `\x03\x04\x02\x01` + that). Suggested follow-up (W9i, terminal flooding): clamp the repeat to
  `rows * cols` per sequence.
* **`linediff` at the default cap** costs ~100 ms under ASan for two 3000-line edits that share nothing (Myers is
  O(cap^2); `docs/BENCHMARKS.md` has the optimised figure). The harness uses caps of 1-300 to keep runs fast; the default
  cap is only reached by the unit tests and by the benchmark.
* `WorkspaceExplorer`'s `wildMatch` (file-name wildcards inside an Include pattern) backtracks exponentially for names
  like `aaaa...` against `*a*a*a*...b`; the harness cannot reach it (it needs a directory holding such names) and a
  file name is at most 255 bytes, so it was left alone.
* Not covered by any harness: `LspClient`'s response handlers as a whole (only its `*FromJson` converters, inside the
  `jsonrpc` handlers), `SemanticEngine` (JSON from clangd beyond the converters), git / p4 command execution.
* **`pathMatchesGlob` is O(depth x pattern x length)** by construction (it retries the match for every directory prefix of
  the path, to cover "the pattern names a directory above the file"); a 1.8 KB path of 600 one-letter segments costs
  ~15 ms optimised. Real paths have a few dozen segments; left alone.
* `jsonrpc` reports 420 slow inputs (up to 1.6 s for 3 KB) that are not the parser: the harness writes the replies to a
  real child process (`sh -c 'cat >/dev/null'`) and the machine was at load average 15; replaying them alone takes
  15 ms.

## Generated codebases (W9g)

`tools/stress/gen_codebase.py` writes C/C++ projects of 300 to 20,000 files together with the anchors and the
`tenfold-expect.txt` (the generator knows every definition and use it wrote; nothing is taken from real projects). How it
works, profiles, variants and the commands are in `docs/TESTING.md` ("Generated codebases (W9g)"). This section has the
results of running them: three small projects are committed (`tests/corpus/gen-game`, `gen-service`, `gen-mixed`), medium
(~3k files) and large (~20k files) are generated into a temp dir by `tools/stress/run_generated.sh`, run with
`tenfold-cli corpus --json`, measured (`/proc` sampling of the peak RSS of tenfold and of clangd) and deleted; the nightly
workflow `.github/workflows/stress.yml` repeats them. Machine: 4 cores shared with other jobs (load 1 to 16 during these
runs), Linux, Release build of `a9f2396` + this branch, clangd 22.1.1, generator seed 1. **Latencies and times are from a
loaded shared machine; compare trends, not absolutes.** The fast/final agreement column is the W9g-time figure (about 31 to 46 %); the fast tier has improved since (the committed `gen-*` corpora are at 91 to 94 % after W11-FAST-SCOPE, "W11-FAST-SCOPE" above).

### Results

| run | files | LOC | start+fast index (s) | first TU (s) | clangd index (s) | total (s) | pass / fail / xfail / xpass | fast==final | peak RSS tenfold / clangd (MiB) |
|---|---:|---:|---:|---:|---:|---:|---|---:|---|
| gen-game (small) | 310 | 10244 | 0.1 | 0.0 | 0.4 | 5.3 | 513 / 0 / 45 / 0 | 100/255 (39%) | 13 / 350 |
| gen-service (small) | 324 | 11590 | 0.1 | 0.0 | 0.4 | 5.5 | 498 / 0 / 54 / 0 | 88/258 (34%) | 13 / 192 |
| gen-mixed (small) | 314 | 9998 | 0.1 | 0.0 | 0.8 | 5.6 | 511 / 0 / 33 / 0 | 104/243 (43%) | 12 / 357 |
| game-cmake-medium | 3068 | 115145 | 0.3 | 0.2 | 18.6 | 50.9 | 1342 / 9 / 159 / 0 | 236/702 (34%) | 44 / 871 |
| service-cmake-medium | 3066 | 109994 | 0.3 | 0.3 | 20.8 | 900.0 | 1285 / 21 / 159 / 0 | 218/686 (32%) | 43 / 856 |
| service-vs-medium | 3066 | 109994 | 0.3 | 0.2 | 8.2 | 900.0 | 1286 / 20 / 159 / 0 | 218/686 (32%) | 44 / 668 |
| lib-cmake-medium | 3073 | 99092 | 0.3 | 7.3 | 11.6 | 37.1 | 1299 / 1 / 135 / 0 | 226/669 (34%) | 43 / 776 |
| c-cmake-medium | 3072 | 106464 | 0.2 | 0.2 | 6.6 | 18.0 | 950 / 0 / 58 / 0 | 216/470 (46%) | 34 / 546 |
| mixed-cmake-medium | 3082 | 107601 | 0.2 | 0.1 | 7.6 | 27.9 | 1379 / 3 / 137 / 0 | 259/700 (37%) | 40 / 845 |
| game-cmake-large (run 3, quiet) | 20380 | 964136 | 1.0 | 0.3 | -0.0 | 761.2 | 2178 / 220 / 298 / 0 | 399/1282 (31%) | 224 / 2370 |
| game-cmake-large (run 1) | 20380 | 964136 | 1.3 | 0.2 | -0.0 | 2218.8 | 2350 / 42 / 298 / 6 | 393/1282 (31%) | 214 / 2465 |
| game-cmake-large (run 2, loaded) | 20380 | 964136 | 5.1 | 0.9 | -0.0 | 1136.6 | 2115 / 283 / 298 / 0 | 401/1282 (31%) | 217 / 2350 |

(`first TU` = first document opened until clangd finished parsing it; `clangd index` = the runner's one wait for clangd's
background index, `-` when it did not finish within the runner's 120 s cap; `total` includes waits and time-outs: the two
`service-*-medium` rows and the large rows end in 30 s per-query time-outs, see G-2 and G-5. `fast==final` is the share of
`def`/`decl` queries on which the fast tier's file:line set equals clangd's: low by design, the generated names are
deliberately ambiguous. Peak RSS of the runner (the editor model plus the fast index) stays far below the 500 MB budget of
§1.7; clangd peaks at about 0.3 GB (300 files), 0.6 to 0.9 GB (3k files) and 2.4 GB (20k files).)

Latency in ms, median / 95th percentile (n queries). The fast tier is isolated (`fastOnlyQueries`), so its numbers are the
lexer index alone:

| run | def fast | def final | refs final | hover final | sym fast | sym final | rename final |
|---|---:|---:|---:|---:|---:|---:|---:|
| gen-game (small) | 0.03 / 0.31 (189) | 10 / 22 (189) | 5.67 / 9.75 (99) | 1.19 / 1.83 (85) | 0.12 / 3.72 (31) | 0.97 / 3.92 (39) | 11 / 117 (6) |
| gen-service (small) | 0.03 / 0.12 (193) | 10 / 25 (193) | 5.48 / 10 (102) | 1.16 / 1.66 (86) | 0.14 / 2.65 (27) | 0.95 / 3.73 (35) | 7.90 / 11 (6) |
| gen-mixed (small) | 0.03 / 0.15 (179) | 10 / 30 (179) | 5.57 / 11 (95) | 1.13 / 3.60 (79) | 0.16 / 3.62 (33) | 1.05 / 4.19 (49) | 11 / 72 (6) |
| game-cmake-medium | 0.04 / 0.43 (524) | 19 / 58 (524) | 7.06 / 24 (272) | 1.23 / 6.53 (234) | 1.36 / 11 (76) | 4.66 / 12 (82) | 17 / 748 (16) |
| service-cmake-medium | 0.04 / 0.47 (513) | 21 / 69 (513) | 6.42 / 20 (270) | 1.20 / 4.74 (229) | 4.00 / 17 (71) | 4.53 / 11 (89) | 30000 / 30000 (15) |
| service-vs-medium | 0.05 / 0.48 (513) | 14 / 26 (513) | 6.92 / 12 (270) | 1.43 / 2.85 (229) | 1.17 / 6.44 (71) | 1.92 / 4.93 (89) | 30000 / 30000 (15) |
| lib-cmake-medium | 0.04 / 0.32 (500) | 18 / 52 (500) | 6.80 / 18 (269) | 1.30 / 4.57 (222) | 2.78 / 19 (75) | 4.52 / 11 (96) | 23 / 658 (16) |
| c-cmake-medium | 0.03 / 0.29 (343) | 12 / 27 (343) | 6.44 / 14 (175) | 1.36 / 3.69 (144) | 1.10 / 3.71 (64) | 1.47 / 5.02 (77) | 11 / 74 (16) |
| mixed-cmake-medium | 0.03 / 0.32 (528) | 11 / 24 (528) | 6.20 / 11 (280) | 1.26 / 3.21 (236) | 1.52 / 8.46 (87) | 1.79 / 5.72 (103) | 11 / 279 (16) |
| game-cmake-large (run 3, quiet) | 0.14 / 5.12 (959) | 66 / 208 (959) | 14 / 62 (509) | 2.47 / 20 (427) | 40 / 72 (128) | 24 / 88 (149) | 950 / 30005 (30) |
| game-cmake-large (run 1) | 0.14 / 4.55 (959) | 22 / 75 (959) | 8.29 / 20 (509) | 1.70 / 5.46 (427) | 55 / 101 (128) | 5.00 / 25 (149) | 30000 / 30004 (30) |
| game-cmake-large (run 2, loaded) | 0.15 / 3.35 (959) | 44 / 170 (959) | 11 / 50 (509) | 2.02 / 12 (427) | 89 / 273 (128) | 22 / 159 (149) | 18352 / 30002 (30) |

Against the budgets of §1.7: startup of the fast index 0.5 s to 2.4 s for 20k files / 1M LOC (budget: interactive in
3 s), editor process 217 MiB (≤ 500 MB). **Fast symbol search is at the 50 ms budget at 20k files** (G-6): median 40 / 55 / 89 ms, p95 72 / 101 / 273 ms
in the three runs (quiet, first, loaded); at 3k files it is 1 to 4 ms.

Pass / fail breakdown (everything a run reports as FAIL, nothing else; `xfail` lines are the known fast-tier and clangd
limits, all with a reason in the expectation file):

| size | profile | FAIL (cause) |
|---|---|---|
| small | game, service, mixed | none (committed; 5 consecutive Release runs of `tenfold-corpus-tests` per project, PASS + XFAIL only; ASan+UBSan `tenfold-cli corpus` once per project, no sanitizer report) |
| medium | game 9, service 9 + 12 (rename time-outs), lib 1, c 0, mixed 3 | all `impl` ones are G-1; the service ones G-2 |
| large | game 42 (run 1), 220 (run 3, quiet machine), 283 (run 2, other jobs running) | run 1: 22 rename time-outs (G-2), 10 `refs` and 2 `def` against a background index that was still being built (G-5), 8 G-1 (+ 6 XPASS of a generator rule that was dropped afterwards, G-4); runs 2 and 3: 169 to 210 incomplete `refs`, 16 to 34 incomplete `def`, 10 to 11 G-1, 13 to 15 rename time-outs, a few `sym` / `impl` (all G-5) |

What held: at 3k files **950 to 1,379 expectations pass per profile** in 18 to 51 s (the profiles without the G-2 time-outs), exact `refs`
sets of functions, methods, static members, fields, classes (with forward declarations and out-of-line `Cls::` qualifiers),
constants, enumerators, aliases, macros and macro-declared functions across modules; definition / declaration / type
definition through base references, overloads (exact literal arguments) and template members; `hover` text; workspace
symbols for unique names; direct `impl` sets; the unsaved-edit blocks; the vcxproj route gave the same counts as the
compile-database route (service: 1,286 vs 1,285 pass). No `fast` expectation failed or XPASSed anywhere at any size:
the generator's census of names predicts the fast tier exactly (`fast` on unique names always passes, `xfail fast` on
ambiguous names never starts passing).

### Findings

Tier: fast = lexer index, final = clangd answer. "Clean clangd" = a raw LSP session (clangd 22.1.1, background index
settled), used to separate clangd from tenfold's handling.

| # | expectation | expected | actual | tier | fault | minimal repro |
|---|---|---|---|---|---|---|
| G-1 | `impl` of a virtual function / a base class (small, medium, large: 1 to 12 lines per run) | the overrider's out-of-line definition (`d.cpp`), the subclass's definition | the in-class declaration (`d.h`) or a forward declaration of the subclass, once an earlier query (go to definition, also a `fast def` navigation) has opened the header; deterministic for a given sequence, the answer flips with the sequence | final | clangd (the dynamic index of an open file lacks the definition and shadows the background index); tenfold exposes it because navigation opens the target file | `b.h: struct B{virtual int f() const=0;};` `d.h: struct D:B{int f() const override;};` `d.cpp: int D::f() const{return 1;}`; implementation at `B::f` gives `d.cpp:2`; after `didOpen d.h` it gives `d.h:2` |
| G-2 | the `rename` block (16 renames, 3k files) | each rename answered in ms | after 0 to 7 successful renames one `textDocument/rename` gets no answer for ever; the next 10 to 15 expectations (renames, then the edit block and everything else if it comes first) all time out at 30 s | final | clangd (request received, never answered, process idle) triggered by tenfold's rename sequence | medium `service` seed 1 (`tools/stress/gen_codebase.py /tmp/p --profile service --size medium`, `tenfold-cli corpus /tmp/p`): hung in 4 of 6 runs (cmake and vs), `game` 0 of 2 with the renames last and 3 of 3 with the renames in the middle (an earlier generator order), `lib`, `c`, `mixed` 0 of 2 each; large: 13 to 22 of 30 renames time out. Evidence of one hang: gdb: the runner waits in `Editor::waitFor`, every engine thread idle, clangd 0 CPU with all AST workers waiting; `strace -e write`: the 6th `textDocument/rename` (id 1336, `route.h`) was written at 21:30:46 after the previous rename's `didOpen`x6 / `didChange` / `didSave` / `didClose` / `didChangeWatchedFiles` burst, clangd's only output afterwards was `fileStatus: file is queued` for the next file; not reproduced with a single rename (10 ms in a raw session, or alone in tenfold), nor under `strace -e read,write` (slower) |
| G-3 | `refs` of a `static inline` function defined in a C header (mixed profile) | the uses from the C and from the C++ translation units | only the uses of the language that was indexed first (two USRs) | final | clang / clangd | `h.h: static inline int f(int v){return v;}` included from `a.c` (uses `f`) and `b.cpp` (uses `f`); references of `f` in `a.c` lacks `b.cpp`. Kept `xfail` in `gen-mixed` |
| G-4 | `refs` / `rename` of a `static` function in a C file (c, mixed; the generator's `static int pick(int)` helpers) | uses in its own file only | also the uses of a same-named static function in a same-named file of another directory (`d1/s.c`, `d2/s.c`) | final | clangd (the USR of an internal-linkage symbol holds only the base name of the file; with C++ the namespace separates them) | `d1/s.c` and `d2/s.c` each with `static int pick(int)` and a call; references of `pick` in `d1/s.c` returns both files |
| G-5 | any cross-TU answer at 20k files (large) | the same as at 3k files | `refs` missing uses (10 to 210 lines), `def` returning text-match fallbacks (`file:line:col` of unrelated identifiers, 2 to 34 lines), 5 `impl` empty | final | harness: `Runner` waits at most 120 s for clangd's background index (`min(120000, remaining)`), 7k translation units need longer (RSS 2.4 GB at the end), so the first final queries are asked against a partial index; the failure count follows the load of the machine (42, 283 in two runs) | `tools/stress/run_generated.sh large game`; `clangdIndex` is `-1` in the JSON. A `--index-wait` option (or a `waitForClangdIndex` that follows clangd's progress to the end) would make the large run meaningful |
| G-6 | `fast sym` at 20k files | ≤ 50 ms (§1.7) | median 40 to 89 ms, p95 72 to 273 ms (three runs: quiet, first, loaded) | fast | tenfold (the fuzzy search grows with the symbol count: 1 to 4 ms at 3k files) | any 20k-file project, `sym "name"`; `tools/stress/run_generated.sh large game` |

Everything below is not a wrong answer but shaped the ground truth (found while the generator disagreed with the tools,
each case checked against clang or clangd and fixed in the generator):

* A base class's injected class name hides a namespace-scope class of the same name inside the derived class, so a
  pointer member of a forward-declared `Info` was a pointer to the base `Info`: the generator now qualifies member types.
* Hover does not show the doc comment of a declaration that has an anchor comment between the doc comment and the name,
  and omits `provided by "header"` when the use is in the header's own source file: neither is asserted.
* Workspace symbol ranking is fuzzy and case-insensitive (`sym "helper"` ranked the class `Helper` first): `sym` lines are
  written only for names unique ignoring case.
* `impl` of a virtual function answers one level only (W9d finding 10): every deeper hierarchy gives a pair `impl >= direct`
  (passes) and `xfail impl = all` (fails). Class `impl` answers the forward declaration instead of the class when the header
  with the forward declaration is open (G-1).
* `rename` and `refs` need exact anchor lists: a class with explicit constructors / a destructor is never tracked, a
  `usr`-clashing static function (C) is not renamed.

### Open questions and assumptions

* Should `implementations` answer the definition even when the declaring header is open (G-1)? Assumed yes (what a person
  who opens `d.h` and asks for the implementations of `B::f` expects is a place with a body).
* Which side should absorb G-2? The engine could stop opening documents for rename edits (apply them on disk) or time the
  requests out and re-sync; clangd should answer or fail the request.
* The committed corpora use `--impl-position first` so that every `impl` line is asked before any navigation has opened a
  header; the consequence is that their `fast` checks all run after the TU is ready (`fastChecksBeforeTu` counts only
  the cold ones in the nightly runs, where the generator's default `last` keeps them cold).
* Assumed: a reasonable CI budget is ~60 s per small project (these take 5 to 15 s) and the nightly job can end in
  time-outs (G-2, G-5) without being red; it only fails when a project cannot be generated or set up.
* Not done: Windows / macOS runs of the stress workflow, a `lib` / `c` small project in the repo (limit of three), the
  `.sln` variant for `lib`, `c` and `mixed`, `--check` (clang syntax check) on the 20k-file output (done up to 3k files for
  game, mixed, lib; small for all profiles).
