# 10x feature parity

The brief is: fix two problems, keep everything else exactly as 10x does
it. 10x is closed source, so "exactly" has to be re-derived from observed
behaviour. This file is the working inventory. Items marked **verify** need
checking against a running copy of 10x before they are implemented, so we
copy what it actually does rather than what we remember.

Legend: ✅ done in the engine · 🔶 engine support exists, needs the GUI ·
⬜ not started · — excluded by the user

## The two fixes

| Feature | Status | Notes |
|---|---|---|
| Navigate Backward / Forward | ✅ | `NavigationHistory`; VS-style semantics; edit tracking. Keys: Alt+Left / Alt+Right and Ctrl+- / Ctrl+Shift+- (VS), thumb buttons Mouse4/5. UI (B6): toolbar Back/Forward buttons with key-aware tooltips, history dropdown (long-press / right-click), optional "← 3 \| 1 →" status counts (`nav.statusCounts`), Go to Last Edit Location (Ctrl+Shift+Backspace). |
| Navigation that cannot miss | ✅ | clangd authoritative, fast index approximate, budgeted delivery, cold-start augmentation. |
| Still instant | ✅ | fast index at startup, persisted to `.tenfold/fastindex.bin` so warm starts only re-lex changed files (A4); clangd background index persisted; approximate results within the budget. |

## Workspace and files

