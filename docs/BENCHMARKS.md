# Benchmarks

Reproducible numbers for the performance budgets in `IMPLEMENTATION_PLAN.md` section 1.7.

> **PROVISIONAL: measured under load.** The numbers below were taken on a 4-core machine that was
> running about six other agents at the same time (load average 6 to 12). Treat them as upper bounds.
> The orchestrator will re-run `tools/bench.sh` on a quiet machine and replace them.

## How to run

```sh
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release   # bench.sh does this if needed
tools/bench.sh                                                # examples/sample_project (on a temp copy)
tools/bench.sh --large /path/to/godot --file scene/3d/node_3d.cpp:171:45 --sym Node3D --sym Vector3
```

* `tenfold-cli bench <workspace> [--file <rel path>:<line>:<col>] [--sym <query>]... [--runs N] [--no-clangd]
  [--wait-index <ms>] [--json]` runs one process and prints a table, or one JSON object with `--json`.
  `--runs` is the number of repeats inside the process for the warm `def` and `sym` medians (default 5).
* `tools/bench_workspace.py` runs the CLI `--runs` times cold, then `--runs` times warm, and prints a Markdown
  table (median and min per phase, budget and pass/fail where a budget exists). Cold deletes `<ws>/.tenfold`
  and `<ws>/.cache/clangd` before each run; warm leaves everything from the previous run in place.
  `--json-out file` keeps every raw run.
* JSON shape: `{"schema":1,"meta":{...strings},"counts":{...numbers},"phases":{...milliseconds}}`. The
  aggregator treats every numeric key under `phases` generically, so new phases (A4 `fastIndexWarm`, A6
  `findInFiles`, ...) are added by calling `rep.phase("name", ms)` in `runBench`; no script change is needed.
  To give a new phase a budget column, add it to `BUDGETS` in `bench_workspace.py`.

### Timing asserts in the test suites (A11)

Wall-clock budgets are flaky on loaded or shared machines, so unit tests never assert on elapsed time unless
that is the test's point. `renderer_perf_visible_region` (`tests/ui/test_renderer.cpp`, the 2 ms CPU per frame
target) always prints its `PERF:` lines but only fails the build when `TENFOLD_ENFORCE_PERF=1` is set in the
environment; use that on a quiet machine (`TENFOLD_ENFORCE_PERF=1 xvfb-run -a ./build-ui/tenfold-ui-tests
renderer_perf`). Without it, a missed budget prints `PERF: over the 2 ms budget (not enforced ...)`.

### Phases

