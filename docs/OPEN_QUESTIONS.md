# Open questions for the user

Items the implementation needs verified against a running copy of 10x (or
decided by the user). Each has a default we implement until answered.

| # | Question | Default we use meanwhile | Plan ref |
|---|---|---|---|
| 1 | Exact key bindings in your 10x setup (export `KeyMappings.10x_settings` if possible) | **Answered:** handle key bindings the same way as 10x, with the same defaults. **Implemented (B4b; source: the 10x documentation, Commands / KeyMappings / KeyBindings / SettingsFiles):** key files are `.10x_settings` files (`Control Shift F: Search`, chords separated by `, `), command names are 10x's (tenfold ids stay valid aliases), `assets/KeyMappings.default.10x_settings` is 10x's complete default key map plus a marked tenfold-additions section, an installed 10x's `%APPDATA%\10x\Settings\KeyMappings.10x_settings` is imported below your tenfold file (setting `keys.import10x`, default on), your tenfold file is `<user config dir>/tenfold/KeyMappings.10x_settings` and the workspace's `.tenfold/KeyMappings.10x_settings`. Python statements are reported as unsupported; the old tenfold format is no longer read. 10x commands tenfold lacks stay bound and say `not implemented yet: <Command>` (list: FEATURE_PARITY, "10x commands not yet implemented"). Remaining question: if your 10x key file differs from the documented defaults, `keys.import10x` picks it up; send it if some binding should become a default | B4b |
| 2 | Which Visual Studio configuration/platform 10x uses for parsing by default | **Implemented (C5):** the workspace setting `vsConfiguration`/`vsPlatform` in `.tenfold/workspace.json` (default Debug / x64) when the solution lists it; otherwise Debug\|x64 if the solution has it, otherwise the first configuration of the solution (the engine logs the fallback). `Editor::selectVsConfiguration` / `tenfold-cli vsconfig --set` change it, regenerate the compile commands and restart clangd. Each project is evaluated with the configuration the solution maps it to; projects the solution does not build there are skipped | A2, C5 |
| 3 | Undo grouping: does 10x undo a whole typed word or one character? And does a typed space end the word it follows (so "hello world" undoes as "world", then "hello ") or start the next one? | **Still open: the 10x documentation does not describe undo grouping** (Undo: "Undo the last operation"). Meanwhile: word-level grouping with 1 s pause boundary; whitespace joins and then closes the current step. Settings `undoGrouping` ("word"/"char") and `undoGroupPauseMs` in `.tenfold/workspace.json` | A5 |
| 4 | Does 10x soft-wrap long lines by default? Smooth scrolling? | **Answered:** word wrap on by default, smooth scrolling off by default (`editor.wordWrap`, `editor.smoothScroll`) | B2c |
| 5 | Tab/panel behaviours: pinned tabs, MRU Ctrl+Tab order, drag between panels. Also: does closing the last tab of a panel close the panel? Does closing a dirty tab prompt to save? Does split duplicate the current tab or move it? | **Answered from the 10x documentation:** pinned tabs exist (commands PinTab, CloseAllButPinnedTabs); Ctrl+Tab opens a switch-file panel over all files by default (setting SwitchFileMode = All, or only the current panel's files); duplicating a panel exists (DuplicatePanel, DuplicatePanelLeft / Right), which fits "split duplicates the active tab"; closing a modified file prompts to save, since RestoreUnsavedFiles is false by default ("If true then it won't prompt to save unsaved files") = `editor.confirmUnsaved` true. **Still open (docs silent):** MRU order of the switch panel, whether an emptied panel closes, drag between panels, and files changed on disk. tenfold: all three tab behaviours implemented; an emptied panel auto-closes after a drag but not after a manual close; split duplicates the active tab. Save prompt (P2): closing the last view of a modified tab, closing the window and quitting ask Save / Don't Save / Cancel (setting `editor.confirmUnsaved`, default true); **VS-TABS (2026-11-06, Visual Studio per the user's decision): several modified documents (quit, Close All, Close All But This / Pinned, Close Panel) get one list dialog "Save changes to the following items?" with Yes / No / Cancel (VS_BEHAVIOUR T6).** Files changed on disk (P2, 10x says nothing): **VS-TABS (VS_BEHAVIOUR T9): the question "The file X has been modified outside of the source editor. Do you want to reload it?" with Yes / Yes to All / No / No to All, settings `editor.detectExternalChanges` (true) and `editor.reloadUnlessUnsaved` (false); the P2 setting `editor.reloadExternalChanges` (`auto` \| `prompt` \| `never`, was `auto`) is now an optional override, empty by default.** Also VS-TABS: MRU switch panel on Ctrl+Tab (T1), preview tab (T2), pinned boundary (T11), Ctrl+K Ctrl+Z (T12). | B3 |
| 6 | Does symbol search show a preview of the target line? | **Still open: the 10x documentation does not say** (FindSymbol: "Find a symbol in the workspace"; the only related setting is FindSymbolFuzzyMatching, default true, which tenfold's search already does). Meanwhile yes (`picker.preview`). **VS-NAV (VS_BEHAVIOUR P1, implemented):** the pickers have a "Preview: on/off" switch in the header (click or Alt+P), like the eye of Visual Studio's Code Search | B5 |
| 7 | Default theme colours and font (screenshots of your 10x would be ideal) | **Answered:** dark theme that looks as close to Visual Studio (2022 Dark) as possible: `vs-dark` theme is the default | B7b |
| 8 | Which editing conveniences you rely on (auto-close brackets, comment toggle, bookmarks, ...) and how 10x behaves for each | **Partly answered from the 10x documentation (B8b):** the editing defaults now follow 10x's settings, see the list below (each item names the 10x setting or says the documentation is silent); the rest of the question (which conveniences you rely on) stays open | B8, B8b |
| 9 | How you debug from 10x (external debugger launch vs integrated) | **Answered:** strong integration with the Visual Studio debugger (drive VS through its automation interface: start/stop/step, breakpoints synced both ways, follow the current statement) | C2a–C2c |
| 10 | Perforce/Git operations you use from inside 10x | **Answered:** Git status/diff/revert; Perforce auto-checkout on edit, plus status/diff/revert; inline diffs for Perforce files; p4merge launched for a full diff when installed | C3a–C3c |
| 11 | Do you use 10x's Python scripting API? | **Answered:** no; excluded completely (C4 dropped) | C4 |
| 12 | Any `.10x` workspace files or settings you want imported | **Answered:** no `.10x` import; the only workspace settings that matter come from the open project's Visual Studio solution (defines, include paths, compiler options per configuration) | C5 |
| 13 | Target platforms and priority (Windows first?) | **Answered:** Windows first, then Linux (macOS best effort, kept building in CI) | A1/A2/B9 |
| 14 | Code formatting: the existing code is hand-formatted in a compact style that `.clang-format` cannot reproduce (986 residual hunks; even one-line edits reflow neighbouring statements). Adopt clang-format wholesale with one reformatting commit, or keep the hand style? | **Answered:** adopt clang-format and reformat. Whole tree reformatted once (`examples/` and `tests/fixtures/` excluded: tests depend on their line/column positions); CI format job is blocking | A7 |
| 15 | Visual Studio connection at startup: should tenfold connect to Visual Studio on its own? | **Partly answered from the 10x documentation (VSDebugger, setting `VisualStudioSync`):** 10x connects to / launches Visual Studio when the debugger is started, keeps breakpoints in sync, and before the debugger starts makes Visual Studio's solution configuration and platform match 10x's. tenfold does the same (C2d): the workspace setting `debug.visualStudioSync` in `.tenfold/workspace.json` (default on, mirrors `VisualStudioSync`; off = no breakpoint sync, no configuration sync, no attach at startup, commands still work), and start / start without debugging / restart / run to cursor first activate the solution configuration `Workspace::activeVsConfiguration()` in Visual Studio. As an addition tenfold also attaches at startup (and retries every 10 s while breakpoints exist) to a Visual Studio that already has the workspace's solution open, so breakpoints reach it early; it never launches Visual Studio before you press a debug key. That addition is behind `debug.attachAtStartup` (default on). Still open: whether the startup attach is wanted at all | C2b2, C2d |
| 16 | Version-control marks for new / untracked files: Visual Studio paints the whole file as added; should tenfold? | no marks for untracked files; a file staged as new (or opened for add in Perforce) shows as one added block. **VS-VCS (VS_BEHAVIOUR section 4):** VC1 keeps "no whole-file mark" (could not be confirmed that VS paints it), VC2 (Git glyphs, low) unchanged until checklist V7; Perforce files get P4VS badges (VC3 implemented): red check = checked out, red plus = opened for add, blue ? = not in the depot, yellow triangle = newer revision in the depot, red ? = needs resolve | C3c, VS-VCS |
| 17 | Perforce: please sanity-check the commands tenfold relies on against your server (listed in docs/ARCHITECTURE.md, "Perforce"), in particular `p4 -ztag -F %clientRoot% info`, `p4 where <dir>/...` for an unmapped directory, what `p4 edit` prints / returns for an exclusively locked file, `p4 -ztag filelog -L -m 200 <file>` (PerforceHistory) and `p4v -p <port> -u <user> -c <client> -s <depot path>` (PerforceShowInP4v: does it select the file?) | implemented against a fake `p4` only; no real server was available | C3b, W8f |
| 18 | Licence for tenfold itself (the repository has no LICENSE file; packages ship third-party notices only) | none chosen; packaging picks up `LICENSE` automatically once it exists | D1 |
| 19 | Visual Studio 2022 Dark colours. **Sourced and applied (W10-THEME, docs/VS_BEHAVIOUR.md 5.1/5.3):** shell, tab, menu, scrollbar, input, selection and status colours from Microsoft's "Color value reference for Visual Studio", and the syntax colours from Microsoft's "Visual Studio 2019 Dark (C++)" theme (control-flow keywords included). **Still needs a screenshot from the user** (marked `[?]` in assets/themes/vs-dark.txt): squiggles (error / warning / info), Git margin and inline diff colours, find highlight, bracket match, current-line outline grey, editor selection, whitespace dots, column guide, caret, dirty marker, status bar hover / debugging orange / no-solution purple, string / enum / macro token colours where the two sources disagree, tooltip and find-bar colours, the integrated terminal palette | sourced values | B7b, C2c, C3c, W10-THEME |


## User decisions of 2026-11-06 (these override the defaults written further down)

* **Characters the file's encoding cannot hold:** show the user the character(s) (each one, with its line and column and
  its code point) and **refuse to save**. Replaces the W9-FIX-ENC default "save as UTF-8". *Implemented (W10-ENC), see
  "W9 detail: encodings, line endings and the BOM".*
* **Line endings:** handle them **the same way as Visual Studio** (load, display, typing new lines, mixed endings, the
  consistency check, save). Replaces the W9-FIX-ENC default "normalise to the dominant ending on save".
* **Go to definition through a `using` declaration or a `typedef` / alias:** let the user **choose** between the
  declaration (the using-declaration / typedef) and the real entity. **Implemented (W10-CHOOSE)**, see below.
* **Go to definition on a constructor call such as `Widget(2)`, `Widget{2}`, `new Widget(2)`:** let the user **choose**
  between the class and the constructor that is called. **Implemented (W10-CHOOSE)**, see below.
* **Every other unanswered question** (rows still open below and the defaults chosen in the detail sections where the
  10x documentation is silent): **match Visual Studio's behaviour where Visual Studio has one** (research it, cite the
  source). Where Visual Studio has no equivalent, keep the documented default.
  *Implemented for the tab well (VS-TABS), see docs/VS_BEHAVIOUR.md section 7 rows T1, T2, T6, T9, T11 and T12:* the
  switch panel (Ctrl+Tab) lists files most recently used first with the previous file selected and commits when Ctrl is
  released; a single click on an explorer file or a find / references result and the debugger stepping into an unopened
  file open the italic preview tab (`tabs.previewTab`, `tabs.previewFromFindResults`); Close All / Close All But This /
  Close All But Pinned / Close Panel / quit with several modified documents show one "Save changes to the following
  items?" list (Yes / No / Cancel); files changed on disk ask "The file X has been modified outside of the source editor.
  Do you want to reload it?" (Yes / Yes to All / No / No to All; `editor.detectExternalChanges`,
  `editor.reloadUnlessUnsaved`, with `editor.reloadExternalChanges` kept as an optional override); pinned tabs cannot be
  crossed by moves or drags; Ctrl+K, Ctrl+Z restores the last closed tab (a tenfold addition). T3, T4, T5, T7, T8, T10
  and T13 are unchanged.
  *Implemented for the workspace explorer (VS-TREE), see docs/VS_BEHAVIOUR.md section 11 rows X2, X3, X5, X6, X7, X8 and
  X9 and row C10 of section 8:* typing in the tree jumps to the next name that starts with the letters (type-ahead, one
  second reset) and Ctrl+; starts the old fuzzy filter; Switch Views (Solution / Folder) and Show All Files; the expanded
  nodes, the selection and both switches are saved in `.tenfold/explorer.json`; the explorer opens by itself the first
  time a workspace with a solution is started; New File / New Folder / Rename (F2) / Delete (confirmed; to the Recycle Bin / Trash) and
  drag and drop (move with the "Prompt before moving" question, Ctrl copies) that keep the `.vcxproj` / `.filters`
  Include values in step; a read-only Properties page. X1 (External Dependencies, optional), X4 (low: 10x wins) and X10
  (search dropdown, optional) are unchanged, and the medium-low context-menu rows C6-C9 are not done.
  *Implemented for version control (VS-VCS), see docs/VS_BEHAVIOUR.md section 4 rows VC3, VC5 and VC6:* Perforce files show
  P4VS-style badges in the explorer (check, plus, blue ?, yellow triangle, red ?) and matching tab squares; the new setting
  `perforce.promptOnEdit` (default off, because 10x's documented behaviour (#10) is auto-checkout) makes the first edit of a
  read-only depot file ask "Check out?"; Enter on a `PerforceHistory` revision opens an action menu (Open, Diff against
  previous, Diff against have). VC1 and VC4 already match Visual Studio, VC2 (low) is skipped. All of it is tested against the
  fake `p4` only (#17).
  *Implemented for the editing rows (VS-EDIT), see docs/VS_BEHAVIOUR.md section 14:* Z4 Copy / Cut with no selection take
  the line with its terminator, with the two Visual Studio switches `editor.copyLineWhenEmpty` and `editor.copyBlankLines`
  (both true); Z7 wheel 3 lines, Z11 clipboard ring 20 and Z20 Tab / Shift+Tab (already as Visual Studio); Z8 zoom scales
  only the editor text, with a "N %" indicator in the status bar and Control + wheel (`editor.mouseWheelZoom`, 10x
  MouseWheelZoom). **Low-confidence rows built but OFF by default, on the user's verification checklist (VS_BEHAVIOUR V12
  to V14):** Z1 `editor.autoCloseQuotes` (VS C++ pairs quotes), Z14 `editor.textFileNames` (`TextFileN.txt` instead of
  "Untitled N"), Z19 `editor.useEditorConfig` (`.editorconfig` `trim_trailing_whitespace` / `insert_final_newline` on save;
  no Microsoft source found that VS applies them on save). Not changed: Z2, Z3, Z5, Z6 (user decision), Z9 (10x's Alt+R),
  Z10, Z12, Z13, Z18.
  *Implemented for navigation (VS-NAV), see docs/VS_BEHAVIOUR.md sections 6, 10 and 13 and "VS-NAV detail" below:* P1 the
  symbol search / function picker preview switch; N1 Go To Document on an `#include` line (F12, Ctrl+F12 and the 10x
  middle-click open the file; the 10x keys already matched Visual Studio's F12 / Ctrl+F12 / Shift+F12); N5 (partly) the
  Find All References list is grouped (Definition, then per file) with Line:Col and Read / Write / Definition / Other;
  I5 `diagnostics.miscFiles` (false): no squiggles in files that are in no translation unit. Not done: P2 (optional), I4
  and I1 (medium-low), N2, N3, N6, N7, N9 (low), N4 and N8 (no change by design), I6 (cosmetic).

### W10-CHOOSE: how the two choosers were implemented (and what was decided where the decision said nothing)

* **Default selection:** the real entity for aliases and using-declarations, the constructor for constructor calls, as
  decided. For the inherited constructor it is the resolved base constructor.
* **Order of the list:** declaration first, entity last (`using-declaration`, `entity`; `alias B3`, `typedef B2`, `typedef B1`,
  `struct Foo`; `class Widget`, `constructor Widget(int)`), so the corpus can say `def use_x = using_x entity_x`.
* **The constructor is shown at its definition** (`Widget::Widget(int)` in the .cpp, else the declaration) because the
  command is "go to definition". Choose the class row for the class.
* **Implicit constructor** (no written one): one target, so no chooser; F12 jumps to the class and the status bar says
  `implicit constructor`. **Aggregate initialisation** (`Plain p{1, 2}`): the class only, nothing said (no constructor runs).
* **Forms covered:** `T(args)`, `T{args}`, `T name(args);`, `T name{args};`, `new T(args)`, base / delegating mem-initializers,
  with a template argument list after the name; also deduction (`Pair p{1, 2.0}`). Not covered, class only: a bare
  `T name;` and `T name = x;` (the call is not spelled), `make<T>(...)` / `std::make_unique<T>` (the constructor is called
  inside the library).
* **Not a choice:** a typedef of a builtin type or of something with no declaration (`typedef int myint`) is one target.
  The caret on a declaration (the constructor's own name, the alias being declared) is the usual single answer.
* **Fast tier / chooser consistency:** the fast index cannot tell a constructor call from a type use, nor an alias from
  its target by name alone. An approximate answer that is a single `typedef` / alias, or a single class spelled like a
  construction, is flagged `refinable`; the Editor does not jump on it and waits for clangd (status bar `waiting for
  clangd`), then jumps (one target) or the front end shows the chooser once. If clangd fails or finds nothing it jumps to
  the held-back approximate answer, so nothing is lost when clangd is absent. Known gap: a name whose only alias-ness is a
  **using-declaration** (not indexed by the fast tier) is a plain single fast answer, so a slow clangd (more than the
  250 ms budget) makes F12 jump first and the chooser appear afterwards at the authoritative answer.
* **Intermediate aliases** are followed with clangd's `typeDefinition` at the caret for the first hop, then with clangd at
  the alias' declaration when that document is open, else with the lexical index (only when the aliased name is
  unambiguous in the workspace); an alias template to a library class (`Vec = std::vector<T>`) has no further hop.
* **The chooser's position:** a small popup below the caret (above when there is no room), or at the symbol under the
  mouse for the middle-click command; the same `ListPopup` as the other pickers, so Up / Down / Enter / Escape work and
  the popup is modal.

## #8 detail: editing behaviours and their 10x settings

Settings are `editor.*` in `settings.txt`; key bindings are 10x's (#1) in `assets/KeyMappings.default.10x_settings`.
"Answered from the 10x documentation: X" means tenfold's default equals the documented default of the 10x setting X (B8b);
"docs silent" means 10x's documentation does not describe the behaviour, so the value is still a guess.

* Auto-indent on Enter (`editor.autoIndent`, on): answered from the 10x documentation: AutoIndent (true). Copy the
  previous line's indentation; one extra level after a line ending in `{`, `(` or `[`, and after a line ending in a
  binary operator (`+ - * / % & | ^ = ?` or `<<`); a statement that was continued by an operator returns one level when
  it ends with `;`. The operator rules are docs silent.
* `}` typed as the first non-blank character dedents to the line of its `{` (`editor.autoIndentBraces`, on): answered
  from the 10x documentation: AutoIndentBraces (true, "Update the indentation of braces as they are typed"; reading
  taken from the description). Not done: UpdateIndentsOnCloseBrace (re-indent the whole block), UpdateIndentsOnPaste.
* Enter between `{` and `}` (`editor.formatBracesOnEnterSplit`, off): answered from the 10x documentation:
  FormatBracesOnEnterSplit (false). Off, Enter behaves like any other Enter (`{`, indented line holding the rest of the
  line, so the `}` follows the caret); on, the closer moves to its own line.
* Indent unit (`editor.indentUnit` = `tab`, `editor.detectIndent` off): answered from the 10x documentation: IndentMode
  (Tabs) and AutoDetectIndentMode (false). Detection, when switched on, reads the first 400 lines (tabs vs. the most
  common step). `editor.tabWidth` = 4: TabWidth (4). Plain Tab inserts one unit (a multi-line selection indents its lines),
  Shift+Tab outdents: docs silent. Not done: BackspaceDeletesTabs.
* Auto-close, split like 10x: `editor.autoAddCloseBrace` (`{`, off) and `editor.autoAddCloseBracket` (`( [`, off):
  answered from the 10x documentation: AutoAddCloseBrace (false), AutoAddCloseBracket (false). The closer is only added
  when the next character is blank, end of line or one of `) ] } ; ,`; typing the closer when it is the next character
  steps over it (stateless), Backspace between an empty pair deletes both; these two follow the auto-close setting of
  that pair and are docs silent. `editor.autoCloseQuotes` (`" '`) is tenfold only: 10x documents no setting for
  quotes. Off by default; Visual Studio C++ pairs quotes ("Automatic brace completion") but the row is low confidence (VS_BEHAVIOUR §14 Z1, checklist V12). Quotes are not
  paired after an identifier character, a backslash or the same quote.
* Wrap selection (`editor.bracketSelectedText`, on): answered from the 10x documentation: BracketSelectedText (true,
  "Typing an open bracket/brace char ((,{,[) with selected text will enclose the selection"). The selection stays
  selected (docs silent). Quotes wrap a selection only with `editor.autoCloseQuotes`.
* Comments: Ctrl+/ toggles `//` at the minimum indentation of the selected lines (blank lines untouched; uncomments
  when every non-blank line has it); a selection ending at column 0 does not take that last line. Ctrl+Shift+/ wraps each
  selection in `/* */` (a bare caret: its line's text) and unwraps when already wrapped. C / C++ comment syntax only.
  Docs: 10x has one ToggleComment (line comment without a selection, block with one) plus Commentline / Uncommentline;
  CommentBlockUsesLineComments (true) makes ToggleComment always use `//`, CommentBlockIncrementLine (false) = the caret
  does not move to the next line after commenting (tenfold does not move either). tenfold's two commands stay until the
  command layer is revisited (docs/FEATURE_PARITY.md, "10x settings not yet supported").
* Lines: duplicate (Ctrl+Shift+D: the line below, or the selection copied right after itself and selected), move
  (Alt+Up / Alt+Down), join (Ctrl+J: leading blanks of the next line become one space), delete (Ctrl+Shift+K), go to
  line (Ctrl+G, `line[:column]`, column 0 of the line by default, through the navigation history). Docs silent.
* Bookmarks: Ctrl+F2 toggles, F2 / Shift+F2 step through all bookmarks of the workspace (by path, then line; wrapping),
  `Editor.ListBookmarks` (no default key) lists them; they move with edits like history entries and persist in `.tenfold/bookmarks.json`. The 10x
  commands are ToggleBookmark, PrevBookmark / NextBookmark (all files), PrevBookmarkInFile / NextBookmarkInFile,
  RemoveAllBookmarks, RemoveAllBookmarksFromFile; whether the next/prev commands wrap is docs silent (they wrap, W8d
  added the in-file variants and the per-file clearing).
* Find / replace in the file: Ctrl+F / Ctrl+H, incremental, case-insensitive by default, Alt+C / Alt+W / Alt+R,
  Enter / F3 next, Shift+Enter / Shift+F3 previous, Replace and Replace All (one undo step), Escape closes. 10x settings
  FindTextInitialValue / ReplaceTextInitialValue (Current), CloseFindPanelOnEnter (false), ShowFindTextMatchCount (false):
  not mirrored yet (gap).
* Trailing whitespace: `editor.trimTrailingWhitespace`, off: docs silent (no 10x setting). Line endings LF / CRLF
  switch commands; files in other encodings are converted (see "W9 detail: encodings"); new files (`NewTextFile`) take `editor.defaultLineEnding` (CRLF, as 10x's
  DefaultLineEnding).
* Display: `editor.showWhitespace` off (ShowWhitespace false), `editor.columnGuide` 0 (VerticalRulers empty),
  `editor.wheelLines` 3 (MouseWheelScrollStep 3; also Visual Studio's default, Z7), caret blink (CursorBlinkRate auto = the OS rate; tenfold 530 ms, the
  Windows default), `editor.highlightCurrentLine` on (docs silent; 10x highlights symbols, words and the selection
  instead). `editor.wordWrap` true and `editor.smoothScroll` false differ from 10x (false, true) by the user's decision
  (#4).
* Not implemented: regex group references in Replace. Encodings: see "W9 detail: encodings".

## W8b detail: tab, panel and view commands (10x documents only one line each)

The 10x documentation names these commands and gives one sentence; everything below is tenfold's reading, to be checked
against a running 10x.

* Focus mode (`ToggleFocusMode`, `ToggleFullScreenFocusMode`): the docs say only "Toggle focus mode" / "Toggle fullscreen
  focus mode". tenfold shows nothing but the current panel (its tab strip and editor over the whole window): no status
  bar, top bar, bottom dock or other panels. The layout and splitter sizes are untouched, so toggling back restores the
  window. The fullscreen variant switches both on, or both off when both are on.
* `MovePanelLeft` / `Right` / `Up` / `Down`: "Move the current panel left" is read as the panel trading places with its
  neighbour in that direction; there is no re-docking to the window edge. `MovePanelFocus*` picks the neighbour by position.
* `DuplicatePanel`: a copy of the panel with views of every tab, placed to the right; the focus stays on the original.
  `DuplicatePanelLeft` / `Right` ("and move to the left / right"): the copy opens on that side and takes the focus.
* `MovePrevPanelTab` / `MoveNextPanelTab` stop at the ends (no wrapping); `PrevPanelTab` / `NextPanelTab` wrap round.
* `ZoomIn` / `ZoomOut`: one point per step, session only (the docs give no persistence; EditorFontSize is a setting).
  **VS-EDIT (VS_BEHAVIOUR §14 Z8, implemented): only the editor text is zoomed** (a second renderer at the zoomed size;
  tabs, status bar, menus and pickers keep their size; the terminal follows the editor font as before), the status bar
  shows the zoom as "113 %" while it is not 100 % (Visual Studio's "100 %" box; a click resets it), and **`MouseWheelZoom`
  is mirrored** as `editor.mouseWheelZoom` (default true): Control + wheel over the editor zooms.
* `NewTextFile`: "Untitled N" (VS-EDIT, VS_BEHAVIOUR §14 Z14: `editor.textFileNames` switches to Visual Studio's `TextFileN.txt`; off, low confidence, checklist V13) in the workspace root, line ending `editor.defaultLineEnding` (CRLF), UTF-8. Saving asks for a
  path (relative to the workspace, or absolute) in an input line; it refuses a file that exists (10x's save dialog is not
  documented) and a folder that does not exist. An untitled document is not saved in `layout.json`, is not sent to clangd
  and has no version-control state until it has a file; quitting with it dirty offers Save, which says to use Control S
  first. `AutoNameUntitledFiles`: the first word typed (up to the first blank) names the tab and is the Save As
  suggestion; the file is still untitled until saved.
* `InternetSearch`: the selection of the first caret, else the word under it; a blank run becomes one blank, the text is
  percent-encoded and appended to `editor.internetSearchCommand`. `OpenFolderForActivefile`: Windows `explorer /select,`,
  macOS `open -R`, elsewhere `xdg-open` on the folder (no portable "select").

## W8e detail: build and search commands (10x documents one line each)

* `RebuildActiveWorkspace`: "Rebuild the currently active workspace". Default: the detected build system's own rebuild
  (`msbuild /t:Rebuild`, `cmake --build --clean-first`, `make clean && make`); a custom `buildCommand` has no known clean,
  so a `rebuildCommand` workspace setting (a shell command) is required and the command says so when it is missing.
  `CleanActiveWorkspace` is not implemented.
* `BuildCurrentFile`: "Build the current file". Default: a real compile (not `-fsyntax-only` / `/Zs`): `msbuild
  <project> /t:ClCompile /p:SelectedFiles=<file>` for a solution / `.vcxproj`, the Ninja object target for a build
  directory with `build.ninja`, otherwise the file's compile database command run as is (it writes the command's output).
  The saved file on disk is compiled (no implicit save, like `BuildActiveWorkspace`); `buildCommand` is ignored; a header
  or any file without a project or compile entry reports an error instead of guessing a translation unit.
* `ReplaceInFiles`: "Show the ReplaceInFiles panel". Default: the find-in-files query syntax (options, ` :: globs`) plus
  a replace field and a per-match checklist; every match starts checked; the replacement is literal text (no group
  references, as in the file bar); open documents are edited in place and stay unsaved, closed files are written after
  one confirmation; a match whose line changed since the search is skipped, not forced.
* `ReplaceNextInFile` / `ReplaceAllInFile`: "from the Find.Replace panel". Default: they use the find/replace bar's last
  query, options and replacement, also when the bar is closed or the editor has the focus; next = the first match at or
  after the caret (the selected match first, wrapping). They collide with tenfold's Alt+R regex toggle: the commands win
  when the bar shows its replace row, the toggle wins in the find-only bar and in pickers (Alt+E toggles regex in the bar
  too).
* `NewSearchPanel`: "Open new search panel". 10x has separate result panels 1-4; tenfold has one picker. Default: the
  picker keeps the last three result lists (Alt+PageUp / Alt+PageDown bring them back); `ShowFindInFilesResults1-4` and
  `ToggleFindInFilesResultsPanel1-4` stay unimplemented.
## W8d detail: multi-cursor and navigation commands (10x documents only one line each)

* `MultiCursorMoveLeft` / `MultiCursorMoveRight`: the docs say "Increase" / "Decrease selection end for all multi-cursors",
  with Left the increasing one. tenfold follows the words, not the key names: Left moves the later end of every selection
  (the end of its range, whichever way the selection points) one character later, so a caret becomes a one-character
  selection; Right moves that end one character earlier and stops at the selection's start. If a running 10x turns out to
  extend leftwards with Left, only the two `adjustSelectionEnds` calls in `EditorCommands.cpp` need to be swapped. Plain
  Shift+Left / Right (`SelectLeft` / `SelectRight`) already move the caret end of every selection.
* `SelectAllOccurrences`: "Select all of the find results". With the find bar open and a non-empty query, its text and
  case / whole word / regex toggles are used; otherwise the primary selection's text (single line, case-sensitive, not
  whole word) or, with a bare caret, the word at the caret (whole word, case-sensitive, like FindInFileNextCurrentWord).
  The match at or after the primary caret becomes the primary one. Matches that touch merge into one selection (carets
  never touch).
* `UndoSelectNextMultiCursor`: undoes one `SelectNextMultiCursor` step per press, back to the single caret the first step
  started from; the steps are dropped as soon as the carets or the text changed since the last one (ordinary edits, moves,
  other caret commands), so it never restores a stale caret set.
* `AlignMultiCursor`: "Align all cursors". Spaces are inserted in front of each caret so that its visual column (tabs are
  `editor.tabWidth` stops, one column per code point) equals the rightmost caret's; never tabs, never removal of text. Of
  several carets on one line only the first moves. A selection collapses to its caret end. One undo step.
* `AddCursorAtMousePos`: Control + middle button over an editor view adds a caret at the pointer (the new caret is the
  primary); over anything else (a tab, a picker) the click is left to the widget, so tab closing by middle click still
  works with Control held. A plain middle click is unchanged.
* `PrevLocationCurrentFile` / `NextLocationCurrentFile`: the same history and cursor as `PrevLocation` / `NextLocation`,
  skipping the entries of other files; the live position is only recorded when the move happens. Nothing happens (and the
  history is untouched) when the current file has no entry in that direction.
* `PrevBookmarkInFile` / `NextBookmarkInFile` wrap within the current file (also when it holds a single bookmark, which
  then is revisited); nothing happens in a file without bookmarks. `RemoveAllBookmarksFromFile` /
  `RemoveAllBreakpointsFromFile` act on the current document's file only and need no confirmation (like the all-files
  variants).
* `RestoreRecentlyClosedFile`: a stack of the files whose last tab was closed (30 deep, session only, untitled files
  excluded); closing one of several tabs of the same file does not add an entry. It reopens the newest entry through
  `gotoLocation` (so Back returns) at the caret the file had when it was closed, in the active panel; an entry whose file is
  open again or gone from disk is skipped. Layout restore at start-up does not fill the stack.
* `GotoNextResult` / `GotoPrevResult`: "the last search or build operation" is whichever of a find-in-files search and a
  build started last (`Editor::lastResultSource`); stepping continues from where that list left off. After a build the keys
  walk the build's errors and warnings that have a source location, exactly like `GotoNextBuildWarningOrError` (F8), which
  keeps walking the build whatever came last.
## #5 detail: the workspace explorer (W8f)

The 10x documentation names the tree ("workspace tree", "workspace explorer") in `ToggleWorkspaceExplorer`,
`ShowInWorkspaceTree[WithToggle]`, `TrackActiveFileInWorkspaceTree` and `EnableWorkspaceTreeDragDrop`, and says a 10x
workspace shows files and folders (exclude filters, folder sync). It does not describe the tree's look, keys or default
state, so these are choices, taken from Visual Studio's Solution Explorer:

* Closed at start; a dock on the left (default 22 % of the window), saved in `layout.json` as `"left"`.
* A `.sln` workspace shows solution -> solution folders -> projects -> files (filters from `.vcxproj.filters`, else
  folders on disk); other workspaces show the scanned folder tree, whose file list is the workspace scan's (known source,
  header and text extensions only, ignore rules applied) rather than every file on disk. With several `.sln` files in the
  folder the first (alphabetical) is shown, the one the build and the debugger also pick.
* Sort: folders / filters / projects first, then files, case-insensitive. Visual Studio sorts the same way.
* Not shown: "External Dependencies", references, project properties, a node per configuration.
* `ToggleWorkspaceExplorer` focuses the tree when it opens it (a toggle that left the keys in the editor would need a
  second command to reach the tree). `ShowInWorkspaceTree` also focuses it, because it is the way to jump into the tree.
  Escape with an empty filter hands the keys back; Enter on a file does too.
* `ShowInWorkspaceTreeWithToggle` "if it's already being shown": read as the panel is open and the current file is the
  selection and is on screen. Otherwise it reveals.
* Single click selects and opens the file in the italic preview tab (VS-TABS, T2; `tabs.previewTab`), double click or
  Enter opens it as a normal tab. Typing **jumps** to the next name starting with the letters (VS-TREE, X5: Windows
  type-ahead, the typed text is forgotten after one second); the fuzzy filter is still there behind **Ctrl+;** (10x
  documents neither, so there is no 10x behaviour to keep).
* `TrackActiveFileInWorkspaceTree` (default false in 10x) is `explorer.trackActiveFile`; tracking never takes the keys.
* `EnableWorkspaceTreeDragDrop` is `explorer.dragDrop` (default true, like 10x); drag and drop and the file operations
  (new file / folder, rename, delete, properties) are implemented by VS-TREE (VS_BEHAVIOUR section 11 X6 / X7, C10), see
  "VS-TREE detail" below. The 10x workspace file's "Sync Folder" and exclude filters are not implemented.

### VS-TREE detail: what was decided where Visual Studio's behaviour was not certain

* **Where the state lives.** VS_BEHAVIOUR X9 says `.tenfold/layout.json`; it is a separate `.tenfold/explorer.json`
  because `layout.json` belongs to the Workbench (panels, tabs, dock widths) and the explorer is a separate object that
  is built after it. The file holds the view, Show All Files, the expanded nodes (by the names from the root) and the
  selection.
* **Default open (X8).** Only when no `layout.json` was restored (a first start) and the workspace has a solution that lists
  projects; it never takes the keys. After that the layout remembers, including a closed explorer.
* **Type-ahead (X5).** One second reset (the Windows tree-view default); the same letter again cycles; the search starts at
  the selection (after it for a single letter, at it for a longer prefix) and wraps; only visible rows are searched. The
  filter key is **Ctrl+;**, which is where Visual Studio puts the Solution Explorer search box; filtering remains fuzzy.
* **Show All Files (X2).** Applies to the Solution view only; the extra files come from the workspace scan, not from a
  fresh walk of the disk, and are placed in their folders on disk under the project (merged with a filter of the same name).
* **Delete (X7).** Visual Studio's Delete key in Solution Explorer offers Remove or Delete; tenfold has no "Remove from
  project only", so Delete means delete from disk **and** from the projects. The item goes to the Recycle Bin (Windows,
  SHFileOperationW with FOF_ALLOWUNDO) or the freedesktop.org Trash (Linux; `src/core/Trash`), with the confirmation
  "'x' will be moved to the Recycle Bin." / "... Trash." (wording adapted from Visual Studio's, not verified). macOS has
  no trash here (it needs Objective-C `trashItemAtURL`) and so, like a Linux file system with no usable trash folder,
  asks "'x' will be deleted permanently." and deletes for good.
* **Project files are edited as text.** Rename, move and delete change only the `Include="..."` attribute values (and remove
  whole items) in `.vcxproj` and `.vcxproj.filters`; wildcards and items that need other MSBuild properties are left as
  they are. A new file or folder, and a copy, are not added to any project (VS_BEHAVIOUR C8 "Include In Project" is
  medium-low).
* **F2 and Delete in the tree.** With the tree focused they rename and delete (Visual Studio); in the editor F2 is still
  10x's `NextBookmark`. Alt Enter is 10x's `ToggleFullscreen`, so Properties has no key.
* **Move confirmation text.** "Move 'a.cpp' to 'Lib'?" with OK / Cancel is tenfold's wording; the exact Visual Studio text of
  the "Prompt before moving files to a new location" question was not verified.
## W8f detail: code folding (CollapseRegions)

The 10x documentation gives one sentence per command and one setting (ShowCollapseRegionMargin); nothing on what a
"region" is or how the recursive variants differ. Since the decision of 2026-11-06 ("like Visual Studio" wherever 10x is
silent) the defaults below follow docs/VS_BEHAVIOUR.md section 9 (task W10-OUTLINE, rows marked **VS** are implemented;
the rest is tenfold's reading, to be checked against a running 10x):

* **What a region is.** `{ ... }` blocks spanning at least two lines, multi-line `/* */` comments and `#pragma region` /
  `#pragma endregion` pairs, from the lexer (braces in strings and comments do not count). `#if` / `#endif` blocks and
  runs of `//` comments are not regions (**VS**, O1: C++ outlining covers blocks, comments and pragma regions). Only C and
  C++ files have regions. **VS**: the three C++ outlining options exist as `outline.enabled` (Enable Outlining),
  `outline.pragmaRegions` (Outline Pragma Regions) and `outline.statementBlocks` (Outline Statement Blocks), all `true`.
  Reading of the last one (the VS page does not define "statement block" further): off drops the brace blocks nested in a
  function body (if / for / while / try / bare blocks); function, class, namespace, enum and `extern "C"` bodies stay.
  Not done: user-defined regions (Hide Selection, Ctrl+M Ctrl+H), Stop Outlining.
* **What collapsing hides.** Every line after the first one through the closing line, shown as a marker after the first
  line (`head {...}`); text after the closing brace (`} else {`, `};`) is hidden with it. 10x's exact marker is unknown;
  VS shows a boxed `...`, which the marker box imitates (**VS**, O2). **VS**: resting the pointer on the marker shows the
  hidden code in a tooltip (up to 15 lines, common indentation removed).
* **"The current region"** (`CollapseRegion`, `ToggleCollapseExpandRegion`): the innermost region around the caret line
  that is not collapsed already (so repeating the key walks outwards). `ExpandRegion`: the collapsed region that starts on
  the caret line (the caret is never inside a collapsed one). Toggle already matches VS's Toggle Outlining Expansion
  (O4): a collapsed head opens, otherwise the innermost open region around the caret collapses.
* **"All regions"**: `CollapseAllRegions` / `ExpandAllRegions` act on the outermost level (collapse: regions not
  inside another region; expand: collapsed regions whose first line is visible, nested ones keep their state), the
  `...Recursive` variants on every depth. 10x may mean something else by "recursive".
* **`ToggleCollapseExpandAllRegions`** (Visual Studio's Toggle All Outlining, **VS**, O5, changed): nothing collapsed ->
  collapse everything (all depths); anything collapsed, including a mixed state -> expand everything.
* **Collapse to Definitions** (**VS**, O6, new): the tenfold command `Editor.CollapseToDefinitions`, bound to VS's
  `Control M, Control O` (10x binds only M,M and M,L; it has no such command). It collapses the bodies of functions and
  other members (brace blocks that are not a namespace / class / struct / union / enum / `extern "C"` body and not inside
  another such body) and expands everything else, so types stay open and statement blocks, comments and pragma regions
  inside open types are as in a fresh file. A body is recognised from the text before its `{` (the previous line for
  Allman style); a `template <class T> void f() {` is a function because of the parenthesis.
* **Opening on landing.** A caret that lands in a collapsed region (jump, find, build error, Left/Right/Up/Down onto a hidden
  line is not possible for Up/Down, but is for Left/Right and word moves) expands every region that hides it; so does an edit
  touching a region's lines, including typing at the end of its first line (**VS**, O8: navigation expands the regions
  around the target).
* **`editor.showFoldGlyphs`** mirrors ShowCollapseRegionMargin and now defaults to **true** (**VS**, O3; 10x's default is
  true as well). The golden images that show the gutter were regenerated for it. The margin is VS's: a boxed `-` on
  the first line of an open region with a guide line through the region to a tick on its last line, a boxed `+` on a
  collapsed one; the theme key `foldGlyph` colours it. Not done: highlighting the region under the pointer, double-click
  on the margin. 10x's MarginWidth (16) is not mirrored: the lane is one line height wide.
* **Persistence** (O7, medium-low confidence, so not implemented): folds are per view and per session; they are not
  saved in `layout.json` and are lost when the tab's view is rebuilt. VS opens a file fully expanded as well.
* **Undo** (O9, optional): folding is not part of the undo history.


## W8f detail: context menus (10x documents almost nothing)

The documentation only says `ShowContextMenu` "shows the context menu for the currently selected UI element" (Shift F10,
AppsKey) and mentions right-clicking the workspace node or a project in the workspace tree for Settings... (workspace
settings, the debugger tab). It lists no menu contents, so apart from those two the menus follow Visual Studio's context
menus, the editor the user is switching from. Defaults to confirm against a running 10x:

* Editor menu: Cut / Copy / Paste, the navigation and refactoring commands (Go To Definition, Go To Declaration, Find All
  References, Rename, Quick Info, Switch Header/Source), Format Selection, Toggle Comment, Select All, Toggle Breakpoint,
  Run To Cursor, Revert Change / Show Change on a version-control change, Open in Visual Studio, Show in Workspace Tree,
  Open Containing Folder, Copy Full Path. Only commands that exist are listed; those that cannot run now are greyed out.
* Right-click in the editor moves the caret to the pointer unless the pointer is inside a selection (VS behaviour);
  Shift F10 / AppsKey open the menu at the caret and select its first item.
* Tab menu: Close, Close All But This, Close All But Pinned, Pin / Unpin Tab, Copy Full Path, Open Containing Folder,
  Show in Workspace Tree (the last once the tree exists). Right-clicking a tab activates it. The "close all" items leave
  tabs with unsaved changes open instead of prompting once per tab.
* Status bar segments (position, line ending, configuration, flags, clangd, build) get a one-to-three item menu; this is
  tenfold's own idea, not 10x's or VS's.
* The workspace explorer tree's menu (`EditorApp::explorerMenu`, a context menu provider): a right click selects the
  node under the pointer first (and gives the tree the focus); Shift F10 / AppsKey with the tree focused use the selected
  node and open at its row. File node: Open (the same as Enter), Collapse All, Open Containing Folder, Copy Full Path and,
  for `.c` / `.cc` / `.cpp` / `.cxx`, Build Current File. Node with children (folder, project, solution, root): Expand or
  Collapse, Collapse All, Open Containing Folder, Copy Full Path (when it has a path). The path commands take the node's
  path as their argument (`File.ShowInFolder`, `File.CopyPath`, `Build.BuildCurrentFile`), so the file is not opened first.
  "Build Current File" is offered for any source file of the tree, not only those in a project; a file outside every
  project is reported by the build like the command does. The docs say to right-click the workspace node (to uncheck "Sync
  Folder") or a project (Debug tab, "Sync from project") and choose **Settings...**. tenfold has no workspace or project
  settings dialog (`WorkspaceSettings` is unimplemented, settings are files, OPEN_QUESTIONS #12), so "Settings..." is
  left out rather than opening something else; it appears when such a command exists (add the entry to the provider).
  Expand / Collapse / Collapse All are tree operations with no command, so those items call the view directly.
* Keys inside the menu: arrows, Enter, Escape, first-letter jump; disabled items are skipped by the arrows (Windows
  menus stop on them). A left press runs an item, not the release.
* `ShowContextMenu` pressed with the menu open closes it (a toggle).
* `CloseAllOtherTabs` closes the other unpinned tabs of the active panel (pinned ones stay), `CloseAllButPinnedTabs`
  every unpinned tab of the active panel; 10x's own wording is only "all tabs except for the current tab" / "all
  non-pinned tabs", so whether pinned tabs survive "close all other tabs" is a guess. `CopyActiveFilePath` copies the
  path with the platform's separators.

## W8f detail: paste history, key sequences, Perforce history and p4v (10x documents one line each)

* `PasteHistory`: "Show the paste history dropdown". Size, content and position are not documented. tenfold keeps the last
  20 texts (`editor.pasteHistorySize`, 0 = off) that Copy, Cut and CutLine put on the clipboard, plus a clipboard change by
  another program when the window regains focus; a text already listed moves to the top. The list is the usual modal
  picker (centre of the window, substring filter), not a dropdown anchored at the caret. Enter pastes with the Paste rules
  and also makes the entry the clipboard text. Text copied inside tenfold's own input lines (find bar, pickers) is not
  recorded. The history is not saved between sessions.
* `RecordKeySequence` / `PlaybackKeySequence`: "Start/stop recording a key sequence" / "Playback a key sequence". Whether
  10x records raw keys, whether mouse input and other panels count, whether the recording survives a restart, and whether
  playback is one undo step are not documented. tenfold records commands (as resolved from bindings, chords and the
  palette) and typed text, only while the editor view has the keys, never the record / playback keys and not mouse clicks;
  only the last recording is kept, in memory. Playback is one undo step of the document current when it started. Playback
  is refused while recording.
* `PerforceHistory`: "Show the perforce history for the current file". tenfold shows `p4 filelog` (last 200 revisions) in a
  picker; Enter opens an action menu for the chosen revision (VS-VCS, VS_BEHAVIOUR section 4 VC6, after P4VS's File History):
  Open (read-only tab from a read-only temp file), Diff against previous revision, Diff against have revision (external diff
  tool). Whether 10x diffs instead, or offers both, is unknown (Vcs.DiffFileExternal diffs the working file against `#have`).
* `PerforceShowInP4v`: "Show the current file in p4v". tenfold starts `p4v -p <port> -u <user> -c <client> -s <depot path>`
  with the connection from `p4 info` and the path from `p4 where`; p4v's `-s` option is from p4v's own command-line help,
  not from the 10x docs, and has not been tried against a real p4v (OPEN_QUESTIONS #17 applies).
## W8f detail: terminal panel

The 10x documentation gives one sentence per terminal command ("Create a terminal panel", "Create or show an existing terminal.
Cycle through if there are multiple.", "Create a new Claude Code terminal", "Create a new Cursor terminal") and the settings
TerminalCommand (`cmd.exe`), ClaudeTerminalCommand (`claude`), CursorTerminalCommand (`cmd /c agent`), TerminalFont,
TerminalFontSize. Everything else below is tenfold's choice, to be checked against a running 10x.

* Host: a **tab in an editor panel**. 10x's panels hold tabs and "Create a terminal panel" reads as a panel-tab; the alternative
  (a bottom dock like Build) would give one terminal only and no cycling. A terminal can be dragged to any panel. No setting.
* `ShowTerminal` cycling: creation order; focused terminal -> next (wraps); terminal shown but not focused -> focus it; not
  shown -> the one shown last. `NewTerminal` always creates.
* Default shell (VS-TERM, supersedes the 10x default `cmd.exe` as tenfold's default, VS_BEHAVIOUR §12 M3): `terminal.command` is empty
  by default = `pwsh.exe` if on `PATH`, else `powershell.exe` on Windows, `$SHELL` else `/bin/sh` elsewhere; any value set (including
  10x's TerminalCommand) is authoritative. Windows defaults on other platforms: `terminal.command = cmd.exe` means `$SHELL`, else `/bin/sh`; a leading `cmd /c` is dropped
  (`cmd /c agent` runs `agent`). 10x has no Linux default, this is the guess. Settings are the 10x names in tenfold's
  dotted style: `terminal.command`, `terminal.claudeCommand`, `terminal.cursorCommand`, `terminal.font`, `terminal.fontSize` (0 =
  the editor size; 10x: "the tool font / size", read as tenfold's editor font).
* Working directory: workspace root, else the current file's folder, else home (10x documents none).
* Exit: the tab stays with the final screen and `[process exited with code N]`; `terminal.closeOnExit` (default false) closes it.
  No restart key. Closing a tab kills the process tree without asking even if a program is running; so does quitting.
* Scrollback 10000 lines (`terminal.scrollbackLines`). Copy/paste follows Visual Studio's terminal
  (VS_BEHAVIOUR §12 M1/M2, implemented as VS-TERM): ~~Control C copies a selection (and clears it; no selection = ^C), Control V
  pastes, right click pastes/copies~~ **superseded**: Control C is always ^C and Control V its literal character; copy is Control
  Shift C / Control Insert, paste Control Shift V / Shift Insert / right click (a right click never copies); all are key map
  bindings of the `terminal` context (`Terminal.Copy`, `Terminal.Paste`). `terminal.ctrlCCopiesSelection` (default false) restores
  "Control C copies a selection". The 10x key map documentation defines no terminal copy/paste keys, so nothing there wins. "Open in
  Terminal" in the explorer menu opens the node's folder (M4); the workspace root stays the default. No copy-on-select.
* Keys that stay the app's while a terminal is focused: terminal commands, `Workbench.*` (tab/panel keys), quit, command palette,
  explorer/build-panel/fullscreen/focus-mode/zoom toggles. Everything else, including Control K/F/O/W and Alt+letters, goes to
  the program. Ctrl+Alt letters are treated as AltGr (text), not as a shortcut.
* Look: `vs-dark` terminal background #1E1E1E, foreground #CCCCCC, ANSI palette "Campbell" (Windows console), steady block cursor
  (blinking only when the program asks), bold = bright + double strike, no italic face. Values marked `[?]` in vs-dark.txt (see #19).
* Not done: reflow, search, links, split inside a terminal, persistence across sessions, shell integration, OSC 52, a context menu
  inside the terminal (right click pastes).
* ConPTY is untested at run time (compile-tested only); the first run on Windows may show problems the Linux tests cannot.

## W9 detail: encodings, line endings and the BOM (W9-FIX-ENC; 10x documents nothing about them)

Decided without a user decision because the alternative was the editor terminating; to be confirmed.

* **Detection.** Valid UTF-8 (incl. ASCII, with or without BOM) is UTF-8. A UTF-16 BOM plus well-formed UTF-16 is
  UTF-16 LE/BE. Everything else is **Windows-1252** (the "ANSI" code page 10x and Visual Studio use on Western
  Windows; also covers ISO-8859-1 apart from the C1 range). No other code pages (Shift-JIS, GBK, ...) and no
  BOM-less UTF-16/32: such a file loads as Windows-1252, i.e. readable only as far as its ASCII goes, but it never
  fails and is written back byte for byte. VS-ENC added the status-bar encoding segment, Reopen / Save with Encoding
  and the settings `editor.autoDetectUtf8` and `editor.defaultEncoding` (see "VS-ENC detail" below).
* **Saving.** The file is written in the encoding it was read in; the BOM of UTF-8/UTF-16 files is kept. If the text
  holds a character that encoding cannot represent, the save is **refused** (user decision of 2026-11-06, implemented
  by W10-ENC; this replaced the W9-FIX-ENC default of writing UTF-8 instead). Nothing is written, the document stays
  dirty and keeps its encoding, and the user gets a dialog (`SaveRefusedDialog`) that shows each offending character
  as text with its `U+XXXX`, `line:column`, says that the file was not saved and why (the file's encoding), and
  jumps to the selected character on Enter (`Editor::gotoLocation`); the first 8 are listed, then "and M more"
  (all are counted; the engine keeps the first 200). A status-bar message says the same in one line. What can be
  refused: Windows-1252 cannot hold anything outside its 256 characters (so `漢`, an emoji, `≠` are refused
  while `€` and `é` are fine); UTF-16 holds every Unicode scalar value (surrogate pairs), so only text that is not
  UTF-8 at all is refused there: a stray byte or an encoded lone surrogate (`ED A0 80`), which would otherwise
  silently become U+FFFD; UTF-8 never refuses (it writes such bytes as they are). It applies to every path that
  writes a document: File.Save, File.SaveAll (the other documents are still saved, the refused ones are listed
  together), Save As of an untitled document (the document stays untitled), the save on closing a tab and on quit
  (nothing is closed or quit; the other dirty documents are still saved), and renames that write closed files (the
  file stays open and dirty, `RenameResult::refused`). Replace in files on a **closed** Windows-1252 file converts the
  replacement to Windows-1252 and, if it contains an unencodable character, does not write that file and reports it
  (`ReplaceInFilesResult::refused`; the characters are placed at the matches, columns of the decoded file); open
  documents are edited in memory and are refused when they are saved. The corpus harness restores files byte for byte
  from raw copies and never goes through `Document::saveToDisk`, so it cannot hit this. There is no auto-save and no
  save-before-build in tenfold yet; when they come they must use `Editor::save` and show the same dialog.
  The way out of a refusal besides editing the text is **Save with Encoding...** (VS-ENC; also a row of the dialog).
* **Line endings.** Replaced by the Visual Studio behaviour (user decision of 2026-11-06; see "W10 detail: line
  endings like Visual Studio" below). The old default, one dominant ending per document written for every line on save,
  no longer exists.
* **Find in files / replace in files** still search the raw bytes of a file: for a Windows-1252 file the match text is
  not valid UTF-8 and its columns are raw byte columns. Replacing in a closed Windows-1252 file converts the
  replacement to Windows-1252 first (and refuses unencodable characters, see "Saving"); a UTF-16 file is never
  written by replace in files. Decoding the search itself is a follow-up.
* **Locations in open Windows-1252 files.** clangd's answers that come from data computed on the raw disk bytes (a
  stale preamble of another file, the background index) carry raw byte columns, while the open document is decoded;
  such a location can be a few columns off in a line with non-ASCII bytes before the symbol, until clangd reparses.
  Fixing it fully needs either the UTF-16 offset encoding (full-text sync per keystroke) or a token check.
* **Version control** (`src/vcs`, not touched by W9-FIX-ENC) splits the committed text at LF/CRLF only and compares it
  with the buffer as raw bytes: for a Windows-1252 or UTF-16 file the gutter marks every non-ASCII line as changed (the
  CR-only part was fixed by W10: `splitLines` uses `TextBuffer`'s line rule). The fix is the same decode (`decodeFileBytes`) and `TextBuffer`'s line rule applied to
  the base text.

## W10 detail: line endings like Visual Studio (W10-LE)

Implements docs/VS_BEHAVIOUR.md section 1.3. Where Visual Studio's behaviour is established (high / medium confidence) it is
followed; the rows below are the places where it is not, so the most plausible reading was chosen. Each is one
constant, branch or setting, easy to change.

* **Implemented as documented:** per-line terminators (CRLF / LF / CR), load as is, save as stored (no normalisation), the
  load check (`editor.checkLineEndings`, default on) with VS's message text and the Yes / No answer (Yes = one undoable
  edit, document modified; No = unchanged, not asked again until reopened), Enter / generated lines replicating the
  buffer's ending (`editor.replicateNewLine`, default on), paste keeping the pasted breaks, copy putting the stored breaks
  on the clipboard, a status-bar indicator, Save with Line Endings (Advanced Save Options).
* **Which line supplies the ending on Enter (L6, low):** the line the caret is on (the first line of a selection that is
  replaced); the last line has no ending, so the line before it; a document without any break uses
  `editor.defaultLineEnding`. `TextBuffer::endingForNewLine`, `Editor::newLineEndingAt`.
* **Empty / break-less buffer (L6, L15):** `editor.defaultLineEnding`, default CRLF (VS's `NewLineCharacter` is `\r\n`;
  10x's DefaultLineEnding is CRLF). On Linux and macOS a user who wants LF in new files sets `editor.defaultLineEnding =
  LF`; a platform-dependent default was not chosen so that the settings file means the same everywhere. Note that a
  loaded one-line file without a trailing newline also counts as a buffer without a break. **To confirm:** CRLF as the
  default on Linux.
* **`.editorconfig` `end_of_line` (L13):** tenfold reads no `.editorconfig`, so it is skipped. When `.editorconfig` support
  arrives it replaces `defaultLineEnding` for new lines of files under the config (not the replicated ending of a file that
  already has breaks, matching "VS does not convert existing styles").
* **Pre-selected ending in the dialog (L4, unknown):** the document's most frequent ending (ties LF, then CRLF).
  Buttons: VS has a drop-down plus Yes / No; tenfold's modal list has one "Yes: normalize line endings to <ending>" row
  per ending plus "No" (a list popup has no drop-down). Escape and a click outside are No. The "do not show again"
  check box is not drawn: switch `editor.checkLineEndings` off in the settings file.
* **Unicode separators (L1, L4):** NEL, LS and PS are not line breaks and are not offered by the dialog (VS_BEHAVIOUR 1.3
  item 8: clang / clangd would disagree on columns). A file using them keeps them as ordinary characters.
* **Paste (L8, medium-low):** the Learn text says VS keeps the pasted breaks and one report says it does not; the Learn text
  is followed (`editor.pasteKeepsLineBreaks`, default true; false converts to the Enter ending). Multi-caret paste of N
  lines splits at any break. **To verify (V3).**
* **Copy (L9):** the selection is put on the clipboard with the stored breaks (several selections are joined with the
  document's dominant ending); without a selection the caret line with its own break (the last line: the ending the
  document would use). tenfold does not convert LF to CRLF for other applications on Windows.
* **Status bar label for a mixed file (L14, unknown, V1):** the dominant ending (`CRLF` / `LF` / `CR`), as for any file;
  `lineEndingLabel` in `Workbench.cpp` has a constant `kShowMixedLabel` that shows `Mixed` instead. A click opens the
  existing status menu (Windows (CR LF), Unix (LF), Macintosh (CR), Save with Line Endings...); the choice converts the
  whole document as one undo step (VS's own click behaviour is unknown).
* **Save with Line Endings (L11):** converts the document (an undoable edit, as the status-bar conversion) and saves it;
  after the save the buffer holds the new endings, and the next plain Save keeps them ("not a persistent option" in VS
  terms). Save with Encoding (VS-ENC) is the same flow with the encoding added (see "VS-ENC detail").
* **Commands that rewrite lines (move line, comment toggle over several lines):** their replacement text takes the ending of
  the first line of the edited range, so in a mixed file the lines they rewrite become uniform. Edits that copy buffer text
  (comment a selection, duplicate a selection, wrap in brackets, change case) keep each line's own ending.
* **Reload of a changed file:** line-ending-only changes on disk are picked up (the diff compares terminators); the load
  check is not repeated (VS: on load only).
* **Version control:** the gutter compares lines without terminators (as before), so a line whose only difference is its
  ending is never a changed line; the base text now splits at a lone CR too. The external diff tool gets a mixed buffer
  with its dominant ending only.
* **clangd:** gets CRLF and LF lines as they are and lone CR as LF (see ARCHITECTURE, "Text for clangd"), so lines and
  columns always agree.


## VS-ENC detail: encodings like Visual Studio (implemented; VS_BEHAVIOUR section 2, rows E1-E6)

Implemented from the user's rule "for all other unanswered questions match Visual Studio behaviour if possible"; 10x
documents nothing here except `DefaultEncoding`. Choices where VS's behaviour is unknown or tenfold deviates:

* **Status segment (E3).** Labels `UTF-8`, `UTF-8 with signature`, `UTF-16 LE`, `UTF-16 BE`, `Windows-1252`. VS names the
  code page (`Western European (Windows) - Codepage 1252`); the short name was chosen for the status bar. A click with
  either mouse button opens a two-item menu (VS opens the encoding dialog directly; the menu is the task's choice).
* **UTF-8 signature.** It stays part of the text of a file that has one (columns of line 1 count it, as before), so
  "with signature" is derived. Save with Encoding "UTF-8" on such a file removes it (an undoable edit of line 1);
  "UTF-8 with signature" on a file without one makes the save write it (no text edit). VS lets one reopen with
  "UTF-8 with signature" too; here Reopen lists a single UTF-8 row because the signature is a property of the bytes
  (the label then shows which one the file has).
* **Reopen (E4).** Lists the encodings the codec has (no code-page list). The dialog for a modified document is the
  existing Save / Don't Save / Cancel prompt (VS's wording unknown). Reopening as UTF-16 a file that is not well-formed
  UTF-16 fails with a message; reopening as UTF-8 a file with stray bytes keeps the bytes (saving writes them back; VS
  shows U+FFFD and loses them). UTF-16 chosen for a file without BOM is written back with a BOM. An external-change
  reload detects the encoding again (the reopen choice is not remembered).
* **Save with Encoding (E5).** One list: Save on top, then the Encoding and Line endings groups, Enter selects a row and shows
  the list again (VS has two combo boxes and OK/Cancel). Refusals use the existing dialog and change nothing, including
  the requested line endings. After a successful save the document keeps the new encoding. A failure after the
  conversion (read-only file, I/O error) leaves the document converted and modified. `File.SaveWithLineEndings`
  is kept as the line-endings-only list. **The SaveRefusedDialog row** "Save with Encoding..." (one refused file only)
  starts the list with UTF-8 selected; VS has no such dialog, so it is a tenfold convenience.
* **Detection (E1/E2).** `editor.autoDetectUtf8` (default true, medium confidence for VS's default). The fast index,
  clangd's file reads and the corpus runner keep using automatic detection. On Windows the fallback should be `GetACP()`;
  the codec only has Windows-1252, so another system page (e.g. 932) is read as 1252 and logged once: a
  Shift-JIS file shows mojibake instead of Japanese. Implementing it with `MultiByteToWideChar` /
  `WideCharToMultiByte` (`WC_NO_BEST_FIT_CHARS` and a lossless round-trip check so refusal still works) is the follow-up.
* **Default for new files (E6).** `editor.defaultEncoding`, default `utf-8` (10x's DefaultEncoding default); `utf-8-bom`,
  `utf-16le`, `utf-16be`, `windows-1252` also accepted, anything else means `utf-8`. An untitled document with a non-UTF-8
  default refuses to Save As text it cannot hold, like any document.
* **`.editorconfig` `charset`:** not read (tenfold reads no `.editorconfig`). Skipped.

## W9 detail: Visual Studio configurations (W9-FIX-vs)

Nothing here blocks the work; these are the assumptions to confirm.

* **A project the solution maps but does not build (`.ActiveCfg` without `.Build.0`) is evaluated for navigation** with
  the mapped project configuration, which is what Visual Studio's IntelliSense does as far as I know (the "Build"
  checkbox of the Configuration Manager does not hide a project from IntelliSense). Not verified on a Windows machine
  with Visual Studio. If wrong, the change is one line in `compdb::fromSolution` (`map->build`).
* **Indexes kept per configuration: 4.** Each visited configuration keeps a clangd background index under
  `.tenfold/vs/<configuration>/.cache` (size of one project index; hundreds of MB on a big solution). The limit is
  `SemanticEngine::Options::maxVsConfigIndexes` (not yet a `workspace.json` key). Say if it should be a setting or
  smaller by default.
* **Off Windows the `.sln` route uses the host's standard library**, with `-m32`/`-m64` dropped, `_MSC_VER=1933` defined
  and `-fms-extensions` on. Verified with libstdc++ on Linux; macOS (libc++, `-isysroot`) is wired the same way but was
  not run. The Windows SDK (`<windows.h>`) stays unavailable there.

## VS-NAV detail: navigation like Visual Studio (VS_BEHAVIOUR sections 6, 10, 13)

What was decided where Visual Studio's behaviour was not certain, and what stays on the verification checklist.

* **Go To Document (N1) takes the whole directive.** The caret may be anywhere from the `#` to just after the closing
  delimiter, also on the word `include`; VS documents only that Go To Document works on an `#include` line, not the exact
  caret range. Applies to Go To Definition and Go To Declaration (both are "the file" there); Go To Type Definition and
  Find All References are unchanged. The answer is **authoritative at once** when the include search path of the
  includer's compile command finds the file (it is a file-system fact, not a parse); only a spelling on no search path is
  approximate (workspace file whose path ends with the spelling, nearest first) and then asked of clangd. The caret goes to
  line 1, column 1. A missing header answers nothing ("No results"). A context-menu item "Open Document" (VS has one on the
  editor menu) is not added: menus belong to VS-MENUS.
* **Find All References (N5).** VS's default grouping is "Project, then Definition"; tenfold has no projects in the list, so
  "Definition, then Path" (one of VS's own groupings) is used. The "Definition" group holds the places where the fast index
  has a declaration or definition of that name on that line. Read / Write is derived from the line's text (VS_BEHAVIOUR N5
  says only that the C++ column is Read/Write/Other): Write = assigned, compound-assigned, incremented / decremented (also
  through subscripts), Other = preprocessor lines (not conditions), using / typedef / namespace / class / struct / enum
  lines, Read = the rest. **Verify against VS (checklist V10):** how VS classifies calls, declarations and type uses.
  The incremental delivery, scope list, filter, Keep Results and the Disconfirmed class are not done (a modal picker, clangd
  has no "disconfirmed").
* **Misc files (I5).** "A file in no translation unit" = no entry of the compile database for it, no TU context for it and
  not reachable by #include (resolved like `compdb::filesReachableFrom`, every #if branch counts) from any entry. With no
  compile database nothing is a misc file. While the fast index is still reading, nothing is (squiggles stay). The rule
  hides the clangd diagnostics and the build diagnostics are untouched. Default false = VS; `true` shows them.
* **Preview switch (P1).** VS's switch is an eye icon; tenfold writes "Preview: on/off" (no icon font is assumed). The state
  is kept for the session (not written to the settings file); reading the settings again (a change in the file) resets
  it to `picker.preview`.
* **Not changed on purpose:** F12 on a definition (N2) and with several candidates (N3) stay as they are (low confidence in
  VS's behaviour, checklist V9); virtual-function references (N6) and range-for begin/end (N7) stay clangd's; Alt+F12 Peek
  is not bound (10x has no Peek; N4 "keep").
