# Open questions for the user

Items the implementation needs verified against a running copy of 10x (or
decided by the user). Each has a default we implement until answered.

| # | Question | Default we use meanwhile | Plan ref |
|---|---|---|---|
| 1 | Exact key bindings in your 10x setup (export `KeyMappings.10x_settings` if possible) | **Answered:** handle key bindings the same way as 10x (its settings-file format) with 10x's default bindings. **Blocked on material:** the 10x documentation site is not reachable from the build environment and the defaults must not come from memory; needed: 10x's default key bindings file (plus its command list), or network access to 10xeditor.com. Placeholders stay until then | B4b |
| 2 | Which Visual Studio configuration/platform 10x uses for parsing by default | **Implemented (C5):** the workspace setting `vsConfiguration`/`vsPlatform` in `.tenfold/workspace.json` (default Debug / x64) when the solution lists it; otherwise Debug\|x64 if the solution has it, otherwise the first configuration of the solution (the engine logs the fallback). `Editor::selectVsConfiguration` / `tenfold-cli vsconfig --set` change it, regenerate the compile commands and restart clangd. Each project is evaluated with the configuration the solution maps it to; projects the solution does not build there are skipped | A2, C5 |
| 3 | Undo grouping: does 10x undo a whole typed word or one character? And does a typed space end the word it follows (so "hello world" undoes as "world", then "hello ") or start the next one? | word-level grouping with 1 s pause boundary; whitespace joins and then closes the current step. Settings `undoGrouping` ("word"/"char") and `undoGroupPauseMs` in `.tenfold/workspace.json` | A5 |
| 4 | Does 10x soft-wrap long lines by default? Smooth scrolling? | **Answered:** word wrap on by default, smooth scrolling off by default (`editor.wordWrap`, `editor.smoothScroll`) | B2c |
| 5 | Tab/panel behaviours: pinned tabs, MRU Ctrl+Tab order, drag between panels. Also: does closing the last tab of a panel close the panel? Does closing a dirty tab prompt to save? Does split duplicate the current tab or move it? | all three implemented; an emptied panel auto-closes after a drag but not after a manual close; no save prompt yet (dirty documents stay open in the Editor); split duplicates the active tab | B3 |
| 6 | Does symbol search show a preview of the target line? | yes | B5 |
| 7 | Default theme colours and font (screenshots of your 10x would be ideal) | **Answered:** dark theme that looks as close to Visual Studio (2022 Dark) as possible: `vs-dark` theme is the default | B7b |
| 8 | Which editing conveniences you rely on (auto-close brackets, comment toggle, bookmarks, ...) and how 10x behaves for each | all listed in B8 are implemented, each behaviour that may differ from 10x behind a setting; see the list below | B8 |
| 9 | How you debug from 10x (external debugger launch vs integrated) | **Answered:** strong integration with the Visual Studio debugger (drive VS through its automation interface: start/stop/step, breakpoints synced both ways, follow the current statement) | C2a–C2c |
| 10 | Perforce/Git operations you use from inside 10x | **Answered:** Git status/diff/revert; Perforce auto-checkout on edit, plus status/diff/revert; inline diffs for Perforce files; p4merge launched for a full diff when installed | C3a–C3c |
| 11 | Do you use 10x's Python scripting API? | **Answered:** no; excluded completely (C4 dropped) | C4 |
| 12 | Any `.10x` workspace files or settings you want imported | **Answered:** no `.10x` import; the only workspace settings that matter come from the open project's Visual Studio solution (defines, include paths, compiler options per configuration) | C5 |
| 13 | Target platforms and priority (Windows first?) | **Answered:** Windows first, then Linux (macOS best effort, kept building in CI) | A1/A2/B9 |
| 14 | Code formatting: the existing code is hand-formatted in a compact style that `.clang-format` cannot reproduce (986 residual hunks; even one-line edits reflow neighbouring statements). Adopt clang-format wholesale with one reformatting commit, or keep the hand style? | **Answered:** adopt clang-format and reformat. Whole tree reformatted once (`examples/` and `tests/fixtures/` excluded: tests depend on their line/column positions); CI format job is blocking | A7 |

## #8 detail: what B8 implemented (confirm or correct)

Defaults and settings (`editor.*` in `settings.txt`); key bindings are placeholders (see #1) in `assets/keybindings.default.txt`.

* Auto-indent on Enter (`editor.autoIndent`, on): copy the previous line's indentation; one extra level after a line
  ending in `{`, `(` or `[`, and after a line ending in a binary operator (`+ - * / % & | ^ = ?` or `<<`); a statement
  that was continued by an operator returns one level when it ends with `;`; Enter between `{` and `}` opens a block
  with the closer on its own line; typing `}` as the first non-blank character dedents to the line of its `{`.
  Not done: indenting the body of a brace-less `if` / `for`, aligning continuation lines under an open paren, `case`.
* Indent unit (`editor.indentUnit` = `tab` or a space count, default 4; `editor.detectIndent` on): detected per
  document from its first 400 lines (tabs vs. the most common step), else the setting. Plain Tab inserts one unit
  (a multi-line selection indents its lines), Shift+Tab outdents.
* Auto-close (`editor.autoClosePairs`, on): `( [ { " '`; the closer is only added when the next character is blank,
  end of line or one of `) ] } ; ,`; quotes are not paired after an identifier character, a backslash or the same
  quote; typing the closer when it is the next character just steps over it (stateless: it does not remember which
  closers were auto-inserted); a selection is wrapped and stays selected; Backspace between an empty pair deletes both.
* Comments: Ctrl+/ toggles `//` at the minimum indentation of the selected lines (blank lines untouched; uncomments
  when every non-blank line has it); a selection ending at column 0 does not take that last line. Ctrl+Shift+/ wraps each
  selection in `/* */` (a bare caret: its line's text) and unwraps when already wrapped. C / C++ comment syntax only.
* Lines: duplicate (Ctrl+Shift+D: the line below, or the selection copied right after itself and selected), move
  (Alt+Up / Alt+Down), join (Ctrl+J: leading blanks of the next line become one space), delete (Ctrl+Shift+K), go to
  line (Ctrl+G, `line[:column]`, column 0 of the line by default, through the navigation history).
* Bookmarks: Ctrl+F2 toggles, F2 / Shift+F2 step through all bookmarks of the workspace (by path, then line; wrapping),
  Ctrl+Shift+F2 lists them; they move with edits like history entries and persist in `.tenfold/bookmarks.json`.
* Find / replace in the file: Ctrl+F / Ctrl+H, incremental, case-insensitive by default, Alt+C / Alt+W / Alt+R,
  Enter / F3 next, Shift+Enter / Shift+F3 previous, Replace and Replace All (one undo step), Escape closes.
* Trailing whitespace: `editor.trimTrailingWhitespace`, off by default (VERIFY). Line endings LF / CRLF switch
  commands; encoding is UTF-8 only.
* Not implemented: regex group references in Replace, non-UTF-8 encodings.