| Key | Meaning |
|---|---|
| `scanMs` | Workspace scan (from the engine's own log line, whole milliseconds). |
| `compdbMs` | Compile-command discovery/generation: end of scan to "compile database" ready. In heuristic mode this includes waiting for the fast index (include-directory inference needs it). |
| `clangdStartMs` | clangd launch to `initialized`. |
| `startReturnMs` | `Editor::start()` returned: scan + compdb + clangd start. Used as "startup to interactive". |
| `fastIndexDoneMs` / `fastIndexMs` | Launch to fast index complete / the indexing wall time itself. |
| `tuParsedMs` / `tuParsedAfterOpenMs` | Launch (or file open) to clangd `fileStatus` idle for the chosen file. |
| `defFirstMs` | First `def` after the TU is parsed, to the final callback (authoritative with clangd). `defFirstApproxMs` appears only when an approximate callback came first. |
| `defWarmMs` | Median of N further `def` calls. |
| `symFast[q]Ms` | Fast-tier symbol search (`approximateSymbols`), median of N. |
| `symAuth[q]Ms` | `findSymbols` to the authoritative callback, median of N. Depends on clangd's background index; `counts.clangdIndexPercent` records how far it had got. |
| `peakRssMiB` | Peak RSS (`VmHWM`) of the editor process, clangd excluded. Best effort off Linux (`getrusage`; `-1` on Windows). |

Not measured: keystroke-to-paint is n/a until B1/B2 (no UI yet).

## Fetching the big workspaces

```sh
git clone --depth 1 https://github.com/godotengine/godot.git      # about 7.8k C/C++ files at the time of writing
git clone --depth 1 https://github.com/llvm/llvm-project.git      # much larger; use llvm/ or clang/ as the workspace
```

Godot: workspace root is the clone; use `scene/3d/node_3d.cpp:171:45`, which is
`data.local_transform = data.parent->get_global_transform() * get_transform();` with the caret inside
`get_global_transform`. `data.parent` is a `Node3D *` reached through a member, so the fast index cannot
resolve it from the name alone and the answer needs clangd's semantic resolution. Suggested queries:
`--sym Node3D --sym get_global_transform --sym Vector3`. For LLVM, pick any call through a pointer or
`auto` variable in e.g. `llvm/lib/IR/Instruction.cpp` and pass its position the same way.

Neither tree ships a `compile_commands.json`, so the numbers are for heuristic compile flags (see the Godot
note in the plan: generated headers are missing and some types degrade).

## Results

Machine: 4 cores, Linux, GCC Release build, clangd 22.1.1. Medians of 3 runs, each in a separate process.

### `examples/sample_project` (3 files)

`tools/bench.sh --runs 3`, file `src/main.cpp:12:17` (`s->area()`), `--wait-index 20000`.

| Phase | Cold median | Warm median | Budget cold / warm | Cold | Warm |
|---|---:|---:|---|---|---|
| `scanMs` | 0 | 0 | | | |
| `compdbMs` | 6.6 | 0.3 | | | |
| `clangdStartMs` | 127 | 145 | | | |
| `startReturnMs` | 134 | 145 | 3000 / 1000 ms | pass | pass |
| `fastIndexMs` | 4.1 | 1.7 | | | |
| `tuParsedMs` | 1026 | 1105 | | | |
| `defFirstMs` | 4.3 | 0.54 | | | |
| `defWarmMs` | 0.27 | 0.94 | | | |
| `symFast[Shape]Ms` | < 0.01 | < 0.01 | 50 / 50 ms | pass | pass |
| `symAuth[Shape]Ms` | 0.59 | 0.20 | | | |
| `peakRssMiB` | 5.4 | 5.4 | 500 / 500 MiB | pass | pass |

### Godot (`/home/user/bench/godot`, shallow clone)

This clone has 7772 C/C++ files by the count used in the task brief; the engine scan counts 3568 sources +
4759 headers = 8327 indexed code files (the plan's historical table used a different, 8755-file tree).
507,098 symbols. File `scene/3d/node_3d.cpp:171:45`, queries `Node3D`, `get_global_transform`, `Vector3`.
Measured under heavy load (load average up to 12 on 4 cores), so provisional.

| Phase | Cold median | Warm median | Budget cold / warm | Cold | Warm |
|---|---:|---:|---|---|---|
| `scanMs` | 50 | 55 | | | |
| `compdbMs` | 4225 | 79 | | | |
| `clangdStartMs` | 94 | 239 | | | |
| `startReturnMs` | 4369 | 390 | 3000 / 1000 ms | FAIL | pass |
| `fastIndexDoneMs` | 4369 | 3374 | | | |
| `fastIndexMs` | 3225 | 3277 | | | |
| `tuParsedMs` | 6030 | 4884 | | | |
| `tuParsedAfterOpenMs` | 1661 | 1862 | | | |
| `defFirstMs` | 7.3 | 5.1 | | | |
| `defWarmMs` | 0.17 | 0.14 | | | |
| `symFast[Node3D]Ms` | 134 | 129 | 50 / 50 ms | FAIL | FAIL |
| `symFast[get_global_transform]Ms` | 86 | 94 | 50 / 50 ms | FAIL | FAIL |
| `symFast[Vector3]Ms` | 74 | 101 | 50 / 50 ms | FAIL | FAIL |
| `symAuth[Node3D]Ms` | 117 | 136 | | | |
| `peakRssMiB` | 336 | 208 | 500 / 500 MiB | pass | pass |

Reading the table:

* Cold start misses the 3 s budget because heuristic compile flags wait for the whole fast index (3.2 s here,
  against 1.4 s on the quiet machine in the historical table). The warm start passes only because it does not
  wait for the index; the index still takes about 3.3 s in the background until A4 persists it.
* Fast-tier symbol search misses 50 ms, but the machine was saturated (clangd background indexing plus other
  agents; the fast index itself had finished); the min column in the raw output is 45 to 74 ms. Re-measure on a quiet machine before acting.
* clangd's background index was at 0 percent when `sym` ran, so `symAuth` numbers cover a partial index.
  `def` after the TU parse is single-digit milliseconds.

## After Wave 1 (A1-A8 merged), light load

Re-run by the orchestrator on integrated `main` (07af945) with one agent idle in the background
(load average ~1.5-2 on 4 cores). Includes A4's persisted fast index (warm runs load
`.tenfold/fastindex.bin`). Supersedes the provisional numbers above.

`tools/bench.sh`

Workspace: `/tmp/tmp.6iWoXqeJN6/sample_project`  
clangd: clangd clangd version 22.1.1 linux x86_64-unknown-linux-gnu; compile flags: heuristic; file: `src/main.cpp:12:17`; runs: 3 cold, 3 warm (median / min)

Counts: sourceFiles=2, headerFiles=1, fastFiles=3, fastSymbols=43, compdbEntries=2, clangdIndexPercent=100

| Phase | Cold median | Cold min | Warm median | Warm min | Budget cold / warm | Cold | Warm |
|---|---:|---:|---:|---:|---|---|---|
| `scanMs` (ms) | 0.00 | 0.00 | 0.00 | 0.00 |  |  |  |
| `compdbMs` (ms) | 1.43 | 1.08 | 0.40 | 0.38 |  |  |  |
| `clangdStartMs` (ms) | 55.3 | 51.9 | 54.2 | 52.1 |  |  |  |
| `startReturnMs` (ms) | 56.8 | 53.0 | 54.6 | 52.5 | 3000 / 1000 ms | pass | pass |
| `fastIndexDoneMs` (ms) | 56.8 | 53.0 | 54.6 | 52.5 |  |  |  |
| `fastIndexMs` (ms) | 0.44 | 0.44 | 1.28 | 0.33 |  |  |  |
| `tuParsedMs` (ms) | 771.0 | 703.8 | 630.0 | 590.8 |  |  |  |
| `tuParsedAfterOpenMs` (ms) | 714.2 | 630.1 | 577.5 | 536.2 |  |  |  |
| `defFirstMs` (ms) | 0.49 | 0.41 | 0.62 | 0.59 |  |  |  |
| `defWarmMs` (ms) | 0.13 | 0.12 | 0.37 | 0.35 |  |  |  |
| `clangdIndexWaitMs` (ms) | 153.3 | 134.3 | 0.00 | 0.00 |  |  |  |
| `symFast[Shape]Ms` (ms) | 0.00 | 0.00 | 0.00 | 0.00 | 50 / 50 ms | pass | pass |
| `symAuth[Shape]Ms` (ms) | 0.41 | 0.37 | 0.53 | 0.39 |  |  |  |
| `symFast[area]Ms` (ms) | 0.00 | 0.00 | 0.00 | 0.00 | 50 / 50 ms | pass | pass |
| `symAuth[area]Ms` (ms) | 0.47 | 0.42 | 0.45 | 0.32 |  |  |  |
| `peakRssMiB` (MiB) | 5.99 | 5.97 | 5.98 | 5.95 | 500 / 500 MiB | pass | pass |

`tools/bench.sh --large /home/user/bench/godot --file scene/3d/node_3d.cpp:171:45 --sym Node3D --sym get_global_transform --sym Vector3`

Workspace: `/home/user/bench/godot`  
clangd: clangd clangd version 22.1.1 linux x86_64-unknown-linux-gnu; compile flags: heuristic; file: `scene/3d/node_3d.cpp:171:45`; runs: 3 cold, 3 warm (median / min)

Counts: sourceFiles=3568, headerFiles=4759, fastFiles=8327, fastSymbols=507098, compdbEntries=3568, clangdIndexPercent=0

| Phase | Cold median | Cold min | Warm median | Warm min | Budget cold / warm | Cold | Warm |
|---|---:|---:|---:|---:|---|---|---|
| `scanMs` (ms) | 51.0 | 40.0 | 39.0 | 38.0 |  |  |  |
| `compdbMs` (ms) | 1791.6 | 1783.4 | 97.7 | 90.5 |  |  |  |
| `clangdStartMs` (ms) | 96.9 | 72.9 | 56.8 | 56.0 |  |  |  |
| `startReturnMs` (ms) | 1930.6 | 1914.3 | 328.0 | 327.0 | 3000 / 1000 ms | pass | pass |
| `fastIndexDoneMs` (ms) | 1930.6 | 1914.3 | 332.7 | 330.3 |  |  |  |
| `fastIndexMs` (ms) | 1175.7 | 1149.6 | 161.3 | 154.8 |  |  |  |
| `tuParsedMs` (ms) | 2888.1 | 2777.3 | 1165.5 | 1124.1 |  |  |  |
| `tuParsedAfterOpenMs` (ms) | 940.2 | 863.0 | 793.8 | 793.3 |  |  |  |
| `defFirstMs` (ms) | 4.47 | 0.54 | 0.42 | 0.41 |  |  |  |
| `defWarmMs` (ms) | 0.24 | 0.20 | 0.16 | 0.13 |  |  |  |
| `symFast[Node3D]Ms` (ms) | 47.8 | 43.0 | 46.6 | 40.6 | 50 / 50 ms | pass | pass |
| `symAuth[Node3D]Ms` (ms) | 42.4 | 39.6 | 48.0 | 47.5 |  |  |  |
| `symFast[get_global_transform]Ms` (ms) | 30.3 | 28.5 | 32.0 | 27.6 | 50 / 50 ms | pass | pass |
| `symAuth[get_global_transform]Ms` (ms) | 29.2 | 29.0 | 30.3 | 28.2 |  |  |  |
| `symFast[Vector3]Ms` (ms) | 33.7 | 33.1 | 34.4 | 33.1 | 50 / 50 ms | pass | pass |
| `symAuth[Vector3]Ms` (ms) | 40.1 | 39.9 | 38.7 | 33.4 |  |  |  |
| `peakRssMiB` (MiB) | 374.2 | 363.4 | 200.5 | 200.5 | 500 / 500 MiB | pass | pass |

Find in files (A6, measured by the A6 agent under load, Godot, 8327 files, warm page cache):
literal `Vector2` 124 ms, case-insensitive 163 ms, whole word 94 ms, regex 96 ms (target < 400 ms).

## A10: fast-tier symbol search within budget, off the UI thread

**What changed.** `FastIndex::search` used to score every name, push every match (hundreds of thousands for a
short query) and `stable_sort` all of them. It now keeps only the best `limit` candidates in per-thread bounded
heaps (merged at the end, same total order: score desc, name asc, insertion order), skips names that lack one of
the query's characters (32-bit case-insensitive mask per name), prunes names that cannot beat the current worst
kept score, and scans in parallel (atomic block counter, up to `hardware_concurrency` threads, max 8). Qualified
queries (`a::b`) scan file by file with a per-symbol mask of `scope::name`. `SemanticEngine::workspaceSymbols` now
computes the fast tier on its own symbol thread (a newer query cancels an older one between scan blocks), so
`Editor::findSymbols` returns immediately. See `docs/ARCHITECTURE.md` (thread-safety table).

**Measurement conditions (read this first).** Release, `/home/user/bench/godot` (8327 files, 507,098 symbols),
4 cores that were **shared with other agents** the whole time (load average 3-7). Numbers are best-of-5 (in-process
harness) or median-of-5 (`tenfold-cli bench`, `--no-clangd`, 5 cold + 5 warm runs, 5 inner runs). Because the
machine was oversubscribed the multi-threaded "after" wall times are pessimistic; the CPU-time column is the
total CPU burnt by the query (all threads), so `CPU / 4` is a rough estimate for an idle 4-core machine.

### Per query, in-process (`FastIndex::search(q, 200)` on the persisted Godot index, load ~3.5)

| Query | Before wall (ms) | After wall (ms) | After CPU (ms) | Est. idle 4 cores (ms) |
|---|---:|---:|---:|---:|
| `n` | 209.4 | 11.2 | 26.2 | ~7 |
| `no` | 121.6 | 8.9 | 21.7 | ~5 |
| `nod` | 70.2 | 5.0 | 14.0 | ~4 |
| `Node3D` | 43.6 | 1.2 | 3.2 | ~1 |
| `get_transform` | 39.6 | 1.0 | 1.7 | <1 |
| `set_pos` | 50.1 | 3.1 | 8.6 | ~2 |
| `RenderingServer` | 43.4 | 1.5 | 2.8 | <1 |
| `zzzzz` (no match) | 33.2 | 1.6 | 3.1 | <1 |
| `Node3D::get` (qualified) | 256.7 | 5.4 | 8.7 | ~2 |
| `geo::Tr` (qualified) | 262.7 | 12.2 | 21.6 | ~5 |

### Per query through `tenfold-cli bench --no-clangd` (`symFast[...]Ms`, warm median, load 6-7 for "after")

| Query | Before (ms) | After (ms) | `symCall` (ms): `findSymbols` returns |
|---|---:|---:|---:|
| `n` | 246.2 | 39.8 | 0.01 |
| `no` | 126.9 | 36.1 | 0.00 |
| `nod` | 74.4 | 19.3 | 0.00 |
| `Node3D` | 49.1 | 3.1 | 0.00 |
| `get_transform` | 46.4 | 4.0 | 0.00 |
| `set_pos` | 55.9 | 5.6 | 0.00 |
| `RenderingServer` | 48.2 | 4.0 | 0.00 |
| `zzzzz` | 34.7 | 4.0 | 0.00 |
| `Node3D::get` | 263.2 | 7.0 | 0.00 |
| `geo::Tr` | 279.9 | 34.2 | 0.00 |

These "after" runs happened at load average 6-7 on 4 cores (the 4.0 ms plateau is the scheduler quantum: a worker
thread that was descheduled holds the join); the in-process table above is the cleaner measurement. On a
busy machine `n`, `no` and `geo::Tr` can exceed the 20 ms goal (they stay under the 50 ms budget); on an idle
machine the CPU column says they should land at about 5-7 ms. I could not get an idle machine, so that estimate
is unverified.

### The A10 command from the task (`tools/bench.sh --large ... --sym n --sym Node3D --sym get_global_transform --sym Vector3`, clangd on, load ~4)

| Phase | Before cold / warm (median ms) | After cold / warm (median ms) |
|---|---:|---:|
| `symFast[n]Ms` | 298.2 / 243.4 | 16.0 / 16.1 |
| `symFast[Node3D]Ms` | 44.8 / 49.3 | 2.93 / 3.39 |
| `symFast[get_global_transform]Ms` | 36.0 / 38.1 | 3.98 / 2.26 |
| `symFast[Vector3]Ms` | 40.5 / 42.4 | 1.40 / 3.47 |
| `symAuth[n]Ms` (end to end through `Editor::findSymbols`) | 263.0 / 251.9 | 26.1 / 30.9 |
| `symAuth[Node3D]Ms` | 59.8 / 48.8 | 7.12 / 4.63 |
| `symCall[...]Ms` (new: time `Editor::findSymbols` takes to return) | n/a (blocked for the full `symFast` time) | 0.01-0.02 |
| `defFirstMs` (approximate definition path, unchanged: one hash lookup) | 0.59 / 0.43 | 0.47 / 0.37 |

`approximateSymbols` is still the synchronous API the `symFast` phases time, so those numbers measure the same
thing before and after; the new `symCall` / `symFirst` phases (time for `findSymbols` to return / time to its
first callback) measure the asynchronous path the UI uses.

### Index build time and memory (must not regress by more than about 5%)

| | Before | After |
|---|---:|---:|
| `FastIndex::importFiles` of the 507k-symbol Godot index, in-process, median of 5 per session, alternating sessions | 114-137 ms (avg 125) | 116-142 ms (avg 131), about +5% on average but inside the session-to-session noise (the same binary varies by +-10%) |
| `fastIndexMs` via `tenfold-cli bench --runs 7`, cold (two sessions) | 1157.6 / 1546.1 ms | 1184.5 / 1473.4 ms |
| `fastIndexMs`, warm | 196.5 / 210.4 ms | 182.8 / 188.2 ms |
| Search structures (`prepareSearch()`, runs on the index thread after `fastIndexDone`, not part of `fastIndexMs`) | none | 25-45 ms of background work (under load) |
| Index memory after warm-up (RSS delta in the in-process harness) | 43.0 MiB | 48.4 MiB (+5.4 MiB: name table 4.8 MiB + qualified masks 2 MiB) |
| Editor process peak RSS, cold (median of 7) | 365 / 392 MiB | 396 / 385 MiB (about +3%, within the session-to-session spread of 357-392 MiB) |
| Editor process peak RSS, warm (median of 7) | 150.3 / 150.9 MiB | 143.2 / 143.7 MiB (about -5%: the old search materialised every match) |

### Tests

`tests/test_symbol_search.cpp` keeps the pre-A10 algorithm as an oracle (score every name, push every match,
`stable_sort`, truncate) and checks that the new search returns exactly the same list (file, index, score) for
random queries (prefixes, substrings, subsequences, qualified, empty, whitespace, case changes, limits 1 to
1,000,000) over a 200,000-symbol synthetic index, a small tie-heavy index and the sample project, with 1 and 4
threads. Further tests cover cancellation, index mutations, concurrent searches and writers, asynchronous
delivery and superseding in the engine, `findSymbols` returning before the fast tier is done, and tie order after
a file is removed and re-added. The suite also passes under ASan+UBSan; under ThreadSanitizer the only reports
are the pre-existing `compileCommands_` race (see `docs/ARCHITECTURE.md`), none in the search or engine code.

## B2c: soft word wrap, keystroke-to-paint with wrap on

Release build (`build-ui`), llvmpipe, quiet machine (4 cores), `tenfold-ui-tests perf`, 1920x1080, DejaVu Sans Mono 15 px
(about 200 cells per row). A key event is dispatched, the Editor edits, the view syncs, then layout + paint + glyph /
rect recording run (the GL submit is not part of the number). "edit" is the key event up to the view sync (the
Editor's edit and the view's change tracking), "paint" is layout + paint + record. Two runs, the second in brackets
where it differs noticeably. Budget: 8 ms.

| Scenario (wrap ON, the new default) | edit | paint | total avg | p99 |
|---|---:|---:|---:|---:|
| `textview_perf_keystroke_to_paint_100k_lines`, 100k lines, 1 caret (the B2a test, now with wrap on) | 0.004 ms | 0.09-0.12 ms | 0.10-0.13 ms | 0.17 ms |
| same, 1000 carets | 0.25-0.32 ms | 0.30-0.39 ms | 0.55-0.70 ms | 0.9 ms |
| 100k lines, every line fits one row (`wrapview_perf_...`) | 0.004 ms | 0.09-0.11 ms | 0.10-0.11 ms | 0.15-0.38 ms |
| 100k lines, a third of them wrap to 2-3 rows | 0.004 ms | 0.13-0.19 ms | 0.14-0.20 ms | 0.20-0.82 ms |
| same, caret at line 77777 (rows above it are estimates) | 0.01 ms | 0.16-0.32 ms | 0.16-0.33 ms | 0.4 ms |
| one 50 MB line (50 MB of `tokenN ` words), caret at the start | 2.7-2.8 ms | 0.38 ms | 3.0-3.2 ms | 4.1-5.2 ms |
| same, wrap off (for reference) | 2.8-2.9 ms | 0.03 ms | 2.9 ms | 3.7 ms |
| 50 MB line, caret in the middle | 1.5 ms | 0.34-0.39 ms | 1.9-2.0 ms | 2.2-4.4 ms |
| 50 MB line, caret at the end | 0.24-0.35 ms | 0.29-0.46 ms | 0.53-0.81 ms | 0.7-1.6 ms |

* The 50 MB rows are dominated by the Editor's own cost of inserting into a 50 MB `std::string` (the "edit" column is the
  same with wrap off); wrapping itself adds about 0.35 ms of paint, which is the glyphs of about 60 rows instead of
  the few columns of one clipped row.
