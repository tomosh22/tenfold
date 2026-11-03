# Open questions for the user

Items the implementation needs verified against a running copy of 10x (or
decided by the user). Each has a default we implement until answered.

| # | Question | Default we use meanwhile | Plan ref |
|---|---|---|---|
| 1 | Exact key bindings in your 10x setup (export `KeyMappings.10x_settings` if possible) | **Answered:** handle key bindings the same way as 10x, with the same defaults. **Implemented (B4b; source: the 10x documentation, Commands / KeyMappings / KeyBindings / SettingsFiles):** key files are `.10x_settings` files (`Control Shift F: Search`, chords separated by `, `), command names are 10x's (tenfold ids stay valid aliases), `assets/KeyMappings.default.10x_settings` is 10x's complete default key map plus a marked tenfold-additions section, an installed 10x's `%APPDATA%\10x\Settings\KeyMappings.10x_settings` is imported below your tenfold file (setting `keys.import10x`, default on), your tenfold file is `<user config dir>/tenfold/KeyMappings.10x_settings` and the workspace's `.tenfold/KeyMappings.10x_settings`. Python statements are reported as unsupported; the old tenfold format is no longer read. 10x commands tenfold lacks stay bound and say `not implemented yet: <Command>` (list: FEATURE_PARITY, "10x commands not yet implemented"). Remaining question: if your 10x key file differs from the documented defaults, `keys.import10x` picks it up; send it if some binding should become a default | B4b |
| 2 | Which Visual Studio configuration/platform 10x uses for parsing by default | **Implemented (C5):** the workspace setting `vsConfiguration`/`vsPlatform` in `.tenfold/workspace.json` (default Debug / x64) when the solution lists it; otherwise Debug\|x64 if the solution has it, otherwise the first configuration of the solution (the engine logs the fallback). `Editor::selectVsConfiguration` / `tenfold-cli vsconfig --set` change it, regenerate the compile commands and restart clangd. Each project is evaluated with the configuration the solution maps it to; projects the solution does not build there are skipped | A2, C5 |
| 3 | Undo grouping: does 10x undo a whole typed word or one character? And does a typed space end the word it follows (so "hello world" undoes as "world", then "hello ") or start the next one? | **Still open: the 10x documentation does not describe undo grouping** (Undo: "Undo the last operation"). Meanwhile: word-level grouping with 1 s pause boundary; whitespace joins and then closes the current step. Settings `undoGrouping` ("word"/"char") and `undoGroupPauseMs` in `.tenfold/workspace.json` | A5 |
| 4 | Does 10x soft-wrap long lines by default? Smooth scrolling? | **Answered:** word wrap on by default, smooth scrolling off by default (`editor.wordWrap`, `editor.smoothScroll`) | B2c |
| 5 | Tab/panel behaviours: pinned tabs, MRU Ctrl+Tab order, drag between panels. Also: does closing the last tab of a panel close the panel? Does closing a dirty tab prompt to save? Does split duplicate the current tab or move it? | **Answered from the 10x documentation:** pinned tabs exist (commands PinTab, CloseAllButPinnedTabs); Ctrl+Tab opens a switch-file panel over all files by default (setting SwitchFileMode = All, or only the current panel's files); duplicating a panel exists (DuplicatePanel, DuplicatePanelLeft / Right), which fits "split duplicates the active tab"; closing a modified file prompts to save, since RestoreUnsavedFiles is false by default ("If true then it won't prompt to save unsaved files") = `editor.confirmUnsaved` true. **Still open (docs silent):** MRU order of the switch panel, whether an emptied panel closes, drag between panels, and files changed on disk. tenfold: all three tab behaviours implemented; an emptied panel auto-closes after a drag but not after a manual close; split duplicates the active tab. Save prompt (P2): closing the last view of a modified tab, closing the window and quitting ask Save / Don't Save / Cancel (Save All / Don't Save / Cancel for several documents; setting `editor.confirmUnsaved`, default true). Files changed on disk (P2, a guess, 10x says nothing): reload automatically unless the document has unsaved edits, then ask Reload (lose my changes) / Keep my version (setting `editor.reloadExternalChanges` = `auto` \| `prompt` \| `never`, default `auto`). | B3 |
| 6 | Does symbol search show a preview of the target line? | **Still open: the 10x documentation does not say** (FindSymbol: "Find a symbol in the workspace"; the only related setting is FindSymbolFuzzyMatching, default true, which tenfold's search already does). Meanwhile yes (`picker.preview`) | B5 |
| 7 | Default theme colours and font (screenshots of your 10x would be ideal) | **Answered:** dark theme that looks as close to Visual Studio (2022 Dark) as possible: `vs-dark` theme is the default | B7b |
| 8 | Which editing conveniences you rely on (auto-close brackets, comment toggle, bookmarks, ...) and how 10x behaves for each | **Partly answered from the 10x documentation (B8b):** the editing defaults now follow 10x's settings, see the list below (each item names the 10x setting or says the documentation is silent); the rest of the question (which conveniences you rely on) stays open | B8, B8b |
| 9 | How you debug from 10x (external debugger launch vs integrated) | **Answered:** strong integration with the Visual Studio debugger (drive VS through its automation interface: start/stop/step, breakpoints synced both ways, follow the current statement) | C2a–C2c |
| 10 | Perforce/Git operations you use from inside 10x | **Answered:** Git status/diff/revert; Perforce auto-checkout on edit, plus status/diff/revert; inline diffs for Perforce files; p4merge launched for a full diff when installed | C3a–C3c |
| 11 | Do you use 10x's Python scripting API? | **Answered:** no; excluded completely (C4 dropped) | C4 |
| 12 | Any `.10x` workspace files or settings you want imported | **Answered:** no `.10x` import; the only workspace settings that matter come from the open project's Visual Studio solution (defines, include paths, compiler options per configuration) | C5 |
| 13 | Target platforms and priority (Windows first?) | **Answered:** Windows first, then Linux (macOS best effort, kept building in CI) | A1/A2/B9 |
| 14 | Code formatting: the existing code is hand-formatted in a compact style that `.clang-format` cannot reproduce (986 residual hunks; even one-line edits reflow neighbouring statements). Adopt clang-format wholesale with one reformatting commit, or keep the hand style? | **Answered:** adopt clang-format and reformat. Whole tree reformatted once (`examples/` and `tests/fixtures/` excluded: tests depend on their line/column positions); CI format job is blocking | A7 |
| 15 | Visual Studio connection at startup: should tenfold connect to Visual Studio on its own? | **Partly answered from the 10x documentation (VSDebugger, setting `VisualStudioSync`):** 10x connects to / launches Visual Studio when the debugger is started, keeps breakpoints in sync, and before the debugger starts makes Visual Studio's solution configuration and platform match 10x's. tenfold does the same (C2d): the workspace setting `debug.visualStudioSync` in `.tenfold/workspace.json` (default on, mirrors `VisualStudioSync`; off = no breakpoint sync, no configuration sync, no attach at startup, commands still work), and start / start without debugging / restart / run to cursor first activate the solution configuration `Workspace::activeVsConfiguration()` in Visual Studio. As an addition tenfold also attaches at startup (and retries every 10 s while breakpoints exist) to a Visual Studio that already has the workspace's solution open, so breakpoints reach it early; it never launches Visual Studio before you press a debug key. That addition is behind `debug.attachAtStartup` (default on). Still open: whether the startup attach is wanted at all | C2b2, C2d |
| 16 | Version-control marks for new / untracked files: Visual Studio paints the whole file as added; should tenfold? | no marks for untracked files; a file staged as new (or opened for add in Perforce) shows as one added block | C3c |
| 17 | Perforce: please sanity-check the commands tenfold relies on against your server (listed in docs/ARCHITECTURE.md, "Perforce"), in particular `p4 -ztag -F %clientRoot% info`, `p4 where <dir>/...` for an unmapped directory, and what `p4 edit` prints / returns for an exclusively locked file | implemented against a fake `p4` only; no real server was available | C3b |
| 18 | Licence for tenfold itself (the repository has no LICENSE file; packages ship third-party notices only) | none chosen; packaging picks up `LICENSE` automatically once it exists | D1 |
| 19 | Visual Studio 2022 Dark colours: the `vs-dark` theme was written from memory (values marked `[?]` in assets/themes/vs-dark.txt: panel background, tabs, scrollbars, squiggles, diff and debugger colours). Corrections or a screenshot would let us match exactly | current values | B7b, C2c, C3c |

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
  that pair and are docs silent. `editor.autoCloseQuotes` (`" '`, off) is tenfold only: 10x documents no setting for
  quotes; off is the conservative choice. Quotes are not paired after an identifier character, a backslash or the same quote.
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
  switch commands; encoding is UTF-8 only; new files (`NewTextFile`) take `editor.defaultLineEnding` (CRLF, as 10x's
  DefaultLineEnding).
* Display: `editor.showWhitespace` off (ShowWhitespace false), `editor.columnGuide` 0 (VerticalRulers empty),
  `editor.wheelLines` 3 (MouseWheelScrollStep 3), caret blink (CursorBlinkRate auto = the OS rate; tenfold 530 ms, the
  Windows default), `editor.highlightCurrentLine` on (docs silent; 10x highlights symbols, words and the selection
  instead). `editor.wordWrap` true and `editor.smoothScroll` false differ from 10x (false, true) by the user's decision
  (#4).
* Not implemented: regex group references in Replace, non-UTF-8 encodings.

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
  tenfold has one font for the whole UI, so zooming scales the tabs and status bar too. `MouseWheelZoom` is not mirrored.
* `NewTextFile`: "Untitled N" in the workspace root, line ending `editor.defaultLineEnding` (CRLF), UTF-8. Saving asks for a
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
* Single click selects, double click opens (Visual Studio's preview-on-click is not done). Typing filters instead of
  jumping to the next name starting with the letters.
* `TrackActiveFileInWorkspaceTree` (default false in 10x) is `explorer.trackActiveFile`; tracking never takes the keys.
* `EnableWorkspaceTreeDragDrop`, the 10x workspace file's "Sync Folder" and exclude filters, and file operations (new,
  rename, delete, show in file manager from the tree) are not implemented.
## W8f detail: code folding (CollapseRegions)

The 10x documentation gives one sentence per command and one setting (ShowCollapseRegionMargin); nothing on what a
"region" is or how the recursive variants differ. tenfold's reading, to be checked against a running 10x:

* **What a region is.** `{ ... }` blocks spanning at least two lines, multi-line `/* */` comments and `#pragma region` /
  `#pragma endregion` pairs, from the lexer (braces in strings and comments do not count). `#if` / `#endif` blocks and
  runs of `//` comments are not regions. Only C and C++ files have regions.
* **What collapsing hides.** Every line after the first one through the closing line, shown as a marker after the first
  line (`head {...}`); text after the closing brace (`} else {`, `};`) is hidden with it. 10x's exact marker is unknown.
* **"The current region"** (`CollapseRegion`, `ToggleCollapseExpandRegion`): the innermost region around the caret line
  that is not collapsed already (so repeating the key walks outwards). `ExpandRegion`: the collapsed region that starts on
  the caret line (the caret is never inside a collapsed one).
* **"All regions"**: `CollapseAllRegions` / `ExpandAllRegions` act on the outermost level (collapse: regions not
  inside another region; expand: collapsed regions whose first line is visible, nested ones keep their state), the
  `...Recursive` variants on every depth. 10x may mean something else by "recursive".
* **`ToggleCollapseExpandAllRegions`** (Visual Studio's Toggle All Outlining): when every region is collapsed or hidden by a
  collapsed one it expands everything, otherwise it collapses everything (all depths).
* **Opening on landing.** A caret that lands in a collapsed region (jump, find, build error, Left/Right/Up/Down onto a hidden
  line is not possible for Up/Down, but is for Left/Right and word moves) expands every region that hides it; so does an edit
  touching a region's lines, including typing at the end of its first line.
* **`editor.showFoldGlyphs`** mirrors ShowCollapseRegionMargin (10x default true) but defaults to **false**: turning it on
  widens the gutter, which would change every existing golden image and the text position. A collapsed region always
  has its marker, which can be clicked. Decide whether the default should follow 10x. 10x's MarginWidth (16) is not mirrored:
  the lane is one line height wide.
* Folds are per view and per session; they are not saved in `layout.json` and are lost when the tab's view is rebuilt.
