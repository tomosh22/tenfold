# Testing tenfold: the corpus harness (W9a)

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
./build/tenfold-cli corpus tests/corpus/seed --no-clangd           # fast-tier expectations only; the rest are SKIP
./build/tenfold-cli corpus <dir> --timeout 600 --verbose --keep    # time limit (s), engine log, keep the scratch copy

./build/tenfold-corpus-tests                                       # every tests/corpus/*/ (skipped without clangd)
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
2. Put **anchors** in the sources, as a comment directly before the token: `/*@name*/token`.
   `int /*@area_def*/area() const` anchors the `a` of `area`. Anchors must not change what the code means.
3. Write `tenfold-expect.txt` at the project root.
4. Run `tenfold-cli corpus tests/corpus/<name>`; fix the anchors until the output is what you mean, then decide for every
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

One expectation per line; blank lines and lines starting with `#` are ignored, and `#` after the expectation starts a
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
13. Not handled yet: a UTF-8 BOM on the first line shifts the anchors of line 1 by three bytes in the scan but not
    necessarily in the document (found by the format corpora when they add such a file).

## Generated codebases (W9g)

`tools/stress/gen_codebase.py` writes realistic C/C++ projects of 300 to 20,000 files **together with their answers**:
it knows every definition and every use it generated, so it emits the anchors and the `tenfold-expect.txt` itself. Nothing
is fetched or copied from real projects; the output is a pure function of `(profile, size, seed, variant, samples)`
(byte-identical for equal arguments; a private splitmix64 generator, no `random`, no hash or set order).

```sh
python3 tools/stress/gen_codebase.py /tmp/p --profile game --size small --seed 1 [--variant cmake|vs] [--samples N] \
        [--impl-position first|last] [--stats stats.json] [--check]
./build/tenfold-cli corpus /tmp/p --json /tmp/p.json --timeout 600

tools/stress/run_generated.sh medium [game service lib c mixed]    # generate, run, measure RSS, delete, summary.md
python3 tools/stress/report_table.py reports/ > summary.md           # Markdown tables from runner JSON (+ .meta.json)
python3 -m unittest discover -s tools/stress -p 'test_gen_codebase.py'    # determinism, anchors, project files (2 s)
TENFOLD_GEN_CHECK=1 python3 -m unittest ...                         # also compiles every TU with clang -fsyntax-only
```

* **Sizes**: `small` ~300 files / 10k LOC (committed), `medium` ~3,000 files / 120k LOC, `large` ~20,000 files / 1M LOC
  (100 MB, 7,000 translation units), or a number of files. `--check` compiles every translation unit and every header on
  its own with `clang++ -fsyntax-only` (all five profiles are clean at small size; the generated code is valid C++17 / C11).
* **Profiles**: `game` (math, render, physics, entities: class hierarchies with virtuals, math templates, tables),
  `service` (layered business code: interfaces and implementations, nested `Impl`/`Node` types, enums, logging macros),
  `lib` (template-heavy: class templates with specialisations, function templates, aliases, constants), `c` (a C11 project:
  prefixed names, structs with function-pointer-free "methods", enums, macros, `static` helpers named `helper`/`clamp`,
  `extern "C"` guards), `mixed` (C and C++ modules together; C++ modules call C headers).
  Every profile mixes: modules in folders with public (`include/<module>/`) and private (`src/*_priv.h`) headers, an `app/`
  layer calling several modules, a namespace per module (`gx::render`), classes with fields, static members, virtuals and
  override chains across modules, overloaded free functions, templates (class, function, an explicit specialisation),
  enums (scoped and plain), `constexpr` constants, `typedef`/`using` aliases, macros (object-like, function-like and a macro
  that declares functions), forward declarations used through pointers, deep include chains (5 to 12 headers), long
  generated tables (data arrays plus up to 120 functions and a function-pointer table), comments and doc comments, and
  many short names reused across modules (`get`, `size`, `Impl`, `Node`, `update`, ...) so that name-based guesses are wrong.
* **Variants**: `cmake` writes a `compile_commands.json` with `@CORPUS_ROOT@` (include directories are the module and its
  transitive dependencies; no build is run). `vs` writes `gen<profile>.sln` and one `.vcxproj` per module (+ `app`), with
  `$(SolutionDir)modules\<dep>\include` include directories and Debug|x64 / Release|x64. Both are standard-library free:
  `mstd/mstd.h` is the generator's own mini std (`mstd::vector`, `pair`, `min`, `max`).
