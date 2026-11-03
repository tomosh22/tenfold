# Stress test results (W9)

## Fuzzing (W9h)

Mutation fuzzing of every parser that reads untrusted or user-edited input, under gcc 13.3 AddressSanitizer + UBSan
(`-fno-sanitize-recover=undefined`, `-O1`), with edge-coverage feedback (`-fsanitize-coverage=trace-pc`). The fuzzer, the
21 harnesses and how to run them are described in `docs/TESTING.md` ("Fuzzing"); the regression inputs are in
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
