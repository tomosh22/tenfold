#Testing tenfold

Tenfold's code intelligence is judged against **corpus projects**: small C/C++ projects under `tests/corpus/<name>/`
whose sources carry *anchors* and whose `tenfold-expect.txt` states what go to definition, references, hover and the
other navigation features must answer. The runner opens the project in the real editor model (`Editor` +
`SemanticEngine` + clangd), checks the **fast tier** (the lexer index, before clangd has the translation unit) and the
**final answer** (clangd) separately, and measures. The unit tests (`tenfold-tests`) are for components; the corpus is
for end-to-end answers on awkward code.

* [Running a corpus](#running-a-corpus)
* [Writing a corpus](#writing-a-corpus)
* [The expectation file](#the-expectation-file)
* [How an expectation is judged](#how-an-expectation-is-judged)
* [Reading the report](#reading-the-report)
* [Spec decisions](#spec-decisions)

## Running a corpus

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build tenfold-cli tenfold-corpus-tests

./build/tenfold-cli corpus tests/corpus/seed                       # one project, report on stdout
./build/tenfold-cli corpus tests/corpus/seed --json seed.json      # plus the JSON report
./build/tenfold-cli corpus tests/corpus/seed --no-clangd           # fast-tier expectations only;
the rest are SKIP./ build / tenfold - cli corpus<dir> --timeout 600 --verbose-- keep #time limit(s), engine log,
    keep the scratch copy

                ./
            build / tenfold
        - corpus -
        tests #every tests / corpus /*/ (skipped without clangd)
TENFOLD_CORPUS_ONLY=seed TENFOLD_CORPUS_REPORT_DIR=reports ./build/tenfold-corpus-tests
```

`tenfold-cli corpus` exits `0` when every expectation holds (an `xfail` that still fails is fine), `1` on any `FAIL`
or `XPASS`, `2` on a format or setup error (a malformed expectation, an unknown or duplicate anchor, clangd missing
without `--no-clangd`). `tenfold-corpus-tests` runs every corpus and reports one failure per broken project; it needs
clangd on `PATH` (or bundled next to the binary) and prints `SKIPPED` without it. Environment of
`tenfold-corpus-tests`: `TENFOLD_CORPUS_ONLY=<name>`, `TENFOLD_CORPUS_REPORT_DIR=<dir>` (writes `<dir>/<name>.json`),
`TENFOLD_CORPUS_TIMEOUT=<seconds>` (per project, default 300). It is deliberately not part of `ctest`; CI runs it in
the `corpus` job and uploads the JSON reports as an artifact.

The project is **copied to a temporary directory first**. Rename, `edit` and the `.tenfold/` caches (fast index,
generated compile database, clangd logs) never touch the repository. `--keep` prints where the copy is.

## Writing a corpus

1. Make `tests/corpus/<name>/` with the sources and whatever tells tenfold how to compile them:
* a `.sln` / `.vcxproj` at the root (needed for `config`; the seed does this),
* or a `compile_commands.json` (git ignores that file name by default; `.gitignore` has an exception for
`tests/corpus/`). Use relative paths or `@CORPUS_ROOT@` in it: the runner replaces that text with the scratch copy's
path in every `compile_commands.json` it copies. `"directory": "@CORPUS_ROOT@"`, `"file": "@CORPUS_ROOT@/src/a.cpp"`.
* or nothing: tenfold then guesses include directories like it does for any folder.
2. Put **anchors** in the sources, as a comment directly before the token: `/*@name*/
                             token`.
   `int /*@area_def*/ area() const` anchors the `a` of `area`.Anchors must
                             not change what the code means.3. Write `tenfold
        - expect.txt` at the project root.4. Run `tenfold - cli corpus tests / corpus / <name>`; fix the anchors until the output is what you mean, then decide for every
   `FAIL` whether tenfold is wrong (keep it failing and file it, or mark it `xfail` with the reason) or the expectation is.

Anchor rules:

* Names are `[A-Za-z0-9_.]+` and unique across the **whole project** (every text file below the root is scanned, except
  `.tenfold/`, `.git/`, `compile_commands.json`, `tenfold-expect.txt` and binary files). A duplicate is an error that
  names both places.
* The anchor's location is the **first character of the token after the comment**, on the same line. Blanks between the
  comment and the token are skipped. Several anchors can precede one token (`/*@a*//*@b*/foo`). An anchor at the end of
  its line is an error.
* The token is the identifier that starts there, `~` plus identifier for a destructor, otherwise one punctuation
  character (`/*@plus*/+`).
* Columns are byte offsets. Only the exact pattern `/*@name*/` is an anchor; `/*@brief */` and the like are ignored.
* Queries are asked **at the first identifier character of the token** (for `~Foo`, at the `F`).
* Name anchors for what a person would click on: the use site for `def`/`decl`/`typedef`/`hover`, the declaration for
  `refs`/`impl`/`rename`, and the *target* spots for the right-hand sides.

## The expectation file

One expectation per line;
blank lines and lines starting with `#` are ignored, and `#` after the expectation starts a
trailing comment. `A`, `B`, `C` are anchors.

| line | meaning |
|---|---|
| `def A -> B` | go to definition at A lands on B. `-> B \| C` : on any of them. `-> none` : no result. |
| `decl A -> B`, `typedef A -> B` | go to declaration / type definition; same forms as `def`. |
| `impl A -> B C ...` | go to implementations returns exactly this set (`=` is the same, `>=` at least). |
| `refs A = B C D` | find references returns exactly these (`>=` at least these, `= none` empty). Include the declaration only if listed. |
| `hover A ~ "text"` | the hover text contains the text (several quoted strings: all must occur). |
| `sym "query" -> B` | workspace symbol search ranks B **first** (`-> B \| C`: first is one of them, `-> none`: empty). |
| `sym "query" ∋ B C` | B and C are **among** the results (ASCII spelling: `has`). |
| `rename A newName` + indented `  B C ...` lines | renaming A to `newName` must change the token of exactly these anchors. |
| `fast <expectation>` | judged on the fast tier only (clangd's answers held back). Not for `refs`/`rename`. |
| `xfail <expectation>  # reason` | a known wrong answer: reported `XFAIL`, not a failure; becomes `XPASS` (a failure) when it starts passing. The reason is mandatory. `fast` and `xfail` combine in either order. |
| `config Name\|Platform` | switch the active Visual Studio configuration (same call as the UI: `Editor::selectVsConfiguration`, which regenerates the compile commands and restarts clangd); applies to the lines that follow. |
| `open <file>` | open the document (clangd gets `didOpen`). |
| `edit <file>:<line>:<col> "text"` | insert text into the open document (unsaved), 1-based line and column in the document's **current** text; `\n` `\t` `\"` `\\` are escapes. Anchors in the inserted text become usable by the following lines, and anchors below the insertion move with it. |

Lines are executed **in order**; `config`, `open` and `edit` change the state for everything after them. Put `fast`
lines first if you want their latency measured cold (clangd has parsed nothing yet).

## How an expectation is judged

**Tiers.** Both tiers are queried through the same code the UI uses: `Editor::gotoDefinition`, `gotoDeclaration`,
`findReferences`, `findSymbols`, `hover`, `renameSymbol` (go to type definition and implementations have no `Editor`
wrapper yet; the runner calls `SemanticEngine::gotoTypeDefinition` / `findImplementations`, which `Editor` would call).

* The **fast tier** is isolated by a small engine hook, `SemanticEngine::Options::fastOnlyQueries` /
  `setFastOnlyQueries()`: while it is on, navigation, hover and symbol queries behave as if clangd were not running (they
  deliver only the fast index's *Approximate* answer) although clangd keeps running and parsing. So a `fast` check never
  races clangd, whatever it has parsed. The report counts how many fast checks ran before and after the file's TU was
  ready (`fastIsolation`). Fast tier answers per kind: `def`/`decl`/`typedef`/`impl` the fast index's definitions of the
  word under the caret (that is what the UI shows as approximate for all four); `hover` its "kind name (location)" text;
  `sym` its fuzzy search. `refs` and `rename` have no fast tier.
* The **final answer** is the first *Authoritative* callback (or any error). Before it the runner waits for clangd to
  have the document's TU (`fileStatus idle`) and, once per project and after every `config`, for clangd's background
  index (it resolves definitions in other TUs). The time spent waiting is not part of the latency.

**Locations.** An answer is a list of `file:line:col` positions (column = start of the name). It *lands on* anchor B
when it is in B's file and line, **and starts at B's token or inside it** (`col` in `[B.col, B.col + len)`); an answer
that is a range (document symbols) also lands on B when it **contains** B's first character.

* `def`/`decl`/`typedef` `-> B | C`: the answer must be non-empty and **every** location must land on one of the
  alternatives (two results that are both listed pass; a stray extra result fails).
* `=` / `impl ->`: every expected anchor is landed on by some result **and** every result lands on a listed anchor.
  `>=`: every expected anchor is landed on, extra results are fine.
* `sym ->`: the first result. `sym ∋`: each anchor is landed on by some result.
* `none`: the answer is empty.

**Rename.** The runner snapshots every file with anchors, calls `Editor::renameSymbol` at A, then **re-scans the
anchors** in the resulting text (documents that are open, files on disk otherwise) and compares the identifier at each
anchor: every listed anchor must now read `newName`, and no *other* anchor may have become `newName`. It then restores
the corpus (open documents get their text back, files the rename wrote and closed are rewritten byte for byte) so later
lines see the original again.

**Status.** `PASS`/`FAIL`; for `xfail` lines `XFAIL` (still wrong, fine) and `XPASS` (right now: **counts as a
failure** so the line gets promoted). `SKIP`: needs clangd and `--no-clangd` was given.

**Fast/final agreement.** At **every `def` and `decl` line** (with or without `fast`) the runner asks both tiers and
records whether they agree: the same set of `file:line`s (order, duplicates and columns ignored). The rate is a
property of the engine, not of the expectation: `xfail`, `-> none` and failing lines count too.

**Time limit.** `--timeout` (default 180 s) bounds the whole project; when it is used up the remaining lines FAIL with
"project time limit reached".

## Reading the report

One line per expectation, in order:

```
PASS  seed:34  def use_length_sq -> Vec2_length_sq_def  [final 3.2 ms]
FAIL  seed:35  decl use_length_sq -> Vec2_length_sq_decl  [final 2.9 ms]
        expected: Vec2_length_sq_decl
        actual:   include/geometry.h:20:7 [Vec2_length_sq_def]
XFAIL seed:9   xfail fast def use_length_sq -> Vec2_length_sq_def  # ...  [fast 0.1 ms]
        expected: Vec2_length_sq_def
        actual:   include/geometry.h:15:5 [Vec2_length_sq_decl], include/geometry.h:20:7 [Vec2_length_sq_def]
        known wrong answer: the lexer index cannot tell ...
```

`actual` is printed as `file:line:col` (1-based, relative to the corpus root) followed by `[anchor]` when it lands on an
anchor or `(near anchor)` for the closest anchor of that file. The last line summarises counts and the agreement.

`--json <file>` writes the same plus numbers:

| key | content |
|---|---|
| `counts` | `total pass fail xfail xpass skip` |
| `fastAgreement` | `queries`, `agree`, `rate` and every `disagreements[]` (line, query, fast answer, final answer) |
| `fastIsolation` | how the fast tier was isolated; `fastChecksBeforeTu` / `fastChecksAfterTu` |
| `latencyMs` | per `<kind>.fast` / `<kind>.final` (`def`, `decl`, `typedef`, `impl`, `refs`, `hover`, `sym`, `rename`): `n`, `median`, `p95` (nearest rank), `max` in ms |
| `timingMs` | `start` (Editor::start: scan, compile database, clangd launch), `fastIndex` (the index's own wall time), `firstTu` (first document opened until clangd finished parsing it), `clangdIndex` (first wait until clangd's background index was idle), `total` |
| `peakRssMiB` | peak resident set of the runner process (clangd is a child and not included) |
| `results[]` | per line: `line text status kind tier ms expected actual note` |

Latencies are wall-clock on whatever machine ran them; compare trends, not absolutes (see `docs/BENCHMARKS.md`).

## Spec decisions

Where `docs/IMPLEMENTATION_PLAN.md` (W9) left room, the harness does this:

1. **Result matching**: file and line equal and the answer's column inside the anchor's token (start at it or within);
   ranges also match when they contain the token start. Whole-line matching was rejected because two anchors can share
   a line.
2. **`def`/`decl`/`typedef` alternatives**: all returned locations must be among the alternatives and there must be at
   least one. "Any of" is not enough, a second stray location is a wrong answer for "lands on".
3. **`impl A -> B C`** is an exact set (same as `=`); `>=` and `=` are also accepted for `impl`.
4. **`refs`/`rename` cannot be `fast`** (no approximate tier exists); they are a parse error, not a silently skipped line.
5. **`fast` isolation** holds clangd's answers back with the `fastOnlyQueries` hook instead of racing it; `typedef`/`impl`
   `fast` lines see the fast index's definition lookup, which is what the UI shows.
6. **`xfail` needs a reason** after `#`.
7. **`sym ->`** requires the first result, `∋`/`has` membership; `-> none` means an empty result list.
8. **Edit** inserts (never replaces) and uses the document's current coordinates; anchors are re-scanned from the
   buffer after each edit, anchors introduced by `edit` text are valid in later lines.
9. **Rename** is checked by re-scanning anchors (so changed column widths do not matter), forbids unlisted anchors from
   becoming the new name, and restores the corpus afterwards.
10. **Agreement** compares `file:line` sets at every `def`/`decl` line; `rate` is `agree / queries`.
11. **Hover** is the raw text clangd returns (markdown); `~` is a plain substring test.
12. **`@CORPUS_ROOT@`** in a copied `compile_commands.json` names the scratch copy.
13. Encodings and line endings: a line ends at `\r\n`, `\n` or a lone `\r` (the scanner, `TextBuffer` and clang agree).
    Files are scanned as the UTF-8 text tenfold shows: a Latin-1 / UTF-16 file is decoded first (`core/TextEncoding.h`), so
    anchor columns are byte offsets of that text (for UTF-8 files: of the file as stored). A UTF-8 BOM stays in line 1 and
    its three bytes count in the columns of line 1 (clangd's do too). Symbolic links are copied as links and are neither
    followed nor scanned, so their targets' anchors appear once; `tests/corpus/symlinks` needs a checkout that creates
    symlinks and is skipped on Windows (marker file `tenfold-corpus-linux-only`).
14. An exception thrown by the engine while an expectation runs makes that line a `FAIL` (note `exception: ...`); the run
    and the report continue.

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

`--scale full` is the plan's size (the whole set is about 350 MB; the largest single scenario is `bigfiles`, 270 MB),
`--scale asan` is about 1/20th for sanitizer builds, `--scale small` is for smoke runs. `--knob name=value` overrides one
size (`gen_pathological.py --list` shows the knobs).

```sh
python3 tools/stress/gen_pathological.py --out /tmp/patho --scale full --scenario bigfiles   # just generate
```

### Full-size runs (the driver)

```sh
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build-rel tenfold-cli tenfold-tests
python3 tools/stress/run_pathological.py --build build-rel --scale full --clangd --md-out /tmp/stress.md
#sanitizer build, smaller sizes
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
used for the second rule: a forked child shows its parent's pages until it execs, which double counts. The second rule is
not applied to sanitizer builds (`asan` in `--build`, or `--sanitized`): shadow memory and the quarantine inflate RSS.) (The driver kills a process tree above 7 GiB
so a runaway cannot take the machine down.) `fsodd` runs as the unprivileged user `nobody` when started as root, since
`chmod 000` does not stop root. Exit status 1 when any step fails; `--json-out` keeps the raw numbers.

Disk: about 350 MB for `--scale full` if every scenario is kept; the driver deletes each scenario after its steps.

### Regression tests (seconds)

* `tests/test_pathological.cpp` (`tenfold-tests pathological_`): small versions of every scenario, generated into a temp
  dir at test time, plus the regressions the stress runs found (`pathological_nested_scopes_cost_linear_memory`,
  `pathological_non_regular_files_are_refused`, `pathological_large_non_code_documents_skip_the_fast_index`,
  `pathological_edits_in_large_code_documents_reindex_lazily`, `pathological_invalid_utf8_documents_open_and_edit_with_a_language_server`,
  `pathological_huge_source_files_are_not_indexed`, and the churn test for the watcher fix). `pathological_full_*` are the full-size bodies the driver
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
