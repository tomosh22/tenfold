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
| P-1 (fixed) | `pp-macros` `fast def use_max_items` (and every other macro: 68 of 82 fast/final disagreements) | the `#define` | no answer | fast | tenfold fast tier | any `#define X` + use of `X` before clangd is ready |
| P-2 (not fixed, by design) | `pp-macros` `fast def use_Gadget`, `use_compute_b`, `use_warm`; `pp-xmacros` `fast def use_Red_1/use_op_add/use_Triangle`; `pp-macros-std` `fast def use_Color_names/use_Red` | the declaring macro argument / invocation | no answer | fast | tenfold fast tier | names declared through macro arguments (`DECLARE_CLASS(Gadget)`), `.def` lists (not scanned), `##` |
| P-3 (fixed) | `pp-macros` `fast def use_clamp01_paren` | `int (clamp01)(int v)` | no answer | fast | tenfold fast tier | parenthesised declarator is not a declaration for the lexer |
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
| 10 | `c-basic:fast def use_helper_a`, `use_helper_b`, `use_counter_a`; `layout-dupnames:fast def use_Engine1/2`, `use_a_helper`, `use_b_helper`, `use_a_Settings`, `use_shallow_leaf`; `c-mixed-cpp:fast def use_Codec_run` | the entity in this file / namespace / sub-project | every same-named entity in the workspace | fast | **tenfold** fast index (name only, no scope, translation unit or include path); same as finding 1 of W9d | two `static int helper(int)` in two .c files |
| 11 | `layout-unity`: 20 lines in `beta.cpp` / `gamma.cpp` (`def`, `hover`, `refs`, `rename`) | resolved as in the unity TU: `def use_bump_beta -> bump_def` etc.; rename of `scaled` / `g_counter` / `Item::weight` renames all parts | no result / not renamed in `beta.cpp`, `gamma.cpp`; references list only `alpha.cpp`; rename leaves code that no longer compiles in the unity TU | final | **clangd and tenfold**: only `build_unity.cpp` is in the database, so clangd parses the included `.cpp` files standalone (names declared by an earlier part are undeclared) and, even with `beta.cpp` closed, its index has no references from the parts (raw clangd and `tenfold-cli` agree). tenfold could map an `#include`d source that has no database entry to the includer's command (the same problem exists for any non-self-contained source) | `unity.cpp` includes `a.cpp` then `b.cpp`; `b.cpp` calls what `a.cpp` defines |
| 12 (fixed) | every `fast def use_f` of a function with a prototype and a definition (e.g. `c-basic:fast def use_list_push`; 9 `xfail` lines in `c-basic`, 3 in `c-mixed-cpp`) | the definition | prototype and definition | fast | **tenfold** fast index lists the prototype as a definition (accepted as `def \| decl` in the other projects, listed as xfail in `c-basic` and `c-mixed-cpp` to keep the correct expectation) | `int f(); int f(){return 0;} int g(){return f();}` |
| 13 | `layout-exts`: `def use_dot_in_len2`, `use_x_in_dot`, `use_y_in_dot` (in `vec.tpp`), `def use_state_in_step` (`engine.ipp`) | resolved as in the including TU | no result (`vec.tpp:4:11`, the namespace, for the first three) | final | **clangd**: `.tpp` / `.ipp` files that need the class from the header that includes them are parsed standalone when open. Tenfold passes the file with its borrowed command, which is all it can do today | `types.hh` includes `vec.tpp` at its end; open `vec.tpp`, go to definition of `x` |
| 14 | `layout-exts:refs Vec_x`, `refs Vec_y`, `refs Engine_state_field`, `refs Engine_step_decl >= ... Engine_step_def`, **`rename Engine_state_field state`** | all uses incl. those in the `.tpp` / `.ipp`; rename changes them | the uses in `vec.tpp` / `engine.ipp` are missing; rename leaves `state_ += v` in `engine.ipp`, which no longer compiles | final | **clangd**: the standalone parse of the open `.ipp` replaces that file's references in the dynamic index (not the cause for closed files: `refs` with `engine.ipp` closed is complete). Dangerous for rename | `Engine::step` defined in `engine.ipp`, rename `state_` with `engine.ipp` open |
| 15 | `layout-exts:sym "far_sum"` | the symbol at its real column (~6100) | column 4096 | final | **clangd**: index positions keep the column in 12 bits, so a symbol on a line longer than 4095 columns is clamped (the fast tier is right) | `int a[]={...6000 numbers...}; int far_sum();` on one line |
| 16 | `layout-dupnames:refs Engine1`, `refs Engine2`, `refs Engine1_id`, `refs Engine2_id`, `rename Engine1_id`, `sym "Engine" ∋ both` | the entity of this sub-project | references of the other sub-project's `Engine` / `Engine::id_` too; rename changes both; symbol search returns one of the two (which one varies between runs) | final | **clangd** (two global classes with equal names have one USR; not distinguishable from one entity). A workspace with two unrelated sub-projects reports each other's uses | `sub1/engine.h` and `sub2/engine.h`, both `class Engine { int id_; }` |
| 17 | `layout-exts:decl use_scale` (flaky, accepts `scale_decl \| scale_def`) | the declaration in `types.hh` | the inline definition in `scale.inl` in about 2 of 5 runs, the declaration otherwise | final | **clangd** (depends on whether the standalone parse of the open `scale.inl` is already in the dynamic index) | declaration in `types.hh`, `inline` definition in `scale.inl`, both open |
| 18 | `c-basic:sym "g_counter"` | the definition with the initialiser | the first tentative definition (`int g_counter;`) | final | **clangd** (a tentative definition counts as a definition for its indexer). `def` answers any of the three; accepted, see open questions | `int g; int g; int g = 100;` |

