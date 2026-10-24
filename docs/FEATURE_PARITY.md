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
| Still instant | ✅ | fast index at startup; clangd background index persisted; approximate results within the budget. |

## Workspace and files

| Feature | Status | Notes |
|---|---|---|
| Open a folder as a workspace | ✅ | `Workspace::scan`, ignore rules, extension filters. |
| Open a Visual Studio `.sln` / `.vcxproj` | ✅ | Projects enumerated; compile flags evaluated per configuration/platform. **verify**: which configuration 10x picks by default. |
| CMake projects | ✅ | Configured into `.tenfold/cmake-build` for compile commands. |
| Makefile / bare folders | ✅ | Heuristic include paths. |
| Per-workspace settings file | ✅ | `.tenfold/workspace.json`. **verify** 10x's `.10x` workspace format if import is wanted. |
| Fast "open file" fuzzy search | 🔶 | `Workspace` has the file list; reuse `FastIndex::fuzzyScore`. |
| Find in workspace (text search) | ⬜ | Parallel grep over `Workspace::codeFiles()`; results feed `NavigationHistory`. |
| File watching / external changes | ⬜ | |

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
| Semantic highlighting | 🔶 | clangd supports `textDocument/semanticTokens`; add to `LspClient`. |
| Call hierarchy / type hierarchy | 🔶 | clangd supports both; thin wrappers needed. |
| Include fix-its / code actions | 🔶 | `textDocument/codeAction` wrapper needed. |

## Editing

| Feature | Status | Notes |
|---|---|---|
| UTF-8 text, CRLF preserved | ✅ | |
| Undo / redo | ✅ | Per document, linear. **verify** 10x's grouping of typed characters. |
| Multi-caret / column select | ⬜ | View-layer feature over `Document::replace`. |
| Auto-indent, bracket matching, comment toggling | ⬜ | Lexer available (`CppLexer`). |
| Large files | ✅ | Line vector; tested approach, piece table as upgrade path. |

## Shell

| Feature | Status | Notes |
|---|---|---|
| GPU-rendered text, tabs, split panels | ⬜ | Next milestone. Recommended stack: native window + own GPU renderer (D3D11/Vulkan/Metal via a thin platform layer), like 10x. |
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
| Process spawning | ⬜ | `Process` interface fixed; needs CreateProcess + pipes. |
| MSVC headers for clangd | ⬜ | Ship `clangd.exe`; `clang-cl` driver commands already generated; add `--query-driver` for the installed `cl.exe`. |
| Paths / URIs | ✅ | `pathToUri`/`uriToPath` handle drive letters. |