* First time a caret goes deep into the line, the rows before it are scanned once: 16-25 ms for the first 25 MB (caret
  in the middle), another 16-23 ms for the rest (caret at the end), so about 40 ms for the whole 50 MB. After that,
  typing re-scans only a few rows (a 3 MB line: 0.001 ms to re-find the caret's row after typing at the end).
* 50 x Up with the caret at the end of the 50 MB line, including a paint each: 0.25-0.35 ms per key.
* Under ASan + UBSan (Debug) the same tests are 10-50x slower and the budget is not asserted there; the asserts
  (`CHECK(avg < 8.0)`) only run in Release builds without sanitizers.
* Not part of the number: `TextView::positionText()` (status bar "Ln, Col") is O(column) on the caret's line, as before
  B2c, so with the caret at the end of the 50 MB line the status bar (not in these tests) pays for it.

## Historical numbers (from the plan, quiet machine, 8755-file Godot)

| Workspace | Launch to interactive | Launch to first TU parsed | `def` after parse | Fast index |
|---|---|---|---|---|
| `examples/sample_project` | 0.09 s | about 1.5 s | < 10 ms | 2 ms |
| Godot, cold | 2.4 s | 3.7 s | < 10 ms | 1.4 s, 507k symbols |
| Godot, warm | 0.2 s | 1.8 s | < 10 ms | 1.4 s |

## Budget summary (plan section 1.7)

From the post-Wave 1 run above.

| Budget | Sample | Godot |
|---|---|---|
| Startup to interactive, cold <= 3 s | pass (0.06 s) | pass (1.93 s) |
| Startup to interactive, warm <= 1 s | pass (0.05 s) | pass (0.33 s) |
| Symbol search, fast tier <= 50 ms | pass (< 0.01 ms) | pass after A10: 1-12 ms in-process on a loaded machine (was 30-250 ms; see the A10 section), `Editor::findSymbols` returns in < 0.05 ms |
| Editor memory <= 500 MB | pass (6 MiB) | pass (374 MiB cold, 200 MiB warm) |
| Keystroke-to-paint <= 8 ms | pass (B2a / B2c: 0.1-0.2 ms on a 100k-line file with wrap on, 0.5-3 ms on a 50 MB single line, see the B2c section) | not measured in the editor view |
