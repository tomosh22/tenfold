# Open questions for the user

Items the implementation needs verified against a running copy of 10x (or
decided by the user). Each has a default we implement until answered.

| # | Question | Default we use meanwhile | Plan ref |
|---|---|---|---|
| 1 | Exact key bindings in your 10x setup (export `KeyMappings.10x_settings` if possible) | Alt+Left/Right and Ctrl+-/Ctrl+Shift+- for Back/Forward; F12 family for navigation | B4 |
| 2 | Which Visual Studio configuration/platform 10x uses for parsing by default | Debug / x64, overridable in `.tenfold/workspace.json` | A2 |
| 3 | Undo grouping: does 10x undo a whole typed word or one character? And does a typed space end the word it follows (so "hello world" undoes as "world", then "hello ") or start the next one? | word-level grouping with 1 s pause boundary; whitespace joins and then closes the current step. Settings `undoGrouping` ("word"/"char") and `undoGroupPauseMs` in `.tenfold/workspace.json` | A5 |
| 4 | Does 10x soft-wrap long lines by default? Smooth scrolling? | no wrap; smooth scroll on | B2 |
| 5 | Tab/panel behaviours: pinned tabs, MRU Ctrl+Tab order, drag between panels. Also: does closing the last tab of a panel close the panel? Does closing a dirty tab prompt to save? Does split duplicate the current tab or move it? | all three implemented; an emptied panel auto-closes after a drag but not after a manual close; no save prompt yet (dirty documents stay open in the Editor); split duplicates the active tab | B3 |
| 6 | Does symbol search show a preview of the target line? | yes | B5 |
| 7 | Default theme colours and font (screenshots of your 10x would be ideal) | dark theme, Consolas/JetBrains Mono 11pt | B7 |
| 8 | Which editing conveniences you rely on (auto-close brackets, comment toggle, bookmarks, ...) and how 10x behaves for each | all listed in B8 are implemented, each behaviour that may differ from 10x behind a setting; see the list below | B8 |
| 9 | How you debug from 10x (external debugger launch vs integrated) | launch external debugger at file:line | C2 |
| 10 | Perforce/Git operations you use from inside 10x | Git status/diff/revert; Perforce auto-checkout on edit | C3 |
| 11 | Do you use 10x's Python scripting API? | not implemented until answered | C4 |
| 12 | Any `.10x` workspace files or settings you want imported | sample files needed | C5 |
| 13 | Target platforms and priority (Windows first?) | Windows first, Linux second, macOS third | A1/A2/B9 |
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
* Not implemented: word wrap (OPEN_QUESTIONS #4 says off by default; the soft-wrap layout is the largest item and was
  left out), regex group references in Replace, non-UTF-8 encodings.
