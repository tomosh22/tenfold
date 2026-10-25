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

* **Quitting asks.** Ctrl+Q (`App.Quit`) and closing the window list the documents with unsaved edits and offer
  **Save All / Don't Save / Cancel** (one document: **Save / Don't Save / Cancel**). Enter is Save, Escape is Cancel.
  If a save fails (a read-only Perforce file, for example) tenfold stays open and the status bar says why. This includes
  edited documents that no longer have a tab.
* **Closing a tab asks** when it is the last view of a document with unsaved edits (the tab's x, middle click or
  `Workbench.CloseTab`, Ctrl+F4). Don't Save closes the document and drops the edits. A second view of the same document (a
  split) closes without asking.
* **Files that change on disk are reloaded** when they have no unsaved edits (a checkout, a branch switch or a code
  generator rewrote them); carets, scroll position and navigation history keep their place. A file with unsaved edits
  asks **Reload (lose my changes) / Keep my version** (Keep is the default choice). A file deleted on disk keeps its
  buffer; its tab says `(deleted)` and the status bar `deleted on disk`.

All three follow Visual Studio, because 10x's behaviour is not confirmed (OPEN_QUESTIONS #5), and each has a setting:
`editor.confirmUnsaved` (default `true`; `false` skips every question: closing a tab then keeps the edited document in
memory without a tab, and quitting discards it) and `editor.reloadExternalChanges` (`auto`, `prompt`, `never`; see
[Settings](#settings-files)).

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
.inl .ipp .tpp .cuh` (and files with no extension), and `.txt .md .json .cmake .py .lua .natvis .def .rc` count as
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
   `.vcxproj` files itself and writes `.tenfold/compile_commands.json` with `clang-cl` style commands for the active
   configuration. All `.sln` files in the root are used. See [Visual Studio solutions](#visual-studio-solutions).
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
| Go to Definition / Declaration (F12, Ctrl+F12) with a single answer | **No mark.** tenfold jumps at once, even on an approximate answer. If the authoritative answer is a different place it jumps again. Both jumps are in the navigation history, so Back returns. | The same jump, once. |
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
  [command palette](#command-palette) (Ctrl+Shift+P), click `flags: heuristic` in the status bar, or bind a key in
  `keybindings.txt`, for example `Ctrl+Alt+F1 = Help.ShowCompileCommand`.
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

* **Alt+Left / Alt+Right**, **Ctrl+-** / **Ctrl+Shift+-**, and the mouse thumb buttons (Mouse4 / Mouse5) go back and
  forward. A toolbar above the panels has Back and Forward buttons with a tooltip showing the key; **long-press or
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
* **Ctrl+Shift+Backspace** (`Nav.LastEditLocation`) goes to where you last edited, through the history, so Back
  returns. The remembered spot follows edits above it, like history entries.
* Setting `nav.statusCounts` shows the back and forward counts in the status bar.

Switching tabs is not a navigation jump and is not recorded; the tab order for Ctrl+Tab is most recently used first.

## Pickers and search

All pickers are keyboard-driven lists: Up / Down / PageUp / PageDown move, Enter accepts, Escape closes, a click on a
row accepts it, a click outside closes it.

| Keys | What |
| --- | --- |
| **Ctrl+P** or **Ctrl+O** (`File.OpenFile`) | Open a file by fuzzy name. The file name weighs four times as much as the rest of the path; recently used files come first and lead the empty list. Several words must all match. Opening is a tab switch, not a history jump. |
| **Ctrl+,** (`Search.SymbolSearch`) | Workspace symbol search: fuzzy and qualified (`geo::Tr`). Approximate rows appear at once and are replaced by clangd's; the right pane previews the target line (`picker.preview`). Enter jumps and records history. |
| **Ctrl+Shift+F** (`Search.FindInWorkspace`) | Find in files. Results stream in grouped by file with the match highlighted. **Alt+C** case, **Alt+W** whole word, **Alt+R** regex (PCRE2). Add file globs after ` :: `, for example `foo :: *.cpp, !third_party` (`!` excludes). A single-line selection seeds the query. Open buffers are searched with their unsaved text. |
| **F8** / **Shift+F8** | Next / previous find-in-files result, wrapping, even after the popup is closed; files with matches get marks on the scrollbar. |
| **Ctrl+Shift+M** (`View.ShowDiagnostics`) | List of the diagnostics of the open files (clangd) and of the last build, errors first; type to filter. clangd only reports files that are open, and the list says so. |
| **F12**, **Ctrl+F12**, **Shift+F12** | Go to definition, declaration, find all references. One definition: jump at once. Several: a list. References always show a list. |
| **Alt+O** | Switch between header and source (clangd's answer, else by file name). |
| **Ctrl+G** (`Editor.GotoLine`) | Go to line: `42` or `42:7` (line:column, 1-based). Numbers past the end go to the last line. Recorded in the history. |
| **Ctrl+F** / **Ctrl+H** | Find / replace in the current file (see [Editing](#editing)). |
| **Ctrl+Alt+B** | Breakpoint list (see [Debugging](#debugging-through-visual-studio)). |
| **Ctrl+Shift+F2** | Bookmark list (see [Editing](#editing)). |

Hover: rest the mouse on an identifier for 400 ms. Diagnostics under the mouse are shown first (the last build's
message for that spot follows, marked `build`), then clangd's hover text. Completion opens at the caret after `.`,
`->`, `::` and once a word reaches two characters; Tab or Enter accepts it as one undo step. Signature help opens on
`(` and `,` with the active parameter highlighted; Escape or `)` closes it.

## Editing

The text model is UTF-8 with LF or CRLF preserved per file (`Editor.LineEndingLF` / `Editor.LineEndingCRLF` convert;
they have no default key). Other encodings are not supported.

**Carets.** Ctrl+Alt+Up / Down add a caret above / below, **Ctrl+D** selects the word under the caret and then adds the
next occurrence, **Ctrl-click** adds a caret, **Alt-click** adds a caret and **Alt-drag** makes a column selection,
double-click selects a word, triple-click a line. Carets that touch merge. Every edit acts on every caret and is one undo
step; Ctrl+C / Ctrl+X with several carets copy one line per caret, and pasting as many lines as carets puts one line in
each. Escape collapses to the primary caret (it does not quit).

**Undo.** Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z. Typed characters are grouped into one undo step per word: the group ends at
whitespace, after a 1 s pause, or when the caret moves. Set `"undoGrouping": "char"` in `.tenfold/workspace.json` for one
step per character (`undoGroupPauseMs` changes the pause). How 10x groups typing is not confirmed (OPEN_QUESTIONS #3).

**Conveniences** (all default keys are placeholders, see [Key bindings](#key-bindings); the behaviours are guesses about
10x that need your check, OPEN_QUESTIONS #8):

* Auto-indent on Enter (`editor.autoIndent`): copies the previous line's indentation, one level deeper after a line
  ending in `{`, `(`, `[` or a binary operator, returns one level when a continued statement ends with `;`; Enter
  between `{` and `}` opens a block with the closer on its own line; typing `}` as the first character on a line dedents
  it to the line of its `{`. Not done: the body of a brace-less `if` / `for`, aligning continuation lines under an open
  parenthesis, `case` labels.
* Indent unit (`editor.indentUnit`, `editor.detectIndent`): detected per file from its first 400 lines, else the
  setting. Tab inserts one unit (a multi-line selection indents its lines); Shift+Tab outdents.
* Auto-close (`editor.autoClosePairs`): `( [ { " '` close themselves when the next character is blank, the end of
  the line or one of `) ] } ; ,`; quotes are not paired after an identifier character, a backslash or the same quote;
  typing the closer steps over it (tenfold does not remember which closers it inserted); a selection is wrapped; Backspace
  inside an empty pair deletes both.
* **Ctrl+/** toggles `//` comments at the lines' minimum indentation; **Ctrl+Shift+/** wraps each selection in `/* */`
  (a bare caret: its line's text) and unwraps again. C and C++ comment syntax only.
* **Ctrl+Shift+D** duplicates the line (or the selection), **Alt+Up / Alt+Down** move lines, **Ctrl+J** joins lines,
  **Ctrl+Shift+K** deletes lines.
* **Bookmarks**: Ctrl+F2 toggles, F2 / Shift+F2 step through all bookmarks of the workspace (wrapping), Ctrl+Shift+F2
  lists them. They follow edits and are saved in `.tenfold/bookmarks.json`.
* **Find / replace in the file** (Ctrl+F, Ctrl+H): a bar at the top right of the editor; typing searches at once and
  highlights every match, Enter / F3 next, Shift+Enter / Shift+F3 previous, Alt+C / Alt+W / Alt+R toggle case, whole word,
  regex. In the replace field Enter replaces and moves on, Ctrl+Enter replaces all (one undo step), Tab switches fields,
  Escape closes. Regex replacements do not expand group references (`$1`).
* `editor.trimTrailingWhitespace` removes trailing blanks on save (off by default).
* The matching bracket of the one at the caret is highlighted. `editor.showWhitespace`, `editor.columnGuide`,
  `editor.highlightCurrentLine` and the caret blink are settings.

Not available yet: code actions and fix-its, rename, formatting, call and type hierarchy, inlay hints, folding,
the document outline. The engine has them; the GUI does not (see [What does not exist yet](#what-does-not-exist-yet)).

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
`Debug|Win32` for a project); projects the solution does not build in that configuration are skipped, which also means
their files get no entry in the database.

To change it:

* the command **`Build.SelectConfiguration`** (a configuration picker, being added to the window at the same time as
  this guide; the engine side, `Editor::selectVsConfiguration`, already exists; if your build does not have the command,
  use the CLI);
* or `tenfold-cli vsconfig <folder> --list` and `tenfold-cli vsconfig <folder> --set "Release|x64"`, or edit
  `workspace.json` and restart.

Changing it regenerates `.tenfold/compile_commands.json` and restarts clangd against it. **The build uses the same
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
  in `workspace.json`, which it saves once found) and starts clangd with `--query-driver` for it. If `cl.exe` is not
  found the log says `cl.exe not found; set "msvcToolchain" in .tenfold/workspace.json`.
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

**Keys** (Visual Studio's, see the table in [Key bindings](#key-bindings)): F5 start / continue, Ctrl+F5 start without
debugging, Shift+F5 stop, Ctrl+Shift+F5 restart, F9 toggle breakpoint, Ctrl+F9 enable / disable, Ctrl+Shift+F9 delete all,
F10 step over, F11 step into, Shift+F11 step out, Ctrl+F10 run to cursor, Ctrl+Alt+Break break all, Ctrl+Alt+B list
breakpoints (Enter jumps, Space enables / disables, Delete removes). `Debug.EditCondition` (no key) sets the condition
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
elsewhere do not touch the files. None of the `Vcs.*` commands has a default key (the key map is waiting for your 10x
bindings, OPEN_QUESTIONS #1); `assets/keybindings.default.txt` lists commented examples to copy into your own file:

```
[editor] Alt+Shift+Down = Vcs.NextChange
[editor] Alt+Shift+Up   = Vcs.PrevChange
[editor] Alt+Shift+D    = Vcs.ToggleInlineDiff
[editor] Alt+Shift+Z    = Vcs.RevertHunk
Alt+Shift+R = Vcs.RevertFile
Alt+Shift+M = Vcs.DiffFileExternal
Alt+Shift+F5 = Vcs.RefreshStatus
Alt+Shift+E = Vcs.CheckOut
```

Not done: blame, commit or stage UI, side-by-side diff inside tenfold.

## Building

`Build.Build` (**Ctrl+Shift+B**) runs the workspace build in the background and opens the Build panel at the bottom of
the window (**Ctrl+Alt+O** shows / hides it, `Build.Cancel` is **Ctrl+Break**). The command is chosen in this order:

1. `buildCommand` in `.tenfold/workspace.json` (run through `cmd /s /c` on Windows, `sh -c` elsewhere, in the
   workspace folder); or an argument to the command: `Ctrl+Shift+B = Build.Build make -j8` in your key file.
2. A `.sln` in the folder (the first one): `msbuild <sln> /m /p:Configuration=<vsConfiguration> /p:Platform=<vsPlatform>`;
   or, with no `.sln`, a `.vcxproj` in the folder. `msbuild` is started by name, so it has to be on `PATH`: start
   tenfold from a Visual Studio *Developer Command Prompt*, or set `buildCommand`.
3. A `CMakeLists.txt`: `cmake --build <dir> --config <vsConfiguration>`. `<dir>` is `buildDir`, else the folder of the
   compile database clangd uses if it holds a `CMakeCache.txt`, else `.tenfold/cmake-build`. tenfold never configures the
   build tree for you.
4. A `Makefile`: `make -j<cores>`.

Output streams into the panel (colour codes removed). Lines that are errors or warnings are coloured, and clicking one,
or pressing Enter on it, jumps to the source; **F4** and **Shift+F4** step through the errors and warnings that have a
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
| `editor.wheelLines` | `3` | int | Lines scrolled per mouse-wheel notch. |
| `editor.tabWidth` | `4` | int | Display width of a tab character, in columns. |
| `editor.columnGuide` | `0` | int | Draw a vertical guide line at this column. 0 turns it off. A guide such as 90 is only a candidate until compared with 10x. |
| `editor.showWhitespace` | `false` | bool | Draw dots for spaces and arrows for tabs (first 4000 bytes of each line). |
| `editor.indentUnit` | `4` | string | One indent level: `tab`, or a number of spaces from 1 to 16 (anything else means 4). Used when `editor.detectIndent` finds nothing in the file, or is off. |
| `editor.detectIndent` | `true` | bool | Take the indent unit from the file itself (its first 400 lines: tabs, or the most common step) instead of `editor.indentUnit`. |
| `editor.autoIndent` | `true` | bool | Enter copies the previous line's indentation (one level deeper after `{`, `(`, `[` or a trailing operator); `}` typed first on a line dedents. |
| `editor.autoClosePairs` | `true` | bool | `( [ { " '` insert their closer, typing the closer steps over it, a selection is wrapped, Backspace between an empty pair removes both. |
| `editor.trimTrailingWhitespace` | `false` | bool | Remove trailing blanks on every line when saving (one undo step). Off by default (VERIFY against 10x). |
| `editor.confirmUnsaved` | `true` | bool | Closing the last tab of a modified file, closing the window and `App.Quit` ask Save / Don't Save / Cancel. `false` skips every question. Follows Visual Studio (OPEN_QUESTIONS #5). |
| `editor.reloadExternalChanges` | `auto` | string | A file that changed on disk behind the editor's back: `auto` reloads it unless it has unsaved edits (then asks Reload / Keep my version), `prompt` always asks, `never` leaves the buffer alone and only says so in the status bar. Follows Visual Studio's "reload modified files unless there are unsaved changes" (OPEN_QUESTIONS #5). |
| `font.editor` | (empty) | font | Editor font as `family or path[, size in pt]`. Empty means unset: `font.fallbacks` are tried in order. A family name is matched against font file names in the system font folders (case and spaces ignored), so give a full path if it is not found. |
| `font.fallbacks` | `Cascadia Mono, Consolas` | string | Comma-separated font names tried in order when `font.editor` is unset or not found; after the last one the platform's usual monospace font is used. |
| `font.size` | `10` | float | Font size in points when `font.editor` does not carry one. |
| `ui.theme` | `vs-dark` | string | Theme name: `.tenfold/themes/<name>.txt` in the workspace, then `themes/<name>.txt` in the user config folder, then the built-ins `dark`, `light`, `vs-dark`. |
| `nav.statusCounts` | `false` | bool | Show the back and forward counts in the status bar (an arrow with the number of entries in each direction). |
| `picker.preview` | `true` | bool | Symbol search shows a preview of the target line on the right (VERIFY against 10x, OPEN_QUESTIONS #6). |
| `vcs.diffTool` | (empty) | string | External diff program for `Vcs.DiffFileExternal`. Empty means automatic: `p4merge` from PATH (on Windows `p4merge.exe`, then `%ProgramFiles%\Perforce\p4merge.exe`). |
| `vcs.diffToolArgs` | `"{base}" "{current}"` | string | Arguments of that program, split like a command line (double quotes group). Placeholders: `{base}` (temp file with the committed text), `{current}` (temp file with the buffer), `{baseTitle}`, `{currentTitle}`, `{file}` (the real path). A value wrapped in quotes loses the outer pair, which is why the default file doubles them. With these default arguments and a tool called p4merge, tenfold also passes titles (`-nl`, `-nr`). |
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
`vs-dark`. Choose one with `ui.theme = light` in a settings file. Several `vs-dark` colours were written from memory
and are marked `[?]` in the theme file's header: panel background, tabs, scrollbars, squiggles, diff and debugger colours
(OPEN_QUESTIONS #19). Corrections are welcome. Control-flow keywords (`if`, `return`) and string escapes have no
colour of their own yet.

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
  line. The editor, debugger and version-control colours are optional: leaving them out gives the built-in look.
* `token.<kind>[.<modifier>]`: colours of the lexical kinds (`keyword identifier number string char comment
  preprocessor operator punctuation`) and of clangd's semantic kinds (`namespace type class struct interface enum
  enumMember typeParameter concept function method property variable parameter macro event decorator modifier regexp`).
  A modifier is one of `declaration definition readonly static deprecated abstract async modification documentation
  defaultLibrary`. A token falls back along `method.static` -> `method` -> `function` -> `identifier`.
* Colours are `#rrggbb` or `#rrggbbaa`.

## Key bindings

Bindings are a text file, one binding per line:

```
[context] chord [chord ...] = Command.Id [argument]
```

* **Context** is optional: `global` (the default), `editor` (a text view has focus) or `picker` (a popup list is open).
  A binding in a narrower context beats a `global` one for the same keys.
* **Chords** are `Modifier+Modifier+Key`; several chords separated by spaces make a sequence (`Ctrl+K Ctrl+C`; a pending
  sequence shows in the status bar and expires after a timeout). Modifiers: `Ctrl`, `Shift`, `Alt`, `Super` (also
  `Control`, `Option`, `Win`, `Cmd`, `Meta`). Keys: letters, digits, `F1` to `F24`, `Space Tab Enter KpEnter Escape Backspace
  Delete Insert Home End PageUp PageDown Left Right Up Down Break Pause`, and punctuation either as the character or by
  name (`Minus Plus Comma Period Slash Backslash Semicolon Quote Backquote Equals LBracket RBracket`). `Mouse4` and
  `Mouse5` are the thumb buttons. Key names are case-insensitive.
* **Command** is a command id, optionally followed by an argument (`Workbench.FocusPanel 2`, `Build.Build make -j8`). A
  command of `-` removes the binding for those keys from an earlier layer.
* `#` starts a comment, only at the start of a line.

**Layers**, later wins: the embedded `assets/keybindings.default.txt`, then `%APPDATA%\tenfold\keybindings.txt` (see
[Settings files](#settings-files) for the other platforms), then `<workspace>\.tenfold\keybindings.txt`. Mistakes
(unknown key, unknown modifier, missing `=`) are reported in the status bar with the file and line and the line is
skipped; a duplicate binding in one file warns and the later one wins. **Key bindings are read once at start**; restart
tenfold after editing them.

Example `keybindings.txt`:

```
Ctrl+Alt+F1 = Help.ShowCompileCommand
Ctrl+K Ctrl+C = Editor.ToggleLineComment
Ctrl+Shift+B = Build.Build msbuild /m /p:Configuration=Release /p:Platform=x64
Alt+Left = -
```

### The current defaults are provisional

**These are placeholders, not 10x's bindings.** You asked for 10x's own key-binding file format and 10x's own default
bindings. The format here is tenfold's own, and the defaults come from Visual Studio habits and common editor
conventions. 10x's real defaults could not be obtained (its documentation was unreachable from the build environment and
they must not be taken from memory), so the key map is blocked on OPEN_QUESTIONS #1: send an export of your 10x
`KeyMappings.10x_settings` (plus its command list), or access to 10xeditor.com, and the defaults will be replaced.
Expect them to change.

Defaults today (`assets/keybindings.default.txt` is the authority):

| Keys | Command | Keys | Command |
| --- | --- | --- | --- |
| Alt+Left, Ctrl+-, Mouse4 | `Nav.Back` | F12 | `Editor.GotoDefinition` |
| Alt+Right, Ctrl+Shift+-, Mouse5 | `Nav.Forward` | Ctrl+F12 | `Editor.GotoDeclaration` |
| Ctrl+Shift+Backspace | `Nav.LastEditLocation` | Shift+F12 | `Editor.FindReferences` |
| Ctrl+S | `File.Save` | Alt+O | `Editor.SwitchHeaderSource` |
| Ctrl+Q | `App.Quit` (asks about unsaved edits) | Ctrl+P, Ctrl+O | `File.OpenFile` |
| Ctrl+, | `Search.SymbolSearch` | Ctrl+Shift+F | `Search.FindInWorkspace` |
| F8 / Shift+F8 | `Search.NextResult` / `PrevResult` | Ctrl+Shift+M | `View.ShowDiagnostics` |
| Ctrl+Shift+B | `Build.Build` | Ctrl+Break | `Build.Cancel` |
| F4 / Shift+F4 | `Build.NextError` / `PrevError` | Ctrl+Alt+O | `View.ToggleBuildPanel` |
| F5 | `Debug.Start` | Ctrl+F5 | `Debug.StartWithoutDebugging` |
| Shift+F5 | `Debug.Stop` | Ctrl+Shift+F5 | `Debug.Restart` |
| F9 | `Debug.ToggleBreakpoint` | Ctrl+F9 | `Debug.ToggleBreakpointEnabled` |
| Ctrl+Shift+F9 | `Debug.DeleteAllBreakpoints` | Ctrl+Alt+B | `Debug.ShowBreakpoints` |
| F10 / F11 / Shift+F11 | `Debug.StepOver` / `StepInto` / `StepOut` | Ctrl+F10 | `Debug.RunToCursor` |
| Ctrl+Alt+Break | `Debug.BreakAll` | Alt+Z | `View.ToggleWordWrap` |
| Alt+1 .. Alt+4 | `Workbench.FocusPanel 1..4` | Ctrl+Tab / Ctrl+Shift+Tab | `Workbench.CycleMruNext` / `CycleMruPrev` |
| Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z | undo, redo, redo | Ctrl+A | `Editor.SelectAll` |
| Ctrl+C, Ctrl+X, Ctrl+V | copy, cut, paste | Ctrl+D | `Editor.AddCaretAtNextOccurrence` |
| Ctrl+Alt+Up / Down | `Editor.AddCaretAbove` / `AddCaretBelow` | Escape | `Editor.CollapseCarets` |
| Ctrl+/, Ctrl+Shift+/ | `Editor.ToggleLineComment`, `ToggleBlockComment` | Shift+Tab | `Editor.Outdent` |
| Ctrl+Shift+D | `Editor.Duplicate` | Alt+Up / Alt+Down | `Editor.MoveLinesUp` / `MoveLinesDown` |
| Ctrl+J | `Editor.JoinLines` | Ctrl+Shift+K | `Editor.DeleteLines` |
| Ctrl+G | `Editor.GotoLine` | Ctrl+F / Ctrl+H | `Editor.Find` / `Editor.Replace` |
| F3 / Shift+F3 | `Editor.FindNext` / `FindPrev` | Ctrl+F2, F2, Shift+F2, Ctrl+Shift+F2 | bookmark toggle, next, previous, list |
| Ctrl+Shift+P | `App.CommandPalette` | Ctrl+F4 | `Workbench.CloseTab` |

The arrow, Home / End, Page, Delete, Backspace, Enter and Tab keys and their Shift / Ctrl word and document variants are
bound to the `Editor.Caret*`, `Editor.Select*` and editing commands in the `editor` context.

Commands **without** a default key: `Help.ShowCompileCommand`, `Clangd.Restart`, `Debug.EditCondition`,
`Debug.OpenInVisualStudio`, `Build.SelectConfiguration`, `Editor.Indent`, `Editor.LineEndingLF`, `Editor.LineEndingCRLF`,
and all `Vcs.*` (`NextChange PrevChange ToggleInlineDiff RevertHunk RevertFile DiffFileExternal RefreshStatus CheckOut`).
They are reachable from the [command palette](#command-palette) (Ctrl+Shift+P); there is no menu yet. A command whose
user interface is missing says `<name> is not available yet` in the status bar.

### Command palette

**Ctrl+Shift+P** (`App.CommandPalette`; a provisional key like all defaults, OPEN_QUESTIONS #1) opens a list of every
command: the human name, the command id (dim) and its current key binding on the right (the first one when there are
several; a command with no key shows none). Type to filter: the query is matched fuzzily against the name, else the id
(`restart clangd`, `vcsrevertf`), tighter and earlier matches first. Enter closes the palette and runs the command as its
key would; Escape closes it. A command that cannot run right now (no file open, ...) is greyed out and Enter says so in the
status bar. Not listed: commands that need an argument to do anything (`Workbench.FocusPanel`, which is bound to Alt+1..4)
and the palette itself. `Build.Build` is listed and runs the detected build; a custom build command still needs a key
binding with an argument.

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
* *`'vector' file not found` or other standard-library and Windows SDK headers missing:* clangd gets the MSVC headers
  from `cl.exe`. The report's `MSVC toolchain:` line shows which `cl.exe`, or `cl.exe not found`. Start tenfold from a
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
* GUI for code actions / fix-its, rename, formatting, call and type hierarchy, inlay hints, folding, the document outline.
* An "open log" command.
* Scripting (the Python API of 10x is deliberately not implemented, OPEN_QUESTIONS #11), `.10x` workspace import (not
  wanted, #12), Unreal Engine awareness.
* 10x's key bindings (OPEN_QUESTIONS #1).

## Provisional and unverified

* **Behaviours copied from memory of 10x** and waiting for your confirmation: undo grouping (#3), the tab / panel
  behaviours (#5), the symbol-search preview (#6), the `vs-dark` colours marked `[?]` (#19), every default of the
  editing conveniences (#8), the column guide, and whether trimming whitespace on save should be on.
* **Key bindings:** all placeholders until 10x's map is available (#1).
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
