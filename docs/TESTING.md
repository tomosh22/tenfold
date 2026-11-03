# Testing tenfold

## Fuzzing

W9h. Every parser that reads untrusted or user-edited text is mutation-fuzzed under AddressSanitizer + UBSan. The
fuzzer lives in `tests/fuzz/`; every input that ever crashed, hung, threw, or broke an invariant is kept in
`tests/fuzz/regressions/<target>/` and replayed by the unit tests on every run (a few milliseconds each).

* [Targets](#targets)
* [Running the fuzzer](#running-the-fuzzer)
* [A finding: minimise, fix, keep the input](#a-finding-minimise-fix-keep-the-input)
* [Writing a harness](#writing-a-harness)
* [libFuzzer](#libfuzzer)
* [CI](#ci)
* [Design notes](#design-notes)

### Targets

One harness per parser, a function `void run(const uint8_t*, size_t)` that must neither crash, hang nor throw and may
check invariants of the parser's result (`FUZZ_REQUIRE`).

| target        | code under test                                                                                        | invariants checked                                           |
|---------------|--------------------------------------------------------------------------------------------------------|--------------------------------------------------------------|
| `lexer`       | `CppLexer` (fast lexer)                                                                                | tokens are views into the input, in order, non-empty         |
| `extract`     | `extractSymbols` (fast symbol extractor)                                                               | lines/columns in range, names non-empty, deterministic       |
| `indexstore`  | `FastIndexStore::deserialize` (persisted fast index; header size/hash/fingerprint repaired first)      | accepted stores survive a round trip                         |
| `xml`         | `xml::parse` (XmlLite)                                                                                 | finite tree                                                  |
| `msbuild`     | `.vcxproj` + imported `.props` + `Directory.Build.props` + `.sln` + `.filters` through `fromVcxproj`, `fromSolution`, `readSolution` | none beyond "no crash / throw / hang"         |
| `msbuildexpr` | `evaluateCondition`, `expandProperties` (conditions, `$(..)`, property functions)                      |                                                              |
| `compdb`      | `CompileDatabase::load` / `save` (compile_commands.json)                                               | save-then-load keeps the arguments                           |
| `compdbcmd`   | the command-line splitter behind `"command"` entries                                                   |                                                              |
| `jsonrpc`     | `JsonRpcConnection::processBuffer`: Content-Length framing, partial / garbled frames, request / notification / response dispatch, LspClient converters inside handlers | the reader thread survives |
| `buildoutput` | `stripAnsiEscapes` + `ErrorParser` (MSVC / GCC / Clang / make / include chains)                        | line/column non-negative                                     |
| `gitstatus`   | `GitProvider::parseStatusV2` (`git status --porcelain=v2 -z`)                                          |                                                              |
| `p4output`    | `PerforceProvider::parseTagged` / `classify` / `parseFilelog` (`p4 -ztag`)                             |                                                              |
| `linediff`    | `splitLines`, `diffLines`, `hunkBaseLines`, `revertHunkEdit`                                           | hunks ordered, in range, untouched gaps equal; applying the hunks to the base gives the live text |
| `vtscreen`    | `VtScreen` terminal parser, with resizes and scrollback changes between chunks                         | cursor and scroll region in range, rows have `cols` cells, wide-cell pairs consistent |
| `search`      | find-in-files: literal / whole word / PCRE2 regex search over in-memory text, include / exclude globs, `globMatch`, `pathMatchesGlob`, `replaceMatchesInText` | counts consistent |
| `statefiles`  | `.tenfold/workspace.json`, `breakpoints.json`, `bookmarks.json` readers (`Workspace::loadSettings`, `Breakpoints::load`, `Bookmarks::load`) | what loads, saves and loads again |
| `keybindings` | 10x `KeyMappings.10x_settings` parser (`parseKeyBindings`, `parseKeyChords`) and the chord state machine | chords print and parse back                                 |
| `settings`    | tenfold settings files (`parseSettings`, `Settings::loadTexts`, value parsers)                         |                                                              |
| `theme`       | theme files (`parseTheme`)                                                                             |                                                              |
| `layout`      | `Workbench::restoreLayoutFromJson` (`.tenfold/layout.json`), real `Editor` + `Workbench`, null renderer (UI build only) | accepted layouts save and restore again |
| `explorer`    | `WorkspaceExplorer::readProject` (+ `.filters`) and `buildSolutionTree` (UI build only)                |                                                              |

The git diff / p4 diff views use `linediff`; there is no separate unified-diff text parser.

Inputs of several files (`msbuild`, `linediff`, `search`, `statefiles`, `explorer`, `jsonrpc`, `msbuildexpr`) carry them
in one byte string, separated by the line `#--#`; the harness documents the order of the parts at its top.

### Running the fuzzer

Build with a sanitizer. clang's libFuzzer runtime is not needed (and was not installed where this was written); the
driver `tenfold-fuzz` brings its own mutation loop, and with `-DTENFOLD_FUZZ_COVERAGE=ON` (gcc or clang) the engine is
instrumented with `-fsanitize-coverage=trace-pc` so that inputs reaching new code join the corpus.

```sh
cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -O1 -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" -DTENFOLD_FUZZ_COVERAGE=ON   # add -DTENFOLD_BUILD_UI=ON for layout / explorer
cmake --build build-fuzz --target tenfold-fuzz

TMPDIR=/dev/shm build-fuzz/tests/fuzz/tenfold-fuzz --list
TMPDIR=/dev/shm build-fuzz/tests/fuzz/tenfold-fuzz lexer --seconds 600 --seed 1 --corpus /tmp/corpus-lexer
tests/fuzz/run_all.sh build-fuzz/tests/fuzz/tenfold-fuzz 30 1          # every target, 30 s each (what CI does)
build-fuzz/tests/fuzz/tenfold-fuzz --replay tests/fuzz/regressions     # the regression inputs, outside the unit tests
```

`TMPDIR=/dev/shm` matters for the harnesses that read files (`msbuild`, `compdb`, `statefiles`, `explorer`): they write
the input to a scratch directory first.

Options: `--seconds N` / `--runs N` (a deterministic budget), `--seed S`, `--corpus DIR` (read at start, new-coverage
inputs written back), `--out DIR` (default `fuzz-out`), `--max-len N`, `--timeout-ms N` (hang limit, default 10000),
`--slow-ms N` (smallest time worth reporting as slow, default 10), `--repo DIR` (where the seeds are read, default the
source tree), `--no-seeds`.

Seeds are the repository's own files (sources under `src/` and `examples/`, `tests/fixtures/vs/**`, `assets/**`, the
corpus projects of W9a under `tests/corpus` when present) cut into pieces of at most `maxLen` bytes, plus a few
synthetic samples per format written in each harness. Mutations: byte / bit flips, arithmetic, insertions,
deletions, duplication, swaps, splices with other corpus entries, line operations, "interesting" numbers (`2147483648`,
`18446744073709551616`, `1e999` ...), long repetitions (deep nesting) and per-target dictionary tokens (C++ keywords and
punctuators, XML and MSBuild tags and property functions, JSON tokens, escape sequences, regex and glob syntax ...).

What counts as a finding, and how it is reported:

* **crash** (ASan / UBSan abort, uncaught exception, failed `FUZZ_REQUIRE`): the input is written to
  `<out>/<target>/current` before every run, and copied to `crash-<target>-<hash>` by the sanitizer death callback or
  signal handler, so it survives the abort;
* **hang**: a watchdog thread stops an input that runs longer than `--timeout-ms`, prints the stack of the stuck thread
  and saves `hang-<target>-<hash>`;
* **slow input**: the time per KiB of input is compared with the running median; an input over `--slow-ms` and over 100x
  the median rate that stays slow in two more runs is saved as `slow-<hash>` and counted (quadratic or exponential
  behaviour shows up here before it becomes a hang). The slowest input of a run is always saved as `slowest`.

The last line a finished run prints is `RESULT target=... execs=... corpus=... edges=... slow=... crashes=0 hangs=0`.

### A finding: minimise, fix, keep the input

```sh
build-fuzz/tests/fuzz/tenfold-fuzz --minimize fuzz-out/xml/crash-xml-8de1b7bf440ffdaf xml --out min.xml
```

The minimiser (delta debugging, whole chunks, then single bytes) re-runs every candidate in a forked child and keeps it
when it still fails with the same signature (the sanitizer's `SUMMARY:` line, the exception text, a timeout), so it
shrinks crashes, exceptions, invariant failures and hangs. Then:

1. fix the root cause in the parser (minimal, local);
2. copy the minimised input to `tests/fuzz/regressions/<target>/<what-it-is>.<ext>` (the directory name selects the
   harness; a short descriptive file name, not the hash);
3. the unit test `fuzz_regression_inputs_replay` (`tenfold-tests`; `fuzz_ui_regression_inputs_replay` in
   `tenfold-ui-tests` for `layout` / `explorer`) runs every file through its harness, so the fix stays fixed.

### Writing a harness

Add a function and a `Registrar` to a `tests/fuzz/targets_*.cpp` file: name, function, seed sources (`"dir:.ext1,.ext2"`
scans a repository directory), synthetic samples, dictionary tokens and the preferred input length. Keep a harness
fast (a few thousand runs per second means more coverage per minute), deterministic, and free of leaks between runs;
file-based parsers get the input through `writeScratch`. Add the new target to `_libfuzz_targets` in
`tests/fuzz/CMakeLists.txt`.

### libFuzzer

`-DTENFOLD_FUZZ_LIBFUZZER=ON` (a compiler with `-fsanitize=fuzzer`) builds one `tenfold-libfuzz-<target>` binary per
harness around `LLVMFuzzerTestOneInput` (`tests/fuzz/libfuzzer_main.cpp`):
`build/tests/fuzz/tenfold-libfuzz-lexer tests/fuzz/regressions/lexer -max_total_time=600`.

### CI

The `linux-gcc-asan-fuzz` job builds `tenfold-fuzz` (gcc, ASan + UBSan, coverage feedback, UI library on), replays the
regression inputs, then fuzzes every target for 30 s with seed 1; findings are uploaded as an artifact.

### Design notes

* The harness objects are linked into `tenfold-fuzz` and into the unit-test binaries, so a regression input exercises
  exactly the code the fuzzer ran.
* `JsonRpcConnection::processBuffer` is the reader thread's framing step, made public for the `jsonrpc` harness (no
  child process involved).
* The settings / theme / key-binding sources of `src/ui/settings` are compiled into the fuzz binaries without the UI
  library (they need no SDL or GL); `Widget.cpp` comes along for `SettingsService::applyTo`, with a weak stub for
  `TextRenderer::flushText` (the parsers never draw).
* With `-DTENFOLD_FUZZ_COVERAGE=ON` the engine calls `__sanitizer_cov_trace_pc`, defined in the small static library
  `tenfold_fuzz_cov` (`fuzz_cov.cpp`, never instrumented itself); every binary that links the engine gets it, so the
  other executables (`tenfold-tests`, `tenfold-cli`) still link in a fuzz build.
* The `search` harness compares the optimised glob matcher with the old exponential one on small inputs, the
  `linediff` harness checks the hunks against the live text, `indexstore` repairs the checksum fields before it
  deserialises: a harness that only waits for "no crash" would miss wrong answers and would never get past the
  checksum.
* Windows: the driver uses fork and POSIX signals; `tests/fuzz` is skipped there.
* Known environment difference: under gcc with `-fsanitize=undefined` the unit test `crash_handler_writes_report`
  fails in its "segv" case (UBSan's null check reports the helper's deliberate null store and exits before the SIGSEGV
  handler runs). The `asan` preset uses clang; this was only seen with gcc, and the fuzz job does not run that test.