* **How the ground truth is made.** Every place where the generator writes a declared name goes through one function that
  leaves a marker for `(entity, role)` (`decl`, `def`, `use`, `fwd`, `var`). After generation a deterministic sample
  (`--samples` entities per category: free functions, methods, static members, virtuals, classes, constants, enums,
  enumerators, aliases, macros, macro-declared functions, template members, table and chain functions, C structs, ...) is
  *tracked*: its markers become `/*@name*/` anchors, all other markers vanish. So `refs` sets are exact (a set bigger than 40
  uses is capped and becomes `>=` on the anchored sample). Per tracked entity the generator writes, as applicable:
  `def`/`decl` from a use (definition: the out-of-line definition if there is one, else the declaration), `refs`, `hover`
  (kind, return type, first parameter, type or header taken from clangd's format), `sym "name"` (only when the name is
  unique in the project, also case-insensitively), `typedef` from a local variable to its class, `impl` over hierarchies
  (all overriders / subclasses; direct ones as `>=` and the full transitive set as `xfail`, W9d finding 10), `rename`
  (small symbols only) and `open`/`edit` blocks that insert a call into an open file and ask `def`/`decl`/`refs` at the new
  anchor.
* **`fast` and `xfail fast`** come from the generator's own census of names (every function, method, class, field,
  enumerator, constructor, destructor, ... it declared): `fast def`/`fast sym`/`fast hover` where the word has exactly one
  definition, `xfail fast def` with the count of definitions where it has more (the lexer index has no scope), macros
  always `xfail fast` (not indexed) and macro-declared functions `xfail fast` (names in macro arguments).
* **Placement and stability.** `impl` answers depend on which files are open in the editor (clangd's dynamic index of an
  open header shadows the background index: the implementation lands on the declaration or the forward declaration instead
  of the definition, see STRESS_RESULTS G-1). The generator knows which `impl` lines have such a second spelling and puts
  them first (`--impl-position first`, every fast check then runs after the TU is ready; used for the committed corpora so
  they are deterministic) or last (default, the nightly runs keep the fast checks cold). Renames come last (G-2).
* **Anchors that would change meaning are not written**: no anchors inside templates' dependent parts beyond the names, no
  anchors on the `Name::` of constructors, destructors or classes with explicit special members are expected as exact
  `refs` (such classes are never tracked for `refs`), the doc comment of a declaration is not asserted in `hover` (the anchor
  comment between the doc comment and the name keeps clangd from attaching it), `static inline` functions of C headers used
  from both C and C++ translation units are `xfail refs` (two USRs, G-3).
* **Committed corpora**: `tests/corpus/gen-game` (game, cmake, seed 1), `gen-service` (service, vs, seed 2) and `gen-mixed`
  (mixed C and C++, cmake, seed 3), each ~310 files, ~0.9 MB, ~560 expectations, ~12 s. They run in the `corpus` job of
  `ci.yml` like every `tests/corpus/*/`. Regenerate with `tools/stress/regen_committed.sh build/release/tenfold-cli`: it
  generates, runs, keeps the engine's known wrong answers as `xfail ... # reason` (`xfail_from_report.py`) and runs
  again. The ground truth stays the generator's; an `xfail` that starts passing is an `XPASS` and fails the corpus.
* **On demand**: `run_generated.sh` (generate into a temp dir, `tenfold-cli corpus --json`, sample `/proc` for the peak RSS of
  tenfold and clangd, delete the data) and `.github/workflows/stress.yml` (nightly and `workflow_dispatch`, medium and
  large, uploads `stress-reports-*` with the JSON reports, `*.meta.json` and `summary.md`; never blocks a push). The result
  tables are in `docs/STRESS_RESULTS.md` ("Generated codebases").
* **When an expectation FAILs**: decide whether the generator's ground truth is wrong (fix the generator, add the case to the
  generator tests) or the engine / clangd is (keep the expectation, mark it `xfail` with the reason and record a finding in
  `docs/STRESS_RESULTS.md`). Do not "fix" the engine from a generated corpus without a minimal repro.
