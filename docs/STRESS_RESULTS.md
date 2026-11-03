# W9 stress test results

Each corpus agent adds one self-contained section below. Numbers are from `tenfold-cli corpus` / `tenfold-corpus-tests`
(Release, linux/x86_64, clangd 22.1.1, a machine running several builds at once: load average 10-17 on 4 cores).

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
| `vs-configs` | `.sln` + 2 `.vcxproj`, std-free: Debug\|x64, Release\|x64, Debug\|x86 (-> Win32), custom Profile\|x64; per-config `PreprocessorDefinitions`, `ForcedIncludeFiles`, `$(Configuration)`/`$(Platform)` in include dirs, `ExcludedFromBuild`, implicit `_DEBUG`/`_WIN64`/`_MSC_VER`, a header shared by both projects | 151 | 141 | 10 | 15 / 91 | 2-3 s |
| `vs-props` | `.sln` + 2 `.vcxproj` + `.props`: sheet imported by two relative spellings, `Import Condition="Exists(...)"` on `$(Configuration).props`, properties overridden after the sheet (item definitions see final values), `Directory.Build.props`, `$(SolutionName)`/`$(ProjectName)`/`$(MSBuildThisFileDirectory)`, conditional `ItemGroup`, `gen\*.cpp` wildcard, per-file metadata, ActiveCfg-only mapping | 131 | 126 | 5 | 4 / 95 | 2-3 s |
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

### Findings

Tier: **fast** = lexer index, **final** = clangd answer. Fault: **clangd**, **tenfold** (solution evaluation / compile
commands / configuration switch / waiting / navigation), **harness** (W9a runner). "clean session" = clangd started on the
same project with only the stated file opened and a settle time of 8 s (my own LSP probe), "editor session" = the
corpus runner.