Harness findings (not tenfold):

| # | what | detail |
|---|---|---|
| H1 (fixed) | symlinks cannot be put in a corpus | `Runner::copyTree` computes `fs::relative(it->path(), from)`, which resolves symlinks, so a symlinked directory is copied as a symlink named like its **target** (`copy_symlink` fails with "File exists") and the link itself never appears in the scratch copy; for a symlinked **file** `scanAnchors` uses `is_regular_file` (follows links) so the target's anchors are found twice and the corpus fails with `duplicate anchor`. Result: no corpus can contain a symlink. Checked by hand instead: a symlinked directory inside a workspace (`link_lib -> real_lib`, header included through the link) works end to end: clangd answers the real path, the workspace scan does not follow the link (no duplicates), `refs` is complete in both directions |
| H2 (fixed) | an exception in the engine aborts the whole run | the runner does not catch exceptions from `Editor` calls (`terminate called ...`), so a single throwing line takes all following lines and the JSON report with it, and the `fast def` agreement check (`flushAgreement`) silently performs a `def` that jumps into (and opens) the target file, which can crash a run for a line that is only `fast` |
| H3 | line model | `scanAnchors` splits at `\n` (and drops one `\r`): CR-only files are one line and U+2028 / U+0085 are not separators; this matches `TextBuffer`, not clang. A BOM is kept in the line text, so byte columns on line 1 include its three bytes: this agrees with tenfold and clangd (utf-8), so the BOM case noted as "not handled yet" in `docs/TESTING.md` §13 works as is: `encodings` has anchors on line 1 after a BOM in `.h`, `.cpp` and BOM + CRLF files, all passing |
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

* No crash, no hang, no sanitizer report in any scenario after the fixes below: 7 problems were fixed, each with a
  regression test; 8 more need a design change and are listed under "Not fixed" with reproducers.
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

Keystroke-to-paint (budget 8 ms), caret at the end of the document, average of 60 keystrokes:

| File | wrap off | wrap on |
|---|---|---|
| 300k-line C++ file | 0.59 ms | 0.56 ms |
| 100k-line text file | 0.34 ms | 0.41 ms |
| 2 MB single-line comment | 0.33 ms (was 80 ms) | 0.63 ms |
| 1 MB single-line string | 1.1 ms (was 57 ms) | 0.9 ms |
| 1 MB single-line C++ expression | 20 ms (was 64 ms) | 15 ms |
| 550 kB minified C++ line | 9.3 ms (was 31 ms) | 7.1 ms |
| 2 MB line of wide characters | 35 ms (was 78 ms) | 0.5 ms |

Open to first frame: under 55 ms for the 300k-line file, 15 to 45 ms for the one-line files. PageDown x 20 and Ctrl+End are
below 1 ms each. The over-budget cells are the "Not fixed" items 1 and 2 below; those combinations are printed but excluded
from the `PERF_CHECK` (so `TENFOLD_ENFORCE_PERF=1` passes).

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

