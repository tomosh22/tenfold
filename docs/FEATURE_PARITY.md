# 10x feature parity

The brief is: fix two problems, keep everything else exactly as 10x does
it. 10x is closed source, so "exactly" has to be re-derived from observed
behaviour. This file is the working inventory. Items marked **verify** need
checking against a running copy of 10x before they are implemented, so we
copy what it actually does rather than what we remember.

Legend: ✅ done in the engine · 🔶 engine support exists, needs the GUI ·
⬜ not started

## The two fixes

| Feature | Status | Notes |
|---|---|---|
| Navigate Backward / Forward | ✅ | `NavigationHistory`; VS-style semantics; edit tracking. Suggested keys: Alt+Left / Alt+Right and Ctrl+- / Ctrl+Shift+- (VS). |
| Navigation that cannot miss | ✅ | clangd authoritative, fast index approximate, budgeted delivery, cold-start augmentation. |
| Still instant | ✅ | fast index at startup, persisted to `.tenfold/fastindex.bin` so warm starts only re-lex changed files (A4); clangd background index persisted; approximate results within the budget. |

## Workspace and files

| Feature | Status | Notes |
|---|---|---|
| Open a folder as a workspace | ✅ | `Workspace::scan`, ignore rules, extension filters. |
| Open a Visual Studio `.sln` / `.vcxproj` | ✅ | Projects enumerated; compile flags evaluated per configuration/platform. **verify**: which configuration 10x picks by default. |
| CMake projects | ✅ | Configured into `.tenfold/cmake-build` for compile commands. |
| Makefile / bare folders | ✅ | Heuristic include paths. |
| Per-workspace settings file | ✅ | `.tenfold/workspace.json`. **verify** 10x's `.10x` workspace format if import is wanted. |
| Fast "open file" fuzzy search | 🔶 | `Workspace` has the file list; reuse `FastIndex::fuzzyScore`. |
| Find in workspace (text search) | 🔶 | `TextSearch` + `Editor::findInWorkspace` (A6): parallel, streaming, cancellable; literal fast path, PCRE2 regex, globs, open-buffer override; `gotoSearchMatch` records history. CLI `grep`. UI in B5. |
| File watching / external changes | ✅ | `FileWatcher` (A3): inotify on Linux (polling fallback past the watch limit), `ReadDirectoryChangesW` on Windows (Windows test pass in progress); live fast-index, history and file-list updates; `Editor::onExternalChange` / `reloadFromDisk` for open documents (UI prompt in B-phase). |

## Code intelligence

| Feature | Status | Notes |
|---|---|---|
| Go to definition / declaration | ✅ | |
| Go to type definition / implementation | ✅ | |
| Find references | ✅ | |
| Symbol search (workspace, fuzzy, qualified `a::b`) | ✅ | Workspace symbols rank before system headers. |
| Document outline | ✅ | `documentSymbols` |
| Autocomplete | ✅ | clangd, detailed style; approximate fallback. |
| Signature help | ✅ | |
| Hover / quick info | ✅ | |
| Rename | ✅ | Returns a per-file edit map; GUI applies it. |
| Diagnostics (squiggles) | ✅ | Pushed per file; `Editor::diagnostics(file)`. |
| Switch header/source | ✅ | clangd extension, fallback by stem. |
| Semantic highlighting | 🔶 | Engine done (A9): `Editor::onSemanticTokens` / `semanticTokens`, full + delta, decoded to byte-column `TokenSpan`s, debounced after edits. Needs GUI (B2). |
| Call hierarchy / type hierarchy | 🔶 | Engine done (A9): `Editor::callHierarchy` / `incomingCalls` / `outgoingCalls`, `typeHierarchy` / `supertypes` / `subtypes`, `gotoHierarchyItem` (navigation history). Needs GUI. |
| Include fix-its / code actions | 🔶 | Engine done (A9): `Editor::codeActions` + `applyCodeAction` (edits and `workspace/executeCommand` tweaks, server `workspace/applyEdit`), one undo group per document. Also inlay hints, folding ranges, document highlights and clangd formatting (document / range). Needs GUI. |

## Editing

| Feature | Status | Notes |
|---|---|---|
| UTF-8 text, CRLF preserved | ✅ | |
| Undo / redo | ✅ | Per document, linear; nestable undo groups restore caret sets; typed-word grouping (A5, setting). **verify** 10x's grouping of typed characters (OPEN_QUESTIONS #3). |
| Multi-caret / column select | 🔶 | Engine done (A5): carets/selections, add above/below, next occurrence, column select, word/line/page moves; property-tested. GUI in B2. |
| Auto-indent, bracket matching, comment toggling | ⬜ | Lexer available (`CppLexer`). |
| Large files | ✅ | Line vector; tested approach, piece table as upgrade path. |

## Shell

| Feature | Status | Notes |
|---|---|---|
| GPU-rendered text, tabs, split panels | 🔶 | B1 foundation merged (`-DTENFOLD_BUILD_UI=ON`): SDL3 window/input, batched GL 3.3 renderer, FreeType glyph atlas + monospace layout, headless PNG screenshots with golden tests. `tenfold-ui <file>` renders files with a line-number gutter (B1c; 0.29 ms CPU/frame for a 10k-line file). Retained widget layer + TabStrip (pinned, MRU, drag reorder), Splitter, Panel, StatusBar (B3a; `tenfold-ui --shell`, 2 draw calls/frame). TextView (B2) and Workbench layout persistence (B3b) in progress. |
| Key bindings as a text file | ⬜ | **verify** 10x's `KeyMappings.10x_settings` grammar if users want to import theirs. |
| Colour schemes as text files | ⬜ | **verify** format. |
| Settings as text file | ⬜ | **verify** 10x's `Settings.10x_settings` keys. |
| Build integration (run command, parse errors, jump) | ⬜ | Error jumps should go through `Editor::gotoLocation` so they are in history. |
| Debugger integration | ⬜ | **verify** what 10x does (external debugger launch vs. integrated). |
| Version control (Perforce / Git) | ⬜ | **verify** exact operations 10x exposes. |
| Python scripting API | ⬜ | **verify** 10x's `N10X` module surface before designing ours. |
| Unreal Engine awareness | ⬜ | `.uproject` → generate compile commands; UE macros are handled by clangd automatically. |

## Windows

| Item | Status | Notes |
|---|---|---|
| Process spawning | ✅ | `Process_win32.cpp` (A1): CreateProcessW, inherited pipes, kill-on-close job object. Verified via mingw + Wine and native MSVC 2022 / clang-cl CI (blocking). |
| MSVC headers for clangd | 🔶 | `cl.exe` discovery (VCToolsInstallDir / vswhere / `msvcToolchain` setting) and `--query-driver` done (A2). Shipping `clangd.exe` + bundled `bin/` lookup is D1. |
| Paths / URIs | ✅ | `pathToUri`/`uriToPath` handle drive letters and UNC; case/separator-insensitive `pathKey`/`pathsEqual` on Windows (A2). |
