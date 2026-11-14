# 10x feature parity

The brief is: fix two problems, keep everything else exactly as 10x does
it. 10x is closed source, so "exactly" has to be re-derived from observed
behaviour. This file is the working inventory. Items marked **verify** need
checking against a running copy of 10x before they are implemented, so we
copy what it actually does rather than what we remember.

Legend: ✅ done, and reachable in the GUI (`tenfold-ui`) where it is a user feature · 🔶 engine support exists, no GUI
yet · ⬜ not started · — excluded by the user

## The two fixes

| Feature | Status | Notes |
|---|---|---|
| Navigate Backward / Forward | ✅ | `NavigationHistory`; VS-style semantics; edit tracking. Keys: 10x's `Control -` / `Control Shift -` (`PrevLocation` / `NextLocation`) and the thumb buttons `MouseBackButton` / `MouseForwardButton`. UI (B6): toolbar Back/Forward buttons with key-aware tooltips, history dropdown (long-press / right-click), optional "← 3 \| 1 →" status counts (`nav.statusCounts`), Go to Last Edit Location (Control Shift Backspace, 10x's `PrevEditLocation`). |
| Navigation that cannot miss | ✅ | clangd authoritative, fast index approximate, budgeted delivery, cold-start augmentation. The fast index answers scope-aware (W11-FAST-SCOPE: the caret's namespace / class chain, `static` and anonymous-namespace entities, the declared type of a receiver, argument count, include graph, locals), and follows `#if` / `#ifdef` / `#elif` branches by the compile command's defines (W12-FAST-PP), so its first jump equals clangd's in about five of six `def` / `decl` lines of the corpora (`docs/STRESS_RESULTS.md`). |
| Still instant | ✅ | fast index at startup, persisted to `.tenfold/fastindex.bin` so warm starts only re-lex changed files (A4); clangd background index persisted; approximate results within the budget. |

## Workspace and files

| Feature | Status | Notes |
|---|---|---|
| Open a folder as a workspace | ✅ | `Workspace::scan`, ignore rules, extension filters. |
| Open a Visual Studio `.sln` / `.vcxproj` | ✅ | C5: projects enumerated; compile flags evaluated like MSBuild passes them to `cl.exe` per file and configuration (defines/undefines, implicit `_WIN32`/`_DEBUG`/`_DLL`/`_MT`/`UNICODE`/`_MBCS`, include / external include dirs, forced includes, pch, standards, `/permissive-`, `/EH`, `/GR-`, runtime library, `CompileAs`, per-file metadata, `ExcludedFromBuild`, property sheets, `Directory.Build.props/targets`). Solution -> project configuration mapping (`ProjectConfigurationPlatforms`); unbuilt projects skipped. Default Debug\|x64, else the first solution configuration (OPEN_QUESTIONS #2). `Build.SelectConfiguration` picker and a status-bar segment (click to switch) change the active configuration. |
| Switch solution configuration / platform | ✅ | `Editor::selectVsConfiguration` / `vsConfigurations`, `tenfold-cli vsconfig`; regenerates compile commands and restarts clangd. GUI: the `Build.SelectConfiguration` picker and the status-bar configuration segment. |
| CMake projects | ✅ | Configured into `.tenfold/cmake-build` for compile commands. |
| Makefile / bare folders | ✅ | Heuristic include paths. |
| Per-workspace settings file | ✅ | `.tenfold/workspace.json`. No `.10x` import (OPEN_QUESTIONS #12); compiler settings come from the Visual Studio solution (C5). |
| Fast "open file" fuzzy search | ✅ | B5a picker (Control O `OpenFile`, Shift Alt O `FindFile`): `FastIndex::fuzzyScore` on filename (weight 4) and path, MRU files first (persisted in `layout.json`), 2-4 ms per keystroke on 8.7k Godot paths. Opening is a tab switch, not a navigation jump. |
| Find in workspace (text search) | ✅ | `TextSearch` + `Editor::findInWorkspace` (A6): parallel, streaming, cancellable; literal fast path, PCRE2 regex, globs, open-buffer override; `gotoSearchMatch` records history. CLI `grep`. UI (B5b): Ctrl+Shift+F (10x's `Search`) modal list grouped by file, live streaming, case / word / regex toggles, include / exclude globs, F8 / Shift+F8, scrollbar markers. W8e: `ReplaceInFiles` (Ctrl+Shift+H) is a modal checklist of the matches with a replace field and the replacement shown in each row; open documents are edited in place (one undo step each), closed files are written after one confirmation. `NewSearchPanel` (Ctrl+Shift+A) keeps the current result list (the last three) and starts an empty one, Alt+PageUp / Alt+PageDown bring a kept list back. Gaps against 10x's search box: see "Find in files options not supported". |
| File watching / external changes | ✅ | `FileWatcher` (A3): inotify on Linux (polling fallback past the watch limit), `ReadDirectoryChangesW` on Windows (both tested in CI, including renames and churn under load); live fast-index, history and file-list updates; `Editor::onExternalChange` / `reloadFromDisk` for open documents. UI (P2, VS-TABS T9): as Visual Studio, a changed file asks "The file X has been modified outside of the source editor. Do you want to reload it?" (Yes / Yes to All / No / No to All; a dirty one always asks); `editor.detectExternalChanges`, `editor.reloadUnlessUnsaved` (silent reload of clean files), `editor.reloadExternalChanges` kept as an override; a deleted file keeps its buffer and marks its tab and the status bar. |
| Unsaved changes | ✅ | P2: closing the last view of a modified tab, closing the window and `App.Quit` ask Save / Don't Save / Cancel; several modified documents (quit, Close All, Close All But This / Pinned, Close Panel) get Visual Studio's one list "Save changes to the following items?" with Yes / No / Cancel (VS-TABS T6); a failed or encoding-refused save keeps the app open and shows the error; `editor.confirmUnsaved`. Follows Visual Studio (OPEN_QUESTIONS #5). No autosave. |

## Code intelligence

| Feature | Status | Notes |
|---|---|---|
| Go to definition / declaration | ✅ | VS-NAV: on an `#include` line F12 / Ctrl+F12 / the 10x middle-click open the file (Visual Studio's Go To Document): the includer's directory, then the compile command's `-iquote` / `-I` / `-isystem` directories; exact and instant. |
| Go to type definition / implementation | 🔶 | Engine only (`Editor::gotoTypeDefinition`, `findImplementations`, used by the corpus runner). There is no GUI command and no CLI command for either; in C++ Visual Studio has no Go To Implementation either (VS_BEHAVIOUR N8). |
| Find references | ✅ | VS-NAV: the list is grouped (Definition, then one heading per file), with Line:Col, the source line and Read / Write / Definition / Other (from the line's text). Not done: Group by menu, scope list, filter, Keep Results, incremental results. |
| Symbol search (workspace, fuzzy, qualified `a::b`) | ✅ | Workspace symbols rank before system headers. GUI picker (B5a, Ctrl+,): approximate results greyed with "verifying...", swapped for the authoritative ones keeping the selection, target-line preview (`picker.preview`, **verify** with 10x; VS-NAV: a "Preview: on/off" switch in the header, Alt+P), Enter records a navigation jump. The fast tier runs on its own thread (A10): 1-12 ms in-process on Godot under load, and `findSymbols` returns at once (BENCHMARKS.md, A10). |
| Document outline | 🔶 | `Editor::documentSymbols` (fast index first, clangd's list replaces it) feeds the `FindFunction` picker (Alt M). There is no outline panel. |
| Autocomplete | ✅ | clangd, detailed style; approximate fallback (greyed). GUI (B5b): popup at the caret, `.` `->` `::` and 2+ identifier characters, client-side fuzzy filter, docs pane, Tab / Enter accept as one undo step. |
| Signature help | ✅ | GUI (B5b): on `(` / `,`, active parameter highlighted, follows commas locally, Escape / `)` closes. |
| Hover / quick info | ✅ | GUI (B5b): 400 ms dwell, diagnostics first, approximate text greyed. |
| Rename | ✅ | clangd only, no approximate tier (an approximate rename would be wrong). GUI (W8c, Control R, Enter): inline box pre-filled with the symbol; open documents are edited in place (one undo step per file), closed files are edited and written to disk. |
| Diagnostics (squiggles) | ✅ | VS-NAV: none in files that are in no translation unit (Visual Studio's Miscellaneous Files; `diagnostics.miscFiles`, default off). Pushed per file; `Editor::diagnostics(file)`; squiggles + scrollbar markers in TextView (B2b); list of open-file and build diagnostics (B5b, `View.ShowDiagnostics`, no default key). |
| Switch header/source | ✅ | clangd extension, fallback by stem. |
| Semantic highlighting | ✅ | Lexical colours instantly (cached per line, background exact pass), clangd semantic tokens override identifiers only — no flicker (A9 + B2b). Palette: the Visual Studio 2022 Dark theme (OPEN_QUESTIONS #7). |
| Call hierarchy / type hierarchy | ✅ | W11-GUI-LSP. `View.CallHierarchy` (**Control K, Control T**, Visual Studio's View Call Hierarchy; also in the editor context menu): the Hierarchy panel of the bottom dock with the symbol, "Calls To 'x'" and "Calls From 'x'", lazily expanded (every caller / callee has both nodes again, as in Visual Studio), `name path:line` per entry, Enter / double click jumps through `Editor::gotoHierarchyItem` (navigation history, Back returns). `View.TypeHierarchy` (command palette and context menu, no default key: Visual Studio has no C++ type hierarchy window, so this is a tenfold addition) shows Base Types / Derived Types in the same panel. Not done: Visual Studio's Overrides / Implements nodes and its details pane of call sites. Misc-files rule: no answers outside a translation unit. |
| Include fix-its / code actions | ✅ | W11-GUI-LSP. **Control .** (`Editor.QuickActions`, Visual Studio's Quick Actions and Refactorings, also the first editor context menu item) lists clangd's code actions at the caret (quick fixes for the diagnostics on the line, tweaks) in a popup; Enter applies one as one undo step per document (`Editor::applyCodeAction`). A **light bulb** in the margin marks a line with a quick fix (red "error light bulb" for an error, yellow otherwise; click opens the list). Not done: Visual Studio's screwdriver for refactorings alone, Alt+Enter (10x's `ToggleFullscreen`), the preview of the change. **Inlay hints** (`editor.inlayHints`: `off` / `on` / `while-pressed`, default `while-pressed`, Alt+F1 held shows them like Visual Studio): parameter names and deduced types as dim virtual text (`InlayMap`, theme keys `inlayHint`, `inlayHintBackground`). The engine also has folding ranges, document highlights and clangd formatting; GUI: formatting is `Editor.FormatSelection` (10x's `FormatSelection`, also in the context menu); document highlights have no GUI (folding in the GUI is tenfold's own lexical outline, not clangd's folding ranges). **Known gap (Windows/MSVC STL):** clangd offers no "add #include" fix for `std::` names there (works on libstdc++/libc++); the test skips on `_WIN32` only when clangd returns no action, other code actions (e.g. typo fix-its) are tested on all platforms. Revisit with a newer clangd. |

## Editing

| Feature | Status | Notes |
|---|---|---|
| UTF-8 text, CRLF preserved | ✅ | W10: every line keeps its own ending (LF / CRLF / CR); mixed files are saved as they are; Visual Studio's load check, status-bar conversion and Save with Line Endings |
| Undo / redo | ✅ | Per document, linear; nestable undo groups restore caret sets; typed-word grouping (A5, setting). **verify** 10x's grouping of typed characters (OPEN_QUESTIONS #3). |
| Multi-caret / column select | ✅ | Engine (A5) + TextView (B2a): click/drag/double/triple/Alt-drag column select, Ctrl/Alt-click add caret, multi-caret clipboard; keystroke-to-paint 0.36 ms with 1000 carets on 100k lines. |
| Auto-indent, auto-close brackets and quotes | ✅ | B8 / B8b: Enter copies / extends the indentation (brace and operator continuation), `}` dedents, closing and wrapping are split like 10x's settings (`editor.autoAddCloseBrace`, `editor.autoAddCloseBracket`, `editor.bracketSelectedText`, tenfold-only `editor.autoCloseQuotes`, off; VS C++ pairs, unverified, VS-EDIT Z1) with 10x's defaults for the rest (braces and brackets do not pair, selections are bracketed, tab indent, no detection). Formatting, case alignment, re-indent on paste are not done (see "10x settings not yet supported"). |
| Bracket matching | ✅ | B2b: box behind the matching `(){}[]` of the caret. |
| Comment toggle, duplicate / move / join / delete line, indent / outdent | ✅ | B8, W8a: 10x's keys (`Commentline` Ctrl+K Ctrl+C, `ToggleComment` Ctrl+Shift+/, `DuplicateLine` Ctrl+E Ctrl+V, `MoveLineUp` / `MoveLineDown` Ctrl+Shift+Up / Down, `DeleteLine` Ctrl+Shift+L, `IndentLine` Ctrl+]) plus tenfold's Ctrl+/ (line comment), Ctrl+J (join lines), Ctrl+Shift+K (delete line), Tab / Shift+Tab; multi-caret, one undo step each. |
| Go to line | ✅ | B8: Ctrl+G, `line[:column]`, recorded in the navigation history. |
| Bookmarks | ✅ | B8, W8d: toggle (Ctrl+F2) / next / previous (F2, Shift+F2) / in file / list (`Editor.ListBookmarks`) / remove from file, follow edits, `.tenfold/bookmarks.json`, gutter flag. **verify** 10x's behaviour (OPEN_QUESTIONS #8). |
| Find / replace in file | ✅ | B8: Ctrl+F / Ctrl+H bar, incremental highlight of all matches, case / word / regex, F3 / Shift+F3, replace one / all (one undo step). Regex group references in the replacement are not expanded. |
| Trim trailing whitespace on save, line ending switch | ✅ | B8: `editor.trimTrailingWhitespace` (default off; 10x documents no such setting), `Editor.LineEndingLF/CRLF/CR`. Encodings: UTF-8 (with or without signature), UTF-16 LE/BE (BOM) and Windows-1252, shown in the status bar, with Reopen / Save with Encoding (VS-ENC). |
| Word wrap | ✅ | B2c: soft wrap on by default (`editor.wordWrap`; smooth scrolling off by default), at word boundaries, continuation rows indented, line number on the first row; Up / Down / Home / End by visual row; `View.ToggleWordWrap` (Alt+Z, the Visual Studio binding). No hard wrap / reflow and no wrap at a fixed column (a possible later setting). |
| Large files | ✅ | Line vector; tested approach, piece table as upgrade path. |

## Shell

| Feature | Status | Notes |
|---|---|---|
| GPU-rendered text, tabs, split panels | ✅ | `-DTENFOLD_BUILD_UI=ON`: SDL3 window/input, batched GL 3.3 renderer (Direct3D 11 on Windows, see below), FreeType glyph atlas + monospace layout, headless PNG screenshots with golden tests. Retained widget layer (`src/ui/widgets`): TabStrip (pinned, MRU, drag reorder, preview tab), Splitter, Panel, StatusBar, TextView with gutter, selections, multi-caret, soft wrap and folding; the Workbench persists tabs and panel layout in `<workspace>/.tenfold/layout.json`. Frame-time budgets are in BENCHMARKS.md. |
| Key bindings as a text file | ✅ | 10x's `.10x_settings` format (`Control Shift F: Search`, `Control S, Control S: SaveAll`), 10x command names with tenfold ids as aliases, 10x's complete default key map (B4b), an optional import of an installed 10x's `KeyMappings.10x_settings` (`keys.import10x`), layering with `<user dir>/KeyMappings.10x_settings` and `<workspace>/.tenfold/KeyMappings.10x_settings`, diagnostics in the status bar. Python statements as commands are reported as unsupported. All 183 default bindings run a tenfold command (W12-CMD); only 10x commands without a default key stay open (section below). |
| Colour schemes as text files | ✅ | Theme files with lexical + semantic token colours and fallbacks, hot reload (B7). Default is `vs-dark`, a Visual Studio 2022 Dark look (B7b, W10-THEME: values from Microsoft's colour reference, a control-flow keyword colour; the few values without a source are marked `[?]` in the theme header; no string escape-sequence colour). |
| Settings as text file | ✅ | Mechanism done (B7): layered `settings.txt` with typed keys, diagnostics, hot reload. **verify** 10x's `Settings.10x_settings` keys for an importer. |
| Local diagnostic log (no telemetry) | ✅ | D3: `.tenfold/log.txt` (user data dir without a workspace), 5 MB x 3 files, `TENFOLD_LOG=debug`, `tenfold-cli log <root> [--tail N]`, `Editor::logFilePath()`. A log viewer / "Open log" menu entry is not built yet. See ARCHITECTURE "Local log file". |
| Compile-command diagnostics | ✅ | D4: `Editor::compileCommandInfo(file)` (source of the compile database, active Visual Studio configuration, the file's exact argv or the fallback flags, whether clangd has the file open and parsed), `tenfold-cli why <root> <file>`, and the `Help.ShowCompileCommand` command (read-only popup; no default key). See ARCHITECTURE "Compile-command diagnostics". |
| clangd visibility, command palette | ✅ | P2: the status bar shows the engine's clangd text ("clangd restarting (2)...", "clangd stopped: crashed 5 times") and `flags: heuristic` (click: `Help.ShowCompileCommand`); `Clangd.Restart`; `App.CommandPalette` (Ctrl+Shift+X, 10x's `ShowCommandPanel`) lists every runnable command with its key and filters fuzzily, which also reaches the commands that have no key (`Vcs.*`, `Build.SelectConfiguration`, ...). |
| User guide | ✅ | D4: `docs/USER_GUIDE.md`; its settings table is checked against `assets/settings.default.txt` by a test (`user_guide_settings_table_matches_the_default_settings_file`). |
| Build integration (run command, parse errors, jump) | ✅ | W8e: `RebuildActiveWorkspace` (Control Alt F7: `msbuild /t:Rebuild`, `cmake --build --clean-first`, `make clean && make`, or the `rebuildCommand` workspace setting) and `BuildCurrentFile` (Control F7: `msbuild <project> /t:ClCompile /p:SelectedFiles=<file>`, `ninja -C <dir> <object>`, or the file's compile database command) run in the same panel with the same error navigation. Engine (C1): `Editor::build/cancelBuild/nextBuildMessage/prevBuildMessage/buildDiagnostics`, `tenfold-cli build`. UI (C1-UI): resizable bottom dock with a streaming Build panel (clickable, severity-coloured lines, Build/Cancel, status), Ctrl+Shift+B / Ctrl+Break / F4 / Shift+F4, build squiggles + scrollbar marks + hover, status bar segment. All jumps go through `gotoLocation`. See ARCHITECTURE "Build panel and bottom dock". |
| Debugger integration | ✅ | Visual Studio debugger through EnvDTE (OPEN_QUESTIONS #9). Engine (C2a): breakpoints that follow edits (`.tenfold/breakpoints.json`), `DebugSession` worker with two-way breakpoint sync and follow-current-statement. Backend (C2b): late-bound COM EnvDTE, finds the VS instance with the solution via the ROT or launches devenv, run/step/break/run-to-cursor, breakpoint sync, current statement; verified against Visual Studio 2022 in the blocking `windows-vs-debugger` CI job (launch, breakpoint hit, step, VS-side breakpoint, stop). UI (C2c): breakpoint glyph lane (click toggles; disabled / conditional shapes), current-statement arrow and line band, `Debug.*` commands on Visual Studio keys (F5, F9, F10, F11, …), breakpoint list (Ctrl+Alt+B, `ListBreakpoints`), debugger status segment and orange status bar while debugging. `Debug.OpenInVisualStudio` (10x's `OpenCurrentFileInDebugger`, Alt X) opens the current file at the caret in Visual Studio. The backend exists on Windows only (elsewhere: "no debugger backend on this platform"). |
| Version control (Perforce / Git) | ✅ | OPEN_QUESTIONS #10. Engine and Git (C3a): provider interface, `git` status / base text / revert, Myers line diff, debounced off-thread hunks, revert file / hunk. Perforce (C3b, tested against a fake `p4` only): auto-checkout on first edit (`p4 edit`), fstat status, `print #have` base, revert, `vcs.provider` workspace setting, clear read-only save error. UI (C3c): change strip between line numbers and text and scrollbar change marks, inline diff block under a hunk (revert hunk, close, Escape), `Vcs.NextChange` / `PrevChange` / `ToggleInlineDiff` / `RevertHunk` / `RevertFile` (confirmation) / `DiffFileExternal` (p4merge or `vcs.diffTool`) / `RefreshStatus` / `CheckOut`, tab markers, status-bar segment ("Git: modified", "P4: checked out"), errors in the status bar, refresh on window focus. Keys: 10x's `Control P, Control E` / `Control P, Control D` / `Control P, Control R` (check out, external diff, revert file); the change commands have no key. VS-VCS (VS_BEHAVIOUR section 4): P4VS badges on explorer rows and tabs (checked out, opened for add, not in the depot, newer revision, needs resolve), `perforce.promptOnEdit` (ask "Check out?" on the first edit, off by default), a PerforceHistory action menu (Open, Diff against previous, Diff against have). Not done: verification against a real Perforce server, blame, commit / stage UI, side-by-side diff. |
| Python scripting API | — | Excluded by the user (OPEN_QUESTIONS #11). |
| Unreal Engine awareness | ⬜ | `.uproject` → generate compile commands; UE macros are handled by clangd automatically. |

## Windows

| Item | Status | Notes |
|---|---|---|
| Process spawning | ✅ | `Process_win32.cpp` (A1): CreateProcessW, inherited pipes, kill-on-close job object. Verified via mingw + Wine and native MSVC 2022 / clang-cl CI (blocking). |
| MSVC headers for clangd | ✅ | `cl.exe` discovery (VCToolsInstallDir / vswhere / `msvcToolchain` setting) and `--query-driver` (A2). D1: the Windows package ships `clangd.exe` in `bin/` with its `lib/clang` headers and the engine prefers it (`clangdPath` / `--clangd` override). |
| Direct3D 11 renderer | ✅ | `D3d11Renderer` + `WindowsSwapChain` (B9): flip-model swap chain with frame-latency waitable object, hardware then WARP, runtime-compiled HLSL; default backend of `tenfold-ui` on Windows (`--renderer=gl` for OpenGL). Verified natively in the CI job `windows-ui-d3d11` on WARP: it runs the whole UI suite and the `d3d11` renderer tests (the `b1_scene` golden had a max channel diff of 2 when it was added). The text golden is skipped there (no DejaVuSansMono on the runner). Device-loss recovery is a follow-up. |
| SDL3 window / input on Windows | ✅ | Same `Window` / `translateSdlEvent` code as Linux (SDL3 Win32 backend); window created without `SDL_WINDOW_OPENGL` for D3D11. Built and driven by the UI tests in native Windows CI; interactive input not yet exercised on a real desktop. |
| Paths / URIs | ✅ | `pathToUri`/`uriToPath` handle drive letters and UNC; case/separator-insensitive `pathKey`/`pathsEqual` on Windows (A2). |

## 10x commands not yet implemented

Source: the 10x documentation (Commands, KeyMappings), the only reference for 10x commands (OPEN_QUESTIONS #1). Every
binding of 10x's default key map is in `assets/KeyMappings.default.10x_settings`; all 183 run a tenfold command
(W12-CMD; the table in the user guide, "Key bindings"). A binding of a command tenfold lacks stays bound, so a user's
file matches 10x; pressing its keys shows `not implemented yet: <Command>` in the status bar. Descriptions are 10x's one-liners. The
mapping from 10x names to tenfold commands is `src/ui/commands/CommandNames.cpp` (every one of 10x's 307 commands is in it,
those without a tenfold command have an empty target).

Where a mapped command is only close to the 10x one, the alias table carries a note (shown in the user guide): the
move-line keys also move every line of a selection, `DuplicateLine` duplicates a selection instead of the line,
`ToggleComment` follows `editor.commentBlockUsesLineComments` for a selection (default: line comments), `OpenFile` and `FindFile` both open
tenfold's fuzzy file picker, `ShowSwitchPanelPanel` is the recent-documents switcher, `Search` / `FindInFiles` are one
find-in-files picker (`NewSearchPanel` keeps up to three earlier result lists in it; there are no separate result panels, so
the `ShowFindInFilesResults1-4` commands stay open), `ListBreakpoints` opens the breakpoint picker instead of writing to the output panel.


**Parser** (no default-bound command is left; W12-CMD)

`InsertPrevAutocomplete` (`Shift F14`) and `InsertNextAutocomplete` (`F14`) were left open in W8c because the key map's
one-liner ("Insert prev/next autocomplete") says nothing. The 10x changelog (https://10xeditor.com/versions.htm) does:
version 1.0.182 (03/07/2024) "added InsertPrevAutocomplete and InsertNextAutocomplete commands to insert the next
autocomplete without the dropdown (F14, shift+F14)"; later "InsertNextAutocomplete now submits autocomplete dropdown if
it's visible" and "fixed InsertNextAutocomplete getting into weird state if used with Autocomplete enabled". tenfold:
with no list visible, F14 / Shift+F14 replace the word before the caret in place with the next / previous candidate of the
list the dropdown would show (same source and order: clangd, else the fast tier), asked for asynchronously, without
showing the dropdown and without the as-you-type list popping up afterwards. Further presses cycle (wrapping at both
ends), each replacing the previous insertion; the whole cycle is one undo step (the typed prefix comes back). Typing, a
caret move or any edit ends the cycle. With the list visible F14 accepts the selected item (as Tab / Enter);
Shift+F14 then does nothing. No candidates: the status bar says "No completions here". Only a single empty caret.
**Unverified (the changelog is silent):** Shift+F14 with the list visible, wrap versus clamp at the ends, the undo
grouping, multiple carets, and whether 10x's list for these commands is the dropdown's ranked list (assumed).

`AutocompleteUp` / `Down` / `PageUp` / `PageDown` (W12-CMD) are real commands (`Editor.CompletionUp` ...). They share
their keys with `MoveCursorUp` / `Down` / `PageUp` / `PageDown`: while the completion list is open in the focused
editor they move its selection (a page is 10 rows, clamped at both ends); with no list open the same key moves the caret.
Shift variants are separate chords (`SelectUp` ...). Remapping `AutocompleteDown` to another key makes that key move the list.


### Commands without a default key that tenfold does not implement either

118 of the 307 10x commands (the rows of `CommandNames.cpp` with an empty target; the other 189 rows map to 186 tenfold
commands, `Search` / `FindInFiles`, `PerforceRevert` / `GitRestore` and `FindFile` / `OpenFile` pairs sharing one). None
of the 118 has an entry in the default key map; they resolve to `not implemented yet:` when bound in a key file.
Thirteen implemented 10x commands have no default key either: `RemoveAllBreakpoints`, `CopyActiveFilePath`,
`ShowInWorkspaceTreeWithToggle`, `ClaudeCodeTerminal`, `CursorTerminal`, `CollapseAllRegions`, `ExpandAllRegions`,
`CollapseAllRegionsRecursive`, `ExpandAllRegionsRecursive`, `CloseAllOtherTabs`, `CloseAllButPinnedTabs`, `PinTab`,
`TogglePinTab` (tenfold's own commands without a key are listed in the user guide, "Key bindings").

- **File**: `SaveFileAs`, `CloseAll`, `ToggleReadOnly`
- **Workspace**: `CreateWorkspace`, `OpenWorkspace`, `SaveWorkspace`, `CloseWorkspace`, `OpenSolution`, `OpenFolder`, `WorkspaceSettings`, `ActiveWorkspaceSettings`, `LogWorkspaceSettings`, `SyncWorkspace`
- **General**: `CreateSecondaryWindow`, `Show10XOutput`, `CloseOutputPanel`, `CloseFindFilePanel`, `CloseCommandPanel`, `ToggleOutputPanel`, `Settings`, `LocalSettings`, `EditKeyBindings`, `EditCurrentColourScheme`, `Colours`, `ToggleAccessibility`, `ToggleTextToSpeech`, `ImportKeyBindingsFromVS`
- **Edit**: `ToggleLineEndings`, `ToggleWordWrapForCurrentPanel`, `ClearUpdatedLines`, `RefreshUpdatedLines`
- **Formatting**: `ToggleIndentMode`, `FormatFile`
- **MultiCursor**: `StartDraggingRectSelect`
- **Navigation**: `NextEditLocation`
- **Bookmarks**: `RemoveAllBookmarks`
- **FindInFiles**: `ShowFindInFilesResults1`, `ShowFindInFilesResults2`, `ShowFindInFilesResults3`, `ShowFindInFilesResults4`, `ToggleFindInFilesResultsPanel1`, `ToggleFindInFilesResultsPanel2`, `ToggleFindInFilesResultsPanel3`, `ToggleFindInFilesResultsPanel4`
- **Tabs**: `CloseAllTabs`, `CloseTabsLeft`, `CloseTabsRight`, `CloseUnmodifiedTabs`, `ToggleTabs`
- **PanelGrid**: `SetColumnCountAuto`, `SetColumnCount1`, `SetColumnCount2`, `SetColumnCount3`, `SetColumnCount4`, `SetColumnCount5`, `SetColumnCount6`, `SetColumnCount7`, `SetColumnCount8`, `SetRowCountAuto`, `SetRowCount1`, `SetRowCount2`, `SetRowCount3`, `SetRowCount4`, `SetRowCount5`, `SetRowCount6`, `SetRowCount7`, `SetRowCount8`
- **Build**: `BuildAll`, `RebuildAll`, `CleanAll`, `CleanActiveWorkspace`, `ShowBuildOutput`, `CloseBuildPanel`, `GotoPrevBuildError`, `GotoNextBuildError`, `GotoPrevBuildWarning`, `GotoNextBuildWarning`
- **Debugging**: `VSDebuggerReconnect`, `PrevBreakpoint`, `NextBreakpoint`, `PrevBreakpointInFile`, `NextBreakpointInFile`, `CommandLineArgSelector`, `ToggleCommandLineArgSelector`, `OpenDebugPanels`, `OpenCallstackPanel`, `OpenWatchPanel`, `OpenAttachDebuggerPanel`
- **Parser**: `ReparseFile`, `ReparseAll`, `CancelAutocomplete`, `ToggleParserVerboseLogging`, `CppParser.LogParseStatus`, `CppParser.ListWorkspaceFiles`, `CppParser.FindBrokenDef`, `CppParser.DumpStringTable`, `CppParser.WriteFileStatsCsv`, `CppParser.LogIncludePaths`
- **Perforce**: `PerforceAdd`, `PerforceAnnotate`, `PerforceTimelapse`, `PerforceRevisionGraph`, `PerforceRevertIfUnchanged`
- **SVN**: `SVNDiff`, `SVNAdd`, `SVNUpdateFile`, `SVNUpdateWorkspace`
- **Git**: `GitDiff`, `GitAdd`, `GitBlame`
- **Misc**: `ListCommands`, `Update`, `ToggleFPS`, `ListWorkspaceFiles`, `ListFileStatusMonitorFiles`, `LogMemory`, `LogMemoryStats`, `CheckParserIntegrity`

## Find in files options not supported

Source: 10x's WorkspaceSearch documentation. tenfold's find-in-files is a text search of the workspace files (one query
line plus Alt+C / Alt+W / Alt+R toggles and ` :: globs`); 10x's search box is a combined workspace search. What tenfold
lacks (the replace picker takes the same query line, so it lacks the same things):

| 10x option | Status |
| --- | --- |
| `*` wildcard in the query, implicit `*` at both ends, space as a wildcard in file searches | missing (the query is a literal, or a regex with Alt+R) |
| `AND`, `OR`, `AND NOT` logical expressions | missing (use a regex with alternation) |
| `WORD` / `CASE` operators at the end of the query | missing; Alt+W / Alt+C toggles do the same |
| `IN path` / `NOT IN path` operators | missing; ` :: glob, !glob` does the same with glob syntax |
| `DEF` operator (find a class definition) | missing (use Go to Definition / Find Symbol) |
| Searching file names, workspaces and symbols in the same box | missing; Control O (files) and Control Shift O style pickers are separate |
| Opening files from disk by typing a path, Tab completion | missing |
| Search history (Alt+Down, Down in an empty box) | missing |
| Settings cog, `Search.Extensions`, `Search.ExcludeList`, `Search.GlobalDirectories`, `Search.MaxResults`, `Search.MaxFileSize`, `Search.PathMode` | missing (see the settings table); the picker searches the workspace's source and header files only |
| Ctrl+Backspace, Ctrl+Home / Ctrl+End (first / last match), Escape | supported |
| Alt+C / Alt+W | supported (plus Alt+R for regex) |

## 10x settings not yet supported

Source: 10x's own settings documentation (names, defaults and one-line meanings only). Python, MCP,
telemetry, accessibility, updater, crash-dump and Perforce-connection settings are left out. Effort: S = a setting
plus a few lines, M = a feature of a day or two, L = a subsystem. "Behaviour exists" means tenfold already does the
thing, always, with no switch. Where the note says "check", the 10x meaning or the tenfold behaviour is not certain
from the documentation.

**Settings file format.** 10x stores settings as `Name: Value` lines (a space after the colon is required) in
`Settings.10x_settings` (shared), `LocalSettings.10x_settings`, a `<workspace>.10x_settings` file next to the workspace
and a `.10x_user_settings` file for the user; a removed setting reads as the empty string. tenfold keeps its own
`key = value` `settings.txt` (dotted keys, three layers). Moving to the 10x format and names is a separate task (M).

**Mapped (defaults aligned in B8b).** tenfold key -> 10x setting (10x default):

| tenfold | 10x | Note |
|---|---|---|
| `editor.tabWidth` | TabWidth (4) | |
| `editor.indentUnit` | IndentMode (Tabs) | tenfold also takes a space count |
| `editor.detectIndent` | AutoDetectIndentMode (false) | |
| `editor.autoIndent` | AutoIndent (true) | |
| `editor.autoIndentBraces` | AutoIndentBraces (true) | `}` typed first on a line dedents |
| `editor.formatBracesOnEnterSplit` | FormatBracesOnEnterSplit (false) | |
| `editor.autoAddCloseBrace` | AutoAddCloseBrace (false) | `{` |
| `editor.autoAddCloseBracket` | AutoAddCloseBracket (false) | `(` and `[` |
| `editor.bracketSelectedText` | BracketSelectedText (true) | |
| `editor.showWhitespace` | ShowWhitespace (false) | |
| `editor.showFoldGlyphs` | ShowCollapseRegionMargin (true) | default **true** (10x's and Visual Studio's default): boxed +/- margin with guide lines; a collapsed region always shows its `{...}` marker |
| `editor.columnGuide` | VerticalRulers (empty) | 10x takes a list; tenfold one column |
| `editor.wheelLines` | MouseWheelScrollStep (3) | |
| `editor.caretBlink`, `editor.caretBlinkMs` | CursorBlinkRate (auto) | auto = the OS rate; 0 = off. tenfold uses 530 ms |
| `editor.confirmUnsaved` | RestoreUnsavedFiles (false) | with false 10x prompts to save |
| `editor.wordWrap` | WordWrap (false) | tenfold true by decision (OPEN_QUESTIONS #4) |
| `editor.smoothScroll` | SmoothScroll (true) | tenfold false by decision (#4) |
| `font.editor`, `font.fallbacks`, `font.size` | EditorFont, EditorFontSize (JetBrains Mono NL Medium, 12) | Visual Studio font by decision (#7): `font.editor` empty tries `font.fallbacks` (`Cascadia Mono, Consolas`), size 10. ZoomIn / ZoomOut (and Control + wheel, `editor.mouseWheelZoom`) add to the size of the editor text for the session only (VS-EDIT; tabs, status bar and menus keep their size, the status bar shows "N %") |
| `editor.defaultLineEnding` | DefaultLineEnding (CRLF) | line ending of `NewTextFile` (named `Untitled N`, or `TextFileN.txt` with `editor.textFileNames`, VS-EDIT Z14) |
| `editor.defaultEncoding` | DefaultEncoding (UTF-8) | encoding of new files: `utf-8`, `utf-8-bom`, `utf-16le`, `utf-16be`, `windows-1252` (VS-ENC); Shift_JIS and other code pages are not supported |
| `editor.mouseWheelZoom` | MouseWheelZoom (true) | Control + wheel zooms the editor text, like `ZoomIn` / `ZoomOut` |
| `editor.commentBlockUsesLineComments` | CommentBlockUsesLineComments (true) | `ToggleComment` with a selection: `//` per line (false: `/* */` around the selection) |
| `explorer.trackActiveFile` | TrackActiveFileInWorkspaceTree (false) | |
| `explorer.dragDrop` | EnableWorkspaceTreeDragDrop (true) | VS-TREE: dragging moves a file or folder after a confirmation (`explorer.promptBeforeMoving`), Ctrl copies |
| `terminal.command`, `terminal.claudeCommand`, `terminal.cursorCommand` | TerminalCommand (`cmd.exe`), ClaudeTerminalCommand (`claude`), CursorTerminalCommand (`cmd /c agent`) | tenfold's `terminal.command` is empty by default (PowerShell on Windows, `$SHELL` elsewhere, VS-TERM); off Windows `cmd.exe` means `$SHELL` and a leading `cmd /c` is dropped |
| `terminal.font`, `terminal.fontSize` | TerminalFont, TerminalFontSize | empty / 0 = the editor font and size |
| `debug.visualStudioSync` (`workspace.json`) | VisualStudioSync (true) | `false`: no breakpoint or configuration sync and no attach at startup |
| `editor.copyLineWhenEmpty`, `editor.copyBlankLines` | (none) | VS-EDIT Z4: Visual Studio's two options for Copy / Cut without a selection (both true) |
| `editor.useEditorConfig` | (none) | VS-EDIT Z19 (default off, unverified): save honours `.editorconfig` `trim_trailing_whitespace` / `insert_final_newline` |
| `editor.textFileNames` | (none) | VS-EDIT Z14 (default off, unverified): `NewTextFile` is named `TextFileN.txt` |
| `editor.autoNameUntitledFiles` | AutoNameUntitledFiles (false) | the first word typed names the tab and is the Save As suggestion |
| `editor.internetSearchCommand` | InternetSearchCommand | |
| `ui.theme` | ColorScheme (Sunset) | vs-dark by decision (#7) |
| `vcs.diffTool` | DiffExe (auto) | |
| `perforce.promptOnEdit` | (none) | VS-VCS VC5 (default off): P4VS asks "Check out?" before the first edit; 10x auto-checks out |

No 10x equivalent (tenfold-only): `editor.highlightCurrentLine`, `editor.trimTrailingWhitespace` (10x documents none), `editor.autoCloseQuotes`, `editor.copyLineWhenEmpty`, `editor.copyBlankLines`, `editor.useEditorConfig`, `editor.textFileNames`, `editor.inlayHints`, `editor.pasteHistorySize`, `editor.replicateNewLine`, `editor.pasteKeepsLineBreaks`, `editor.checkLineEndings`, `editor.autoDetectUtf8`, `editor.reloadExternalChanges`, `editor.detectExternalChanges`, `editor.reloadUnlessUnsaved`, `outline.enabled`, `outline.pragmaRegions`, `outline.statementBlocks`, `tabs.previewTab`, `tabs.previewFromFindResults`, `nav.statusCounts`, `keys.import10x`, `picker.preview`, `diagnostics.miscFiles`, `explorer.openWithSolution`, `explorer.promptBeforeMoving`, `vcs.diffToolArgs`, `vcs.p4vPath`, `terminal.scrollbackLines`, `terminal.closeOnExit`, `terminal.ctrlCCopiesSelection`; in `workspace.json`: `debug.attachAtStartup`, `rebuildCommand`, `undoGrouping`, `undoGroupPauseMs`.
`editor.autoCloseQuotes`, `editor.copyLineWhenEmpty`, `editor.copyBlankLines`, `editor.useEditorConfig`, `editor.textFileNames`, `editor.reloadExternalChanges`, `editor.detectExternalChanges`, `editor.reloadUnlessUnsaved`, `tabs.previewTab`, `tabs.previewFromFindResults`, `nav.statusCounts`, `picker.preview`, `diagnostics.miscFiles`, `vcs.diffToolArgs`.

### Editing

| Setting | 10x default | Description | Effort / note |
|---|---|---|---|
| CommentBlockIncrementLine | false | Comment command moves to the next line after commenting a single line | S. tenfold never moves (= 10x's default) |
| AutoAddCloseBraceAndFormat | false | Add the close brace and format the braces | M. Needs a formatter for the brace block |
| AutoFormat | true | Format the statement or block when typing `;` or `}` | L. Needs 10x's formatting rules (the Space* and Format* settings below) |
| OpenBraceOnNewLine | true | Move the open brace to the next line on auto-format | M. Part of AutoFormat |
| UpdateIndentsOnCloseBrace | true | Re-indent the current block when `}` is typed | M. tenfold only dedents the `}` line |
| UpdateIndentsOnPaste | true | Re-indent pasted lines | M. Does not exist |
| BracketAlignFunctionArgs | true | Align continuation lines to the function's open bracket | M. Listed as "not done" in the OPEN_QUESTIONS #8 detail |
| BracketAlignConditionals | false | Align conditional continuation lines to the open bracket | M. Same machinery |
| AlignToEquals | false | Align lines of an assignment to the `=` | M |
| IndentCaseStatements | true | Indent `case` labels inside a switch block | M. tenfold has no `case` rule |
| UseSpacesAfterCurrentIndent | false | With tab indent, align with tabs up to the indent and spaces after | S. Needs the alignment features above |
| ExtendEmptyLines, ExtendEmptyLinesUsingSpaces | false, false | Pad an empty new line with indentation | S. Check against tenfold, which writes the indent on Enter |
| BackspaceDeletesTabs | false | In Spaces mode Backspace deletes TabWidth spaces | S |
| SpaceBrackets, SpaceBraces, SpaceAfterConditionals | false, true, true | Formatting: space inside `()`, inside `{}`, after `if` | M each, with AutoFormat |
| FormatEquals, FormatCommas, FormatExcludeList | true, true, (empty) | Formatting: one space around `=`; none before and one after `,`; words not to format | M, with AutoFormat |
| AutoFixIndirection | false | Replace `.` with `->` (or back) from the variable's type | M. Needs clangd type info at the caret |
| AutoFormatBlockComments, AutoFormatBlockCommentsNewLines | false, false | `/**` adds `*/`; Enter continues with a leading `*` | S |
| MultiCursorPasteMode | Lines | Lines or Block paste of multi-cursor text into a single cursor | S. tenfold pastes per caret when the counts match |
| TextDrag | true | Drag selected text with the mouse | M |
| EndKeyUpdatesRectSelect | true | End extends a rectangle selection | S |
| MoveToNextWordMode, SelectNextWordMode | WordStart, WordStart | Ctrl+Left/Right goes to the word start or end (auto by direction) | S. tenfold has one fixed behaviour |
| CursorMode, OvertypeCursorMode, OvertypeEnabled | Line, auto, true | Caret shape; overtype mode on Insert | M. No overtype mode in tenfold |
| ColorBracesByDepth | false | Colour brackets by nesting depth | M |
| HighlightCurrentSymbol, HighlightCurrentWord, HighlightSelectedWord | true, false, true | Highlight other uses of the symbol, word or selection | M. Does not exist (symbol needs clangd `documentHighlight`) |
| ShowLineEndings | false | Draw line endings | S |
| ShowIndentGuideLines | true | Draw indent guides | M |
| AllowEditingOfReadOnlyFiles | true | Edit read-only files | S. Relates to Perforce auto-checkout (C3b) |

### Navigation, find and view

| Setting | 10x default | Description | Effort / note |
|---|---|---|---|
| FindTextInitialValue, ReplaceTextInitialValue | Current, Current | Seed the find / replace box with the current word, the selection or nothing | S. Check what tenfold seeds today |
| CloseFindPanelOnEnter | false | Enter in the find box closes it | S |
| ReverseFindSelection | false | Caret at the start of the found text | S |
| ShowFindTextMatchCount | false | Match count inside the find box | S |
| FindPanelCursorKeys | true | Left/Right move between the find panel's boxes and buttons | S |
| GotoDefScrollMode, GotoResultScrollMode | Center, Visible | How navigation scrolls the target into view | S. tenfold has one behaviour |
| GotoDefColumn | default | Which column a goto definition / build error opens in | M, with the column presets below |
| GotoDefResolvesTypedefs, GotoFunctionImplementation | false, true | Typedef resolution; a call goes to the implementation, not the declaration | S/M. Result order comes from clangd; check |
| FindSymbolFuzzyMatching | true | Fuzzy matching in Find Symbol | S. Behaviour exists (always on) |
| ShowLineNumbers, MinLineNumberWidth, RelativeLineNumbers | true, 3, false | Line number gutter | S each |
| ShowUpdatedLinesInMargin, ShowUpdatedLinesInScrollBar | true, true | Mark modified lines | S. Version-control marks exist (C3c) but track the VCS base, not unsaved edits |
| ScrollBarMode, MapScrollBarWidth | Map, 60 | Minimap-style scroll bar | L |
| HighlightFindTextInScrollBar, HighlightSelectedWordInScrollBar, ScrollBarHighlightSize | true, false, 1.0 | Marks in the scroll bar | S. Find marks and diagnostics exist; selection marks do not |
| ShowBreakpointsInScrollBar, ShowBookmarksInScrollBar | false, false | Breakpoint and bookmark marks in the scroll bar | S |
| SmoothScrollSpeed | 50 | Speed of smooth scrolling | S |
| VerticalMouseWheelSensitivity, HorizontalMouseWheelSensitivity | 1.0, 1.0 | Wheel scaling | S |
| ControlClickGotoDefinition | true | Ctrl+click goes to definition | S. Check the existing click behaviour |
| HideHoverBoxOnKeyPress, ShowSymbolHoverBoxOnMouseOver, EnableFunctionInfoHoverBox | true, true, true | Hover box and function-argument box behaviour | S. Hover and signature help exist without switches |
| HideMouseCursorWhileTyping | false | Hide the pointer while typing | S |
| VerticalLineGap | 1.0 | Extra line spacing | S |
| UseLigatures | true | Font ligatures | M. Needs shaping |
| UIScale, UIFont, UIFontSize | auto, Segoe UI, default | UI scale and font | M. tenfold scales with the display and has no separate UI font |
| ToolFont, OutputPanelFont, StatusBarFont (and their sizes) | default | Fonts per panel kind | S |
| FontGamma, FontSubPixelRenderingMode | 2.2, Horizontal | Text rendering | M. Greyscale atlas today |

### Files, tabs and layout

| Setting | 10x default | Description | Effort / note |
|---|---|---|---|
| SwitchFileMode | All | Ctrl+Tab cycles all files or only the current panel's | S. tenfold's switch panel (VS-TABS T1: MRU order, the previous file preselected, Ctrl release commits) lists the active panel's tabs, i.e. the "current panel" mode; "All" is not done |
| NewTabPosition | RightOfCurrent | Where a newly opened tab goes | S |
| MaxOpenTabs | 100 | Tab limit | S |
| TabPanelMode | Top | Tab strip on top, left or right | M |
| ReopenFileInCurrentPanel, OpenFileInActivePanel, RememberFilePanelPos | false, false, false | Which panel a file opens in | M |
| UseColumnPresets, Column0Presets, Column1Presets, Column2Presets, ColumnGap | true, sources, headers, `*.bat, *.py`, 0 | Open sources in column 0 and headers in column 1 automatically | M. A 10x signature layout; tenfold opens in the active panel |
| RestoreUnsavedFiles, RememberOpenFiles, OpenLastWorkspaceOnStartup | false, false, false | Restore unsaved buffers; reopen files and the workspace at startup | M |
| FileBackupFrequency | 5000 | Back up unsaved files every N ms | M. tenfold has no backup files |
| AutosaveWorkspace, SaveAllOnLoseFocus | false, false | Autosave | S |
| SaveAsUsesPrevPanelPath | true | Save As starts in the previous file's folder | S, with a Save As dialog |
| BackslashPaths | true | Show paths with backslashes | S |
| WindowTitle, WorkspaceTextInTitleBar, RememberWindowPosition | Name, Name, true | Window title and position | S each |
| HideMainMenu, ShowMainMenuOnMouseHover, ToggleMainMenuOnAlt (and the secondary-menu variants) | false, true, true | Menu bar visibility | Needs a menu bar |
| AutoCloseFloatingToolWindows, LocationHistoryIncludesToolPanels, CentreFloatingPanelsHorz/Vert | true, false, true | Floating tool windows | Skipped unless tool windows float |

### Workspace search

| Setting | 10x default | Description | Effort / note |
|---|---|---|---|
| Search.Extensions | `.c .cpp .h .inl ...` (long list) | File types the workspace search covers | S |
| Search.ExcludeList | `*/sqlite/*` | Wildcards to exclude | S. Find in files already takes `!glob` excludes per query |
| Search.GlobalDirectories | (empty) | Extra directories in the search set | S |
| Search.MaxResults, Search.MaxFileSize | 1000, 10485760 | Result and file-size limits | S |
| Search.PathMode | FolderAndFile | Path display in results | S |
| Search.SearchCurrentWord | true | Seed the search with the current word | S |
| Search.SortOpenFilesByOpenFrequency | true | Rank files by how often they are opened | M |
| Search.SetFindText | true | A result jump sets the find text | S |

### Parser, completion and code intelligence

| Setting | 10x default | Description | Effort / note |
|---|---|---|---|
| Autocomplete | true | Show completion automatically while typing | S. Behaviour exists (always on) |
| AutocompleteCount | 6 | Rows in the dropdown | S |
| AutoCompleteHideLowMatch | true | Hide weak matches behind "more..." | S/M |
| SubmitFirstAutocomplete | true | Tab accepts the first item with nothing selected | S |
| SubmitAutocompleteOnEnter | true | Enter accepts the completion | S. tenfold: Tab and Enter accept |
| SubmitAutocompleteOnOpenBracket | true | Typing `(` accepts the completion | S |
| AutoAddFunctionBrackets | true | Completing a function adds `(` and `)` when it takes no arguments | S/M. Needs the signature from clangd |
| AutocompleteInComments | false | Completion inside comments | S |
| ExactMatchAutoComplete | `else, endif, once` | Dismiss the popup when the typed text equals one of these | S |
| UnderlineBuildErrors, AutoUnderlineErrors | true, true | Underline build errors in the source | M. Squiggles come from clangd diagnostics and from the build output (C1-UI) |
| GlobalDefines | (empty) | Extra defines for the parser | S. tenfold takes defines from the solution / compile commands (OPEN_QUESTIONS #12) |
| ParserExtensions, ParserExcludeFilters, AdditionalIncludePaths, ParserPriorityFiles | (lists) | Which files the parser reads, extra include directories, unity-build priority | M |
| ParserMaxFileSize, MaxPreprocessorTokenCount | 52428800, 10000 | Parser limits | S |
| ShaderFileExtensions | `.glsl .hlsl ...` | Treat these as shader files | S |
| DefinedOutBrightness | 0.55 | Dim inactive `#if` code | M. Needs clangd inactive regions |
| AllowPreprocessorLineIndenting, IndentRequires | true, true | Indenting `#` lines and `requires` | S, with the formatter |
| CPlusPlusDefine, TreatUndefinedDefinesAsTrue, ColourUndefinedKeywords | 199711L, false, false | Settings of 10x's own parser | Not applicable (clangd takes these from the compile flags) |

### Build, debug and source control

| Setting | 10x default | Description | Effort / note |
|---|---|---|---|
| BuildBeforeStartDebugging | true | Build before starting the debugger | S. Check what tenfold's start does |
| BuildBeforeRun | false | Build before running the exe | S |
| StopDebuggingOnBuild | false | Stop the debugger when a full build starts | S |
| CloseBuildOutputAfterBuild | false | Close the build panel after a successful build | S |
| StopScrollingOnError | true | Stop following the output at the first error | S |
| BuildWarningRegex, BuildErrorRegex | (empty) | Regexes for build output lines | S |
| OutputPanelWarningRegex, OutputPanelErrorRegex | `\[warning\]`, `\[error\]` | Regexes for output panel lines | S |
| SyncDebugLine | true | Follow the debugger's current line | S. Behaviour exists (C2c) |
| ToggleBreakpointsWithMouse | true | Click in the margin toggles a breakpoint | S. Behaviour exists |
| VisualStudioEnvBat, VisualStudioInstallPath | auto, default | Where the VS build environment comes from | M. tenfold discovers MSVC itself (A2) |
| DebuggerExe | (empty) | A debugger other than Visual Studio | Out of scope (OPEN_QUESTIONS #9) |
| SourceControl | Perforce | Which provider is used | S. tenfold detects Git / Perforce |
| PerforceOpenForEditOnEdit, PerforceOpenForEditOnSave | true, true | Auto-checkout on edit or save | S. Auto-checkout on edit exists (C3b); a save-time variant is new |
| PerforceTimeout, PerforceEnableP4Config | 40, true | p4 command timeout; p4 config files | S |
