# Testing

> This file is organised as self-contained sections so that sections added by different tasks merge as a union.
> The section below is owned by W9f.

## Pathological workspaces

Stress tests for workspaces no real project looks like (plan W9f). Nothing may crash, hang, leak, or blow the memory /
latency budgets of `IMPLEMENTATION_PLAN.md` section 1.7 on them. Results of the last full run: `docs/STRESS_RESULTS.md`.

### What is generated

`tools/stress/gen_pathological.py` writes deterministic scenario workspaces (same `--seed`, `--scale` and `--knob`s give
byte-identical files; standard library only, nothing is written outside `--out`):

| Scenario | Contents |
|---|---|
| `tree` | 100k small C++ files: a flat directory of 10k files, a 60-level deep chain, a wide fan-out of 2k directories |
| `bigfiles` | a 1M-line C++ file, a 200 MB text file, single lines of 100 kB to 10 MB (expression / comment / string / no trailing newline), a minified one-line C++ file |
| `nesting` | 10k nested braces / parens / brackets / templates / namespaces / structs / lambdas / `#if`s / comments (balanced and not), 64k-char identifiers, 100k declarations in one scope, a 50k-term macro and a 50k-parameter macro, macro and include cycles |
| `encodings` | random bytes with source extensions, invalid and overlong UTF-8, UTF-16 / UTF-32 (with and without BOM), Latin-1, CP1252, NUL bytes (also an all-NUL file), CR-only, mixed line endings, line continuations at EOF, control characters, zero-width / RTL characters |
| `fsodd` | symlink loops (dir to itself, `a` <-> `b`, to the parent), symlinks to files / dirs outside the workspace, to `/`, `/proc/self`, `/dev/zero`, `/dev/urandom`; a broken link; a FIFO; `chmod 000` files and directories; names with spaces, tabs, newlines, quotes, Unicode, RTL marks, a 250-char name; a path of about 4000 bytes |
| `truncated` | random cuts of real-looking code, and files ending mid-identifier / number / string / char / raw string / comment / `#define` / `#include` / `#if` / template / class / namespace / lambda / attribute |
| `mixed` | a small valid project with a `compile_commands.json` next to broken files, to check answers stay correct |
| churn | `gen_pathological.py --churn DIR --seconds N`: keeps creating, modifying, appending, renaming, deleting files and directories, replacing a file by a directory and back, truncating, half-writing; run while an editor indexes and watches the workspace |

`--scale full` is the plan's size (the whole set is about 600 MB; the largest single scenario is `bigfiles`, 270 MB),
`--scale asan` is about 1/20th for sanitizer builds, `--scale small` is for smoke runs. `--knob name=value` overrides one
size (`gen_pathological.py --list` shows the knobs).

```sh
python3 tools/stress/gen_pathological.py --out /tmp/patho --scale full --scenario bigfiles   # just generate
```

### Full-size runs (the driver)

```sh
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build-rel tenfold-cli tenfold-tests
python3 tools/stress/run_pathological.py --build build-rel --scale full --clangd --md-out /tmp/stress.md
# sanitizer build, smaller sizes
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
ninja -C build-asan tenfold-cli tenfold-tests
python3 tools/stress/run_pathological.py --build build-asan --scale asan --timeout 900
```

For each scenario the driver generates into a fresh directory under `$TMPDIR` (`--work` to choose; deleted afterwards
unless `--keep`) and runs these steps, each under a hard timeout (`--timeout`, default 600 s; a hang kills the whole
process tree, clangd included):

* `bench`: `tenfold-cli bench --no-clangd`: workspace scan, fast index, one go-to-definition, symbol searches; the CLI
  reports its own peak RSS.
* `repl`: a scripted `tenfold-cli <ws>` session on the scenario's worst files: `open`, `goto`, `insert`, `def`, `refs`,
  `sym`, `grep`.
* `clangd`, `repl+clangd` (`--clangd`; scenarios where it is meaningful): the same with clangd.
* `editor`: `tenfold-tests pathological_full` with `TENFOLD_PATHOLOGICAL_DIR` set to the generated directory: open, scroll
  to the end, type, undo / redo, find, replace all and go to definition on the giant files (see `tests/test_pathological.cpp`).
* `churn`: a REPL session querying (`grep`, `sym`, `def`, `insert`) while `--churn` mutates its workspace.

A step fails on: a timeout (hang), a non-zero exit or a signal (crash), any ASan / UBSan / LSan report on stderr, a missing
`quit` echo, a query that timed out, or memory: the peak RSS (sampled every 50 ms from `/proc`) of the process tree above 4 GiB, or of the largest single
process (the editor, or clangd) above 10x the on-disk size of the workspace plus a 300 MiB baseline. (The tree sum is not
used for the second rule: a forked child shows its parent's pages until it execs, which double counts.) (The driver kills a process tree above 7 GiB
so a runaway cannot take the machine down.) `fsodd` runs as the unprivileged user `nobody` when started as root, since
`chmod 000` does not stop root. Exit status 1 when any step fails; `--json-out` keeps the raw numbers.

Disk: about 600 MB for `--scale full` if every scenario is kept; the driver deletes each scenario after its steps.

### Regression tests (seconds)

* `tests/test_pathological.cpp` (`tenfold-tests pathological_`): small versions of every scenario, generated into a temp
  dir at test time, plus the regressions the stress runs found (`pathological_nested_scopes_cost_linear_memory`,
  `pathological_non_regular_files_are_refused`, `pathological_large_non_code_documents_skip_the_fast_index`,
  `pathological_edits_in_large_code_documents_reindex_lazily`). `pathological_full_*` are the full-size bodies the driver
  runs; they do nothing unless `TENFOLD_PATHOLOGICAL_DIR` is set.
* `tests/ui/test_pathological_ui.cpp` (`xvfb-run -a tenfold-ui-tests pathological_ui`): the TextView on a 300k-line C++ file,
  a 100k-line text file, one-line files of 1 to 2 MB (expression, comment, string, minified C++, wide characters), binary /
  NUL / invalid UTF-8 / CR-only files and 5000-deep nesting; wrap on and off. It opens, pages down, goes to the end,
  types 60 characters, undoes, goes home. The keystroke-to-paint budget (8 ms) is a `PERF_CHECK`: printed always,
  enforced only with `TENFOLD_ENFORCE_PERF=1` (and only in an optimised, unsanitised build), as in `BENCHMARKS.md`.

```sh
./build-rel/tenfold-tests pathological_
TENFOLD_ENFORCE_PERF=1 xvfb-run -a ./build-ui/tenfold-ui-tests pathological_ui
```

### Adding a scenario

Add a function to `SCENARIOS` in `gen_pathological.py` (it gets the scenario directory, the size knobs and a seeded
`random.Random`), a plan to `PLANS` in `run_pathological.py`, and a small C++ version to `tests/test_pathological.cpp`.
