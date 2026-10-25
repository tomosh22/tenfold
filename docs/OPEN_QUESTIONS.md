# Open questions for the user

Items the implementation needs verified against a running copy of 10x (or
decided by the user). Each has a default we implement until answered.

| # | Question | Default we use meanwhile | Plan ref |
|---|---|---|---|
| 1 | Exact key bindings in your 10x setup (export `KeyMappings.10x_settings` if possible) | Alt+Left/Right and Ctrl+-/Ctrl+Shift+- for Back/Forward; F12 family for navigation | B4 |
| 2 | Which Visual Studio configuration/platform 10x uses for parsing by default | Debug / x64, overridable in `.tenfold/workspace.json` | A2 |
| 3 | Undo grouping: does 10x undo a whole typed word or one character? | word-level grouping with 1 s pause boundary | A5 |
| 4 | Does 10x soft-wrap long lines by default? Smooth scrolling? | no wrap; smooth scroll on | B2 |
| 5 | Tab/panel behaviours: pinned tabs, MRU Ctrl+Tab order, drag between panels | all three implemented | B3 |
| 6 | Does symbol search show a preview of the target line? | yes | B5 |
| 7 | Default theme colours and font (screenshots of your 10x would be ideal) | dark theme, Consolas/JetBrains Mono 11pt | B7 |
| 8 | Which editing conveniences you rely on (auto-close brackets, comment toggle, bookmarks, ...) | all listed in B8 | B8 |
| 9 | How you debug from 10x (external debugger launch vs integrated) | launch external debugger at file:line | C2 |
| 10 | Perforce/Git operations you use from inside 10x | Git status/diff/revert; Perforce auto-checkout on edit | C3 |
| 11 | Do you use 10x's Python scripting API? | not implemented until answered | C4 |
| 12 | Any `.10x` workspace files or settings you want imported | sample files needed | C5 |
| 13 | Target platforms and priority (Windows first?) | Windows first, Linux second, macOS third | A1/A2/B9 |
| 14 | Code formatting: the existing code is hand-formatted in a compact style that `.clang-format` cannot reproduce (986 residual hunks; even one-line edits reflow neighbouring statements). Adopt clang-format wholesale with one reformatting commit, or keep the hand style? | **Answered:** adopt clang-format and reformat. Whole tree reformatted once (`examples/` and `tests/fixtures/` excluded: tests depend on their line/column positions); CI format job is blocking | A7 |
