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

### Findings

"Fault" names whose behaviour is wrong. Where clangd is involved it was also queried directly over LSP (clangd 22.1.1, same compile
database, offset encoding utf-8) to tell clangd from tenfold's handling. "Tier" is the tier of the expectation.

| # | expectation (project) | expected | actual | tier | fault | minimal repro |
|---|---|---|---|---|---|---|
| 1 | not expressible as an `xfail`: the runner aborts. Any `def` / `decl` / `fast def` (the agreement check asks the final answer) / `rename` whose result file, or any query whose anchor file, is not valid UTF-8 (`encodings/src/latin1.*`) | the answer | `terminate called after throwing nlohmann::json type_error 316: invalid UTF-8 byte at index 60: 0xF6` | final | **tenfold**: `JsonRpcConnection::send` calls `msg.dump()` with the default error handler; `Editor::open` -> `SemanticEngine::didOpen` -> `LspClient::didOpen` sends the buffer text raw. `tenfold-cli` prints `error: [json.exception.type_error.316]...` for `open`; nothing in `Editor` / `EditorApp` catches it, so the editor terminates when a file with Latin-1 / Shift-JIS bytes is opened (or jumped into) while clangd runs; the restart path (`SemanticEngine.cpp:709`) re-sends all open documents the same way | `printf 'const char* s = "Gr\xf6\xdfe";\nint f();\n' > a.cpp`, open it with clangd running |
| 2 | `encodings`: all 11 `cr_*` lines (`def`/`decl`/`refs`/`rename`/`sym` on `cronly.c` / `cronly.h`) | answers on the real lines | answers land on other lines (`src/cronly.c:4:19` for the anchor the file has on its line 1), `rename` fails with `Line value is out of range (2)` | final | **tenfold**: `TextBuffer` ends lines at `\n` only, so a CR-only file is one line; clang and clangd also end a line at a lone `\r`, so every position sent to / received from clangd disagrees (the harness splits at `\n` too, i.e. agrees with tenfold, not with the compiler) | file whose lines end in `\r` only |
| 3 | `encodings`: `fast def` / `fast sym` for the CR-only files | the C declarations | none | fast | **tenfold** fast lexer: the whole file is one line starting with `#ifndef` / `#include`, so nothing in it is indexed | same |
| 4 | `encodings:refs CRLF_MAX` (macro defined in a CRLF header, with a `\` continuation) | definition + both uses | only the definition | final | **tenfold**: `TextBuffer::text()` drops every `\r`, so `didOpen` sends clangd text that differs from the file; with that header open clangd stops reporting references of the macro (raw clangd with the original CRLF text: complete; with the `\r\n` replaced by `\n`: only the definition). Functions and fields defined in the same file are not affected | open `crlf.h` (CRLF) with a `#define M(a) \` continued macro, find references of `M` |
| 5 | `encodings:fast def use_bom_v`, `use_bomns_inner`, `use_bomns_A`, `fast sym "inner_bom"`, `"Kind"` | members / namespace contents / enumerators declared by the first token of a BOM file | none (the scope's own name is found, not what is inside it) | fast | **tenfold** fast lexer: the UTF-8 BOM is not skipped, so on line 1 it glues to the first keyword (`\xEF\xBB\xBFnamespace`) and the declaration is mis-parsed. Final answers are right: byte columns agree between harness, tenfold (the BOM stays in the first line) and clangd | file = BOM + `struct S { int v; };` |
| 6 | `c-basic:fast def use_SQUARE`, `use_LIMIT`; `c-mixed-cpp:fast def use_version`; `encodings:fast def use_MAKRO` | the `#define` | none | fast | **tenfold** fast index: macros are not indexed at all (no `sym` either) | `#define M 1` then `fast sym "M"` |
| 7 | `c-basic:fast def use_F_READ` | the enumerator | none | fast | **tenfold** fast index: enumerators of `typedef enum { A, B } T;` (anonymous enum) are not indexed; enumerators of a named enum are | `typedef enum { A, B } T;` |
| 8 | `c-heuristic:fast def use_class`, `use_delete` | the C function named `class` / `delete` | none | fast | **tenfold** fast lexer: C++ keywords are keywords even in a `.c` file. (Language choice itself is right: heuristic `.c` entries get `clang -std=c11`; the final tier passes all 26 other lines.) | `int class(int); int x = class(1);` in a `.c` file |
| 9 | `c-basic:fast def use_node_td_main` | typedef `node` | typedef and struct tag `node` | fast | **tenfold** fast index (word-based; cannot tell `struct node` from `node`) | `typedef struct node node;` |
| 10 | `c-basic:fast def use_helper_a`, `use_helper_b`, `use_counter_a`; `layout-dupnames:fast def use_Engine1/2`, `use_a_helper`, `use_b_helper`, `use_a_Settings`, `use_shallow_leaf`; `c-mixed-cpp:fast def use_Codec_run` | the entity in this file / namespace / sub-project | every same-named entity in the workspace | fast | **tenfold** fast index (name only, no scope, translation unit or include path); same as finding 1 of W9d | two `static int helper(int)` in two .c files |
| 11 | `layout-unity`: 20 lines in `beta.cpp` / `gamma.cpp` (`def`, `hover`, `refs`, `rename`) | resolved as in the unity TU: `def use_bump_beta -> bump_def` etc.; rename of `scaled` / `g_counter` / `Item::weight` renames all parts | no result / not renamed in `beta.cpp`, `gamma.cpp`; references list only `alpha.cpp`; rename leaves code that no longer compiles in the unity TU | final | **clangd and tenfold**: only `build_unity.cpp` is in the database, so clangd parses the included `.cpp` files standalone (names declared by an earlier part are undeclared) and, even with `beta.cpp` closed, its index has no references from the parts (raw clangd and `tenfold-cli` agree). tenfold could map an `#include`d source that has no database entry to the includer's command (the same problem exists for any non-self-contained source) | `unity.cpp` includes `a.cpp` then `b.cpp`; `b.cpp` calls what `a.cpp` defines |
| 12 | every `fast def use_f` of a function with a prototype and a definition (e.g. `c-basic:fast def use_list_push`; 9 `xfail` lines in `c-basic`, 3 in `c-mixed-cpp`) | the definition | prototype and definition | fast | **tenfold** fast index lists the prototype as a definition (accepted as `def \| decl` in the other projects, listed as xfail in `c-basic` and `c-mixed-cpp` to keep the correct expectation) | `int f(); int f(){return 0;} int g(){return f();}` |
| 13 | `layout-exts`: `def use_dot_in_len2`, `use_x_in_dot`, `use_y_in_dot` (in `vec.tpp`), `def use_state_in_step` (`engine.ipp`) | resolved as in the including TU | no result (`vec.tpp:4:11`, the namespace, for the first three) | final | **clangd**: `.tpp` / `.ipp` files that need the class from the header that includes them are parsed standalone when open. Tenfold passes the file with its borrowed command, which is all it can do today | `types.hh` includes `vec.tpp` at its end; open `vec.tpp`, go to definition of `x` |
| 14 | `layout-exts:refs Vec_x`, `refs Vec_y`, `refs Engine_state_field`, `refs Engine_step_decl >= ... Engine_step_def`, **`rename Engine_state_field state`** | all uses incl. those in the `.tpp` / `.ipp`; rename changes them | the uses in `vec.tpp` / `engine.ipp` are missing; rename leaves `state_ += v` in `engine.ipp`, which no longer compiles | final | **clangd**: the standalone parse of the open `.ipp` replaces that file's references in the dynamic index (not the cause for closed files: `refs` with `engine.ipp` closed is complete). Dangerous for rename | `Engine::step` defined in `engine.ipp`, rename `state_` with `engine.ipp` open |
| 15 | `layout-exts:sym "far_sum"` | the symbol at its real column (~6100) | column 4096 | final | **clangd**: index positions keep the column in 12 bits, so a symbol on a line longer than 4095 columns is clamped (the fast tier is right) | `int a[]={...6000 numbers...}; int far_sum();` on one line |
| 16 | `layout-dupnames:refs Engine1`, `refs Engine2`, `refs Engine1_id`, `refs Engine2_id`, `rename Engine1_id`, `sym "Engine" ∋ both` | the entity of this sub-project | references of the other sub-project's `Engine` / `Engine::id_` too; rename changes both; symbol search returns one of the two (which one varies between runs) | final | **clangd** (two global classes with equal names have one USR; not distinguishable from one entity). A workspace with two unrelated sub-projects reports each other's uses | `sub1/engine.h` and `sub2/engine.h`, both `class Engine { int id_; }` |
| 17 | `layout-exts:decl use_scale` (flaky, accepts `scale_decl \| scale_def`) | the declaration in `types.hh` | the inline definition in `scale.inl` in about 2 of 5 runs, the declaration otherwise | final | **clangd** (depends on whether the standalone parse of the open `scale.inl` is already in the dynamic index) | declaration in `types.hh`, `inline` definition in `scale.inl`, both open |
| 18 | `c-basic:sym "g_counter"` | the definition with the initialiser | the first tentative definition (`int g_counter;`) | final | **clangd** (a tentative definition counts as a definition for its indexer). `def` answers any of the three; accepted, see open questions | `int g; int g; int g = 100;` |

Harness findings (not tenfold):

| # | what | detail |
|---|---|---|
| H1 | symlinks cannot be put in a corpus | `Runner::copyTree` computes `fs::relative(it->path(), from)`, which resolves symlinks, so a symlinked directory is copied as a symlink named like its **target** (`copy_symlink` fails with "File exists") and the link itself never appears in the scratch copy; for a symlinked **file** `scanAnchors` uses `is_regular_file` (follows links) so the target's anchors are found twice and the corpus fails with `duplicate anchor`. Result: no corpus can contain a symlink. Checked by hand instead: a symlinked directory inside a workspace (`link_lib -> real_lib`, header included through the link) works end to end: clangd answers the real path, the workspace scan does not follow the link (no duplicates), `refs` is complete in both directions |
| H2 | an exception in the engine aborts the whole run | the runner does not catch exceptions from `Editor` calls (`terminate called ...`), so a single throwing line takes all following lines and the JSON report with it, and the `fast def` agreement check (`flushAgreement`) silently performs a `def` that jumps into (and opens) the target file, which can crash a run for a line that is only `fast` |
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