7. **A 200 MB source file took 3.4 GB and 20 s to index.** The symbol extractor keeps every token of the file in memory
   (about 16 bytes of RSS per source byte, times the indexing threads). Fix: a source above 64 MiB is not indexed
   (`extractSymbols` returns nothing; the file still opens and edits). Test: `pathological_huge_source_files_are_not_indexed`.
   Not a substitute for a streaming lexer, see "Not fixed" 9.

### Not fixed (need a design change), with reproducers

1. **The highlighter re-lexes the whole line on every keystroke.** A C++ line of 500 kB or more costs 10 to 20 ms per
   keystroke (`Highlighter::advance` / `lexLine`); minified bundles and generated one-line tables hit it. Needs a maximum
   tokenisation length (VS Code stops at 20000 characters) with a defined lexer state after the cut, or incremental lexing.
   Reproduce: `TENFOLD_ENFORCE_PERF=1 xvfb-run -a build-ui/tenfold-ui-tests pathological_ui_one_line` and look at the
   `expr.cpp` / `minified.cpp` PERF lines.
2. **Column mapping is O(line) for lines with non-ASCII text.** The fast path in `columnForByte` / `layoutLine` is for
   printable ASCII; a 1M-character line of CJK / accented text with wrap off takes 35 ms per keystroke (the `cjk.txt` case).
   A per-line column index (or caching the last mapping) is needed.
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
| F1 (not fixed) | `fast def use_ar1`, `use_who_r`, `use_conv_char`, `use_pick_p`, `use_b_f_dbl`, `use_c_f` (overloads); `use_kind_ptr`, `use_name_int`, `use_circle_area` (specialisation); `use_describe_num` (cxx20); `use_vmax_base` (basic) | the one overload / the one `b::f` | every function with that word (up to 9 results), qualifier and arguments ignored | fast | fast tier (by design: word lookup) | `int area(); int area(int);` ... `area(3)` |
| F2 (fixed) | `fast def use_draw_sprite`, `fast sym "draw"` (overloads) | the inline hidden friend `friend int draw(Sprite s) {...}` | not in the index: the fast tier answers with the *other* `draw` | fast | fast tier (lexer index does not record friend function definitions inside a class) | `struct S { friend int draw(S) { return 0; } };` then `sym "draw"` |
| F3 (fixed) | `fast def use_Stack`, `use_Pair`, `use_Wrap` (basic) | the class | class **plus** its constructor/destructor declarations (same word) | fast | fast tier | `template<class T> struct Pair { Pair(T); };` click `Pair` |
| F4 (by design) | `fast def use_plus`, `use_lit_km` (operators) | the operator function | nothing (an operator token or a literal suffix is not a word) | fast | fast tier (inherent) | `a + b` |
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

## W9-FIX-C1: fast-tier indexing gaps and answer shaping

Fixes in `src/semantic/FastIndex.*` and the approximate-answer path of `SemanticEngine` (design: `docs/ARCHITECTURE.md`,
"What the fast tier indexes, and how its answer is shaped"). `fast def`/`decl` agreement with clangd over all 29 corpora
(harness summary lines, `def` and `decl` lines): **398/1841 (21.6%) before, 809/1853 (43.7%) after**; 31 `xfail` lines
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
| W9d 1-3, W9b F1/F5, W9c P-4, W9e 10 (name-only lookup, locals, scopes, overloads, `#if`) | not fixed | scope-aware resolution is part 2 of this work |
| W9b F4 operator tokens (`a + b`) and literal suffixes (`2.5_km`) | by design | a punctuator is not a word; the fast tier has no answer, clangd does |
| `typedef struct { int x; } P;` members | not fixed | members of a typedef'd struct body are not indexed (the tag/typedef name is) |

Performance (extractor only, Release, one thread, best of 15 runs; `tools/stress/gen_pathological.py` `bigfiles`
at `--scale asan --knob big_lines=300000`): this repository's `src` + `tests` + `third_party` (653 files, 4.9 MB) 47.4
-> 47.0 MB/s, the pathological `million_lines.cpp` + `minified.cpp` + `minified.js.h` (10.8 MB) 58.0 -> 56.6 MB/s;
symbols 23,787 -> 24,121 (macros behind comments, hidden friends, typedef enumerators) and 37,527 -> 37,527;
`sizeof(Symbol)` stays 80 bytes (`arity` fits in the padding), peak RSS of the benchmark process unchanged (15 / 74
MiB). The difference is within run-to-run noise (about 3%).
