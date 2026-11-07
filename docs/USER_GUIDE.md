# tenfold user guide

For C++ developers on Windows and Visual Studio who are moving from the 10x editor. Everything here was checked
against the source at the time of writing. Where behaviour is provisional (a guess about 10x that still needs your
confirmation, a Windows feature that has only run in automated tests) the text says so; the list of
those items is in [Provisional and unverified](#provisional-and-unverified). Design background is in
`docs/ARCHITECTURE.md`, the open questions for you are in `docs/OPEN_QUESTIONS.md`.

Contents: [Installing](#installing) - [Opening a workspace](#opening-a-workspace) -
[Approximate and authoritative results](#approximate-and-authoritative-results-the-two-tiers) -
[Navigation history](#navigation-history) - [Pickers and search](#pickers-and-search) -
[Editing](#editing) - [Word wrap](#word-wrap) - [Visual Studio solutions](#visual-studio-solutions) -
[Debugging through Visual Studio](#debugging-through-visual-studio) - [Version control](#version-control) -
[Building](#building) - [Settings](#settings-files) - [Themes](#themes) - [Key bindings](#key-bindings) -
[Logs and crash reports](#logs-and-crash-reports) - [Troubleshooting](#troubleshooting) -
[What does not exist yet](#what-does-not-exist-yet) - [Provisional and unverified](#provisional-and-unverified)

## Read this first: unsaved work

tenfold has no autosave and no crash recovery of buffers, so the prompts below are the only safety net.

* **Quitting asks.** Control Q (`Exit`, tenfold `App.Quit`) and closing the window ask about the documents with unsaved
  edits. One document: **Save / Don't Save / Cancel**. Several: one list dialog, like Visual Studio's, **"Save changes to the
  following items?"**, with a row per document (all checked; Space, Enter or a click on a row toggles it) and
  **Yes / No / Cancel** below. **Yes** saves the checked items (unchecked ones are closed without saving), **No** closes without
  saving, **Cancel** (Escape) aborts. If a save fails (a read-only Perforce file, for example) or the file's encoding cannot
  hold a character, nothing is closed: tenfold stays open, the status bar says why (the encoding case also shows the
  characters). This includes edited documents that no longer have a tab. The same list appears for **Close All Documents**
  (`Workbench.CloseAllDocuments`, command palette), **Close Panel** (`Workbench.ClosePanel`), **Close All But This** and
  **Close All But Pinned** when they would close several modified documents; with one it is the per-file prompt.
* **Closing a tab asks** when it is the last view of a document with unsaved edits (the tab's x, middle click or
  `Workbench.CloseTab`, Control F4 = `CloseFile`). Don't Save closes the document and drops the edits. A second view of the same document (a
  split) closes without asking.
* **Files that change on disk ask** (a checkout, a branch switch or a code generator rewrote them): "The file X has been
  modified outside of the source editor. Do you want to reload it?" with **Yes / Yes to All / No / No to All**. A file
  with unsaved edits always asks (the default choice is No; Yes loses the edits). Yes to All also reloads the other
  changed files that wait and have no unsaved edits, No to All keeps them all. Carets, scroll position and navigation
  history keep their place on a reload. With `editor.reloadUnlessUnsaved = true` a file without unsaved edits is reloaded
  silently (the status bar says so). A file deleted on disk keeps its buffer; its tab says `(deleted)` and the status bar
  `deleted on disk`.

All three follow Visual Studio, because 10x's behaviour is not confirmed (OPEN_QUESTIONS #5), and each has a setting:
`editor.confirmUnsaved` (default `true`; `false` skips every question: closing a tab then keeps the edited document in
memory without a tab, and quitting discards it), `editor.detectExternalChanges` (default `true`; `false` ignores changes on
disk) and `editor.reloadUnlessUnsaved` (default `false`, as in Visual Studio; see [Settings](#settings-files)). The older
`editor.reloadExternalChanges` still works when it is set (migration below).

## Installing

Packages are built by the project's CI (pushes to `main` and `v*` tags) as workflow artifacts. They are not code
signed, there is no installer, and there is no auto-update. There is no licence file in the repository yet
(OPEN_QUESTIONS #18).

| Platform | Package | How |
| --- | --- | --- |
| Windows | `tenfold-<version>-windows-x64.zip` | Unzip anywhere. Run `bin\tenfold-ui.exe <workspace folder>`. |
| Linux | `tenfold-<version>-linux-x64.tar.gz` | Unpack anywhere. Run `bin/tenfold-ui <workspace folder>`. Needs a desktop with OpenGL 3.3 (the build is tested on X11) and the usual system libraries. |
| Linux | `tenfold-<version>-x86_64.AppImage` | Make it executable and run it with the workspace folder as argument. Shared libraries are collected into the image. |
| macOS | `.dmg` | Best effort and unsigned; kept building in CI but not a target. |

The package layout is the same everywhere:

```
bin/tenfold-ui   bin/tenfold-cli   bin/clangd          (.exe on Windows)
lib/clang/<major>/include/...                          clangd's own headers
licenses/   THIRD_PARTY_NOTICES.md   clangd-version.txt
```

`clangd` (release 21.1.8 of the clangd project, SHA-256 pinned at packaging time) sits next to the editor and is used automatically.
The order tenfold looks for it is: the `clangdPath` setting in `.tenfold/workspace.json` (or `--clangd <path>` for
`tenfold-cli`); the `clangd` next to the executable; `clangd` on `PATH`. If `clangdPath` is set but does not resolve,
tenfold does not fall back to another clangd: clangd stays off and everything is approximate. The engine log
(`.tenfold/log.txt`) says which one was used: `clangd: using <path> (setting|bundled|PATH)`.

On Windows `tenfold-ui` draws with Direct3D 11 (hardware, falling back to the WARP software renderer) when the build
includes it; `--renderer=gl` forces OpenGL. `tenfold-cli --version` and `tenfold-ui --version` print the version and
the commit. Default settings, key bindings
and themes are embedded in the program, so there are no asset files to install or lose.

Building from source is described in `README.md`.

## Opening a workspace

A workspace is a folder. tenfold has no open-folder dialog, no menus and no recent-workspaces list yet: choose the
workspace on the command line (a shortcut on Windows works well).

```
tenfold-ui C:\src\Game                     the folder (or a .sln / CMakeLists.txt inside it: its folder is used)
tenfold-ui C:\src\Game src\main.cpp        also open a file (relative to the workspace, else to the current folder)
```

Always pass a workspace. Without one, `tenfold-ui` looks for `examples\sample_project` relative to the current
folder, which exists only in a source checkout.

On start the previous layout (tabs, split panels, pinned tabs, dock state) comes back from
`<workspace>\.tenfold\layout.json`. A first run (no saved layout) opens `src\main.cpp` if the workspace has one, else an
empty window.

The folder is scanned recursively. Source extensions are `.cpp .cc .cxx .c .cu .mm .m`, headers `.h .hpp .hh .hxx
.inl .ipp .tpp .cuh .def .inc` (and files with no extension), and `.txt .md .json .cmake .py .lua .natvis .rc` count as
other files. Directories named `.git .svn .hg .vs .vscode .idea .cache .tenfold node_modules __pycache__` are skipped,
and so are build-output directories: those named exactly `build`, `out`, `bin` or `obj` (case-insensitive), and those
whose name starts with `build-` or `cmake-build` (`build-x64`, `cmake-build-debug`). Look-alikes such as `output`,
`objects`, `builder` or `binding` are scanned. Change `sourceExtensions`, `headerExtensions`, `otherExtensions`,
`ignoreDirs`, `ignoreDirNames` and `ignoreDirPrefixes` in
`.tenfold/workspace.json` (see [Workspace file](#workspace-file-tenfoldworkspacejson)). Symbolic links are not followed
unless `followSymlinks` is set.

### Where the compile flags come from

clangd is exact only with the real compile flags, so the first thing tenfold does is find or make a compile
database. It tries these in order and uses the first that applies:

1. **A `compile_commands.json`** in the workspace root, or in `build`, `out`, `cmake-build-debug`,
   `cmake-build-release`, `_build`, `.tenfold`, or one level below `build/` or `out/`. It is used as it is and is
   never modified. It wins over everything below, including a `.sln` in the same folder.
2. **CMake**, when the root has a `CMakeLists.txt` and `runCMake` is not false: tenfold runs
   `cmake -S <root> -B .tenfold/cmake-build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` with CMake's default generator
   (`cmake` must be on `PATH`). CMake only writes `compile_commands.json` for the Makefile and Ninja generators, not
   the Visual Studio ones; if nothing is produced, tenfold moves on to the next entry. A configure failure is logged.
   If your project is also a `.sln` and you want the solution's flags, set `"runCMake": false`.
3. **A Visual Studio solution**: a `.sln` directly in the root folder (not in subfolders). tenfold evaluates the
   `.vcxproj` files itself and writes `.tenfold/vs/<configuration>/compile_commands.json` with `clang-cl` style
   commands for the active configuration (one folder per configuration, each with its own clangd index). All `.sln` files in the root are used. See [Visual Studio solutions](#visual-studio-solutions).
4. **A heuristic guess**: every source file compiled with `-std=c++20` (`cppStandard` in `workspace.json`), and
   `-I` for the smallest set of directories that makes the `#include` lines of the workspace resolve (found with the
   fast index). Good enough to start, wrong for anything that depends on defines, generated headers or a particular
   include order.

The result is named `existing`, `cmake`, `vcxproj` or `heuristic`. The generated databases are cached in
`.tenfold/compile_commands.json` with a stamp that records what made them, so a warm start still reports `heuristic`
or `vcxproj`; a cached heuristic database is regenerated as soon as a `.sln` or a CMake project appears. (A cache made
by an older tenfold has no stamp and shows as `generated(cached)`.) Delete the file to force regeneration. tenfold's
private clangd config (`.tenfold/clangd-config`) is rewritten whenever the source changes, so the heuristic include
flags never apply to a solution or `compile_commands.json` database. The exact answer for any file is one command away: see
[Seeing the compile command for a file](#seeing-the-compile-command-for-a-file).

tenfold writes only into `<workspace>\.tenfold\` (settings, logs, caches, generated databases) and `<workspace>\.cache\`
(clangd's background index). It does not edit your `.gitignore`; add `.tenfold/` and `.cache/` yourself.

## Approximate and authoritative results (the two tiers)

tenfold answers code questions from two sources:

* **The fast index** is a lexer-based index of the whole workspace, built at start (a warm start only re-reads files
  that changed) and kept up to date as files change. It needs no compile flags and answers in a millisecond or so.
  Its answers are *approximate*: it matches by name, so it cannot resolve templates, `auto`, typedef chains,
  overloads or macro-generated members.
* **clangd** is the real Clang front end run with your compile flags. Its answers are *authoritative*. It needs a few
  seconds to parse a file after you open it, and until its background index finishes it can only see the translation
  unit you are in (a function defined in another `.cpp` is first found by the fast index).

The policy: every navigation question is sent to clangd. If clangd answers within 250 ms you see only its answer.
Otherwise you get the approximate answer right away, and the authoritative one replaces it when it arrives. clangd's
answer always wins. If clangd is not running, the approximate answer is all you get, and it stays labelled.

How the interface shows which is which:

| Where | Approximate | Authoritative |
| --- | --- | --- |
| Symbol search (Ctrl+,), the result lists for several definitions and for references | Rows are greyed and the badge says `verifying...` while clangd is still working, or `approximate (reason)` when no authoritative answer will follow (for example no clangd). | Rows in normal text; the selected row stays selected when the list is swapped. |
| Completion | Greyed items and a side note `approximate (clangd not running)`. | Normal items with their return type and documentation. |
| Hover | A line `approximate (clangd not running)` under the text. | Text from clangd. |
| Go to Definition / Declaration (F12, Ctrl+F12) with a single answer | **No mark.** tenfold jumps at once, even on an approximate answer. If the authoritative answer is a different place it jumps again. Both jumps are in the navigation history, so Back returns. **Exception:** a lone approximate answer that clangd may turn into a choice (a name that is a `typedef` / alias, or a class used like a constructor call such as `Widget(2)`) is not acted on: the status bar says `waiting for clangd`, and tenfold jumps or shows the chooser when clangd answers (or jumps to the approximate answer if clangd fails). | The same jump, once; or the chooser (see [Choosing the target](#go-to-definition-choosing-the-target)). |
| Status bar | `clangd: off` (not running), `clangd: starting`, `clangd: indexing 42%`, `clangd: ready`; while clangd is being restarted or has given up the engine's own text replaces these: `clangd restarting (2)...`, `clangd stopped: crashed 5 times`; `fast index: indexing` then `fast index: N files`. | |

Find References and the clangd-only features (diagnostics, semantic colours of identifiers, signature help) have
no approximate tier: they come from clangd or they are not shown. Without clangd, the colours are the lexer's.

While clangd runs on a **guessed compile database** (no solution, CMake project or `compile_commands.json` was found, or a
database tenfold generated earlier from such a guess is cached) the status bar adds **`flags: heuristic`** next to the
clangd text. Clicking it runs `Help.ShowCompileCommand`. A workspace whose flags come from a Visual Studio solution, CMake
or an existing `compile_commands.json` shows nothing there. To see why a result is approximate or how clangd was
configured for the current file, run `Help.ShowCompileCommand` (below) or read
[Troubleshooting](#why-is-this-symbol-approximate).

### Seeing the compile command for a file

The diagnostic that answers "which flags does clangd use for this file?":

* In the editor: the command **`Help.ShowCompileCommand`** ("Show Compile Command for Current File") opens a read-only
  popup for the file in the current tab. Escape or a click outside closes it. It has **no default key**; run it from the
  [command palette](#command-palette) (Control Shift X), click `flags: heuristic` in the status bar, or bind a key in
  `KeyMappings.10x_settings`, for example `Control Alt F1: Help.ShowCompileCommand`.
* From a shell: `tenfold-cli why <workspace folder> <file>` starts the engine on the workspace, opens the file,
  waits up to 15 s for clangd to finish parsing it (`--wait <ms>` changes that, `--no-clangd` skips clangd,
  `--clangd <path>` picks one) and prints the same report. The file is taken relative to the workspace folder. The exit
  code is 0 when a report was printed and 1 when the file cannot be opened.

The report lists:

* **Compile flags**: where the database came from (existing `compile_commands.json`, CMake, Visual Studio solution,
  heuristic or cached) and the database path with its number of entries;
* **Visual Studio**: the solution and its active configuration, and whether the flags were generated for it (they are
  not when a `compile_commands.json` or CMake took precedence);
* **In database**: whether the file has an entry. If yes, its working directory and the exact command (argv). If no,
  a note: clangd then borrows the command of a similar entry (its own choice, not visible to tenfold) or falls back to
  the *fallback flags* (`-std=<cppStandard>` and `-I<workspace root>`);
* the flags tenfold adds to every file through its private clangd config (`.tenfold/clangd-config`, used in heuristic
  mode on Linux), and the MSVC toolchain (`cl.exe`) used for clangd's `--query-driver` when the flags come from a
  solution;
* **clangd**: the executable, where it came from, its version and state (`running`, `not running`, a restart message);
* **This file** and **Confidence**: `authoritative` (clangd has parsed the file), `approximate` (clangd is not
  running, or still parsing the file), or `not open` (the file is not open in the editor, so clangd has not been given
  it).

## Navigation history

This is the feature 10x lacks. It works like Visual Studio's Navigate Backward / Forward.

* **Control -** (`PrevLocation`) / **Control Shift -** (`NextLocation`) and the mouse thumb buttons
  (`MouseBackButton` / `MouseForwardButton`) go back and forward. A toolbar above the panels has Back and Forward buttons with a tooltip showing the key; **long-press or
  right-click** a button for the list of entries (the file name and line, or the symbol name) and click one to jump
  there. The history keeps 200 entries.
* Every jump records the place you left and the place you arrived: go to definition / declaration / references,
  symbol search, find in files, build-error and diagnostics jumps, bookmarks, Go to Line, the breakpoint list, and the
  debugger moving to the current statement.
* An ordinary caret move that lands **10 or more lines** away in the same file (or in another file) also records the
  place you left. Nearby entries in the same file merge.
* Going back from where you are records that spot first, so Forward returns exactly there. A fresh jump after going
  back discards the forward entries (browser behaviour).
* Entries follow edits: lines inserted or removed above an entry move it; an entry inside a deleted block moves to the
  start of the block.
* **Control Shift Backspace** (`PrevEditLocation`, tenfold `Nav.LastEditLocation`) goes to where you last edited, through the history, so Back
  returns. The remembered spot follows edits above it, like history entries.
* Setting `nav.statusCounts` shows the back and forward counts in the status bar.

Switching tabs is not a navigation jump and is not recorded.

**The switch panel.** **Control Tab** (`ShowSwitchPanelPanel`) opens a list of the panel's open files, most recently used
first, with the previous file selected (like Visual Studio's IDE Navigator); **Control Shift Tab** starts on the oldest one.
While you hold Control, **Tab** / **Shift Tab** (or Down / Up) move the selection, wrapping; nothing is switched until you
**release Control** (or press Enter or click a row), which commits. Escape cancels. With a single tab it does not open.

**Tab well.** Pinned tabs stay at the left of the unpinned ones; dragging or moving a tab never crosses that boundary.
**Control K, Control Z** (Visual Studio's Restore Closed Tab; a tenfold addition, 10x leaves the chord free) reopens the
last closed tab like Control Shift T. A **single click on a file in the workspace explorer** (and a click on a result of Find in
Files or Find All References, and the debugger stepping into a file that is not open) opens it in the one **preview
tab** at the right end of the well, shown in italics (colour keys `tabPreviewActive` / `tabPreviewInactive`, falling back to the
normal tab colours). The next preview replaces it. It becomes a normal tab when you edit the file, double-click the tab or the
explorer item, choose Open (or press Enter) in the explorer, or open the file by name. A preview tab cannot be pinned or
moved. Enter or F8 on a result and a double click open normal tabs. Settings `tabs.previewTab` (default `true`) and
`tabs.previewFromFindResults` (default `true`); VS_BEHAVIOUR section 7.

## Pickers and search

All pickers are keyboard-driven lists: Up / Down / PageUp / PageDown move, Enter accepts, Escape closes, a click on a
row accepts it, a click outside closes it.

| Keys | What |
| --- | --- |
| **Control O** (`OpenFile`) or **Shift Alt O** (`FindFile`); tenfold `File.OpenFile` | Open a file by fuzzy name. The file name weighs four times as much as the rest of the path; recently used files come first and lead the empty list. Several words must all match. Opening is a tab switch, not a history jump. |
| **Ctrl+,** (`Search.SymbolSearch`) | Workspace symbol search: fuzzy and qualified (`geo::Tr`). Approximate rows appear at once and are replaced by clangd's; the right pane previews the target line (`picker.preview`); the **Preview: on/off** switch in the header (click, or **Alt+P**) hides or shows it, like the eye of Visual Studio's Code Search, and the choice is kept until the settings are read again. The function picker has the same switch. Enter jumps and records history. |
| **Ctrl+Shift+F** (`Search.FindInWorkspace`) | Find in files. Results stream in grouped by file with the match highlighted. **Alt+C** case, **Alt+W** whole word, **Alt+R** regex (PCRE2). Add file globs after ` :: `, for example `foo :: *.cpp, !third_party` (`!` excludes). A single-line selection seeds the query. Open buffers are searched with their unsaved text. |
| **Ctrl+Shift+H** (`ReplaceInFiles`; tenfold `Search.ReplaceInFiles`) | Replace in files: the find-in-files query (same options and globs) plus a **replace field** (Tab switches fields). The matches are a checklist grouped by file; each row shows the line with the old text struck and the replacement after it, so the list is the preview. **Insert** checks / unchecks the row (a file row: all its matches), **Alt+T** all / none, a click on the box too. **Enter** replaces the checked matches: open documents are edited in place (one undo step per document, left unsaved), files that are not open are rewritten on disk after **one confirmation** (Enter again; Escape goes back). The replacement is literal text (no `$1`), and a match whose line changed since the search is skipped and counted in the message. |
| **F4** / **Shift F4** (`GotoNextResult` / `GotoPrevResult`) | Next / previous result of the last search or build, whichever started last: a find-in-files result (wrapping, even after the popup is closed; files with matches get marks on the scrollbar) or, after a build, an error or warning with a source location (as **F8** / **Shift F8**, which always walk the build). |
| `View.ShowDiagnostics` (no default key; command palette) | List of the diagnostics of the open files (clangd) and of the last build, errors first; type to filter. clangd only reports files that are open, and the list says so. Files that belong to no translation unit (not in the compile database, not included by one of its files) have no squiggles and are not listed, like Visual Studio's Miscellaneous Files; `diagnostics.miscFiles` turns them on. |
| **F12**, **Ctrl+F12**, **Shift+F12** | Go to definition, declaration, find all references. One definition: jump at once. Several: a list. References always show a list, [grouped](#find-all-references-list). Through a `using` declaration, a `typedef` / alias and on a constructor call the list is a small **chooser at the caret** (below). On an `#include "x.h"` / `#include <x>` line F12 (or Ctrl+F12, or the middle mouse button) opens the file, like Visual Studio's Go To Document ([below](#go-to-document-on-an-include)). |
| **Alt O** (`CppParser.ToggleSourceHeader`) | Switch between header and source (clangd's answer, else by file name). |
| **Ctrl+G** (`Editor.GotoLine`) | Go to line: `42` or `42:7` (line:column, 1-based). Numbers past the end go to the last line. Recorded in the history. |
| **Ctrl+F** / **Ctrl+H** | Find / replace in the current file (see [Editing](#editing)). |
| **Ctrl+Alt+B** | Breakpoint list (see [Debugging](#debugging-through-visual-studio)). |
| `Editor.ListBookmarks` (no default key; command palette) | Bookmark list (see [Editing](#editing)). |

### Go to document on an `#include`

With the caret anywhere on an `#include "x.h"`, `#include <x.h>`, `#include_next` or `#import` line, **F12** (Go To
Definition), **Ctrl+F12** (Go To Declaration) and the middle mouse button (`GotoSymbolDefinitionUnderMouse`) open the file
the compiler would pick and put the caret at its first line, recorded in the navigation history (Back returns). The file
is found the way the compiler looks: for `"x.h"` the directory of the including file first, then the `-iquote`, `-I`,
`-isystem` (clang-cl `/I`, `/imsvc`) directories of the file's compile command (a header that is only included uses the
command of the translation unit that includes it); `<x.h>` skips the including file's directory. This answer is exact, so
it is instant and carries no `verifying...` mark. A spelling on no search path (a system header, a file the project does
not list) is looked up in the workspace by its path tail, nearest to the including file first (greyed, approximate), and
clangd is asked for the rest. Nothing is found for an include that does not exist: the status bar says `No results`.
(Visual Studio: F12 on an `#include` line, VS_BEHAVIOUR N1.)

### Find All References list

Shift+F12 lists the places grouped like Visual Studio's "Definition, then Path" grouping: first a **Definition** heading with
the declarations and definitions of the name, then one heading per file in path order, each with its count. A row shows
**Line:Col**, the source line and what the place does: **Write** (assigned, `+=`, `++` / `--`, also through `a[i] =`),
**Read** (any other use of the value), **Definition**, or **Other** (a preprocessor line other than a condition, `using`,
`typedef`, `namespace`, `class`, `struct`, `enum`). Read and Write are read from the text of the line (clangd does not send
them for every symbol), so a function call is a Read and an argument passed by reference is a Read. The title counts the
places and files. Headings are skipped by the arrow keys. Enter or a click (preview tab, `tabs.previewFromFindResults`) goes
to the place. What the Visual Studio window has beyond this (Group by menu, scope list, filter, Keep Results, checkboxes,
results that arrive while clangd is still searching) is not done; the list is a modal picker.

### Go to definition: choosing the target

Some names stand for more than one thing you may want to see. For these F12 (and the middle-mouse
`GotoSymbolDefinitionUnderMouse`) does not jump at once but opens a small **chooser** next to the caret. Every row has the
kind of the target (`class Widget`, `constructor Widget(int)`), the source line, and `file:line`:

| You press F12 on | The chooser lists | Selected |
| --- | --- | --- |
| a name introduced by a **using-declaration** (`using ns::f;` then `f()`; `using ns::Type;` then `Type t;`) | `using-declaration`, then the entity (`function ns::f(int)`) | the entity |
| a **typedef / alias / alias template** (`typedef Foo Bar; Bar x;`, `using Bar = Foo;`, `template <class T> using Vec = Holder<T>;`) | every alias on the way, nearest first (`alias B3 → B2`, `typedef B2 → B1`, `typedef B1 → Foo`), then the real entity (`struct Foo`) | the entity |
| a **constructor call**: `Widget(2)`, `Widget{2}`, `Widget w(2);`, `Widget w{2};`, `new Widget(2)`, a base or delegating `: Widget(1)` | `class Widget`, then the constructor that is called (`constructor Widget(int)`; the overload the compiler picks, at its definition) | the constructor |
| a call of an **inherited** constructor (`using Base::Base;`, then `Derived d(1)`) | `struct Derived`, then the constructor that was resolved (`constructor Base(int) (inherited)`) | that constructor |

Keys: **Up / Down** move, **Enter** jumps (it is a navigation jump, so **Back** returns), **Escape** closes it and nothing
moves; a click on a row jumps. Only the keyboard is needed.

Not a choice, so F12 jumps at once as before: a name with a single meaning, a `typedef` of a builtin type (`typedef int
myint`; the status bar shows `typedef myint → int`), a class with **no written constructor** (the implicit one: tenfold
jumps to the class and the status bar says `... (implicit constructor)`), and aggregate initialisation (`Plain p{1, 2}`
calls no constructor). F12 on a declaration itself (the name of a constructor, the alias being declared) is unchanged.

The chooser needs clangd. Without it the instant answer is the entity or the class, as before. While clangd is still
working, a lone answer that might become a choice waits for it (status bar: `waiting for clangd`) instead of jumping and
then asking; if clangd fails, tenfold jumps to the instant answer.


Hover: rest the mouse on an identifier for 400 ms. Diagnostics under the mouse are shown first (the last build's
message for that spot follows, marked `build`), then clangd's hover text. Completion opens at the caret after `.`,
`->`, `::` and once a word reaches two characters; Tab or Enter accepts it as one undo step. Signature help opens on
`(` and `,` with the active parameter highlighted; Escape or `)` closes it.

**Parser commands (10x's, W8c).** All of them are 10x commands on 10x's default keys:

| Keys | What it does |
| --- | --- |
| **Control Space** (`Autocomplete`) | Opens the completion list at the caret whatever the length of the word (an empty word lists everything the scope offers). Without clangd the list is the fast index's, greyed and marked approximate. The status bar says when there is nothing to offer. |
| **Control Shift Space** (`ShowFunctionArgsInfo`) | Signature help for the call around the caret, with the argument under the caret highlighted. clangd only; outside a call, or without clangd, the status bar says so. |
| **Control K, Control I** (`ShowSymbolInfo`) | The hover popup for the symbol at the caret (diagnostics first, then clangd's text; without clangd the fast index's guess, greyed). Any key dismisses it. |
| **Control R** (`RenameSymbol`), then **Enter** (`SubmitRenameSymbol`) | A small box under the symbol, pre-filled with its name and selected. Enter asks clangd to rename it everywhere: open files are edited in place (one undo step per file, left unsaved), files that are not open are edited and written to disk. Escape or a click elsewhere cancels. **Rename needs clangd**: without it tenfold says so and does nothing, because an approximate rename would change the wrong things. A name that is not an identifier keeps the box open. |
| **Alt M** (`FindFunction`) | Picker of the functions *defined* in the current file (declarations without a body are left out), in source order; type to filter. The fast index's list shows first, greyed with `verifying...`, and clangd's replaces it with the selection kept. Enter jumps there (Back returns). |
| **Middle mouse button** (`GotoSymbolDefinitionUnderMouse`) | Jumps to the definition of the identifier under the mouse pointer, not the one at the caret; the caret does not move. Over anything else (a tab, the gutter, blank space) the click does what it did before: on a tab it closes the tab. |
| **Control K, Control F** (`FormatSelection`) | clangd formats the selection; with no selection, the innermost `{ }` block around the caret (the caret line when it is outside every block). One undo step. Uses your `.clang-format`. clangd only. |

`InsertPrevAutocomplete` / `InsertNextAutocomplete` (`Shift F14` / `F14`) are not implemented: 10x's description is one line
("insert prev / next autocomplete") and does not say what is inserted or from which list.

## Editing

The text model is UTF-8. A file that is not valid UTF-8 opens as Windows-1252 (Latin-1 / "ANSI") or, with a BOM, as UTF-16,
and is saved back in the same encoding. If the text holds a character that encoding cannot hold (a Windows-1252 file
with `漢` or an emoji in it; UTF-16 can hold everything except stray bytes), tenfold **refuses to save**: nothing is written, the
file stays modified, and a dialog lists the offending characters (the character itself, its `U+XXXX` code point and
`line, column`; the first eight, then "and N more") and says which encoding cannot hold them. **Enter** (or a click) on a
character jumps to it, **Escape** closes the dialog; remove or replace the characters and save again. The same happens
for Save All (the other files are still saved), when you close a tab or quit and answer Save (the tab stays open, the
quit is cancelled), and when a rename writes closed files (they stay open and modified). Replace in Files does not write
a closed Windows-1252 file when the replacement text has such a character, and says so.

Encodings are handled like Visual Studio does:

* The **status bar** shows the document's encoding next to the line ending: `UTF-8`, `UTF-8 with signature` (a file that
  starts with the UTF-8 byte order mark), `UTF-16 LE`, `UTF-16 BE` or `Windows-1252`. Clicking it (left or right button)
  opens a menu with **Reopen with Encoding...** (`File.ReopenWithEncoding`) and **Save with Encoding...**
  (`File.SaveWithEncoding`); both commands also run from the command palette and have no default key.
* **Reopen with Encoding** lists UTF-8, UTF-16 LE, UTF-16 BE and Windows-1252 (the current one is marked) and reads the
  file's bytes from disk again with the one you pick, for a file that was detected wrongly. A modified document first
  asks "Save changes to a.cpp?" (Save / Don't Save / Cancel); a Save that is refused (see above) shows the refusal dialog
  and does not reopen. The undo history starts over, as after a reload. UTF-16 needs well-formed text (otherwise the
  status bar says so and nothing changes); a file read as UTF-8 that is not valid UTF-8 keeps its stray bytes and is
  written back unchanged.
* **Save with Encoding** is Visual Studio's *Advanced Save Options*: one list with **Save** on top, the **Encoding**
  group (UTF-8, UTF-8 with signature, UTF-16 LE, UTF-16 BE, Windows-1252) and the **Line endings** group (Current Setting,
  Windows (CR LF), Macintosh (CR), Unix (LF)). Enter on a choice selects it (`(*)`), Enter on Save writes the file in that
  encoding and with those line endings, and the document keeps them. An encoding that cannot hold the text (Windows-1252
  for a document with `漢`) is **refused** like any other save: the refusal dialog lists the characters, and the
  document keeps its encoding and line endings. This is the way out of a refusal: the dialog has a **Save with
  Encoding...** row (tenfold's convenience; Visual Studio has no such dialog) that opens this list with UTF-8 selected.
  For an untitled document it only chooses the encoding; Save As writes it. `File.SaveWithLineEndings` stays as the
  shortcut for the line-ending half.
* The encoding of a file is detected when it is opened: valid UTF-8 is UTF-8 (with a signature if it has one), a UTF-16
  byte order mark selects UTF-16, anything else is read with the fallback code page. `editor.autoDetectUtf8 = false`
  (Visual Studio's *Auto-detect UTF-8 encoding without signature*) turns the UTF-8 guess off: a file without a signature
  and with non-ASCII bytes is then read with the fallback code page. The fallback is Windows-1252; on Windows tenfold
  would use `GetACP()` like Visual Studio, but its codec only knows Windows-1252, so another system code page (932 on
  Japanese Windows) is read as 1252 and the log says so once. New files (`NewTextFile`) are UTF-8 unless
  `editor.defaultEncoding` says `utf-8-bom`, `utf-16le`, `utf-16be` or `windows-1252`.
* `.editorconfig` is not read: its `charset` has no effect.

Line endings are handled like Visual Studio does:

* LF, CRLF and classic Mac CR are recognised, and **every line keeps its own ending**. A file is saved exactly as it was
  loaded, also when its lines end in different ways; nothing is normalised behind your back.
* **Enter** (and every command that adds a line) gives the new line the ending of the line the caret is on
  (`editor.replicateNewLine`); a file without any line break uses `editor.defaultLineEnding`.
* **Paste** keeps the line breaks of the pasted text (`editor.pasteKeepsLineBreaks`), so pasting CRLF text into an LF file
  makes the file mixed. **Copy** puts the stored breaks on the clipboard.
* **Opening a file** whose lines end differently asks "The line endings in the following files are not consistent. Do you
  want to normalize the line endings?" (`editor.checkLineEndings`). The choices are *Yes* with an ending (Windows (CR LF),
  Unix (LF), Macintosh (CR); the file's most frequent ending is pre-selected) and *No*. Yes rewrites the line endings as
  one edit: the file becomes modified and Undo brings the old endings back. No (or Escape) opens the file as it is. The
  question is asked when the file is opened, never while typing or pasting.
* The **status bar** shows the document's line ending (`CRLF`, `LF` or `CR`; a mixed file shows its most frequent
  one). Clicking it (or right-clicking) offers Windows (CR LF), Unix (LF) and Macintosh (CR): the whole document is
  converted as one undoable edit. **Save with Line Endings...** (`File.SaveWithLineEndings`, also in that menu) is Visual
  Studio's Advanced Save Options: Current Setting / Windows (CR LF) / Macintosh (CR) / Unix (LF); the choice converts the
  document and saves it. `Editor.LineEndingLF`, `Editor.LineEndingCRLF` and `Editor.LineEndingCR` run the same
  conversions from the command palette; none has a default key.
* Not like Visual Studio: the Unicode separators NEL, LS and PS are not line breaks here (clangd does not treat them as
  such), and `.editorconfig` `end_of_line` is not read.

**Carets.** Shift+Alt+Up / Down (`MultiCursorMoveUp` / `Down`) add a caret above / below, **Ctrl+D** (`SelectNextMultiCursor`) selects the word under the caret and then adds the
next occurrence, **Ctrl-click** adds a caret, **Alt-click** adds a caret and **Alt-drag** makes a column selection,
double-click selects a word, triple-click a line. Carets that touch merge. Every edit acts on every caret and is one undo
step; Ctrl+C / Ctrl+X with several carets copy one line per caret, and pasting as many lines as carets puts one line in
each. Escape collapses to the primary caret (it does not quit).

More multi-caret keys (10x's): **Ctrl+Shift+D** (`UndoSelectNextMultiCursor`) takes back the last **Ctrl+D** step, one per press, as long as the carets and the text are unchanged since it; **Alt+F3** or **Ctrl+Shift+Alt+J** (`SelectAllOccurrences`) selects every match of the find bar's text and options while the find bar is open, else of the selected text, else of the word at the caret; **Alt+A** (`AlignMultiCursor`) pads with spaces in front of each caret so that all line up at the rightmost one (tabs count as `editor.tabWidth` stops; of several carets on one line the first moves; one undo step); **Shift+Alt+Left / Right** (`MultiCursorMoveLeft` / `MultiCursorMoveRight`) increase / decrease the selection end of every caret by one character as 10x describes them (see the table below); **Ctrl + middle click** (`AddCursorAtMousePos`) adds a caret under the pointer in an editor view (a plain middle click, and a Ctrl+middle click on a tab, behave as before).

**Paste history.** **Ctrl+Shift+V** (`PasteHistory`) opens a list of the texts you copied or cut in tenfold (Copy, Cut,
CutLine; a change another program made to the clipboard is noticed when the window regains focus), newest first, each as
its first line (`...` and `N lines` when there are more). Type to filter. **Enter** pastes the chosen entry with the normal
Paste rules (several carets and as many lines: one line each), moves it to the top and makes it the clipboard text, so
Ctrl+V pastes it again. `editor.pasteHistorySize` (default 20; 10x documents no size) sets how many it keeps, 0 turns it
off. The list opens in the middle of the window like the other pickers, not at the caret.

**Key sequences.** **Ctrl+Shift+R** (`RecordKeySequence`) starts recording (the status bar shows `REC`), the same key
stops. **Ctrl+Shift+P** (`PlaybackKeySequence`) replays the last recording as one undo step of the document that was
current when it started. A recording holds the commands that ran (key bindings, chords, the command palette's choice) and
the text typed into the editor, not mouse clicks and not what you type into a picker or the find bar; a command that opens
one (`GoToLine`, `Find`) replays as just opening it. Starting a recording replaces the last one; stopping one with nothing
in it keeps the old one. The recording lasts until tenfold quits.

**Navigation and bookmarks in the current file.** **Ctrl+=** / **Ctrl+Shift+=** (`PrevLocationCurrentFile` / `NextLocationCurrentFile`) walk the navigation history like Ctrl+- / Ctrl+Shift+- but only over the entries of the file you are in. **Ctrl+Shift+T** (`RestoreRecentlyClosedFile`) reopens the file whose last tab was closed most recently, at the caret it had; press it again for the one before. **Alt+F2** / **Shift+Alt+F2** step to the next / previous bookmark of the current file (wrapping), **Ctrl+Shift+F2** removes all bookmarks of the current file, **Ctrl+Shift+F9** all its breakpoints.

**Undo.** Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z. Typed characters are grouped into one undo step per word: the group ends at
whitespace, after a 1 s pause, or when the caret moves. Set `"undoGrouping": "char"` in `.tenfold/workspace.json` for one
step per character (`undoGroupPauseMs` changes the pause). How 10x groups typing is not confirmed (OPEN_QUESTIONS #3).

**Conveniences** (the defaults follow 10x's documented settings, see the settings table below; default keys are
10x's, see [Key bindings](#key-bindings)):

* Auto-indent on Enter (`editor.autoIndent`): copies the previous line's indentation, one level deeper after a line
  ending in `{`, `(`, `[` or a binary operator, returns one level when a continued statement ends with `;`; Enter
  between `{` and `}` moves the closer to its own line when `editor.formatBracesOnEnterSplit` is on; typing `}` as the
  first character on a line dedents it to the line of its `{` (`editor.autoIndentBraces`). Not done: the body of a brace-less `if` / `for`, aligning continuation lines under an open
  parenthesis, `case` labels.
* Indent unit (`editor.indentUnit`, a tab by default; `editor.detectIndent`, off by default): when detection is on, taken
  per file from its first 400 lines, else the setting. Tab inserts one unit (a multi-line selection indents its lines); Shift+Tab outdents.
* Auto-close, all off by default (braces and brackets as in 10x: `editor.autoAddCloseBrace` for `{`, `editor.autoAddCloseBracket` for `( [`; quotes: `editor.autoCloseQuotes` for `" '`, which Visual Studio C++ pairs by default but the row is unverified, VS_BEHAVIOUR Z1 / V12): the closer is inserted when the next character is blank, the end of the line or
  one of `) ] } ; ,`; quotes are not paired after an identifier character, a backslash or the same quote; typing the
  closer steps over it (tenfold does not remember which closers it inserted); Backspace inside an empty pair deletes
  both. Typing `(`, `[` or `{` over a selection encloses it (`editor.bracketSelectedText`, on by default, independent of
  auto-close); quotes wrap a selection only with `editor.autoCloseQuotes`.
* **Ctrl+/** and **Ctrl+Shift+/** (`ToggleComment`) toggle `//` comments at the lines' minimum indentation;
  `Editor.ToggleBlockComment` (no default key) wraps each selection in `/* */`
  (a bare caret: its line's text) and unwraps again. C and C++ comment syntax only.
* **Control E, Control V** (`DuplicateLine`) duplicates the line (or the selection), **Control Shift Up / Down**
  (`MoveLineUp` / `MoveLineDown`) move lines, **Ctrl+J** joins lines, **Control Shift L** (`DeleteLine`) or
  **Ctrl+Shift+K** deletes lines.
* **Bookmarks**: Ctrl+F2 toggles, F2 / Shift+F2 step through all bookmarks of the workspace (wrapping), `Editor.ListBookmarks` (no default
  key) lists them. They follow edits and are saved in `.tenfold/bookmarks.json`.
* **Find / replace in the file** (Ctrl+F, Ctrl+H): a bar at the top right of the editor; typing searches at once and
  highlights every match, Enter / F3 next, Shift+Enter / Shift+F3 previous, Alt+C / Alt+W / Alt+R toggle case, whole word,
  regex. In the replace field Enter replaces and moves on, Ctrl+Enter replaces all (one undo step), Tab switches fields,
  Escape closes. Regex replacements do not expand group references (`$1`). **Alt+R** / **Shift+Alt+R** (10x's
  `ReplaceNextInFile` / `ReplaceAllInFile`) replace the next match from the caret (the selected one first) / every match
  of the file using the bar's last query, options and replacement text, from the editor or from the bar when its replace
  row is shown; the bar need not be open. They say what they did in the status bar. In the find-only bar (Ctrl+F) and
  in pickers Alt+R stays the regex toggle; **Alt+E** also toggles regex in the bar.
* `editor.trimTrailingWhitespace` removes trailing blanks on save (off by default; 10x documents no such setting). A `.editorconfig` with `trim_trailing_whitespace = true` / `insert_final_newline = true` does the same for the files it matches when `editor.useEditorConfig` is on (off by default).
* The matching bracket of the one at the caret is highlighted. `editor.showWhitespace`, `editor.columnGuide`,
  `editor.highlightCurrentLine` and the caret blink are settings.

Not available yet: code actions and fix-its, call and type hierarchy, inlay hints, the document outline.
The engine has them; the GUI does not (see [What does not exist yet](#what-does-not-exist-yet)). Rename and formatting
are the Control R and Control K, Control F commands above.

## Code folding

C and C++ files have regions you can collapse (10x's CollapseRegions commands). A region is a `{ ... }` block that
spans at least two lines, a multi-line `/* ... */` comment, or a `#pragma region` / `#pragma endregion` pair; braces in
strings, character literals and comments do not count. (10x documents no region syntax; `#if` blocks are not regions.)
A collapsed region shows its first line followed by a marker (`...}` after a line ending in `{`, else `{...}`; `...*/`
for a comment) and hides the other lines, including the closing one. Hidden lines are skipped by the caret keys, mouse
clicks, the scrollbar and selections.

| Key | 10x command | tenfold id | What it does |
|---|---|---|---|
| `Control Shift [` | `CollapseRegion` | `Editor.CollapseRegion` | collapses the innermost open region around the caret line |
| `Control Shift ]` | `ExpandRegion` | `Editor.ExpandRegion` | expands the collapsed region whose first line has the caret |
| `Control M, Control M` | `ToggleCollapseExpandRegion` | `Editor.ToggleCollapseExpandRegion` | the two above in one key |
| `Control M, Control L` | `ToggleCollapseExpandAllRegions` | `Editor.ToggleCollapseExpandAllRegions` | Visual Studio's Toggle All Outlining: nothing collapsed: collapse all; otherwise (all collapsed, or some of each) expand all |
| `Control M, Control O` | (tenfold command) | `Editor.CollapseToDefinitions` | Visual Studio's Collapse to Definitions: collapses the bodies of functions (and other members) and leaves namespaces, classes, structs, enums, comments and `#pragma region` blocks open; everything else expands. 10x has no such command and does not bind this key |
| | `CollapseAllRegions` / `ExpandAllRegions` | `Editor.CollapseAllRegions` / `Editor.ExpandAllRegions` | collapse the outermost regions / expand the collapsed ones that are visible; nested ones keep their state |
| | `CollapseAllRegionsRecursive` / `ExpandAllRegionsRecursive` | `Editor.CollapseAllRegionsRecursive` / `Editor.ExpandAllRegionsRecursive` | every region of the file, at every depth |

A caret that lands in a collapsed region (go to line, find, a build error, Left from the line below) opens it, and so
does any edit that touches its lines, including typing at the end of its first line. Collapsed regions move with
their lines when you edit elsewhere. Click the marker to expand; resting the pointer on it shows the hidden code in a
tooltip (Visual Studio does the same). `editor.showFoldGlyphs` (default on, like 10x's ShowCollapseRegionMargin and
Visual Studio's outlining margin) draws the outlining margin between the line numbers and the text: a boxed minus on
the first line of every open region with a guide line down to a short tick on its last line, a boxed plus on a
collapsed one; click a box to collapse or expand. The three `outline.*` settings are Visual Studio's C/C++ options
(Tools > Options > Text Editor > C/C++ > View > Outlining), all on there: `outline.enabled` (off: no regions and no
margin), `outline.pragmaRegions` (`#pragma region` blocks) and `outline.statementBlocks` (off: braces inside a function
body are not regions; functions, classes, namespaces and comments still are). The marker colours are the theme keys
`foldMarkerBackground`, `foldMarkerText` and `foldGlyph` (the margin's lines and boxes). Regions are
found in the background once typing pauses; the commands compute them on the spot when they are out of date. Folds are
kept per open document view for the session, not saved (Visual Studio does not keep them either: a file opens
fully expanded).

## Word wrap

Soft wrap is **on by default** (10x's behaviour as you described it, OPEN_QUESTIONS #4). Long lines break at word
boundaries, continuation rows are indented, the line number appears on the first row only, and Up / Down / Home / End
move by visual row. There is no hard wrap or reflow. `View.ToggleWordWrap` (**Alt+Z**, Visual Studio's key) turns wrap
off or on in every view until you restart; the persistent switch is `editor.wordWrap`. Smooth scrolling is off by
default (`editor.smoothScroll`).

## Visual Studio solutions

If the workspace folder contains a `.sln` (and no `compile_commands.json` or CMake project takes precedence, see
[Where the compile flags come from](#where-the-compile-flags-come-from)), tenfold reads the solution and its C++
projects itself, without MSBuild, and gives clangd the commands `cl.exe` would get, as `clang-cl` commands.

**Active configuration.** The solution's `Configuration|Platform` list decides what is available. The setting is
`vsConfiguration` / `vsPlatform` in `.tenfold/workspace.json` (default `Debug` / `x64`). If the solution lists it, it is
used; otherwise `Debug|x64` if the solution has it; otherwise the first configuration in the file (the engine logs the
fallback). Each project is evaluated with the configuration *the solution maps it to* (so `Debug|x86` can mean
`Debug|Win32` for a project). A project the solution maps but does not *build* in that configuration (an
`.ActiveCfg` line without `.Build.0`) is still evaluated with the mapped configuration, as Visual Studio's IntelliSense
does; a project the solution does not map at all is skipped and its files get no entry in the database.

To change it:

* the command **`Build.SelectConfiguration`** (a configuration picker, being added to the window at the same time as
  this guide; the engine side, `Editor::selectVsConfiguration`, already exists; if your build does not have the command,
  use the CLI);
* or `tenfold-cli vsconfig <folder> --list` and `tenfold-cli vsconfig <folder> --set "Release|x64"`, or edit
  `workspace.json` and restart.

To see the commands a solution gives clangd, `tenfold-cli compdb Game.sln --config Debug --platform x64` prints the
database to stdout; `-o <file>` writes it to a file. It does not write next to the solution on its own: a
`compile_commands.json` in the workspace folder takes precedence over the `.sln` (see above) and would freeze the flags.
Write the file somewhere else, or only into a folder you want to use that way.

Changing it regenerates the compile database of that configuration (under `.tenfold/vs/`) and restarts clangd
against it. Every configuration you visit keeps its own clangd index (the four most recently used are kept, see
`maxVsConfigIndexes`), so find-references and rename only see what the active configuration compiles, and going back to
a configuration you used before is quick. A header that only the previous configuration includes and that is still open
is not given to the new clangd until you edit or reopen it. **The build uses the same
setting**, so clangd and `msbuild` agree. (Selecting a configuration rewrites `workspace.json` with the keys tenfold
knows; see [the caveat](#workspace-file-tenfoldworkspacejson).) The OPEN_QUESTIONS #2 default (Debug|x64) is a guess
about which configuration you parse with in 10x.

**What is read**

* The `.sln`: the project list, the solution configuration list, and the project-to-configuration mapping
  (`ProjectConfigurationPlatforms`, `.ActiveCfg` and `.Build.0`). Only C++ (`.vcxproj`) projects; C# and other projects
  are ignored.
* The `.vcxproj` files, with MSBuild's evaluation order: properties and conditions, `PropertyGroup`,
  `ItemDefinitionGroup`/`ClCompile` metadata, `ClCompile` items (wildcards, `Condition`, per-file metadata that overrides the
  project-wide one, `ExcludedFromBuild` per file and configuration), `Choose/When/Otherwise`, `%(Name)` self references.
* Imported property sheets (`.props`) and `Directory.Build.props` / `Directory.Build.targets` (the nearest file
  walking up from the project, including chains through `GetPathOfFileAbove`).
* Resulting flags: language standard, exception handling, include directories (`/I`, and external includes as
  `/imsvc`), `/D` and `/U` (with the implicit `_WIN32`, `_WIN64`, `_DEBUG`, `_DLL`, `_MT`, `UNICODE`, `_UNICODE`, `_MBCS` that
  `cl` defines from the project's settings), forced includes and the precompiled header as a forced include (not
  `/Yu`), `/permissive-`, `/GR-`, runtime library, `/arch`, `/openmp`, `AdditionalOptions`, `CompileAs`, target platform.
  A language standard that the project leaves unset becomes `/std:c++17` (a superset of `cl`'s default, on purpose).
* Anything that cannot be resolved (an unknown property, a toolchain macro such as `$(VC_IncludePath)`, a registry
  value) drops only the entry that contains it, never a half-written path.
* A change to the `.sln`, a `.vcxproj`, a `.props` or `.targets` file or `Directory.Build.*` triggers a refresh.

**What is not**

* `.slnx` solutions, non-C++ projects, `Update=` / `Remove=` items, `ProjectReference` include propagation,
  `PlatformToolset`-dependent defaults, `UndefineAllPreprocessorDefinitions`, instance property functions
  (`$(x.Replace(...))`), and other numeric condition operators.
* Warning switches (`DisableSpecificWarnings`, warning level): clangd's diagnostics are clangd's own, not `cl`'s.
* `/Yu` precompiled-header semantics, and struct alignment / calling-convention flags (put them in `AdditionalOptions`
  if you need them).
* Toolset files under `$(VCTargetsPath)` are never imported. The system headers (the MSVC STL, the Windows SDK) come
  from the compiler instead: tenfold finds `cl.exe` (from `VCToolsInstallDir`, `vswhere`, or the `msvcToolchain` setting
  in `workspace.json`, which it saves once found) and starts clangd with `--query-driver` for it. On Windows, if `cl.exe`
  is not found the log says `cl.exe not found; set "msvcToolchain" in .tenfold/workspace.json`. On Linux and macOS
  (no MSVC) tenfold parses the project against **this machine's standard library** instead (the log says so once): your
  defines, include directories and forced includes are kept, `<vector>` and friends resolve, but `-m32`/`-m64` are
  dropped and the Windows SDK (`<windows.h>`) is not available.
* Unreal Engine `.uproject` workspaces: not supported (UE's generated `.sln` and `compile_commands.json` work as any
  other).

## Debugging through Visual Studio

tenfold does not contain a debugger. It drives Visual Studio's own debugger through Visual Studio's automation
interface (EnvDTE), so you edit in tenfold and stop in Visual Studio's engine. This is **Windows only**; elsewhere
the debug commands report `no debugger backend on this platform`. It has been exercised by automated tests, including a
non-blocking CI job that runs a real Visual Studio, and not yet by long use on your solution.

**How tenfold finds or launches Visual Studio.** The first debug command connects. tenfold looks through the running
Visual Studio instances (newest first) for the one whose open solution is your workspace's `.sln` (the first `.sln` in
the folder). If none has it open, it starts a new Visual Studio through automation, makes its window visible, and opens
the solution in it. Only if that fails does it run `devenv.exe` (found with `vswhere -latest`, or the path in the
environment variable `TENFOLD_DEVENV`) and wait up to 90 s for an instance with the solution. That Visual Studio
stays open when you close tenfold. A workspace without a `.sln` uses the newest running instance and says so. A busy
Visual Studio (a modal dialog open) is retried and then reported as `Visual Studio is busy`.

**Attach at startup.** If Visual Studio *already* has the solution open when tenfold starts, tenfold attaches by
itself so breakpoints you set reach it; it never launches Visual Studio until you press a debug key. While nothing is
attached and you have breakpoints, it retries every 10 s, so opening the solution in Visual Studio later picks them
up. Switch it off with `"debug.attachAtStartup": false` in `workspace.json`.

**Breakpoints sync both ways.** A click in the narrow lane left of the line numbers, or F9, toggles a breakpoint; it is
pushed to Visual Studio. Breakpoints you set in Visual Studio appear in tenfold. Disabled breakpoints draw as a ring,
conditional ones with a plus. Your breakpoints are saved in `.tenfold/breakpoints.json` and follow edits like
bookmarks. Only line breakpoints are synced; function breakpoints are ignored. When both sides disagree, Visual
Studio wins for the state it reports.

**Keys** (10x's, which are Visual Studio's, see the table in [Key bindings](#key-bindings)): F5 start / continue, Ctrl+F5
start without debugging, Shift+F5 stop, Ctrl+Shift+F5 restart, F9 toggle breakpoint, F10 step over, F11 step into,
Shift+F11 step out; tenfold additions: Ctrl+F9 enable / disable, Ctrl+F10 run to cursor, Ctrl+Alt+Cancel (Break) break all,
Ctrl+Alt+B list breakpoints (`RemoveAllBreakpoints`, delete all, has no key; Ctrl+Shift+F9 is 10x's
`RemoveAllBreakpointsFromFile`: the current file's breakpoints only, pushed to Visual Studio like any other change). In the breakpoint list Enter jumps, Space enables / disables and
Delete removes. `Debug.EditCondition` (no key) sets the condition
of the breakpoint on the caret's line; an empty condition removes it. `Debug.OpenInVisualStudio` opens the current file at
the caret in Visual Studio's editor (the window command is being added at the same time as this guide; the engine side,
`Editor::openInVisualStudio`, exists).

**While debugging.** The status bar turns orange in the `vs-dark` theme and shows the session state. When execution
stops, tenfold jumps to the current statement (through the navigation history, so Back returns) and shows an arrow and a
line band in every view of that file. Commands are asynchronous; an error such as `Visual Studio did not open <sln>
within ...` goes to the status bar and the log. Locals, watches and the call stack are in Visual Studio: tenfold has no
windows for them.

## Version control

tenfold detects Perforce first, then Git, for the workspace folder, when the first file is opened. Detection results are
in the log (`vcs: git at <root>`, `vcs: perforce at ...` or `vcs: no version control system found for <root>`).
`vcs.provider` in `.tenfold/workspace.json` (`auto`, `perforce`, `git`, `none`) narrows it and `vcs.p4Path` names the `p4`
executable; both can be written nested (`{"vcs": {"provider": "git"}}`) or flat (`"vcs.provider": "git"`), and are read
at start.

**What you see.** A narrow change strip between the line numbers and the text: green for added lines, blue for modified
lines, a red wedge where lines were deleted; the same marks on the scrollbar. A small square on the tab (filled for
modified, added or checked out, outlined for Perforce read-only), and a status bar segment such as `Git: modified` or
`P4: checked out`. A file that is not tracked gets the tab mark and the segment, but no change strip.

**Inline diff.** Click the strip, or run `Vcs.ToggleInlineDiff` with the caret on the change: a block opens under it
showing the old lines on a red tint, with a button to revert that change and a close button. Escape closes the blocks.

**Revert.** `Vcs.RevertHunk` reverts the change at the caret (one undo step). `Vcs.RevertFile` asks first (`Discard all
changes` / `Cancel`) and then reloads the file from disk.

**Perforce.** The first edit of a file you do not have open for edit runs `p4 edit` on it, in the background; typing is
never blocked, and if the checkout fails the message appears in the status bar and your edit stays in the buffer.
`Vcs.CheckOut` does the same on demand. The base text for the diff is `p4 print file#have`. A file that is read-only and
not checked out shows `P4: read-only — will check out on edit`; saving such a file says
`<name> is read-only (not checked out in Perforce; run p4 edit, or revert the edit)`. tenfold never runs `p4 add`
for you. **It has only been tested against a fake `p4`, not a real server** (OPEN_QUESTIONS #17).

**Git.** Status, the diff against `HEAD`, and revert use the `git` on `PATH`. Staged new files show as added; untracked
files show nothing in the strip.

**External diff.** `Vcs.DiffFileExternal` writes the committed text and your buffer (unsaved changes included) to
temporary files and starts p4merge: `vcs.diffTool` empty looks for `p4merge` (Windows: `p4merge.exe` on `PATH`, then
`%ProgramFiles%\Perforce\p4merge.exe`); set `vcs.diffTool` and `vcs.diffToolArgs` for another tool. If none is found
the status bar says `no diff tool found: install p4merge or set vcs.diffTool`. With the default arguments and p4merge, the titles
`<file>#have` (Perforce) or `<file>@HEAD` (Git) and `<file> (unsaved)` are passed as well.

`Vcs.RefreshStatus` re-reads the state; tenfold also does that whenever its window regains focus, because commits made
elsewhere do not touch the files. 10x's Perforce keys are bound: **Control P, Control E** (`PerforceOpenForEdit`, `Vcs.CheckOut`), **Control P, Control D**
(`PerforceDiff`, `Vcs.DiffFileExternal`) and **Control P, Control R** (`PerforceRevert`, `Vcs.RevertFile`). The other
`Vcs.*` commands (`NextChange`, `PrevChange`, `ToggleInlineDiff`, `RevertHunk`, `RefreshStatus`) have no key; copy
examples into your own `KeyMappings.10x_settings`:

```
Alt Shift Down: Vcs.NextChange
Alt Shift Up: Vcs.PrevChange
Alt Shift D: Vcs.ToggleInlineDiff
Alt Shift Z: Vcs.RevertHunk
Alt Shift F5: Vcs.RefreshStatus
```

**History and p4v.** **Control P, Control H** (`PerforceHistory`) runs `p4 filelog` for the current file and lists its
revisions (`#rev  change N  date  user`, the first line of the description, the action); type to filter by any of that.
**Enter** opens that revision (`p4 print`) in a new tab, read-only: the text is written to a temp file
`<temp>/tenfold-p4/<id>/<name>#<rev><ext>` that is read-only on disk, so it cannot be saved over anything. Diffing a revision
is not offered here (Control P, Control D diffs the working file with `#have`). Dates are UTC. **Control P, Control V**
(`PerforceShowInP4v`) starts `p4v -p <port> -u <user> -c <client> -s <depot path>`, taking port, user and client from `p4 info`
(so `P4CONFIG` and `p4 set` apply) and the depot path from `p4 where`; `vcs.p4vPath` names another program than `p4v`.
Both say so in the status bar when the workspace is not under Perforce. Like the rest of the Perforce support they have
only been run against the fake `p4`.

Not done: blame, commit or stage UI, side-by-side diff inside tenfold.

## Workspace explorer

**Control Shift E** (`ToggleWorkspaceExplorer`) opens the explorer on the left of the window and focuses it; the same key
closes it. It is a dock like the Build panel: drag its divider to resize it; whether it is open and how wide is saved in
`.tenfold/layout.json`. It starts closed, except the very first time a workspace with a solution is opened (no
`layout.json` yet): then it opens, without taking the keys, like Solution Explorer (`explorer.openWithSolution`).

**What it shows.** For a workspace with a `.sln` (the first one in the folder): the solution, its solution folders, the
projects and each project's files, as the `.vcxproj` lists them (`gen\*.cpp` style wildcards are expanded; items that need
MSBuild properties other than `$(ProjectDir)` / `$(SolutionDir)` are left out). If the project has a `.vcxproj.filters`
file, the files sit under its filters (empty filters too); a file the project lists but the filters file does not is at the
project's top level. Without a filters file the files are grouped by their folders on disk. Folders and filters come first,
then files, each alphabetical (case-insensitive). Any other workspace shows its folder tree, built from the files the
workspace scan found, so ignored folders (`.git`, `build`, `node_modules`, ...) are not there.

**Keys** (with the tree focused): Up / Down / PageUp / PageDown / Home / End move the selection; Right expands a folder (or
moves to its first child), Left collapses it (or moves to the parent); **Enter** opens the selected file through
`gotoLocation` (so Back returns) and gives the editor the keys, or expands / collapses a folder. **Typing jumps** (type-ahead,
as in Solution Explorer): the letters you type select the next visible node whose name starts with them, ignoring case; the
same letter again cycles through the names that start with it. The typed letters are forgotten after one second without a
key, Backspace takes the last one back, and the first Escape forgets them. **Control ;** starts the **filter** instead:
typing then filters the tree, fuzzy like the pickers, every space-separated word has to match a name; matching nodes and
their folders stay, expanded. Backspace edits the filter, Escape clears it and leaves filter mode, and with nothing typed
Escape gives the keys back to the editor. **F2** renames the selected file or folder and **Delete** deletes it (see "File
operations" below); the tree has these two keys while it is focused (in the editor F2 is still `NextBookmark`). Alt Enter
stays 10x's `ToggleFullscreen`, so Properties is in the right-click menu and the palette only. **Mouse:** a click
selects (a file also opens in the italic **preview tab**, `tabs.previewTab`, keeping the keys in the tree), the arrow (or Enter)
expands, a double click opens a file permanently, the wheel and the thin scroll bar scroll.

**Colours.** A file that version control reports as modified, added / untracked, checked out or read-only (Perforce) gets
the same colour and marker as its tab (the `tabVcs*` theme keys); the marker is a square at the right edge. Folders show
no state.

**Following the workspace.** While the panel is open it rebuilds, at most four times a second, when the workspace scan's
file list changes (the file watcher) and when the solution, a project or its filters file changes on disk. Expanded
folders and the selection stay. With `explorer.trackActiveFile = true` the selection follows the active file.

**Show in tree.** **Control T** (`ShowInWorkspaceTree`) opens the panel if needed, expands to the current file, selects it
and focuses the tree. `ShowInWorkspaceTreeWithToggle` (no default key) does the same but closes the panel when the tree
already shows that file selected on screen. Both say so in the status bar when the file is in no project of the solution.

**Solution view and Folder view.** In a workspace with a solution the explorer starts in the Solution view above. *Switch
to Folder View* (context menu, or the palette command `View.ToggleExplorerFolderView`) shows the folders of the workspace
on disk instead (the workspace scan's file list, so the usual ignore rules apply) with the workspace folder as the root;
*Switch to Solution View* goes back. Without a solution there is only the folder tree and the command says so.

**Show All Files.** *Show All Files* (context menu, or `View.ToggleExplorerShowAllFiles`) adds, under each project, the
files of the workspace below the project's folder that the project does not list, in their folders on disk and dimmed.
It works in the Solution view; in the Folder view every file is already there. The switch applies to the files the
workspace scan knows (the known source, header and text extensions), not to every file on disk.

**It remembers.** The expanded nodes, the selected node, the view and Show All Files are saved a half second after a change
(and when the window closes) in `<workspace>/.tenfold/explorer.json` and restored the first time the explorer is built in
the next session (Visual Studio: "Restore Solution Explorer project hierarchy state on solution load"). A node that no
longer exists is skipped; a damaged file is ignored.

**File operations.** The right-click menu (and the palette commands `Explorer.NewFile`, `Explorer.NewFolder`,
`Explorer.Rename`, `Explorer.Delete`, `Explorer.Properties`, which act on the selected node) offers:

* *New File...* / *New Folder...*: a box asks for the name (it says "already exists" or what is wrong with it); the
  item is made in the node's folder (a file's own folder; a project's folder; a filter or solution folder: the nearest
  folder above). A new file is **not added to a project** (tenfold does not write new items into `.vcxproj` files): in the
  Solution view it shows with Show All Files, dimmed. A new folder is shown even while it is empty.
* *Rename* (F2): the same box, filled with the old name. The file or folder is renamed on disk; the `Include="..."` values
  of the projects and of their `.vcxproj.filters` files that name it (or, for a folder, anything below it) are rewritten in
  the style they were written (backslashes stay backslashes); wildcard items (`gen\*.cpp`) need no change. An open file is
  closed and reopened under its new name when it was the current one. A file with unsaved changes is not renamed: save it first.
* *Delete* (Delete key): asks "'name' will be moved to the Recycle Bin." (Windows) or "... moved to the Trash." (Linux,
  freedesktop.org Trash: `~/.local/share/Trash`, or `$topdir/.Trash-<uid>` for another file system), OK / Cancel; a folder
  says that everything in it goes and a modified open file says its changes are lost. The item can be restored from the
  bin. Where there is no trash (macOS, or a Linux file system with no usable trash folder) the question says "will be
  deleted permanently." and the delete is permanent with no undo. After confirming, tabs are closed and the matching
  project items are removed from the `.vcxproj` and its filters file; a failed move to the trash never turns into a
  permanent delete.
* Only files and folders that exist on disk can be renamed or deleted: solutions, projects and filters (virtual folders
  of a `.vcxproj.filters` file) cannot.

**Drag and drop.** Drag a file or folder onto a folder, a project or the root to **move** it: the drop target is
highlighted, a box asks "Move 'a.cpp' to 'Lib'?" (OK / Cancel; `explorer.promptBeforeMoving`, Visual Studio's "Prompt
before moving files to a new location", default on), then the item is moved on disk and in the projects that list it,
exactly like Rename. **Control** held at the drop **copies** instead (no question; the copy is in no project). Escape while
dragging cancels. A folder cannot be dropped into itself and a name that exists at the target refuses the move. Dropping
on a filter does nothing (a filter is not a folder). `explorer.dragDrop = false` (10x: `EnableWorkspaceTreeDragDrop`)
turns the whole thing off.

**Properties** (context menu, `Explorer.Properties`): a read-only page with the node's name, type and full path, and for
a file its size, the project that lists it, the active configuration (`Debug|x64`) and *Excluded From Build* (Yes / No,
from the project's `<ExcludedFromBuild>` items, with the configurations it names); for a folder, project or solution the
number of files (and projects). Escape closes it. There is no settings dialog, so nothing on it can be changed.

Not done: adding new or existing files to a project (Include In Project / Exclude From Project), moving a file between
filters (a drop on a filter), dragging a file onto the editor to open it, dropping files from the file manager, a recycle
undo for moves, a recycle-bin delete on macOS, "External Dependencies", `.10x` workspaces with their own file lists, a tree of
non-C++ projects (a `.csproj` with explicit items works, an SDK-style one shows an empty project), version-control state on
folders. Visual Studio's richer solution, project and file menus (Build / Rebuild / Clean, Set as Startup Project, Open
With...) are not in the menu: the research rates them medium-low (VS_BEHAVIOUR C6-C9).

## Building

`Build.Build` (**F7**, 10x's `BuildActiveWorkspace`; also **Ctrl+Shift+B**) runs the workspace build in the background and opens the Build panel at the bottom of
the window (**Ctrl+Alt+O** shows / hides it, `Build.Cancel` is **Control Cancel**, i.e. Ctrl+Break). The command is chosen in this order:

1. `buildCommand` in `.tenfold/workspace.json` (run through `cmd /s /c` on Windows, `sh -c` elsewhere, in the
   workspace folder); or an argument to the command: `Control Shift B: Build.Build make -j8` in your key file.
2. A `.sln` in the folder (the first one): `msbuild <sln> /m /p:Configuration=<vsConfiguration> /p:Platform=<vsPlatform>`;
   or, with no `.sln`, a `.vcxproj` in the folder. `msbuild` is started by name, so it has to be on `PATH`: start
   tenfold from a Visual Studio *Developer Command Prompt*, or set `buildCommand`.
3. A `CMakeLists.txt`: `cmake --build <dir> --config <vsConfiguration>`. `<dir>` is `buildDir`, else the folder of the
   compile database clangd uses if it holds a `CMakeCache.txt`, else `.tenfold/cmake-build`. tenfold never configures the
   build tree for you.
4. A `Makefile`: `make -j<cores>`.

**Rebuild** (**Control Alt F7**, `RebuildActiveWorkspace`; tenfold `Build.Rebuild`, an argument is a custom rebuild
command) uses the same detection but builds from scratch: `rebuildCommand` in `workspace.json` (a shell command, like
`buildCommand`), else `msbuild <sln> /t:Rebuild ...`, `cmake --build <dir> --config <cfg> --clean-first`, or
`make clean && make -j<cores>`. A custom `buildCommand` cannot be cleaned, so without a `rebuildCommand` the command
says so and does nothing.

**Build the current file** (**Control F7**, `BuildCurrentFile`; tenfold `Build.BuildCurrentFile`) compiles only the file
of the current tab (the saved version on disk; save first). `buildCommand` does not apply. With a `.sln` (or a lone
`.vcxproj`) it runs `msbuild <project that lists the file> /t:ClCompile /p:SelectedFiles=<file>` with the solution's
configuration mapped to the project's. Otherwise the file's entry in `compile_commands.json` is used: if its build
directory holds a `build.ninja` the object target is built (`ninja -C <dir> <object>`), else the entry's compiler command
runs in its directory (a real compile to the command's output, not a syntax check). A file with no project or compile
command (a header, a text file) reports that in the status bar. Output, error navigation and squiggles work as for a
normal build.

Output streams into the panel (colour codes removed). Lines that are errors or warnings are coloured, and clicking one,
or pressing Enter on it, jumps to the source; **F8** and **Shift+F8** (`GotoNextBuildWarningOrError` / `GotoPrevBuildWarningOrError`) step through the errors and warnings that have a
location, wrapping around, and the jump goes into the navigation history. The parser understands MSVC (`file(line,col):
error C1234`, `LINK : fatal error`, MSBuild prefixes and duplicate lines), GCC and Clang, `In file included from`
chains, NMake and make errors. The same messages are drawn as squiggles in the editor (in a different colour from
clangd's) and as marks on the scrollbar; the line numbers are those of the build time and are not remapped when you
edit afterwards. Starting a build cancels the one that is running; Cancel kills the whole process tree. The status bar
segment `build: ...` shows the last result and toggles the panel when clicked.

## Settings files

Settings are plain text files with one `key = value` per line, dotted keys, and no `[sections]`. A `#` starts a comment
only as the *first* non-blank character of a line, because colours contain `#`. Value types: `true` / `false`, whole
numbers, decimals, strings (optionally in `"quotes"`), colours (`#rrggbb` or `#rrggbbaa`), and fonts
(`family or path[, size in pt]`).

**Layers**, later wins:

1. the defaults embedded in the program (`assets/settings.default.txt` in the source tree; edit a copy, not that file);
2. the user file: `%APPDATA%\tenfold\settings.txt` (Windows), `$XDG_CONFIG_HOME/tenfold/settings.txt` or
   `~/.config/tenfold/settings.txt` (Linux), `~/Library/Application Support/tenfold/settings.txt` (macOS);
3. the workspace file `<workspace>\.tenfold\settings.txt`.

**Hot reload.** The user folder and the workspace's `.tenfold` folder (if it existed when tenfold started) are watched. Save
a settings file or a theme and the open windows update within a moment, fonts included. Unknown keys, wrong value
types and malformed lines are never fatal: the offending line is ignored and the message, with file and line number,
appears in the status bar (for example `<path>\settings.txt:3: error: unknown setting 'editor.tabwidth'`). A key that is set twice in
one file gives a warning, and the later line wins. Key bindings and `workspace.json` are *not* hot-reloaded.

All keys of `assets/settings.default.txt`, in file order. (This table is checked against that file by the test
`user_guide_settings_table_matches_the_default_settings_file`; a key added to the file without a row here fails the
tests.)

<!-- settings:begin -->
| Key | Default | Type | Meaning |
| --- | --- | --- | --- |
| `editor.smoothScroll` | `false` | bool | Animate scrolling instead of jumping. Off by default (OPEN_QUESTIONS #4). |
| `editor.wordWrap` | `true` | bool | Soft-wrap long lines at word boundaries. On by default. `View.ToggleWordWrap` flips it for the session. |
| `editor.caretBlink` | `true` | bool | Blink the caret while the editor has focus. |
| `editor.caretBlinkMs` | `530` | int | Blink interval in milliseconds: the caret is shown for this long, then hidden for this long. Values below 50 are treated as 50. |
| `editor.highlightCurrentLine` | `true` | bool | Highlight the line with the primary caret (an outline in `vs-dark`). |
| `editor.wheelLines` | `3` | int | Lines scrolled per mouse-wheel notch (Visual Studio: "Vertical scrolling sensitivity", default 3; VS_BEHAVIOUR Z7). |
| `editor.mouseWheelZoom` | `true` | bool | Control + mouse wheel over the editor zooms its text, one point per notch (10x MouseWheelZoom; wheel up = larger). Same zoom as `ZoomIn` / `ZoomOut`. |
| `editor.tabWidth` | `4` | int | Display width of a tab character, in columns. |
| `editor.columnGuide` | `0` | int | Draw a vertical guide line at this column. 0 turns it off. 10x's VerticalRulers is empty by default. |
| `editor.showWhitespace` | `false` | bool | Draw dots for spaces and arrows for tabs (first 4000 bytes of each line). |
| `editor.showFoldGlyphs` | `true` | bool | The outlining margin between the line numbers and the change strip: a boxed minus / plus on every region start with a guide line through the open region (click to collapse / expand). 10x: ShowCollapseRegionMargin (default true); Visual Studio shows the margin by default. |
| `outline.enabled` | `true` | bool | Visual Studio's Enable Outlining (C/C++). Off: no regions, no margin, the folding commands do nothing. |
| `outline.pragmaRegions` | `true` | bool | Visual Studio's Outline Pragma Regions: `#pragma region` / `#pragma endregion` pairs are regions. |
| `outline.statementBlocks` | `true` | bool | Visual Studio's Outline Statement Blocks: brace blocks inside a function body (if, for, while, try, plain blocks) are regions. Off: only function, class and namespace bodies and comments. |
| `editor.indentUnit` | `tab` | string | One indent level: `tab`, or a number of spaces from 1 to 16 (anything else means 4). Used when `editor.detectIndent` finds nothing in the file, or is off. 10x: IndentMode (Tabs). |
| `editor.detectIndent` | `false` | bool | Take the indent unit from the file itself (its first 400 lines: tabs, or the most common step) instead of `editor.indentUnit`. 10x: AutoDetectIndentMode (false). |
| `editor.autoIndent` | `true` | bool | Enter copies the previous line's indentation (one level deeper after `{`, `(`, `[` or a trailing operator). 10x: AutoIndent. |
| `editor.autoIndentBraces` | `true` | bool | `}` typed first on a line dedents to the line of its `{`. 10x: AutoIndentBraces. |
| `editor.formatBracesOnEnterSplit` | `false` | bool | Enter between `{` and `}` moves the `}` to its own line below the new indented line. 10x: FormatBracesOnEnterSplit. |
| `editor.autoAddCloseBrace` | `false` | bool | `{` inserts `}`; typing the closer steps over it, Backspace between an empty pair removes both. 10x: AutoAddCloseBrace. |
| `editor.autoAddCloseBracket` | `false` | bool | The same for `(` and `[`. 10x: AutoAddCloseBracket. |
| `editor.autoCloseQuotes` | `false` | bool | `"` and `'` insert a second quote (not after an identifier character, a backslash or the same quote) and wrap a selection. tenfold only: 10x documents no setting for quotes. Visual Studio C++ pairs quotes by default ("Automatic brace completion"), but that is unverified (low confidence, VS_BEHAVIOUR Z1 / V12), so the default stays off; typing the closing quote steps over it and Backspace in an empty pair removes both. |
| `editor.copyLineWhenEmpty` | `true` | bool | Copy and Cut with nothing selected act on the whole caret line including its line ending (Visual Studio: "Cut or Copy commands with no selection", VS_BEHAVIOUR Z4); several carets: their lines. `false`: nothing happens without a selection. |
| `editor.copyBlankLines` | `true` | bool | Copy / Cut with no selection also act on blank (whitespace-only) lines (Visual Studio: "Apply Cut or Copy commands to blank lines when there is no selection", on by default). `false`: a blank caret line is left alone. |
| `editor.bracketSelectedText` | `true` | bool | Typing `(`, `[` or `{` with text selected encloses the selection in the pair. 10x: BracketSelectedText. |
| `editor.trimTrailingWhitespace` | `false` | bool | Remove trailing blanks on every line when saving (one undo step). 10x documents no such setting. |
| `editor.useEditorConfig` | `false` | bool | Saving also honours the file's `.editorconfig` (closest file wins, `root = true` stops the search): `trim_trailing_whitespace = true` trims trailing blanks, `insert_final_newline = true` adds the final line ending (each its own undo step). Visual Studio reads `.editorconfig` too (VS_BEHAVIOUR Z19); `editor.trimTrailingWhitespace` stays off by default. Off by default: it is not established that Visual Studio applies these on save rather than on Format Document / Code Cleanup (VS_BEHAVIOUR Z19 / V14). |
| `editor.textFileNames` | `false` | bool | `NewTextFile` names the file `TextFileN.txt` (the lowest free N) like Visual Studio's File > New > File > Text File, instead of `Untitled N`. Off: unverified (VS_BEHAVIOUR Z14 / V13). |
| `editor.commentBlockUsesLineComments` | `true` | bool | 10x `CommentBlockUsesLineComments`: the 10x `ToggleComment` command (Ctrl+Shift+/) comments a selection with `//` line comments; `false` wraps the selection in `/* */` instead. Without a selection it always toggles a line comment. |
| `editor.confirmUnsaved` | `true` | bool | Closing the last tab of a modified file, closing the window and `App.Quit` ask Save / Don't Save / Cancel. `false` skips every question. Follows Visual Studio (OPEN_QUESTIONS #5). |
| `editor.detectExternalChanges` | `true` | bool | Notice a file that changed on disk behind the editor's back (Visual Studio: "Detect when file is changed outside the environment"). `false`: nothing happens and nothing is said. |
| `editor.reloadUnlessUnsaved` | `false` | bool | Reload a changed file without asking when it has no unsaved edits (Visual Studio: "Reload modified files unless there are unsaved changes", off by default). A file with unsaved edits always asks. |
| `editor.reloadExternalChanges` | (empty) | string | The older single setting. Empty: unused. `auto` = detect + reloadUnlessUnsaved (the old default), `prompt` = detect, always ask, `never` = do not detect and say so in the status bar. **Migration:** the old default `auto` is gone because Visual Studio's default is to ask; a `settings.txt` that sets this key keeps its old behaviour, one that did not now gets the question for clean files too (set `editor.reloadUnlessUnsaved = true` for the old silent reload). |
| `tabs.previewTab` | `true` | bool | A single click on a file in the explorer, and the debugger stepping into an unopened file, open it in the preview tab (Visual Studio: "Allow new files to be opened in the preview tab"). `false`: a single click only selects. |
| `tabs.previewFromFindResults` | `true` | bool | A click on a result of Find in Files or Find All References opens the preview tab (Visual Studio: "Preview selected files in Find Results"). Needs `tabs.previewTab`. |
| `editor.defaultLineEnding` | `CRLF` | string | Line ending of a new untitled file (`NewTextFile`), `CRLF` or `LF`; any other text means `CRLF`. 10x: DefaultLineEnding. The encoding of new files is `editor.defaultEncoding`. |
| `editor.replicateNewLine` | `true` | bool | Enter (and every command that adds a line) copies the line ending of the line the caret is on, so a CRLF file stays CRLF and an LF file stays LF; only a document without any line break uses `editor.defaultLineEnding`. `false` always uses `editor.defaultLineEnding`. Visual Studio: `ReplicateNewLineCharacter`. |
| `editor.pasteKeepsLineBreaks` | `true` | bool | Pasted text keeps its own line breaks (pasting LF text into a CRLF file makes the file mixed, as in Visual Studio). `false` turns them into the line ending Enter would use. |
| `editor.checkLineEndings` | `true` | bool | Opening a file whose lines end in different ways (CRLF and LF, ...) asks "The line endings in the following files are not consistent. Do you want to normalize the line endings?" with a choice of ending. Visual Studio: Tools > Options > Environment > Documents > Check for consistent line endings on load. |
| `editor.autoDetectUtf8` | `true` | bool | Visual Studio's "Auto-detect UTF-8 encoding without signature": a file without a byte order mark that is valid UTF-8 opens as UTF-8. `false`: such a file with non-ASCII text opens as Windows-1252 (ASCII files, files with a UTF-8 signature and UTF-16 files with a BOM are not affected). |
| `editor.defaultEncoding` | `utf-8` | string | Encoding of a new untitled file (`NewTextFile`): `utf-8`, `utf-8-bom`, `utf-16le`, `utf-16be` (both with BOM) or `windows-1252`; anything else means `utf-8`. 10x: DefaultEncoding. |
| `editor.autoNameUntitledFiles` | `false` | bool | An untitled file is named after the first word typed in it; the name is what the Save As prompt offers. 10x: AutoNameUntitledFiles. |
| `editor.internetSearchCommand` | `https://www.google.com/search?q=` | string | `InternetSearch` (F1) opens this address with the word under the caret or the selection appended (percent-encoded) in the default browser. 10x: InternetSearchCommand. |
| `editor.pasteHistorySize` | `20` | int | How many recent clipboard texts `PasteHistory` (Ctrl+Shift+V) keeps; 0 turns it off. 10x documents no size; 20 is the size of Visual Studio's Clipboard Ring (VS_BEHAVIOUR Z11). |
| `font.editor` | (empty) | font | Editor font as `family or path[, size in pt]`. Empty means unset: `font.fallbacks` are tried in order. A family name is matched against font file names in the system font folders (case and spaces ignored), so give a full path if it is not found. |
| `font.fallbacks` | `Cascadia Mono, Consolas` | string | Comma-separated font names tried in order when `font.editor` is unset or not found; after the last one the platform's usual monospace font is used. |
| `font.size` | `10` | float | Font size in points when `font.editor` does not carry one. |
| `ui.theme` | `vs-dark` | string | Theme name: `.tenfold/themes/<name>.txt` in the workspace, then `themes/<name>.txt` in the user config folder, then the built-ins `dark`, `light`, `vs-dark`. |
| `nav.statusCounts` | `false` | bool | Show the back and forward counts in the status bar (an arrow with the number of entries in each direction). |
| `keys.import10x` | `true` | bool | On Windows, read the 10x key mappings file `%APPDATA%\10x\Settings\KeyMappings.10x_settings` as a key-binding layer. |
| `picker.preview` | `true` | bool | Symbol search and the function picker show a preview of the target line on the right (10x documents nothing about it, OPEN_QUESTIONS #6). The "Preview" switch in the picker's header (Alt+P) changes it for the session. |
| `diagnostics.miscFiles` | `false` | bool | Show clangd's squiggles and diagnostics in files that belong to no translation unit of the project (not in the compile database and not included by one of its files). `false` is Visual Studio's rule for "Miscellaneous Files": IntelliSense, but no error squiggles. Without a compile database nothing counts as such a file. |
| `explorer.trackActiveFile` | `false` | bool | Keep the active file selected in the workspace explorer while it is open (10x: TrackActiveFileInWorkspaceTree). |
| `explorer.openWithSolution` | `true` | bool | Open the workspace explorer the first time a workspace with a solution is started (no `.tenfold/layout.json` yet), like Solution Explorer. |
| `explorer.dragDrop` | `true` | bool | Dragging in the explorer moves (Ctrl: copies) files and folders (10x: EnableWorkspaceTreeDragDrop, default true). |
| `explorer.promptBeforeMoving` | `true` | bool | Ask before a drop moves a file (Visual Studio: "Prompt before moving files to a new location"). |
| `vcs.diffTool` | (empty) | string | External diff program for `Vcs.DiffFileExternal`. Empty means automatic: `p4merge` from PATH (on Windows `p4merge.exe`, then `%ProgramFiles%\Perforce\p4merge.exe`). |
| `vcs.diffToolArgs` | `"{base}" "{current}"` | string | Arguments of that program, split like a command line (double quotes group). Placeholders: `{base}` (temp file with the committed text), `{current}` (temp file with the buffer), `{baseTitle}`, `{currentTitle}`, `{file}` (the real path). A value wrapped in quotes loses the outer pair, which is why the default file doubles them. With these default arguments and a tool called p4merge, tenfold also passes titles (`-nl`, `-nr`). |
| `vcs.p4vPath` | (empty) | string | The program `PerforceShowInP4v` starts; empty means `p4v` from `PATH`. |
| `terminal.command` | (empty) | string | The program `NewTerminal` / `ShowTerminal` open, with arguments, split like a command line (double quotes group). 10x: TerminalCommand (10x default `cmd.exe`). Empty (tenfold's default, like Visual Studio's PowerShell default profile) means `pwsh.exe` if it is on `PATH`, else `powershell.exe` on Windows, and `$SHELL`, else `/bin/sh` elsewhere. A value you set is always used as written; off Windows `cmd.exe` still means `$SHELL`. |
| `terminal.claudeCommand` | `claude` | string | The program of `ClaudeCodeTerminal`. 10x: ClaudeTerminalCommand. |
| `terminal.cursorCommand` | `cmd /c agent` | string | The program of `CursorTerminal`. 10x: CursorTerminalCommand. Off Windows a leading `cmd /c` is dropped (`agent`). |
| `terminal.font` | (empty) | font | Terminal font as `family or path[, size in pt]`, looked up like `font.editor`. Empty means the editor font. 10x: TerminalFont. |
| `terminal.fontSize` | `0` | float | Terminal font size in points. 0 means the editor font size (including View.ZoomIn / ZoomOut; a terminal with its own font or size does not zoom). 10x: TerminalFontSize. |
| `terminal.scrollbackLines` | `10000` | int | Lines of scrollback kept per terminal (the alternate screen of full-screen programs has none). Takes effect for new terminals; shrinking it on a running terminal trims it at once. tenfold only. |
| `terminal.closeOnExit` | `false` | bool | `true` closes a terminal's tab as soon as its program exits. `false` keeps the final screen and a `[process exited with code N]` line until you close the tab. tenfold only. |
| `terminal.ctrlCCopiesSelection` | `false` | bool | `false` (Visual Studio): Control C always sends ^C to the program, with or without a selection. `true`: Control C copies (and clears) the selection when there is one, like Windows Terminal; without a selection it is still ^C. tenfold only. |
<!-- settings:end -->

The default file also notes that undo grouping (`undoGrouping`, `undoGroupPauseMs`) does not live here but in
`workspace.json`.

### Workspace file (`.tenfold/workspace.json`)

Per-workspace engine settings, JSON, read **once at start** (restart tenfold after editing by hand). A missing file
means all defaults. Selecting a Visual Studio configuration, and the first discovery of `cl.exe`, *rewrite* the file
with the keys below. The write is a merge: keys tenfold does not know about (`vcs.provider`, `vcs.p4Path`, nested
objects, anything else) are kept, and the file is replaced atomically. A file that is not a valid JSON object is never
overwritten; the operation fails with a message and you fix the file by hand.

| Key | Default | Meaning |
| --- | --- | --- |
| `sourceExtensions`, `headerExtensions`, `otherExtensions` | see [Opening a workspace](#opening-a-workspace) | Which files are scanned and indexed. |
| `ignoreDirs` | `.git .svn .hg .vs .vscode .idea .cache .tenfold node_modules __pycache__` | Directory names never scanned (setting it replaces the list). |
| `ignoreDirNames` | `build out bin obj` | Directories with exactly one of these names (case-insensitive) are skipped. |
| `ignoreDirPrefixes` | `build- cmake-build` | Directories whose name *starts with* one of these are skipped. |
| `followSymlinks` | `false` | Follow directory symlinks when scanning. |
| `watchFiles` | `true` | Keep the index, history and file lists in step with the disk. |
| `cppStandard` | `c++20` | `-std=` for heuristic commands and the fallback flags. |
| `clangdPath` | empty | clangd to use instead of the bundled one (a path, or a name on `PATH`). |
| `clangdExtraArgs` | empty | Extra arguments appended to clangd's command line. |
| `vsConfiguration`, `vsPlatform` | `Debug`, `x64` | Active solution configuration; also used by the build. |
| `runCMake` | `true` | Configure CMake projects into `.tenfold/cmake-build` to get compile commands. |
| `buildCommand` | empty | Custom build command (see [Building](#building)). |
| `rebuildCommand` | empty | Custom rebuild command for `RebuildActiveWorkspace` (see [Building](#building)). |
| `buildDir` | empty | CMake build directory for `cmake --build`. |
| `msvcToolchain` | empty | Path of the MSVC `cl.exe` for clangd's `--query-driver`. Found and saved automatically when empty. |
| `debug.attachAtStartup` | `true` | Attach to a Visual Studio that already has the solution open. |
| `undoGrouping`, `undoGroupPauseMs` | `word`, `1000` | `word` joins typed characters per word; `char` makes each its own step. |
| `vcs.provider`, `vcs.p4Path` (or nested `vcs`) | `auto`, empty | See [Version control](#version-control). |

Other files tenfold keeps in `.tenfold\`: `layout.json` (tabs and panels), `bookmarks.json`, `breakpoints.json`,
`fastindex.bin` (the persisted index), `compile_commands.json` with `compile_commands.stamp` (the generated database
and what it was made for), `cmake-build\`, `clangd-config\` (clangd's private config), `clangd.log` (clangd's stderr),
and `log.txt` ([Logs](#logs-and-crash-reports)). They are all safe to delete while tenfold is closed; they are rebuilt.

## Themes

The default theme is **`vs-dark`**, a Visual Studio 2022 Dark look (OPEN_QUESTIONS #7). Built in: `dark`, `light`,
`vs-dark`. Choose one with `ui.theme = light` in a settings file. The shell, tab, menu, scrollbar, input and selection colours
and the syntax colours come from Microsoft's published Visual Studio palettes (sources in the theme file's header); the
values still written from memory are marked `[?]`: squiggles, diff and debugger colours, find highlight, the terminal
palette (OPEN_QUESTIONS #19). Corrections are welcome. String escapes have no colour of their own yet.

A theme is a settings-style text file that contains only `color.*` and `token.*` keys:

```
# <workspace>\.tenfold\themes\mine.txt   (or %APPDATA%\tenfold\themes\mine.txt)
color.editorBackground = #101418
color.selectionActive  = #264F78
token.comment          = #6A9955
token.function.static  = #DCDCAA
```

Set `ui.theme = mine`. The lookup order for a name is the workspace's `.tenfold\themes\<name>.txt`, then the user
folder's `themes\<name>.txt`, then the built-ins, so a file called `vs-dark.txt` replaces the built-in. **A theme file only
needs the keys it changes; keys it leaves out keep the built-in `dark` theme's values** (not `vs-dark`'s), so copy the
whole of `assets/themes/vs-dark.txt` if you want to start from it. An unknown theme name falls back to `dark` with a
message.

* `color.<name>`: interface and editor colours (`background`, `panelBackground`, `tabActive`, `editorBackground`,
  `gutterText`, `selectionActive`, `findHighlight`, `diagnosticError`, `buildError`, `debugBreakpoint`, `diffAdded`, ...).
  The names are the members of the theme; `assets/themes/vs-dark.txt` lists every one and a wrong name is reported with its
  line. The editor, debugger and version-control colours are optional: leaving them out gives the built-in look. The
  Visual Studio details are optional too and unset in `dark` and `light`: `tabDocumentHover` (hovered document tab),
  `tabPreviewActive` / `tabPreviewInactive` (the preview tab), `scrollbarThumbHover` / `scrollbarThumbPressed`, `inputBackground`,
  `inputBorder`, `inputSelection` (text fields), `menuText` / `menuGlyph` / `menuDisabled`, `link` and
  `statusBackgroundNoSolution` (status bar when no workspace is open).
* `token.<kind>[.<modifier>]`: colours of the lexical kinds (`keyword identifier number string char comment
  preprocessor operator punctuation`) and of clangd's semantic kinds (`namespace type class struct interface enum
  enumMember typeParameter concept function method property variable parameter macro event decorator modifier regexp`).
  `controlKeyword` colours `if else for while do switch case break continue goto return try catch throw co_await
  co_return co_yield` and falls back to `keyword`.
  A modifier is one of `declaration definition readonly static deprecated abstract async modification documentation
  defaultLibrary`. A token falls back along `method.static` -> `method` -> `function` -> `identifier`.
* Colours are `#rrggbb` or `#rrggbbaa`.

## Key bindings

Key bindings work the way they do in 10x: the same file format, the same command names and 10x's own default key map
(from 10x's documentation, OPEN_QUESTIONS #1). A binding is one line of a `.10x_settings` file:

```
<keys>: <command>
```

* **Keys** are chords written as modifiers then the key, separated by spaces: `Control Shift F`, `Alt Enter`,
  `Control Shift Alt /`. Modifiers: `Control`, `Shift`, `Alt` (in any order, case-insensitive). A **sequence** is several
  chords separated by a comma: `Control K, Control C` (the first chord shows in the status bar until the second arrives
  or the chord times out). Keys: letters, digits, `F1` to `F24`, `Up Down Left Right Home End PageUp PageDown Enter Tab
  Space Backspace Delete Insert Cancel AppsKey`, the punctuation keys `' . / = [ ] \ -` as the character itself, and
  `Comma` for the comma (a plain comma separates chords). `Cancel` is the Pause/Break key (`Control Cancel` cancels a
  build), `AppsKey` the context-menu key. Mouse buttons: `MouseMiddleButton`, `MouseBackButton`,
  `MouseForwardButton` (the thumb buttons), also with modifiers (`Control MouseMiddleButton`). tenfold also knows
  `Escape` and `KpEnter`.
* **Command** is a 10x command name (`GotoSymbolDefinition`, `PrevLocation`, ...; names are case-insensitive) or a
  tenfold command id (`Editor.GotoDefinition`); both work in key files and in the [command palette](#command-palette).
  tenfold ids may be followed by an argument (`Workbench.FocusPanel 2`, `Build.Build make -j8`). An **empty** command
  (`Control Q:`) removes the binding for those keys from an earlier layer.
* A **space after the colon is required**, as in every 10x settings file. `#` starts a comment when it is the first
  non-blank character of a line.
* 10x also accepts **Python statements** as the command. tenfold has no scripting: such a line loads with a warning,
  and pressing the key says `Python statements are not supported`; the key stays bound to that, so it does not fall
  back to an earlier layer's binding.
* A 10x command tenfold does not implement yet is a valid command: pressing its key says `not implemented yet:
  <Command>` in the status bar (the list is in [FEATURE_PARITY.md](FEATURE_PARITY.md#10x-commands-not-yet-implemented)).
* The old tenfold format (`Ctrl+K Ctrl+C = Editor.Comment`, in `keybindings.txt`) is **no longer read**. A line in that
  style is a parse error that points at the new format, and a leftover `keybindings.txt` next to the new file gives a
  warning.

**Layers**, later wins (a later layer replaces the earlier bindings of the same keys):

1. the embedded `assets/KeyMappings.default.10x_settings`: 10x's default key map, then a marked "tenfold additions"
   section;
2. an installed 10x's own key file, `%APPDATA%\10x\Settings\KeyMappings.10x_settings` (Windows only; setting
   `keys.import10x`, default on), so your existing 10x customisations apply;
3. your tenfold file `KeyMappings.10x_settings` in the user config dir (`%APPDATA%\tenfold`, see
   [Settings files](#settings-files) for the other platforms);
4. `<workspace>\.tenfold\KeyMappings.10x_settings`.

Mistakes (unknown key or modifier, missing colon, Python statements) are reported in the status bar with the file and
line, and the line is skipped (Python lines load as described above); a duplicate binding in one file warns and the later
one wins. **Key bindings are read once at start**; restart tenfold after editing them. (`keys.import10x` is already
read by the app, but its entry in the settings schema (`settings.default.txt`) is added separately; until then the
import is always on.)

Example `KeyMappings.10x_settings` in your user config dir:

```
Control Alt F1: Help.ShowCompileCommand
Control K, Control C: ToggleComment
Control Shift B: Build.Build msbuild /m /p:Configuration=Release /p:Platform=x64
Control -:
```

### The defaults are 10x's

`assets/KeyMappings.default.10x_settings` holds **every binding of 10x's default key map** (183 bindings in 10x's own
groups, key strings and command names verbatim), followed by a short section of tenfold-only additions. Bindings whose
command tenfold does not implement yet stay in the file, so it matches 10x; they answer `not implemented yet: <Command>`.
10x binds some keys twice for different places: `Up`, `Down`, `PageUp` and `PageDown` move the cursor in the editor and
the highlighted item in the autocomplete list, `Enter` submits a symbol rename. tenfold files each binding under a
*context* chosen by its command: **editor** (a text view has focus), **popup** (the autocomplete list and the rename
box, which handle those keys themselves) and **global**. A binding in the focused context beats a global one, and while a
picker is open the editor bindings are out of the way, so the pickers keep their own arrow keys. Where a 10x binding for a
command tenfold lacks would take a key from the focused input line or the open picker, the widget gets the key first
(`Alt R` toggles regex in the find-only bar and in the pickers; with the replace row shown it is 10x's
`ReplaceNextInFile`).

`Control S` saves; pressed twice (`Control S, Control S`) 10x saves all files, and so does tenfold (`Control Shift S`
saves all at once). The second press runs `File.SaveAll`; the first one has already saved the current file.

The 177 default bindings that run a tenfold command today (all other defaults are in
[FEATURE_PARITY.md](FEATURE_PARITY.md#10x-commands-not-yet-implemented)). The last column says where the tenfold command
differs from the 10x description:

**General**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Control Shift X` | `ShowCommandPanel` | `App.CommandPalette` | a list to pick from; it does not take a typed command line |
| `Control '` | `NewTerminal` | `Terminal.New` | a terminal tab in the active panel; see [Terminal](#terminal) |
| `Control Shift I` | `ShowTerminal` | `Terminal.Show` | creates the first terminal, then cycles through the terminal tabs in creation order |
| `Alt X` | `OpenCurrentFileInDebugger` | `Debug.OpenInVisualStudio` |  |
| `Control Shift E` | `ToggleWorkspaceExplorer` | `View.ToggleWorkspaceExplorer` | opens the explorer panel on the left (focused) or closes it; see [Workspace explorer](#workspace-explorer) |
| `F1` | `InternetSearch` | `Editor.InternetSearch` | the word under the caret or the selection, percent-encoded after `editor.internetSearchCommand` |
| `Control Shift .` | `ZoomIn` | `View.ZoomIn` | one point per step on the editor text only (tabs, status bar and menus keep their size), for this session only (not saved); the status bar shows the zoom as "113 %" while it is not 100 %, a click on it resets it; Control + mouse wheel does the same (`editor.mouseWheelZoom`) |
| `Control Shift Comma` | `ZoomOut` | `View.ZoomOut` | one point per step on the editor text only, for this session only (not saved); see `ZoomIn` |
| `Alt Enter` | `ToggleFullscreen` | `View.ToggleFullscreen` | borderless fullscreen on the current display |
| `Control Alt Enter` | `ToggleFocusMode` | `View.ToggleFocusMode` | shows only the current panel: no status bar, other panels or bottom dock (10x's documentation gives no details) |
| `Shift Alt Enter` | `ToggleFullScreenFocusMode` | `View.ToggleFullscreenFocusMode` | both together; both off when both are on |

**File**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Control O` | `OpenFile` | `File.OpenFile` | opens tenfold's file picker, not a native open dialog |
| `Control F4` | `CloseFile` | `Workbench.CloseTab` | closes the tab (asks about unsaved changes) |
| `Control S` | `SaveFile` | `File.Save` | an untitled file asks for a path first |
| `Control S, Control S`; `Control Shift S` | `SaveAll` | `File.SaveAll` | saves every modified open file; a failure is named in the status bar |
| `Control N` | `NewTextFile` | `File.New` | "Untitled N" (`TextFileN.txt` with `editor.textFileNames`), line ending from `editor.defaultLineEnding`, encoding from `editor.defaultEncoding` (UTF-8); saving asks for a path in an input line |
| `Control Shift Alt /` | `OpenFolderForActivefile` | `File.ShowInFolder` | `explorer /select` (Windows), `open -R` (macOS), `xdg-open` on the folder (Linux) |
| `Control T` | `ShowInWorkspaceTree` | `View.ShowInWorkspaceTree` | opens the explorer when it is closed, selects the current file and gives the tree the keys |
| (none) | `ShowInWorkspaceTreeWithToggle` | `View.ShowInWorkspaceTreeWithToggle` | like `ShowInWorkspaceTree`, but closes the explorer when it already shows the file; bind it yourself |

**Edit**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Control X` | `Cut` | `Editor.Cut` |  |
| `Control C` | `Copy` | `Editor.Copy` |  |
| `Control V` | `Paste` | `Editor.Paste` |  |
| `Control Z` | `Undo` | `Editor.Undo` |  |
| `Control Y` | `Redo` | `Editor.Redo` |  |
| `Control Shift Up` | `MoveLineUp` | `Editor.MoveLinesUp` | also moves every line a selection touches |
| `Control Shift Down` | `MoveLineDown` | `Editor.MoveLinesDown` | also moves every line a selection touches |
| `Control Shift L` | `DeleteLine` | `Editor.DeleteLines` |  |
| `Control Shift /` | `ToggleComment` | `Editor.ToggleComment` | a selection follows `editor.commentBlockUsesLineComments` (default: `//` line comments, `false`: `/* */` around the selection); no selection: line comment |
| `Control E, Control V` | `DuplicateLine` | `Editor.Duplicate` | with a selection it duplicates the selected text, not the line |
| `Control A` | `SelectAll` | `Editor.SelectAll` |  |
| `Delete` | `Delete` | `Editor.Delete` |  |
| `Control Insert` | `Copy` | `Editor.Copy` |  |
| `Shift Insert` | `Paste` | `Editor.Paste` |  |
| `Control Shift Z` | `Redo` | `Editor.Redo` |  |
| `Shift Delete` | `CutLine` | `Editor.CutLine` | cuts the lines the carets (or selections) touch into the clipboard, line break included |
| `Control Shift U` | `MakeSelectionUppercase` | `Editor.MakeSelectionUppercase` | ASCII letters only; the selection stays |
| `Control U` | `MakeSelectionLowercase` | `Editor.MakeSelectionLowercase` | ASCII letters only; the selection stays |
| `Control K, Control C` | `Commentline` | `Editor.CommentLines` | `// ` on every non-blank line the carets touch, at the smallest indentation |
| `Control K, Control U` | `Uncommentline` | `Editor.UncommentLines` | removes `//` (and one space) from the lines that have it |
| `Control L` | `DeleteToEndOfLine` | `Editor.DeleteToEndOfLine` | from the caret (selection: its start) to the end of the line; the line break stays |
| `Control Enter` | `InsertLine` | `Editor.InsertLineAbove` | a blank line above the current line, same indentation, caret on it |
| `Control Shift Enter` | `InsertLineAfter` | `Editor.InsertLineBelow` | a blank line below the current line (one level deeper after `{ ( [`), caret on it |
| `Control W` | `SelectCurrentWord` | `Editor.SelectCurrentWord` | the identifier at or just before the caret |
| `Shift Alt =` | `SelectCurrentScope` | `Editor.SelectCurrentScope` | inside the enclosing `{}`, then with the braces, then the next scope out; braces in comments and literals are ignored |
| `Control Shift M` | `SelectCurrentBracketScope` | `Editor.SelectCurrentBracketScope` | the same for `()`, `[]` and `{}` |
| `Control Shift W` | `ToggleWhitespace` | `View.ToggleWhitespace` | for this session; `editor.showWhitespace` sets the start-up state |

**MultiCursor**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Control D` | `SelectNextMultiCursor` | `Editor.AddCaretAtNextOccurrence` | UndoSelectNextMultiCursor takes the last step back |
| `Shift Alt Up` | `MultiCursorMoveUp` | `Editor.AddCaretAbove` | adds the caret at the same column |
| `Shift Alt Down` | `MultiCursorMoveDown` | `Editor.AddCaretBelow` | adds the caret at the same column |
| `Shift Alt Left` | `MultiCursorMoveLeft` | `Editor.ExtendSelectionEnds` | as 10x describes it: moves the later end of every selection one character later (grows it); a caret becomes a one-character selection |
| `Shift Alt Right` | `MultiCursorMoveRight` | `Editor.ShrinkSelectionEnds` | as 10x describes it: moves the later end of every selection one character earlier (shrinks it, never past the start) |
| `Control Shift D` | `UndoSelectNextMultiCursor` | `Editor.UndoAddCaretAtNextOccurrence` | undoes one SelectNextMultiCursor step while the carets and text are unchanged since it |
| `Control Shift Alt J`; `Alt F3` | `SelectAllOccurrences` | `Editor.SelectAllOccurrences` | the find bar's text and options when the bar is open, else the selected text or whole word at the caret |
| `Control MouseMiddleButton` | `AddCursorAtMousePos` | `Editor.AddCaretAtMouse` | Control + middle click in an editor view |
| `Alt A` | `AlignMultiCursor` | `Editor.AlignCarets` | pads with spaces; of several carets on a line the first moves |

**CollapseRegions** (see [Code folding](#code-folding))

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Control Shift [` | `CollapseRegion` | `Editor.CollapseRegion` | C/C++ files only; brace blocks, multi-line comments, `#pragma region` |
| `Control Shift ]` | `ExpandRegion` | `Editor.ExpandRegion` | the collapsed region starting on the caret line |
| `Control M, Control M` | `ToggleCollapseExpandRegion` | `Editor.ToggleCollapseExpandRegion` | |
| `Control M, Control L` | `ToggleCollapseExpandAllRegions` | `Editor.ToggleCollapseExpandAllRegions` | Visual Studio's Toggle All Outlining: nothing collapsed: collapse all (every depth), else expand all |
| | `CollapseAllRegions`, `ExpandAllRegions` | `Editor.CollapseAllRegions`, `Editor.ExpandAllRegions` | outermost regions / visible collapsed regions only |
| | `CollapseAllRegionsRecursive`, `ExpandAllRegionsRecursive` | `Editor.CollapseAllRegionsRecursive`, `Editor.ExpandAllRegionsRecursive` | every depth |

**Clipboard history, key sequences, Perforce**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Control Shift V` | `PasteHistory` | `Editor.PasteHistory` | a picker in the middle of the window, not a dropdown at the caret; 20 entries |
| `Control Shift R` | `RecordKeySequence` | `App.RecordKeySequence` | records commands and typed text of the editor, not mouse or picker input |
| `Control Shift P` | `PlaybackKeySequence` | `App.PlaybackKeySequence` | one undo step of the document current at the start |
| `Control P, Control H` | `PerforceHistory` | `Vcs.PerforceHistory` | Enter opens the revision read-only in a new tab |
| `Control P, Control V` | `PerforceShowInP4v` | `Vcs.ShowInP4v` | `p4v -p -u -c -s <depot path>` |

**Navigation**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Shift Alt O` | `FindFile` | `File.OpenFile` |  |
| `Control F` | `FindInFile` | `Editor.Find` |  |
| `Shift F3` | `FindInFilePrev` | `Editor.FindPrev` |  |
| `F3` | `FindInFileNext` | `Editor.FindNext` |  |
| `Control H` | `FindReplaceInFile` | `Editor.Replace` |  |
| `Control G` | `GoToLine` | `Editor.GotoLine` |  |
| `Control F3` | `FindInFileNextCurrentWord` | `Editor.FindNextCurrentWord` | searches the selection (as text) or else the word at the caret (whole word, case-sensitive) and selects the next match |
| `Control Shift F3` | `FindInFilePrevCurrentWord` | `Editor.FindPrevCurrentWord` | the same, backwards |
| `Control Shift \` | `MoveToMatchingBracket` | `Editor.MoveToMatchingBracket` | to the same side of the partner bracket (before it when the caret was before the bracket, else after it) |
| `Control Up` | `ScrollOneLineUp` | `Editor.ScrollLineUp` | the carets stay unless they would leave the view; then they move to the nearest visible row |
| `Control Down` | `ScrollOneLineDown` | `Editor.ScrollLineDown` | the same, downwards |
| `Control -` | `PrevLocation` | `Nav.Back` |  |
| `Control Shift -` | `NextLocation` | `Nav.Forward` |  |
| `Control Shift Backspace` | `PrevEditLocation` | `Nav.LastEditLocation` |  |
| `Shift F4` | `GotoPrevResult` | `Search.PrevResult` | walks the results of whichever came last, a find-in-files search or a build |
| `F4` | `GotoNextResult` | `Search.NextResult` | walks the results of whichever came last, a find-in-files search or a build |
| `Control =` | `PrevLocationCurrentFile` | `Nav.BackInFile` | skips the navigation history entries of other files |
| `Control Shift =` | `NextLocationCurrentFile` | `Nav.ForwardInFile` | skips the navigation history entries of other files |
| `Up` | `MoveCursorUp` | `Editor.CaretUp` |  |
| `Down` | `MoveCursorDown` | `Editor.CaretDown` |  |
| `Left` | `MoveCursorLeft` | `Editor.CaretLeft` |  |
| `Right` | `MoveCursorRight` | `Editor.CaretRight` |  |
| `Shift Up` | `SelectUp` | `Editor.SelectUp` |  |
| `Shift Down` | `SelectDown` | `Editor.SelectDown` |  |
| `Shift Left` | `SelectLeft` | `Editor.SelectLeft` |  |
| `Shift Right` | `SelectRight` | `Editor.SelectRight` |  |
| `Home` | `MoveToLineStart` | `Editor.CaretLineStart` |  |
| `End` | `MoveToLineEnd` | `Editor.CaretLineEnd` |  |
| `Control Home` | `MoveToFileStart` | `Editor.CaretDocStart` |  |
| `Control End` | `MoveToFileEnd` | `Editor.CaretDocEnd` |  |
| `Control Shift Home` | `SelectToFileStart` | `Editor.SelectDocStart` |  |
| `Control Shift End` | `SelectToFileEnd` | `Editor.SelectDocEnd` |  |
| `Shift Home` | `SelectToLineStart` | `Editor.SelectLineStart` |  |
| `Shift End` | `SelectToLineEnd` | `Editor.SelectLineEnd` |  |
| `Control Left` | `MoveCursorPrevWord` | `Editor.CaretWordLeft` |  |
| `Control Right` | `MoveCursorNextWord` | `Editor.CaretWordRight` |  |
| `Control Shift Left` | `MoveCursorPrevWordSelect` | `Editor.SelectWordLeft` |  |
| `Control Shift Right` | `MoveCursorNextWordSelect` | `Editor.SelectWordRight` |  |
| `PageUp` | `PageUp` | `Editor.CaretPageUp` |  |
| `PageDown` | `PageDown` | `Editor.CaretPageDown` |  |
| `Shift PageUp` | `SelectPageUp` | `Editor.SelectPageUp` |  |
| `Shift PageDown` | `SelectPageDown` | `Editor.SelectPageDown` |  |
| `MouseBackButton` | `PrevLocation` | `Nav.Back` |  |
| `MouseForwardButton` | `NextLocation` | `Nav.Forward` |  |
| `Shift Alt PageUp` | `SelectPageUp` | `Editor.SelectPageUp` |  |
| `Shift Alt PageDown` | `SelectPageDown` | `Editor.SelectPageDown` |  |
| `Shift Alt Home` | `MoveToLineStart` | `Editor.CaretLineStart` |  |
| `Shift Alt End` | `SelectToLineEnd` | `Editor.SelectLineEnd` |  |
| `Control Shift F` | `Search` | `Search.FindInWorkspace` | tenfold's find-in-files picker (NewSearchPanel keeps a previous result list) |
| `Control Shift A` | `NewSearchPanel` | `Search.NewFindInFiles` | keeps the current find-in-files results aside (the last three) and starts an empty list; Alt PageUp/PageDown in the picker bring them back |

**FindInFiles**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Control Shift G` | `FindInFiles` | `Search.FindInWorkspace` | tenfold's find-in-files picker |
| `Control Shift H` | `ReplaceInFiles` | `Search.ReplaceInFiles` | a modal picker with a replace field; the replacement is literal text, closed files are written after one confirmation |
| `Alt R` | `ReplaceNextInFile` | `Editor.ReplaceNextInFile` | uses the last query and replacement of the find/replace bar; in the find-only bar Alt R still toggles regex |
| `Shift Alt R` | `ReplaceAllInFile` | `Editor.ReplaceAllInFile` | uses the last query and replacement of the find/replace bar; one undo step |

**Parser**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Alt G` | `GotoSymbolDefinition` | `Editor.GotoDefinition` |  |
| `Shift Alt S` | `FindSymbol` | `Search.SymbolSearch` |  |
| `Shift Alt F` | `FindSymbolReferences` | `Editor.FindReferences` |  |
| `Alt O` | `CppParser.ToggleSourceHeader` | `Editor.SwitchHeaderSource` |  |
| `MouseMiddleButton` | `GotoSymbolDefinitionUnderMouse` | `Editor.GotoDefinitionUnderMouse` | only over an identifier of a text view; elsewhere the click is untouched (tab close) |
| `Control R` | `RenameSymbol` | `Editor.RenameSymbol` | clangd only, no approximate fallback |
| `Enter` (rename box) | `SubmitRenameSymbol` | `Editor.SubmitRenameSymbol` |  |
| `Alt M` | `FindFunction` | `Editor.FindFunction` | a picker over the document symbols (fast index first, clangd's list replaces it) |
| `Control Space` | `Autocomplete` | `Editor.Autocomplete` |  |
| `Control Shift Space` | `ShowFunctionArgsInfo` | `Editor.ShowFunctionArgs` | clangd only |
| `Control K, Control I` | `ShowSymbolInfo` | `Editor.ShowSymbolInfo` |  |

**Build**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `F7` | `BuildActiveWorkspace` | `Build.Build` | runs the detected build (solution, CMake or custom command) |
| `Control Alt F7` | `RebuildActiveWorkspace` | `Build.Rebuild` | msbuild /t:Rebuild, cmake --build --clean-first, make clean && make, or the rebuildCommand setting |
| `Control F7` | `BuildCurrentFile` | `Build.BuildCurrentFile` | compiles only the current saved file: msbuild /t:ClCompile, ninja object target or the compile database command |
| `Shift F8` | `GotoPrevBuildWarningOrError` | `Build.PrevError` |  |
| `F8` | `GotoNextBuildWarningOrError` | `Build.NextError` |  |
| `Control Cancel` | `CancelBuild` | `Build.Cancel` |  |

**Debugging**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `F5` | `StartDebugging` | `Debug.Start` | drives the Visual Studio debugger |
| `Control Shift F5` | `RestartDebugging` | `Debug.Restart` |  |
| `Shift F5` | `StopDebugging` | `Debug.Stop` |  |
| `F10` | `DebugStepOver` | `Debug.StepOver` |  |
| `F11` | `DebugStepInto` | `Debug.StepInto` |  |
| `Shift F11` | `DebugStepOut` | `Debug.StepOut` |  |
| `Control F5` | `Run` | `Debug.StartWithoutDebugging` |  |
| `F9` | `ToggleBreakpoint` | `Debug.ToggleBreakpoint` |  |
| `Control Shift F9` | `RemoveAllBreakpointsFromFile` | `Debug.DeleteBreakpointsInFile` |  |

**Bookmarks**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Control F2` | `ToggleBookmark` | `Editor.ToggleBookmark` |  |
| `Shift F2` | `PrevBookmark` | `Editor.PrevBookmark` |  |
| `F2` | `NextBookmark` | `Editor.NextBookmark` |  |
| `Shift Alt F2` | `PrevBookmarkInFile` | `Editor.PrevBookmarkInFile` | wraps within the current file |
| `Alt F2` | `NextBookmarkInFile` | `Editor.NextBookmarkInFile` | wraps within the current file |
| `Control Shift F2` | `RemoveAllBookmarksFromFile` | `Editor.RemoveBookmarksInFile` |  |

**Formatting**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Control ]` | `IndentLine` | `Editor.Indent` |  |
| `Control [` | `UnindentLine` | `Editor.Outdent` |  |
| `Control K, Control F` | `FormatSelection` | `Editor.FormatSelection` | clangd only; the primary selection, else the innermost `{ }` block, else the caret line |

**Tabs**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Control PageUp` | `PrevPanelTab` | `Workbench.PrevTab` | wraps round |
| `Control PageDown` | `NextPanelTab` | `Workbench.NextTab` | wraps round |
| `Control Shift PageUp` | `MovePrevPanelTab` | `Workbench.MoveTabPrev` | stops at the first tab, never leaves the pinned / unpinned group |
| `Control Shift PageDown` | `MoveNextPanelTab` | `Workbench.MoveTabNext` | stops at the last tab, never leaves the pinned / unpinned group |
| `Control Shift T` | `RestoreRecentlyClosedFile` | `Workbench.ReopenClosedTab` | reopens the last file whose last tab was closed, at its caret; repeats go further back |

**Panels**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Alt Left` / `Right` / `Up` / `Down` | `MovePanelFocusLeft` / `Right` / `Up` / `Down` | `Workbench.FocusPanelLeft` / `Right` / `Up` / `Down` | the neighbouring panel by position on screen |
| `Control Alt Left` / `Right` / `Up` / `Down` | `MovePanelLeft` / `Right` / `Up` / `Down` | `Workbench.MovePanelLeft` / `Right` / `Up` / `Down` | swaps places with that neighbour; sizes follow the places |
| `Shift Alt D` | `DuplicatePanel` | `Workbench.DuplicatePanel` | a copy with all the tabs opens to the right, the focus stays |
| `Alt Comma` | `DuplicatePanelLeft` | `Workbench.DuplicatePanelLeft` | the copy opens to the left and takes the focus |
| `Alt .` | `DuplicatePanelRight` | `Workbench.DuplicatePanelRight` | the copy opens to the right and takes the focus |
| `Control Tab` | `ShowSwitchPanelPanel` | `Workbench.CycleMruNext` | tenfold's switch panel: files most recently used first, the previous one selected, Tab / Shift Tab move, releasing Control commits |
| `Control Shift Tab` | `ShowSwitchPanelPrevItem` | `Workbench.CycleMruPrev` | tenfold's recent-documents switcher |

**Perforce**

| Keys | 10x command | tenfold command | Difference |
| --- | --- | --- | --- |
| `Control P, Control E` | `PerforceOpenForEdit` | `Vcs.CheckOut` |  |
| `Control P, Control D` | `PerforceDiff` | `Vcs.DiffFileExternal` | diffs against the version control base revision in the external diff tool |
| `Control P, Control R` | `PerforceRevert` | `Vcs.RevertFile` | asks for confirmation first |

### tenfold additions

Keys for tenfold commands that 10x has no key for (none uses a key 10x binds in the same context):

| Keys | Command |
| --- | --- |
| `Backspace`, `Enter` / `KpEnter`, `Tab`, `Escape`, `Control Backspace`, `Control Delete` (editor) | `Editor.Backspace`, `Editor.Enter`, `Editor.Tab`, `Editor.CollapseCarets`, `Editor.DeleteWordLeft`, `Editor.DeleteWordRight` |
| `Shift Tab` | `UnindentLine` |
| `Control /` | `Editor.ToggleLineComment` |
| `Control J` | `Editor.JoinLines` |
| `Control M, Control O` | `Editor.CollapseToDefinitions` (Visual Studio's Collapse to Definitions; 10x has no such command) |
| `Control K, Control Z` | `RestoreRecentlyClosedFile` (Visual Studio's Restore Closed Tab; same command as 10x's `Control Shift T`) |
| `Control Shift K` | `DeleteLine` |
| `F12`, `Control F12`, `Shift F12` | `GotoSymbolDefinition`, `Editor.GotoDeclaration`, `FindSymbolReferences` |
| `Control Comma` | `FindSymbol` (symbol search) |
| `Control Q` | `Exit` |
| `Control Shift B`, `Control Alt O` | `BuildActiveWorkspace`, `ToggleBuildOutputPanel` |
| `Alt Z` | `ToggleWordWrap` |
| `Control F9`, `Control F10`, `Control Alt Cancel`, `Control Alt B` | `Debug.ToggleBreakpointEnabled`, `Debug.RunToCursor`, `Debug.BreakAll`, `ListBreakpoints` (breakpoint list) |
| `Alt 1` to `Alt 4` | `Workbench.FocusPanel 1..4` |

The earlier placeholder defaults that clash with 10x's map are gone, and the same keys now do what 10x does: for example
`Alt Left` / `Alt Right` are `MovePanelFocusLeft` / `Right` (use `Control -` / `Control Shift -` or the thumb buttons to go
back and forward), `Control Shift P` is `PlaybackKeySequence` (the command palette is `Control Shift X`), `F4` / `F8`
step through search results / build messages, and `Control Shift M` selects the bracket scope (the diagnostics list has no
key now). Commands with no default key: `View.ShowDiagnostics`, `Editor.ListBookmarks`, `Editor.ToggleBlockComment`,
`Editor.Indent`... see the palette. They are reachable from the [command palette](#command-palette) (`Control Shift X`);
there is no menu yet. A command whose user interface is missing says `<name> is not available yet` in the status bar.

### Command palette

**Control Shift X** (`ShowCommandPanel`, tenfold's `App.CommandPalette`) opens a list of every command: the 10x
command name (tenfold's own name for the few commands 10x has no equivalent of), the tenfold command id (dim) and its
current key binding on the right (the first one when there are several; a command with no key shows none). Type to
filter: the query is matched fuzzily against the shown name, tenfold's name and the id (`gotosymboldef`,
`restart clangd`, `vcsrevertf`), tighter and earlier matches first. Enter closes the palette and runs the command as its
key would; Escape closes it. A command that cannot run right now (no file open, ...) is greyed out and Enter says so in the
status bar. Not listed: commands that need an argument to do anything (`Workbench.FocusPanel`, which is bound to
`Alt 1` to `Alt 4`) and the palette itself. `Build.Build` is listed and runs the detected build; a custom build command
still needs a key binding with an argument. The palette lists the commands tenfold has; 10x commands it lacks are not
listed (see FEATURE_PARITY).

## Context menus

**Right-click** opens the context menu of what is under the pointer, **Shift F10** or the **AppsKey** (10x's
`ShowContextMenu`) opens the one for the focused element: the editor menu at the caret. Pressing the key again closes it.

* **Editor text**: Cut, Copy, Paste, Go To Definition, Go To Declaration, Find All References, Rename, Quick Info, Switch
  Header/Source, Format Selection, Toggle Comment, Select All, Toggle Breakpoint, Run To Cursor, Revert Change and Show
  Change (only when the caret is on a version-control change), Open in Visual Studio (Windows), Show in Workspace Tree,
  Open Containing Folder, Copy Full Path. A right-click outside the selection moves the caret
  there first, inside it keeps the selection.
* **A tab**: Close, Close All But This, Close All But Pinned, Pin / Unpin Tab, Copy Full Path, Open Containing Folder. The
  right-clicked tab becomes the active tab first. The two "close all" items ask once about the modified documents they
  would close (the list dialog, see "Read this first").
* **Explorer tree**: right-click a node (it is selected first), or Shift F10 / AppsKey with the tree focused. Files: Open,
  Collapse All, New File..., New Folder..., Rename, Delete, Open Containing Folder, Copy Full Path, Open in Terminal,
  Build Current File (C/C++ sources), then Switch to Folder / Solution View, Show All Files and Properties. Folders,
  projects and the solution: Expand / Collapse, Collapse All, New File..., New Folder..., (Rename and Delete for folders
  on disk), Open Containing Folder, Copy Full Path, Open in Terminal and the same last group. 10x's "Settings..." on the
  workspace and project nodes is not there (no settings dialog). See "Workspace explorer".
* **Status bar**: the position (Go To Line), line ending (CRLF / LF), configuration, compile flags, clangd and build
  segments each offer the commands that belong to them.

Every item runs the same command as its key; the key is shown on the right in 10x's notation, and an item whose command
cannot run right now is greyed out. Up / Down (Home / End) move, Enter runs the item, Escape closes, a typed letter jumps
to the next item that starts with it, Right / Left open and close a submenu. With the mouse, hovering highlights and a
click runs the item; a click anywhere else closes the menu. The menu opens below the pointer or caret, above it when
there is no room, and stays inside the window. Colours: `menuBackground`, `menuBorder`, `menuSelection`, `menuSeparator`
in a theme (they default to the popup colours).

## Terminal

`NewTerminal` (`Control '`) opens a terminal in a new tab of the active editor panel and focuses it; `ShowTerminal`
(`Control Shift I`) creates one if there is none, else cycles: from a focused terminal to the next one, from anywhere else to
the terminal shown last. `ClaudeCodeTerminal` and `CursorTerminal` (no default keys) open terminals running
`terminal.claudeCommand` / `terminal.cursorCommand`. The program starts in the workspace root (else the current file's
folder, else your home folder). Closing the tab (x, Control F4) kills the program and everything it started. When the
program exits the tab keeps its last screen and a `[process exited with code N]` line (`terminal.closeOnExit` closes it
instead). Terminals are not restored at the next start.

Keys go to the program, except the app's tab and panel keys (Control PageUp/PageDown, Control Tab, Alt+arrows, Control F4 ...), the
terminal commands, the command palette and the window toggles. Copy: select with the mouse (double click = word, triple =
line), then Control Shift C or Control Insert (key map command `Terminal.Copy`). Control C always goes to the program as ^C, with or
without a selection, and Control V as its literal control character (Visual Studio 2022; `terminal.ctrlCCopiesSelection = true` makes
Control C copy a selection). Paste: Control Shift V, Shift Insert (`Terminal.Paste`) or a right click, which pastes and never copies a
selection. The copy and paste keys are bindings of the key context `terminal` in the key map, so you can rebind them in your
`KeyMappings.10x_settings` (for example `Control Shift X: Terminal.Copy`). "Open in Terminal" in the explorer's right-click menu opens a
terminal in that folder (for a file, the file's folder); `NewTerminal` starts in the workspace root.
Scrollback: mouse wheel, Shift PageUp/PageDown, the scrollbar; typing scrolls back to the bottom. Hold Shift to select while a
program is tracking the mouse. Colours come from the theme (`color.terminalBackground`, `terminalForeground`, `terminalCursor`,
`terminalSelection`, `terminalAnsi0` ... `terminalAnsi15`; `vs-dark` defines them). Settings: `terminal.*` in the table above.
Not implemented: reflow on resize, search, link detection, split terminals, shell integration. Italic text is drawn upright.

## Logs and crash reports

tenfold sends nothing anywhere. It keeps local files only.

**Log.** `<workspace>\.tenfold\log.txt` once a workspace is open (before that, `<user data folder>\tenfold\log.txt`: 
`%LOCALAPPDATA%` on Windows, `$XDG_DATA_HOME` or `~/.local/share` on Linux). One line per record:
`2026-10-25T12:34:56.789Z I t3 clangd: message` (UTC, level E/W/I/D, thread number, subsystem). It rotates at 5 MB into
`log.1.txt` and `log.2.txt`. The first record of a run has the version, commit, platform and workspace. The categories to
look for are `clangd` (which executable, compile database source and entry count, crashes and restarts), `vcs`, `build`,
`index`, `watch` and `editor`. `TENFOLD_LOG=debug|info|warn|error|off` in the environment sets the level (`off` writes
no file). From a shell: `tenfold-cli log <workspace folder> [--tail N]` prints it; there is no "open log" command in the
window yet. clangd's own stderr is in `.tenfold\clangd.log`.

**Crash reports.** The editor installs a crash handler. On Windows an unhandled exception writes a minidump
`crash-<pid>-<tick>.dmp` to `%LOCALAPPDATA%\tenfold\crashes\`, with a `.txt` note naming the log. On Linux it writes
`crash-<pid>-<time>.txt` (signal, fault address, a backtrace) to `$XDG_DATA_HOME/tenfold/crashes` (or
`~/.local/share/tenfold/crashes`); macOS uses `~/Library/Application Support/tenfold/crashes`. Nothing is uploaded; attach the
file and the end of `log.txt` to a bug report. The last half second of the log may be missing after a crash. A crash of
*clangd* is not a crash of the editor: see the next section.

## Troubleshooting

### Why is this symbol approximate?

"Approximate" means the answer came from the fast index. Work down this list; `Help.ShowCompileCommand` (or `tenfold-cli why`)
answers most of it in one screen, in its `clangd state`, `This file` and `Confidence` lines.

1. **clangd is not running.** The status bar says `clangd: off`. Causes: no clangd was found (the log has no `clangd: using ...` line; start `tenfold-ui` from a console to see
   `tenfold-ui: editor start: clangd not found: ...`; a `clangdPath` that does not resolve gives the same), clangd crashed
   (below), or it has not started yet (`clangd: starting`).
2. **clangd is still working.** `clangd: indexing N%` in the status bar: until its background index is complete it can
   only see the file you are in, so a function defined in another `.cpp` is first answered by the fast index. A freshly
   opened file takes seconds to parse (longer for a huge translation unit); approximate rows turn into authoritative
   ones by themselves. The index is kept in `.cache\clangd`, so the second start of a workspace is warm.
3. **The file is not open in the editor.** clangd only parses files tenfold has opened; open the file.
4. **The flags are a guess.** If the report says `heuristic` (or cached), clangd may not see your defines or include
   directories, so identifiers show as unresolved and diagnostics may be wrong. Give tenfold a real database
   (next section).
5. **The file is not in the database.** Headers are usually not listed (only source files compile), and a source file
   that the project excludes from the active configuration, or whose project the solution does not build there, has no
   entry. clangd then borrows flags from another entry or uses the fallback flags; if the guess is wrong the symbols
   are wrong.
6. **Generated code.** A header produced by your build (a `.gen.h`, protobuf output) does not exist until you build, so
   everything that depends on it degrades. Build once, and use a real `compile_commands.json`.

### Missing headers, wrong defines, wrong flags

Run `Help.ShowCompileCommand` on the file. Read the `Compile flags` line first, then the command.

* *Source is `heuristic`:* produce a real database. For CMake: `cmake -S . -B build -G Ninja
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` puts `build/compile_commands.json` where tenfold finds it. For a Visual Studio
  solution, put the `.sln` directly in the workspace folder instead of a subfolder. For other build systems, any tool that
  writes `compile_commands.json` (Bear, SCons, ...) into the root works. After it appears, tenfold notices the new file
  by itself; if the report still shows the old source, delete `.tenfold\compile_commands.json` and restart.
* *Source is `existing` but you wanted the solution:* a `compile_commands.json` found in the root, `build`, `out` ... wins
  over the `.sln`. Remove or rename it. Likewise a `CMakeLists.txt` wins over the `.sln` (set `"runCMake": false`).
* *Source is `vcxproj`:* check `Visual Studio:` for the configuration. A missing include directory or define is
  something the evaluator could not resolve (an unknown property drops only its own entry) or that lives in a toolset
  file; compare with the project in Visual Studio. The `.vcxproj` limits are listed under
  [What is not](#visual-studio-solutions). The log has `vcxproj: ...` lines for projects it could not read.
* *`'vector' file not found` or other standard-library and Windows SDK headers missing:* on Windows clangd gets the
  MSVC headers from `cl.exe`. (On Linux and macOS the host's standard library is used, see above; only the Windows SDK
  is missing there.) The report's `MSVC toolchain:` line shows which `cl.exe`, or `cl.exe not found`. Start tenfold from a
  Developer Command Prompt (so `VCToolsInstallDir` is set) or set `msvcToolchain` in `workspace.json` to the full path
  of `cl.exe`, then restart.
* *Whole folders missing from search and symbols:* the scan skips directories named exactly `build`, `out`, `bin` or
  `obj`, or starting with `build-` or `cmake-build`, besides the names in `ignoreDirs`; see
  [Opening a workspace](#opening-a-workspace) and change `ignoreDirNames` / `ignoreDirPrefixes`.
* *A language standard mismatch* shows as `-std=`/`/std:` in the command. In heuristic mode change `cppStandard`.

### clangd crashed or is restarting

A clangd that exits unexpectedly is restarted automatically after 0.5 s, then 1, 2, 4 ... up to 30 s between attempts,
and the open documents are re-sent to the new one. While it is down the fast index answers alone (everything
approximate). A clangd that runs for a minute before dying starts the count again; after **5 fast crashes in a row** tenfold
gives up. The status bar shows the engine's text during all of this (`clangd restarting (2)...`, then
`clangd stopped: crashed 5 times` when it gave up), and `starting` / `ready` again after a restart. The same messages are in `.tenfold\log.txt` (`clangd restarting`, crash counts, the exit)
and clangd's own complaints in `.tenfold\clangd.log`. A clangd that crashes on every file usually has a bad flag:
look at the command in the report. **`Clangd.Restart`** ("Restart clangd", in the command palette) restarts clangd now: it resets the
crash count, re-sends the open documents and says `clangd restarted` (or why not) in the status bar; use it after
fixing the flags or when clangd has stopped. Switching the Visual Studio configuration also restarts clangd against the
regenerated database. `clangdExtraArgs`
in `workspace.json` passes extra arguments to clangd.

### Visual Studio not found

The debug commands report the reason in the status bar and the log:

* `no debugger backend on this platform`: you are not on Windows.
* `Visual Studio is not installed (vswhere.exe not found)`: tenfold looked for
  `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe`. Set the environment variable `TENFOLD_DEVENV` to the
  full path of your `devenv.exe`, then restart tenfold.
* `vswhere.exe found no Visual Studio installation`: tenfold asks `vswhere -latest -property productPath` and got
  nothing back. Set `TENFOLD_DEVENV` to your `devenv.exe`.
* `Visual Studio did not open <sln> within 90 s` (the message ends with the exact time): the solution may be waiting on a
  dialog or an upgrade prompt in the Visual Studio window it started. Look at that window, then press the debug key again.
* `Visual Studio is busy (... is a dialog open?)`: a modal dialog in Visual Studio blocks automation. Dismiss it.
* Nothing happens at startup: that is by design. Attach-at-startup only joins a Visual Studio that already has the *same
  solution file* open (compared case-insensitively, with separators normalised); it never launches one.

`Build` failures with `msbuild` not recognised are a different thing: `msbuild` must be on `PATH` (see [Building](#building)).

### Perforce not detected

The status bar segment is empty and the log says `vcs: no version control system found for <root>` or `vcs: git at ...`.
tenfold runs `p4 -ztag -F %clientRoot% info` and `p4 -ztag where <workspace>/...` in the workspace folder, so the
environment must resolve your client there exactly as in your shell (P4CONFIG, P4CLIENT, P4PORT, P4USER). Check:

* `p4` is on `PATH` for the program that launched tenfold, or `vcs.p4Path` names it (a Start menu launch can have a
  different `PATH` from your shell).
* `p4 info` shows a client with a `Client root` (not `*unknown*`), the server is reachable (a 3 s timeout applies), and
  the workspace folder is inside the client view. `p4 where <folder>/...` must print a depot path that is not an
  exclusion. A negative result is remembered for 15 s.
* A Git repository with `p4` installed is fine: if no Perforce client maps the folder, Git is used. To force one, set
  `vcs.provider` to `perforce` or `git` in `workspace.json` (read at start).
* Detection happens when the first file is opened, not when tenfold starts.

If the commands above print what you expect and tenfold still shows nothing, it is a bug or one of the
unverified items in the next sections; the log and the output of those two commands are what is needed (OPEN_QUESTIONS #17).

### Other

* **Fonts.** If `font.editor` is not found tenfold silently tries `font.fallbacks` (`Cascadia Mono`, `Consolas`) and then
  the platform's monospace font. Names are matched against font *file* names in the system font folders; if your font is not
  found, give the path to its `.ttf`.
* **Settings did not apply.** The status bar message lists file and line of the first problems; fix and save, they
  reload. A `.tenfold` folder created after tenfold started is not watched until the next start.
* **A key does nothing.** The key map is read at start, and a command whose UI is missing says so in the status bar. An
  editor key does not work while a popup list is open (the `picker` context). The command palette finds a command by name
  and shows the key it has.

## What does not exist yet

* Opening a folder from the window, menus, a recent-workspaces list, drag and drop.
* Autosave and recovery of unsaved buffers after a crash.
* GUI for code actions / fix-its, call and type hierarchy, inlay hints, the document outline.
* An "open log" command.
* Scripting (the Python API of 10x is deliberately not implemented, OPEN_QUESTIONS #11), `.10x` workspace import (not
  wanted, #12), Unreal Engine awareness.
* 10x's key bindings (OPEN_QUESTIONS #1).

## Provisional and unverified

* **Behaviours copied from memory of 10x** and waiting for your confirmation: undo grouping (#3), the tab / panel
  behaviours (#5), the symbol-search preview (#6), the `vs-dark` colours marked `[?]` (#19), every default of the
  editing conveniences (#8), the column guide, and whether trimming whitespace on save should be on.
* **Key bindings:** 10x's own format and default key map; 6 of its 183 default bindings (6 commands) are not implemented yet (FEATURE_PARITY).
* **Perforce** has only run against a fake `p4` (#17): `p4 -ztag -F %clientRoot% info`, `p4 where` for an unmapped
  folder, and what `p4 edit` returns for an exclusively locked file are unchecked on a real server.
* **Visual Studio debugging and the MSVC toolchain discovery** are covered by CI that does not block merges; a long session
  against your solution has not been done. Whether `Debug.Start` makes Visual Studio build first is up to Visual
  Studio's own settings; tenfold does not check.
* **Windows packaging** (the ZIP layout, the unsigned executable) has been produced by CI and unpacked by a smoke
  test on Linux; a first run on a clean Windows machine is still to be confirmed. The AppImage and the macOS image are best
  effort.
* **`msbuild` lookup** is by name on `PATH` only; whether the Developer Command Prompt route is enough on your machine is
  unconfirmed.
* **Font lookup by file name** on Windows (for example `Cascadia Mono`, which Windows Terminal installs outside
  `C:\Windows\Fonts`) is unconfirmed; use a path if it fails.
* **The CMake Visual Studio generators** not producing `compile_commands.json` is CMake's documented behaviour, not
  something tenfold checks.
* **What clangd does for a file the database does not list** (borrowing a neighbour's command) is clangd's behaviour; tenfold
  cannot display it.