| id | expectation (project:anchor) | expected | actual | tier | at fault | minimal repro |
|---|---|---|---|---|---|---|
| P-1 | `pp-macros` `fast def use_max_items` (and every other macro: 68 of 82 fast/final disagreements) | the `#define` | no answer | fast | tenfold fast tier | any `#define X` + use of `X` before clangd is ready |
| P-2 | `pp-macros` `fast def use_Gadget`, `use_compute_b`, `use_warm`; `pp-xmacros` `fast def use_Red_1/use_op_add/use_Triangle`; `pp-macros-std` `fast def use_Color_names/use_Red` | the declaring macro argument / invocation | no answer | fast | tenfold fast tier | names declared through macro arguments (`DECLARE_CLASS(Gadget)`), `.def` lists (not scanned), `##` |
| P-3 | `pp-macros` `fast def use_clamp01_paren` | `int (clamp01)(int v)` | no answer | fast | tenfold fast tier | parenthesised declarator is not a declaration for the lexer |
| P-4 | `pp-macros` `fast def use_ver_fn`, `use_nested_fn`, `use_fx`; `vs-configs`/`vs-props` every `fast def` into `#if` branches; `pp-includes` `use_dup_*`, `use_branch_forced` | the one active branch | every branch (3-4 candidates) | fast | tenfold fast tier (by design: no `#if`, no `-D`, no include-dir model) | `#if A int f(); #else int f(); #endif` |
| P-5 | `pp-macros` `def/hover/refs` at `(clamp01)(5)` and `= clamp01;` | the function `clamp01` | the macro `clamp01` (`macro clamp01 provided by "basic.h"`) | final | clangd | `#define f(x) x` + `int (f)(int); int (*p)(int) = f;` then def on `f` in `p = f` |
| P-6 | `pp-macros` `refs ADD3`; `pp-xmacros` `refs DECL_FIELD` | include the use `APPLY(ADD3, ...)` / `FIELDS(DECL_FIELD)` | that use is missing | final | clangd | a macro name passed as an argument and expanded when the replacement list is rescanned |
| P-7 | `pp-macros-std` `refs Color` at `DEFINE_ENUM(Color, ...)` (`Name` is also used in `Name##_names`) | the enum and its uses | empty, rename: "no symbol at the given location" | final | clangd | declare-by-macro where the argument is used both directly and as a `##` operand |
| P-8 | `pp-xmacros` `rename use_width w` (member declared once in `FIELDS(X)`) | edit the use and the token in the list macro | edits only the use: the code stops compiling | final | clangd | member declared through a list macro |
| P-9 | `pp-xmacros` last block: `open include/colors.def` then `rename use_Red_1`; `pp-includes` (disabled `#?` lines) `def use_size` landing in `stack_members.inl`, then `rename use_size length` | edit the declaration token in the `.def` / `.inl` and the use | edits the use only, silently | final | clangd (standalone parse of a file that has no TU + its dynamic index shadows the background index) and tenfold (go-to-definition navigates into the file and opens it, which sends `didOpen` to clangd) | `rename use_size length` alone: both edits; after `def use_size -> Stack_size` landed in the `.inl`: one edit |
| P-10 | `pp-xmacros` `refs xc_Red/xc_Green/xs_Triangle` asked at the token inside the `.def` | uses of the enumerator / class | empty | final | clangd / tenfold: a `.def` is never given a compile command, headers are inferred only by extension | any query located in a file that is only `#include`d and not a header extension |
| P-11 | `pp-includes` `refs dup_value_src/inc1/inc2`, `rename use_dup_reversed`, `sym "dup_value"` | per include path: only that header's `dup_value()` and its own uses | all three declarations and every use; `sym` keeps one location | final | clangd (declarations in three headers have one USR) | same inline function in `src/dup.h`, `inc1/dup.h`, `inc2/dup.h` chosen by `-I` order; `decl` on these is additionally non-deterministic (disabled) |
| P-12 | `pp-includes` `sym "impl_mode"`, `refs mode_fast_impl/mode_slow_impl/SlowOnly`; `pp-macros-std` `refs trace_off`, `sym "trace_name"`, `sym "checks_enabled"` | both macro-state variants of a header | only the variant of the first TU (`sym "SlowOnly"` finds nothing at all); refs asked at an inactive declaration: empty | final | clangd (background index indexes a header once; a header position is answered in the first includer) | `mode/mode.h` with `#ifdef MODE_FAST`, two TUs |
| P-13 | `pp-includes` `def use_next_b_in_a` (position inside `inc1/next.h`, which uses `#include_next <next.h>`) | `next_b` in `inc2/next.h` | `next_a` in `inc1/next.h` | final | clangd (the header is parsed standalone as a main file: `#include_next` loses its place) | `#include_next` in a header, go-to-definition inside it |
| P-14 | `pp-includes` `rename use_box_double box_dbl`, `sym "box_double"` | also the token in `#define BOX_FN box_double`; symbol found | use only; symbol missing while `multi.cpp` is open (found when another file is open) | final | clangd (dynamic index of the open TU hides the static symbol whose name is spelled in an object-like macro) | name of a function given by an object-like macro and the header included twice |
| P-15 | `vs-configs` `refs cfg_release/cfg_debug/cfg_profile`, `refs arch_win32/arch_x64`, `rename use_cfg_value` after the first `config` | only the declaration compiled in the active configuration | the declarations of every configuration visited earlier (stale background index, same USR) as well; `rename` edits the other configurations' headers | final | tenfold (configuration switch restarts clangd but keeps one `.cache/clangd/index`) + clangd (USR) | `config Debug\|x64`, `config Release\|x64`, `refs` on `cfg_value` |
| P-16 | `vs-props` Debug\|x86 `def use_level_plugin`, `use_pproj_marker`, `use_pbuild_marker`, `use_pextra_marker` | plugin evaluated as its mapped `Debug\|x64` (flags of plugin.vcxproj) | no compile command; clangd borrows the engine's flags (`LEVEL_9`, `PROJ_EXTRA`, no `PLUGIN_BUILD`) | final | tenfold (solution evaluation: a project with `ActiveCfg` but no `Build.0` in the active solution configuration is dropped from the compile commands) | solution maps `Debug\|x86` to `Debug\|x64` for `plugin` without a `.Build.0` line; open `plugin.cpp` |

Evidence for fault assignment: P-1..P-4 are the answers `tenfold-cli corpus` prints for `fast` lines. P-5..P-8, P-10,
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
  Windows. If wrong, P-16 is not a finding (the borrowed flags would still be arbitrary).
* **Sources of a project not part of the active configuration / `ExcludedFromBuild` file:** what should workspace
  symbol search and navigation do? Only the fast tier is asserted (`fast sym "dbg_only_free"` finds it in every
  configuration); the final-tier answer was not asserted because I am unsure what VS does.
* `_MSC_VER` is defined (clang-cl default) and `compiler_kind()` expects the MSVC branch; assumed correct for the VS route.
* `.sln` mode is std-free because no MSVC/SDK headers exist on Linux; the macro cases that use the standard library are
  in `pp-macros-std` as a `compile_commands.json` project (which `docs/TESTING.md` of this branch does not mention: the
  section about `.sln` workspaces on Linux does not exist there; the std-free rule is inferred from the task text and
  from the seed).
* Anchors inside a `#define` body or macro argument are written in comments before the token; they never alter the
  preprocessing. The scratchpad tools used for the clean-session probes are not part of the repository.
