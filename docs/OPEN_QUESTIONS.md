# Open questions for the user

Items the implementation needs verified against a running copy of 10x (or
decided by the user). Each has a default we implement until answered.

| # | Question | Default we use meanwhile | Plan ref |
|---|---|---|---|
| 1 | Exact key bindings in your 10x setup (export `KeyMappings.10x_settings` if possible) | **Answered:** handle key bindings the same way as 10x (its settings-file format) with 10x's default bindings. **Blocked on material:** the 10x documentation site is not reachable from the build environment and the defaults must not come from memory; needed: 10x's default key bindings file (plus its command list), or network access to 10xeditor.com. Placeholders stay until then | B4b |
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
| 15 | Visual Studio connection at startup: should tenfold connect to Visual Studio on its own? | tenfold attaches at startup (and retries every 10 s while breakpoints exist) only to a Visual Studio that already has the workspace's solution open; it never launches Visual Studio until you press a debug key (F5 etc.). Setting `debug.attachAtStartup` in `.tenfold/workspace.json` (default on) | C2b2 |
| 16 | Version-control marks for new / untracked files: Visual Studio paints the whole file as added; should tenfold? | no marks for untracked files; a file staged as new (or opened for add in Perforce) shows as one added block | C3c |
| 17 | Perforce: please sanity-check the commands tenfold relies on against your server (listed in docs/ARCHITECTURE.md, "Perforce"), in particular `p4 -ztag -F %clientRoot% info`, `p4 where <dir>/...` for an unmapped directory, and what `p4 edit` prints / returns for an exclusively locked file | implemented against a fake `p4` only; no real server was available | C3b |
| 18 | Licence for tenfold itself (the repository has no LICENSE file; packages ship third-party notices only) | none chosen; packaging picks up `LICENSE` automatically once it exists | D1 |
| 19 | Visual Studio 2022 Dark colours: the `vs-dark` theme was written from memory (values marked `[?]` in assets/themes/vs-dark.txt: panel background, tabs, scrollbars, squiggles, diff and debugger colours). Corrections or a screenshot would let us match exactly | current values | B7b, C2c, C3c |

## #8 detail: editing behaviours and their 10x settings

Settings are `editor.*` in `settings.txt`; key bindings are in `assets/keybindings.default.txt` (#1). "Answered from the
10x documentation: X" means tenfold's default equals the documented default of the 10x setting X (B8b); "docs silent" means
10x's documentation does not describe the behaviour, so the value is still a guess.

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
  Ctrl+Shift+F2 lists them; they move with edits like history entries and persist in `.tenfold/bookmarks.json`. The 10x
  commands are ToggleBookmark, PrevBookmark / NextBookmark (all files), PrevBookmarkInFile / NextBookmarkInFile,
  RemoveAllBookmarks, RemoveAllBookmarksFromFile; whether the next/prev commands wrap is docs silent. In-file stepping and
  clearing are not implemented (gap).
* Find / replace in the file: Ctrl+F / Ctrl+H, incremental, case-insensitive by default, Alt+C / Alt+W / Alt+R,
  Enter / F3 next, Shift+Enter / Shift+F3 previous, Replace and Replace All (one undo step), Escape closes. 10x settings
  FindTextInitialValue / ReplaceTextInitialValue (Current), CloseFindPanelOnEnter (false), ShowFindTextMatchCount (false):
  not mirrored yet (gap).
* Trailing whitespace: `editor.trimTrailingWhitespace`, off: docs silent (no 10x setting). Line endings LF / CRLF
  switch commands; encoding is UTF-8 only; new-file line ending (DefaultLineEnding, CRLF) has no tenfold counterpart
  because tenfold cannot create untitled files.
* Display: `editor.showWhitespace` off (ShowWhitespace false), `editor.columnGuide` 0 (VerticalRulers empty),
  `editor.wheelLines` 3 (MouseWheelScrollStep 3), caret blink (CursorBlinkRate auto = the OS rate; tenfold 530 ms, the
  Windows default), `editor.highlightCurrentLine` on (docs silent; 10x highlights symbols, words and the selection
  instead). `editor.wordWrap` true and `editor.smoothScroll` false differ from 10x (false, true) by the user's decision
  (#4).
* Not implemented: regex group references in Replace, non-UTF-8 encodings.
