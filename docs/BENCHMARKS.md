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

## Historical numbers (from the plan, quiet machine, 8755-file Godot)

| Workspace | Launch to interactive | Launch to first TU parsed | `def` after parse | Fast index |
|---|---|---|---|---|
| `examples/sample_project` | 0.09 s | about 1.5 s | < 10 ms | 2 ms |
| Godot, cold | 2.4 s | 3.7 s | < 10 ms | 1.4 s, 507k symbols |
| Godot, warm | 0.2 s | 1.8 s | < 10 ms | 1.4 s |

## Budget summary (plan section 1.7)

| Budget | Sample | Godot (provisional) |
|---|---|---|
| Startup to interactive, cold <= 3 s | pass | FAIL under load (4.4 s; 2.4 s historically) |
| Startup to interactive, warm <= 1 s | pass | pass (0.39 s) |
| Symbol search, fast tier <= 50 ms | pass | FAIL under load (74 to 134 ms) |
| Editor memory <= 500 MB | pass | pass (336 MiB) |
| Keystroke-to-paint <= 8 ms | n/a until B1/B2 | n/a until B1/B2 |