| Feature | Status | Notes |
|---|---|---|
| Open a folder as a workspace | ✅ | `Workspace::scan`, ignore rules, extension filters. |
| Open a Visual Studio `.sln` / `.vcxproj` | ✅ | C5: projects enumerated; compile flags evaluated like MSBuild passes them to `cl.exe` per file and configuration (defines/undefines, implicit `_WIN32`/`_DEBUG`/`_DLL`/`_MT`/`UNICODE`/`_MBCS`, include / external include dirs, forced includes, pch, standards, `/permissive-`, `/EH`, `/GR-`, runtime library, `CompileAs`, per-file metadata, `ExcludedFromBuild`, property sheets, `Directory.Build.props/targets`). Solution -> project configuration mapping (`ProjectConfigurationPlatforms`); unbuilt projects skipped. Default Debug\|x64, else the first solution configuration (OPEN_QUESTIONS #2). `Build.SelectConfiguration` picker and a status-bar segment (click to switch) change the active configuration. |
| Switch solution configuration / platform | 🔶 | `Editor::selectVsConfiguration` / `vsConfigurations`, `tenfold-cli vsconfig`; regenerates compile commands and restarts clangd. Picker UI pending. |
| CMake projects | ✅ | Configured into `.tenfold/cmake-build` for compile commands. |
| Makefile / bare folders | ✅ | Heuristic include paths. |
| Per-workspace settings file | ✅ | `.tenfold/workspace.json`. No `.10x` import (OPEN_QUESTIONS #12); compiler settings come from the Visual Studio solution (C5). |
| Fast "open file" fuzzy search | ✅ | B5a picker (Ctrl+P / Ctrl+O): `FastIndex::fuzzyScore` on filename (weight 4) and path, MRU files first (persisted in `layout.json`), 2-4 ms per keystroke on 8.7k Godot paths. Opening is a tab switch, not a navigation jump. |
| Find in workspace (text search) | 🔶 | `TextSearch` + `Editor::findInWorkspace` (A6): parallel, streaming, cancellable; literal fast path, PCRE2 regex, globs, open-buffer override; `gotoSearchMatch` records history. CLI `grep`. UI (B5b): Ctrl+Shift+F modal list grouped by file, live streaming, case / word / regex toggles, include / exclude globs, F8 / Shift+F8, scrollbar markers. |
| File watching / external changes | ✅ | `FileWatcher` (A3): inotify on Linux (polling fallback past the watch limit), `ReadDirectoryChangesW` on Windows (Windows test pass in progress); live fast-index, history and file-list updates; `Editor::onExternalChange` / `reloadFromDisk` for open documents. UI (P2): a clean document is reloaded (`editor.reloadExternalChanges` = auto, as Visual Studio), a dirty one asks Reload / Keep my version, a deleted file keeps its buffer and marks its tab and the status bar. |
| Unsaved changes | ✅ | P2: closing the last view of a modified tab, closing the window and `App.Quit` ask Save / Don't Save / Cancel (Save All for several documents; a failed save keeps the app open and shows the error); `editor.confirmUnsaved`. Follows Visual Studio (OPEN_QUESTIONS #5). No autosave. |

## Code intelligence

| Feature | Status | Notes |
|---|---|---|
| Go to definition / declaration | ✅ | |
| Go to type definition / implementation | ✅ | |
| Find references | ✅ | |
| Symbol search (workspace, fuzzy, qualified `a::b`) | ✅ | Workspace symbols rank before system headers. GUI picker (B5a, Ctrl+,): approximate results greyed with "verifying...", swapped for the authoritative ones keeping the selection, target-line preview (`picker.preview`, **verify** with 10x), Enter records a navigation jump. Fast tier is 35-270 ms on Godot until A10 makes it asynchronous. |
| Document outline | ✅ | `documentSymbols` |
| Autocomplete | ✅ | clangd, detailed style; approximate fallback (greyed). GUI (B5b): popup at the caret, `.` `->` `::` and 2+ identifier characters, client-side fuzzy filter, docs pane, Tab / Enter accept as one undo step. |
| Signature help | ✅ | GUI (B5b): on `(` / `,`, active parameter highlighted, follows commas locally, Escape / `)` closes. |
| Hover / quick info | ✅ | GUI (B5b): 400 ms dwell, diagnostics first, approximate text greyed. |
| Rename | ✅ | Returns a per-file edit map; GUI applies it. |
| Diagnostics (squiggles) | ✅ | Pushed per file; `Editor::diagnostics(file)`; squiggles + scrollbar markers in TextView (B2b); list of open-file and build diagnostics (B5b, Ctrl+Shift+M). |
| Switch header/source | ✅ | clangd extension, fallback by stem. |
| Semantic highlighting | ✅ | Lexical colours instantly (cached per line, background exact pass), clangd semantic tokens override identifiers only — no flicker (A9 + B2b). Palette pending OPEN_QUESTIONS #7. |
| Call hierarchy / type hierarchy | 🔶 | Engine done (A9): `Editor::callHierarchy` / `incomingCalls` / `outgoingCalls`, `typeHierarchy` / `supertypes` / `subtypes`, `gotoHierarchyItem` (navigation history). Needs GUI. |
| Include fix-its / code actions | 🔶 | Engine done (A9): `Editor::codeActions` + `applyCodeAction` (edits and `workspace/executeCommand` tweaks, server `workspace/applyEdit`), one undo group per document. Also inlay hints, folding ranges, document highlights and clangd formatting (document / range). Needs GUI. **Known gap (Windows/MSVC STL):** clangd offers no "add #include" fix for `std::` names there (works on libstdc++/libc++); the test skips on `_WIN32` only when clangd returns no action, other code actions (e.g. typo fix-its) are tested on all platforms. Revisit with a newer clangd. |

## Editing

| Feature | Status | Notes |
|---|---|---|
| UTF-8 text, CRLF preserved | ✅ | |
| Undo / redo | ✅ | Per document, linear; nestable undo groups restore caret sets; typed-word grouping (A5, setting). **verify** 10x's grouping of typed characters (OPEN_QUESTIONS #3). |
| Multi-caret / column select | ✅ | Engine (A5) + TextView (B2a): click/drag/double/triple/Alt-drag column select, Ctrl/Alt-click add caret, multi-caret clipboard; keystroke-to-paint 0.36 ms with 1000 carets on 100k lines. |
| Auto-indent, auto-close brackets and quotes | 🔶 | B8: Enter copies / extends the indentation (brace and operator continuation), `}` dedents, `( [ { " '` close themselves (skip-over, wrap selection, pair Backspace); settings `editor.autoIndent`, `editor.autoClosePairs`, `editor.indentUnit`, `editor.detectIndent`. **verify** against 10x (OPEN_QUESTIONS #8). |
| Bracket matching | ✅ | B2b: box behind the matching `(){}[]` of the caret. |
| Comment toggle, duplicate / move / join / delete line, indent / outdent | 🔶 | B8: Ctrl+/ and Ctrl+Shift+/, Ctrl+Shift+D, Alt+Up/Down, Ctrl+J, Ctrl+Shift+K, Tab / Shift+Tab; multi-caret, one undo step each. Placeholder keys. |
| Go to line | ✅ | B8: Ctrl+G, `line[:column]`, recorded in the navigation history. |
| Bookmarks | 🔶 | B8: toggle / next / previous / list, per file, follow edits, `.tenfold/bookmarks.json`, gutter flag. **verify** 10x's behaviour (OPEN_QUESTIONS #8). |
| Find / replace in file | 🔶 | B8: Ctrl+F / Ctrl+H bar, incremental highlight of all matches, case / word / regex, F3 / Shift+F3, replace one / all (one undo step). Regex group references in the replacement are not expanded. |
| Trim trailing whitespace on save, line ending switch | 🔶 | B8: `editor.trimTrailingWhitespace` (default off), `Editor.LineEndingLF/CRLF`. Encoding is UTF-8 only. |
| Word wrap | ✅ | B2c: soft wrap on by default (`editor.wordWrap`; smooth scrolling off by default), at word boundaries, continuation rows indented, line number on the first row; Up / Down / Home / End by visual row; `View.ToggleWordWrap` (Alt+Z, the Visual Studio binding). No hard wrap / reflow and no wrap at a fixed column (a possible later setting). |
| Large files | ✅ | Line vector; tested approach, piece table as upgrade path. |

## Shell

| Feature | Status | Notes |
|---|---|---|
| GPU-rendered text, tabs, split panels | 🔶 | B1 foundation merged (`-DTENFOLD_BUILD_UI=ON`): SDL3 window/input, batched GL 3.3 renderer, FreeType glyph atlas + monospace layout, headless PNG screenshots with golden tests. `tenfold-ui <file>` renders files with a line-number gutter (B1c; 0.29 ms CPU/frame for a 10k-line file). Retained widget layer + TabStrip (pinned, MRU, drag reorder), Splitter, Panel, StatusBar (B3a; `tenfold-ui --shell`, 2 draw calls/frame). TextView (B2) and Workbench layout persistence (B3b) in progress. |
| Key bindings as a text file | 🔶 | Done (B4 + B-INT): named commands, chords, contexts, `.tenfold/keybindings.txt` overlay, diagnostics in status bar. Defaults are placeholders: the user wants 10x's format and 10x's defaults (OPEN_QUESTIONS #1, task B4b), blocked on obtaining 10x's default bindings file. |
| Colour schemes as text files | ✅ | Theme files with lexical + semantic token colours and fallbacks, hot reload (B7). Default is `vs-dark`, a Visual Studio 2022 Dark look (B7b; some palette values recalled from memory, listed in the theme header; no control-keyword / escape-sequence colours yet). |
| Settings as text file | 🔶 | Mechanism done (B7): layered `settings.txt` with typed keys, diagnostics, hot reload. **verify** 10x's `Settings.10x_settings` keys for an importer. |
| Local diagnostic log (no telemetry) | ✅ | D3: `.tenfold/log.txt` (user data dir without a workspace), 5 MB x 3 files, `TENFOLD_LOG=debug`, `tenfold-cli log <root> [--tail N]`, `Editor::logFilePath()`. A log viewer / "Open log" menu entry is not built yet. See ARCHITECTURE "Local log file". |
| Compile-command diagnostics | ✅ | D4: `Editor::compileCommandInfo(file)` (source of the compile database, active Visual Studio configuration, the file's exact argv or the fallback flags, whether clangd has the file open and parsed), `tenfold-cli why <root> <file>`, and the `Help.ShowCompileCommand` command (read-only popup; no default key). See ARCHITECTURE "Compile-command diagnostics". |
| clangd visibility, command palette | ✅ | P2: the status bar shows the engine's clangd text ("clangd restarting (2)...", "clangd stopped: crashed 5 times") and `flags: heuristic` (click: `Help.ShowCompileCommand`); `Clangd.Restart`; `App.CommandPalette` (Ctrl+Shift+P, provisional) lists every runnable command with its key and filters fuzzily, which also reaches the commands that have no key (`Vcs.*`, `Debug.OpenInVisualStudio`, ...). |
| User guide | ✅ | D4: `docs/USER_GUIDE.md`; its settings table is checked against `assets/settings.default.txt` by a test (`user_guide_settings_table_matches_the_default_settings_file`). |
| Build integration (run command, parse errors, jump) | ✅ | Engine (C1): `Editor::build/cancelBuild/nextBuildMessage/prevBuildMessage/buildDiagnostics`, `tenfold-cli build`. UI (C1-UI): resizable bottom dock with a streaming Build panel (clickable, severity-coloured lines, Build/Cancel, status), Ctrl+Shift+B / Ctrl+Break / F4 / Shift+F4, build squiggles + scrollbar marks + hover, status bar segment. All jumps go through `gotoLocation`. See ARCHITECTURE "Build panel and bottom dock". |
| Debugger integration | 🔶 | Visual Studio debugger through EnvDTE (OPEN_QUESTIONS #9). Engine (C2a): breakpoints that follow edits (`.tenfold/breakpoints.json`), `DebugSession` worker with two-way breakpoint sync and follow-current-statement. Backend (C2b): late-bound COM EnvDTE, finds the VS instance with the solution via the ROT or launches devenv, run/step/break/run-to-cursor, breakpoint sync, current statement; verified against Visual Studio 2022 in the blocking `windows-vs-debugger` CI job (launch, breakpoint hit, step, VS-side breakpoint, stop). UI (C2c): breakpoint glyph lane (click toggles; disabled / conditional shapes), current-statement arrow and line band, `Debug.*` commands on Visual Studio keys (F5, F9, F10, F11, …), breakpoint list (Ctrl+Alt+B), debugger status segment and orange status bar while debugging. `Debug.OpenInVisualStudio` (no default key) opens the current file at the caret in Visual Studio. |
| Version control (Perforce / Git) | 🔶 | OPEN_QUESTIONS #10. Engine and Git (C3a): provider interface, `git` status / base text / revert, Myers line diff, debounced off-thread hunks, revert file / hunk. Perforce (C3b, tested against a fake `p4` only): auto-checkout on first edit (`p4 edit`), fstat status, `print #have` base, revert, `vcs.provider` setting, clear read-only save error. UI (C3c): change strip between line numbers and text and scrollbar change marks, inline diff block under a hunk (revert hunk, close, Escape), `Vcs.NextChange` / `PrevChange` / `ToggleInlineDiff` / `RevertHunk` / `RevertFile` (confirmation) / `DiffFileExternal` (p4merge or `vcs.diffTool`) / `RefreshStatus` / `CheckOut`, tab markers, status-bar segment ("Git: modified", "P4: checked out"), errors in the status bar, refresh on window focus. Keys unbound until the 10x key map is known. Not done: verification against a real Perforce server, blame, commit / stage UI, side-by-side diff. |
| Python scripting API | — | Excluded by the user (OPEN_QUESTIONS #11). |
| Unreal Engine awareness | ⬜ | `.uproject` → generate compile commands; UE macros are handled by clangd automatically. |

## Windows

| Item | Status | Notes |
|---|---|---|
| Process spawning | ✅ | `Process_win32.cpp` (A1): CreateProcessW, inherited pipes, kill-on-close job object. Verified via mingw + Wine and native MSVC 2022 / clang-cl CI (blocking). |
| MSVC headers for clangd | 🔶 | `cl.exe` discovery (VCToolsInstallDir / vswhere / `msvcToolchain` setting) and `--query-driver` done (A2). Shipping `clangd.exe` + bundled `bin/` lookup is D1. |
| Direct3D 11 renderer | ✅ | `D3d11Renderer` + `WindowsSwapChain` (B9): flip-model swap chain with frame-latency waitable object, hardware then WARP, runtime-compiled HLSL; default backend of `tenfold-ui` on Windows (`--renderer=gl` for OpenGL). Verified natively in the blocking CI job `windows-ui-d3d11` on WARP: 159 UI tests + 5 D3D11 renderer tests pass, `b1_scene` golden max channel diff 2 (0 of 65536 pixels over tolerance). The text golden is skipped there (no DejaVuSansMono on the runner). Device-loss recovery is a follow-up. |
| SDL3 window / input on Windows | 🔶 | Same `Window` / `translateSdlEvent` code as Linux (SDL3 Win32 backend); window created without `SDL_WINDOW_OPENGL` for D3D11. Built and driven by the UI tests in native Windows CI; interactive input not yet exercised on a real desktop. |
| Paths / URIs | ✅ | `pathToUri`/`uriToPath` handle drive letters and UNC; case/separator-insensitive `pathKey`/`pathsEqual` on Windows (A2). |
