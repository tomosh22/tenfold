# Stress test results (W9)

Each W9 corpus agent adds its own self-contained section below. Format and rules of the corpora: `docs/TESTING.md`.

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
| 1 | every `xfail fast def` in all six projects (e.g. `lookup-namespaces:use_inner_value`, `name-collisions:use_a_Node_get`, `lookup-adl:use_draw_circle`) | the one declaration C++ lookup picks | every same-named definition in the workspace (all namespaces, classes, overloads, forward declarations) | fast | fast tier (design: name-only lexer index, no scope, receiver type, ADL or overload step) | `namespace a{int f();} namespace b{int f();} int g(){return b::f();}` : `fast def` on `f` returns both |
| 2 | `lambdas-bindings:use_cap_base`, `use_param_n`, `use_outer_n`, `use_sb_x`, `use_init_y`, `use_e_loop`, `use_lam_x`, `use_sb_inner_y`, `edit_use_b` | the local/parameter/capture/binding | none | fast | fast tier: locals, parameters, captures, init-captures, structured bindings and range-for variables are not indexed | `auto [x,y]=p; return x;` : `fast def` on `x` is empty |
| 3 | `name-collisions:use_i_a1`, `use_param_i` | loop variable `i` / parameter `i` | the unrelated member `Holder::i` | fast | fast tier: with no local scope a same-named member of another class is answered, which is worse than no answer | `int sum(int n){for(int i=0;i<n;++i);} struct H{int i;};` : `fast def` on the `i` in the loop gives `H::i` |
| 4 | `lookup-adl:use_dot`, `fast sym "dot"` | the hidden friend `dot` | none (no symbol, no definition) | fast | fast tier: friend functions defined inside a class are not extracted | `struct V{ friend int dot(V,V){return 0;} };` : `fast sym "dot"` is empty |
| 5 | `oop-ctors:use_Feet_to_Meters` | the conversion operator | none | fast | fast tier: conversion operators (`operator T`) are not indexed | `struct F{ operator int() const; }; f.operator int();` |
| 6 | `oop-virtual:use_Engine`, `oop-ctors:use_dtor_call` | the class `Engine`; the destructor `~Widget` | the class plus all constructors and the destructor | fast | fast tier: the word `Engine`/`Widget` is looked up, so class, ctors and dtor are conflated; `~Widget` is not distinguished from `Widget` | `class E{E(); ~E();}; E e;` : `fast def` on the type gives 3 results |
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

1. What should go to definition answer for a name brought in by a using-declaration: the entity only, or both (clangd)? Accepted both.
2. What should go to definition answer on the type name in a constructor call: the class or the selected constructor? Accepted both;
   VS and CLion jump to the constructor.
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
| F1 | `fast def use_ar1`, `use_who_r`, `use_conv_char`, `use_pick_p`, `use_b_f_dbl`, `use_c_f` (overloads); `use_kind_ptr`, `use_name_int`, `use_circle_area` (specialisation); `use_describe_num` (cxx20); `use_vmax_base` (basic) | the one overload / the one `b::f` | every function with that word (up to 9 results), qualifier and arguments ignored | fast | fast tier (by design: word lookup) | `int area(); int area(int);` ... `area(3)` |
| F2 | `fast def use_draw_sprite`, `fast sym "draw"` (overloads) | the inline hidden friend `friend int draw(Sprite s) {...}` | not in the index: the fast tier answers with the *other* `draw` | fast | fast tier (lexer index does not record friend function definitions inside a class) | `struct S { friend int draw(S) { return 0; } };` then `sym "draw"` |
| F3 | `fast def use_Stack`, `use_Pair`, `use_Wrap` (basic) | the class | class **plus** its constructor/destructor declarations (same word) | fast | fast tier | `template<class T> struct Pair { Pair(T); };` click `Pair` |
| F4 | `fast def use_plus`, `use_lit_km` (operators) | the operator function | nothing (an operator token or a literal suffix is not a word) | fast | fast tier (inherent) | `a + b` |
| F5 | agreement in `templates-specialisation` (5 queries), e.g. `use_swap_int`, `use_convert_bool`, `use_id_v_int` | the explicit specialisation | the primary template | fast | fast tier | `template<> void swap_it<int>(int&, int&)` |
| F6 | `templates-basic`: after `stack.tpp` is open (`templates-cxx20` has the same shape with `functions.tpp`, whose lines were moved after everything else), `def use_data_in_top`, `use_made_in_push`, `use_push_in_all`, `refs Stack_top_decl`, `refs Stack_map_decl`, `rename Stack_push_all_decl`, `rename Stack_top_c_def` | the definitions in `stack.tpp` are found, references and renames include `stack.tpp` | clangd parses the non-self-contained `stack.tpp` **standalone** (names undeclared): `def` answers the `namespace tb` line, the open document replaces the index shard so every reference inside it disappears, `rename` skips it or is refused ("symbol is not a supported kind") | final | **clangd** (non-self-contained header parsed alone) **and tenfold** (it `didOpen`s the file as soon as it is navigated to, or a rename edits it, with no compile context for it) | `stack.h` ends with `#include "stack.tpp"` and `stack.tpp` has no includes; jump to a definition in `stack.tpp`, then find references of a member declared in `stack.h` |
| F7 | `templates-basic` run flake (seen once in 6 runs before the corpus was reordered): `refs Stack_map_decl` lost `stack.tpp` | stable answer | answer depends on whether clangd finished the standalone parse of the just-opened `stack.tpp` when the next query arrives | final | tenfold/harness: `ensureReady` waits only for the *anchor's* file, not for documents a rename or navigation opened as a side effect; underlying cause F6 | `rename` of a member defined in a `.tpp`, then `refs` of another member |
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
   does; only `refs` shows it.
5. F6: what should tenfold do about `.tpp`/`.inl`? Options: do not `didOpen` such files into clangd without a
   containing-TU context, or give them a compile command that includes the owning header first.

### Assumptions

* clangd 22.1.1 from `PATH`, libstdc++ 13 headers, `-std=c++20`.
* Where the C++ rules leave a choice (specialisation vs primary on a type name, rewritten candidates for implicit
  `operator==`, which declaration a using-declaration shows) the corpus line carries a comment and accepts what clangd
  shows today when it is a defensible reading; those are the lines marked "judgement".
* Sources are never reformatted (`tests/corpus/.clang-format` disables it).
