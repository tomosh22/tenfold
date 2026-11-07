# Visual Studio 2022 behaviour for tenfold's open questions (W10 research)

Research for the decision of 2026-11-06 (docs/OPEN_QUESTIONS.md, "User decisions of 2026-11-06"): every question the
10x documentation leaves unanswered is answered the way Visual Studio 2022 behaves, with sources. Research only; no
source code was changed. Date of research: 2026-11-06.

## How to read this document

* **Confidence.** high = stated in a Microsoft page I read; medium = stated by a non-Microsoft page I read, or by a
  Microsoft page indirectly, or consistent across several sources; low = from memory of using Visual Studio, from a search
  summary of a page I could not read, or sources that conflict. **Nothing here was observed in a running Visual Studio**
  (no Windows/VS in this environment). Every low row has a "Verify" entry in the checklist at the end (section V).
* **Source ids** (`[S12]`) are listed in section S at the end. Pages marked "(snippet)" were only seen as a search-result
  summary because the page is script-rendered (developercommunity.visualstudio.com) or could not be fetched.
* The Learn pages are mostly for "VS 2022" but several option pages were last revised for VS 2026 (18.x); I quote only what
  is also true for 17.x or mark the difference. The "Theme Color Tokens" page (Fluent colours) is VS 2026 only and was
  not used for the 2022 palette.
* "tenfold now" is taken from docs/OPEN_QUESTIONS.md, docs/STRESS_RESULTS.md, assets/themes/vs-dark.txt and a few greps of
  `src/core/TextBuffer.h`, `src/app/EditorEditing.cpp`; it was not re-verified by running tenfold.
* Items where the 10x documentation answered the question, and the four user decisions of 2026-11-06 (unencodable
  characters; line endings "like Visual Studio"; alias/using and constructor choosers), are not repeated except where VS
  detail is needed to implement the decision.

---

## 1. Line endings (decision: "exactly like Visual Studio")

### 1.1 What tenfold does now

`TextBuffer` stores `\n` internally and remembers **one** ending per document (the most frequent; ties LF, CRLF) plus a
`mixed` flag; `textForDisk()` writes that one ending for every line (a mixed file is **normalised on save**); Enter and
paste use the document ending; status bar shows LF/CRLF/CR; commands switch between LF and CRLF; new files take
`editor.defaultLineEnding` (CRLF); no consistency prompt on load; clangd gets LF/CRLF text (`lspLineEnding`).

### 1.2 What Visual Studio does (implementable specification)

| # | Aspect | VS 2022 behaviour | Source | Conf. |
|---|---|---|---|---|
| L1 | Recognised line breaks (**implemented, like VS**) | Visual Studio interprets CR LF (U+000D U+000A), LF (U+000A), NEL (U+0085), LS (U+2028) and PS (U+2029) as line breaks. A lone CR is also a break (the dialog offers "Macintosh (CR)", and the Advanced Save Options dialog has a CR choice). | [S2], [S40], [S5] | high (table), medium (lone CR) |
| L2 | Load | The file is loaded **as is**: each line keeps its own terminator text; nothing is converted when opening. A document may therefore hold CRLF, LF, CR lines side by side. VS does not choose a "document ending" at load. | [S2] ("maintains the original ... line break characters"), [S6], [S7] | medium |
| L3 | "Check for consistent line endings on load" | Tools > Options > Environment > Documents. "Select this option to have the editor scan the line endings in a file and display a message box if inconsistencies are detected in how line endings are formatted." Scan happens on load only; typing or pasting later does not re-check (a user must reopen the file to trigger it). Believed on by default. | [S1], [S5] (comments), [S6] | high (text), medium (default on) |
| L4 | The dialog - **implemented (W10-LE dialog: `LineEndingDialog`, widgets `DropDown` / `CheckBox`; the LS / PS rows convert to the Unicode separators (LE-UNICODE))** | Body text: **"The line endings in the following files are not consistent. Do you want to normalize the line endings?"**, then a list of the file(s), a drop-down "Normalize line endings to:" and **Yes / No**. Drop-down entries: Windows (CR LF), Macintosh (CR), Unix (LF), Unicode Line Separator (LS), Unicode Paragraph Separator (PS) (reports also name Next Line (NEL)). A "do not show again"-style checkbox is reported; it is the same switch as the option in L3. The pre-selected entry is not documented (users complain it does not say which ending is most common). | [S5] (question text), search summaries of [S5]/[S6]; [S2] lists NEL | text high; entries medium; checkbox/pre-selection low |
| L5 | Yes / No | **Yes**: the whole document is rewritten with the chosen ending (a normal edit, so the document becomes modified and Undo works). **No**: the document opens unchanged (still mixed) and is not asked again until it is reopened. | [S5], [S6] ("select the desired option from the prompt and save them again") | medium |
| L6 | Enter (Return) | The editor option **`ReplicateNewLineCharacter`**: "whether to duplicate the new line character already present when inserting a new line". The option **`NewLineCharacter`** (default `"\r\n"`) "applies when ReplicateNewLineCharacter is false, or when ReplicateNewLineCharacter is true and the text buffer is empty". So with replication on (VS ships it on for text editors): Enter inserts the line break **already present in the buffer**; only an empty buffer (new file) uses the default newline (CRLF on Windows, or `.editorconfig` `end_of_line`). Which existing line supplies the ending is **not documented**; best reading: the break of the line the caret is on, else the previous line's break when the caret line has none (last line), else the default. Older (2010-2017) VS wrote CRLF into LF files when editing - users reported this until 2016-2017, after which VS replicates. | [S3], [S4], [S6] (history), [S5] | document option: high; "which line": low |
| L7 | Other generated newlines | Auto-indent splits (`{` + Enter), snippet insertion, brace completion on Enter use the same editor newline. Some Microsoft code generators (XAML formatter, templates, C# refactorings) still emit CRLF into LF files; this is a bug class, not a design. | [S7], [S8] (Q&A answer: XAML format produces mixed endings), [S5] comments | medium |
| L8 | Paste | VS **keeps the line breaks of the pasted text**: "When you copy text from another file or application, Visual Studio maintains the original encoding and line break characters in the copied value. For example, text copied from a Notepad file and pasted into a Visual Studio file use the original settings applied in Notepad." So pasting CRLF text into an LF file yields a mixed file (found by the L3 check on the next load). One report says pastes into an LF file came out LF in all tried cases; treat the Learn text as authoritative and test (V3). | [S2]; contrary report: developercommunity (snippet) | medium-low |
| L9 | Copy / cut | The text goes to the clipboard with the buffer's own break characters. Copy/cut with an empty selection copies the **whole line including its end-of-line characters** (and a blank line when "Apply Cut or Copy commands to blank lines when there is no selection" is on, default on). | [S11] | high (empty-selection rule), low (clipboard form) |
| L10 | Save | **No normalisation.** Each line is written with the terminator it has in the buffer ("Visual Studio does not [normalise on save]"). A mixed document stays mixed. | [S7], [S6] | medium |
| L11 | File > Advanced Save Options (and Save As > Save with Encoding) | Dialog "Advanced Save Options": **Encoding** list and **Line endings** list. Line endings entries: *Current Setting*, *Windows (CR LF)*, *Macintosh (CR)*, *Unix (LF)*. Choosing one converts every break to it **as part of that save** (it is not a persistent option; the next plain Save keeps what the buffer now holds). In VS 2017+ the menu item is hidden by default; Tools > Customize > Commands > Menu bar: File > Add Command > File > Advanced Save Options adds it; Save As > drop-down arrow on Save > Save with Encoding always opens it. | [S2], [S12], [S40], [S6] | medium-high |
| L12 | Edit > Advanced commands | "Make line endings CR LF" / "Make line endings LF" exist in third-party extensions only (EndOfTheLine); VS itself offers L5 and L11. | [S41] | medium |
| L13 | `.editorconfig` `end_of_line` | Supported property (`lf`, `cr`, `crlf`). Applies to **new** lines written in files under the config ("Visual Studio doesn't convert existing styles to the new ones until you format the document or run Code Cleanup"); it sets the editor's `NewLineCharacter` for those files. Reports say it is not honoured everywhere (templates, refactorings, pastes). | [S9], [S7], [S42] (snippet) | medium |
| L14 | Status indicator | The editor's **bottom margin** (editing context) shows `Ln: n  Ch: n  SPC|TAB  CRLF|LF|CR` and, optionally, selection count and INS/OVR. Right-click an unused part of the margin to show/hide each element; Tools > Options > Text Editor > General > "Show editing context in the editor": Line/Col, Selections, Insert/Overwrite, Tab/Space, **Line endings**. The Learn page says you can change the line ending code from that margin ("change the default end-of-line codes from CR LF ... to a different setting"). What exactly a click does (cycle? menu? convert the document or only set the newline for new lines?) and whether a mixed file shows a distinct label could not be established. | [S10], [S11 doc "Show editing context"], [S43] | label/options high; click behaviour low |
| L15 | New files | New buffer: Enter uses `NewLineCharacter` (CRLF default, `.editorconfig` may change it). Saving writes what the lines hold. | [S4] | medium |

### 1.3 Proposed tenfold change (line endings) - one task

**Implemented (W10-LE).** Items 1-6 and 8 are in; item 7 (`.editorconfig`) is skipped because tenfold reads no
`.editorconfig`. The choices where this document says "unknown" (which line supplies the ending, the mixed label, the dialog
pre-selection, what a status-bar click does) are listed in docs/OPEN_QUESTIONS.md, "W10 detail: line endings like Visual
Studio". The text below is the original proposal.

1. **Per-line terminators.** Replace the single `lineEnding_` + `mixed_` model by a per-line terminator (e.g. a parallel
   `std::vector<uint8_t>` of {none, LF, CRLF, CR, (NEL, LS, PS optional)}, or a run-length list). Load sets it from the
   file; save writes each line's own terminator; edits that split/join lines maintain it (join keeps the later line's
   terminator; split uses the Enter rule below; replacing a range keeps terminators of untouched lines).
   `textForDisk()` becomes "write as stored". Keep a `dominantEnding()` for the LSP copy (`lspLineEnding`) and for
   status-bar display. Version control (`src/vcs`) must compare text with line terminators stripped.
2. **Enter / auto-indent / brace-split / snippets:** new terminator = terminator of the caret line; if the caret line is
   the last line (no terminator) use the terminator of the previous line; if the document has no terminator at all use
   `editor.defaultLineEnding`, or the `end_of_line` of the matching `.editorconfig` section when present (L13). Setting
   `editor.replicateNewLine` (default true, mirrors VS's option) selects "always `editor.defaultLineEnding`" when false.
3. **Paste** inserts the clipboard text with its own breaks (CRLF, LF, CR split as today) and stores them per line; **copy**
   puts the stored terminators on the clipboard (on Windows converting a lone LF to CRLF is what other apps do; keep the
   stored text for tenfold-to-tenfold). Line copy/cut with no selection includes the line's terminator.
4. **Consistency check on load** (`editor.checkLineEndings`, default true): when a file has more than one kind of
   terminator, show the L4 dialog: message text of L4, a drop-down (Windows (CR LF) / Macintosh (CR) / Unix (LF) / Unicode
   Line Separator (LS) / Unicode Paragraph Separator (PS)), buttons Yes / No, "Always check" off-switch. Pre-select the
   dominant ending (VS's own pre-selection is unknown). Yes = one undoable edit that rewrites all terminators; No = open
   mixed. Never ask again for that document until it is reopened; do not check on paste/typing.
5. **Status bar** segment shows the document-level ending `CRLF`/`LF`/`CR` (VS shows one item for the document, not the
   caret line); for a mixed document show `Mixed` only if checklist item V1 confirms VS does, until then the dominant ending. Clicking opens the
   existing menu with Windows (CR LF) / Unix (LF) / Macintosh (CR); the choice converts the document (one undo step).
6. **Save As > Save with Encoding / "Advanced Save Options"**: a dialog with Encoding and Line endings (*Current Setting*,
   Windows (CR LF), Macintosh (CR), Unix (LF)); the choice is applied on that save (L11).
7. **`.editorconfig`**: read `end_of_line` (and `charset`, see 2.) for new documents and for the "default newline"
   of rule 2; do not rewrite existing documents.
8. **Implemented, like VS (user decision 2026-11-07):** NEL / LS / PS are line breaks of the buffer (`LineEnding::NEL`,
   `LS`, `PS`) and dialog / menu entries. clang and clangd do not treat them as line ends, so the engine sends them to
   clangd as characters and converts every position through `LspLineMap` (docs/ARCHITECTURE.md, "Text for clangd").

---

## 2. Encodings

**Implemented (VS-ENC).** Rows E1, E3, E4, E5, E6 and E8 are in, E2 partly (the system code page is read, but only
Windows-1252 can be decoded, so another page is read as 1252 and logged once), E7 stays as decided (refusal) with the
"Save with Encoding..." row. `.editorconfig` `charset` is skipped (tenfold reads no `.editorconfig`). The choices are
listed in docs/OPEN_QUESTIONS.md, "VS-ENC detail". The "Change" column below is the original proposal.

| Item | tenfold now | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| E1 Detection on load - **implemented (VS-ENC)** | valid UTF-8 (BOM or not) = UTF-8; UTF-16 BOM + valid = UTF-16; else Windows-1252 | "By default, the editor detects encoding by searching for byte order marks or charset tags. If neither is found ... the code editor tries to autodetect UTF-8 encoding by scanning byte sequences" (Tools > Options > Text Editor > General > **Auto-detect UTF-8 encoding without signature**, on by default); otherwise the system ANSI code page. A `charset` in `.editorconfig` also applies to new lines/files. | [S10], [S9] | high (text), medium (default on) | Same algorithm; add setting `editor.autoDetectUtf8` (default true). When off: BOM-less non-ASCII = ANSI code page. Honour `.editorconfig` `charset` (`utf-8`, `utf-8-bom`, `latin1`, `utf-16le/be`) for new files. |
| E2 Code page for the fallback - **partly implemented (VS-ENC): `systemAnsiCodePage()` is `GetACP()`, decoding is 1252 only** | Windows-1252 | the system ANSI code page (`GetACP()`, e.g. 932 on Japanese Windows) | [S10] | medium | On Windows use `GetACP()` instead of fixed 1252; elsewhere 1252. |
| E3 Display of encoding - **implemented (VS-ENC)** | status bar does not show it | The editor bottom margin has an **encoding widget** showing "the current automatically detected encoding for the file, but also lets you change it. You can reopen or save the current file with a different encoding" (a dialog lists encodings). | [S2] | high | Add an encoding segment to the status bar (e.g. `UTF-8`, `UTF-8 with signature`, `Windows-1252`); click opens a menu: Reopen with Encoding..., Save with Encoding... (each opens an encoding picker). |
| E4 Reopen with encoding - **implemented (VS-ENC)** | none | File > Open > drop-down on Open > **Open With...** (or Solution Explorer: right-click > Open With...) then an editor that asks for the encoding; the Encoding dialog lists code pages. Reopening discards nothing if the document is unmodified; modified documents prompt. | [S12] | high | `File.ReopenWithEncoding` command + picker; prompt Save/Discard when modified. |
| E5 Save with encoding - **implemented (VS-ENC)** | none | File > Save As > drop-down arrow on Save > **Save with Encoding** -> Advanced Save Options (Encoding + Line endings). Switching encoding changes how the file is written; the document's encoding becomes the chosen one. | [S12], [S2] | high | `File.SaveWithEncoding` (shares the dialog of 1.3 item 6). |
| E6 Default for new files - **implemented (VS-ENC)** | UTF-8 without BOM | VS added (April 2025, 17.14) **Tools > Options > Environment > Documents > "Save files with the following encoding"** + combo box; unchecked = VS's own defaults (new files: the ANSI code page, historically; many templates write UTF-8 with signature). | [S13] | medium | Keep UTF-8 (no BOM) as default; add `editor.defaultEncoding`. "No VS equivalent that is better" - keep. |
| E7 Unencodable characters - **implemented (W10-ENC, VS-ENC)** | (decision 2026-11-06: refuse to save and show the characters) | VS: "Some Unicode characters in this file could not be saved in the current codepage. Do you want to resave this file as Unicode in order to maintain your data?" with choices to save with another encoding / save anyway / cancel (wording and buttons from memory); since 2025 with the default-encoding option "a dialog will notify you" when VS cannot save with the specified encoding. C4819 docs: fix by Save As > Save with Encoding > e.g. "Unicode (UTF-8 with signature) - Codepage 65001". | [S13], [S44] | dialog text low; the C4819 fix high | Decision stands. Offer in the refusal dialog the VS escape: "Save with Encoding..." (E5). |
| E8 BOM - **implemented (VS-ENC)** | kept | VS keeps the signature of the encoding it loaded ("UTF-8 with signature" vs "UTF-8"); Save with Encoding offers both. | [S44] | medium | Encoding picker lists UTF-8 and UTF-8 with signature separately. |

---

## 3. Undo grouping (OPEN_QUESTIONS #3)

| Item | tenfold now | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| U1 Unit of typed text | word-level; whitespace joins then closes the step; 1 s pause boundary | **Could not be established from documentation.** The VS editor stores edits in `ITextUndoHistory` transactions; typing is merged by policy ("merge policies run when transactions are pushed"), and the Edit > Undo drop-down lists entries named "Typing", "Delete" etc. From memory: consecutive typed characters are one "Typing" undo unit, ended by caret movement (arrow keys, mouse click), Enter (new line is a separate unit), Delete/Backspace after typing, any other command (paste, comment, format) and IntelliSense commit (completion is its own unit). No time-based boundary. A typed space does **not** end the unit. | [S45] (merge policy API), memory | **low** | Change `undoGrouping` default to "typing run": group until caret move / Enter / non-typing command; drop the 1 s pause and the whitespace rule. Keep the setting names. Verify with V6 before shipping. |
| U2 Multi-caret edits | multi-caret typing undone per... (A5) | One keystroke across all carets is one transaction (VS multi-caret edits undo/redo together). | memory | low | Same rule applied per keystroke across all carets; the run of keystrokes groups like U1. |
| U3 Redo | - | Ctrl+Y / Ctrl+Shift+Z redo; a new edit clears the redo stack; undo/redo restore the caret/selection of the unit. Edit > Undo shows the unit list (drop-down on toolbar). | [S46] (snippet) | medium | Ensure caret restoration; no change otherwise. |
| U4 Global undo | n/a | Edit > Undo Last Global Action for multi-file refactorings (rename, replace in files); warns "Display warning when global undo will modify edited files". | [S9], [S1] | high | Optional: map rename/replace-in-files to one global entry. |

---

## 4. Version-control marks (OPEN_QUESTIONS #16)

| Item | tenfold now | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| VC1 Untracked file, Git, editor margin - **no change (matches VS), VS-VCS** | no marks | Two separate margins exist: (a) **Track changes** (Text Editor > General): a yellow bar for lines changed since the file was last saved, green after saving - for a new unsaved file every typed line is yellow, then green; independent of version control; (b) the **Git change margin** (17.3+, "line staging", on by default in 17.6+): green added / blue modified / red deletion wedge *relative to HEAD*, clickable (Peek Difference, Alt+F8). The Learn page does not say an **untracked** file is painted wholly "added"; a 2022 Microsoft blog comment asks exactly for that ("if we create a new file that has not yet been committed, it should be indicated as new as well") i.e. VS did not. The "Difference overview margin" (next to the scroll bar) is red for deletions and green for additions. | [S10], [S32], [S33], [S47] | medium | Keep "no whole-file mark for untracked" (matches VS). The user wrote that VS paints the whole file as added: this could not be confirmed - test V7 (checklist). |
| VC2 Untracked, Git, Solution Explorer / tabs - **skipped (low), VS-VCS** | tab marker `tabVcsAdded` | Solution Explorer shows a source-control glyph left of the item (Git: lock/padlock = committed & unchanged, pencil/check = modified, plus = added/staged, "?" is not used for untracked in VS Git). Tab text has no colour. | [S48] (snippet, community answer on the blue padlock), [S37] (TFS glyphs) | low | No change until V7. |
| VC3 Perforce (P4VS), Solution Explorer - **implemented (VS-VCS)**: red check, red plus, blue ?, yellow triangle and red ? are drawn as small glyphs on the file icon of explorer rows (tab squares use the same colours; theme keys `tabVcsOpenedForAdd`, `tabVcsNotInDepot`, `tabVcsOutOfDate`, `tabVcsConflict`, optional with fallbacks; the P4VS manual names the colours, not hex values, so vs-dark carries `[?]` approximations). Files that are only shown are asked for with one batched `p4 fstat` (`VcsManager::probe`). A synced, unmodified read-only file gets no badge (P4VS shows none). Git is unchanged (VC2). Locked-by-other badge not done (no mapping in the Change column) | `tabVcsCheckedOut` red, `tabVcsReadOnly` | P4VS badges on file icons: **red check** = checked out (open for edit), **red plus** = open for add ("marked for add"), **blue question mark** = file not in the depot (local only/untracked), **yellow triangle** = newer revision in depot, **red question mark** = needs resolve, grey circle = ignored, a lock badge for locked files. | [S31] pp. "Using Solution Explorer", "Adding files to the depot", resolve section | high (add, ?, triangle), medium (check) | Map: untracked in a Perforce workspace -> blue "?" badge/marker; open for add -> red plus; checked out -> red check; newer revision -> yellow triangle. |
| VC4 P4VS editor margin - **no change (matches VS), VS-VCS** | inline diffs for Perforce files | P4VS has **no editor-margin diff**; diffs open in P4Merge (File History: drag one revision onto another; right-click Diff > Diff Against Have Revision / Against...). | [S31] | high | Inline margin is a tenfold extra (keep). The p4merge full diff matches VS. |
| VC5 First edit of a read-only (not checked-out) file - **implemented (VS-VCS)**: setting `perforce.promptOnEdit` (default false = 10x auto-checkout), the question is a modal list "Check out" / "Check out, do not ask again" / "Do not check out" (Escape = do not check out); the edit is already in the buffer when it is asked. Not done: asking again at save, "always use the default changelist", `p4 add` of new files, persisting "do not ask again" to the settings file (it lasts for the session) | auto-checkout on edit | P4VS asks "check out (and save)?" on the first edit/save of a depot file, with "Don't show this dialog again (always use the default changelist)" and "Do this for all files being saved"; option "Check out writable files on save"; "Prompt for changelist when checking out, adding, or deleting files"; "Automatically add new files to the Helix Core server". | [S31] | high | Optional prompt `perforce.promptOnEdit`; keep auto-checkout as the default (10x answered #10). |
| VC6 Perforce history (W8f) - **implemented (VS-VCS)**: Enter on a revision opens an action menu: Open (read-only tab, as before), Diff against previous revision, Diff against have revision (external diff tool, `vcs.diffTool`; each is left out when it makes no sense). Not done: the Details / Integrations / Labels panes and drag-to-diff | picker of `p4 filelog`, opens revision read-only | P4VS: right-click file > **Revisions > Show History** opens the *File History* tool window (tree of revisions with Details / Integrations / Labels); drag a revision onto another to diff in P4Merge; Diff Against Have Revision. | [S31] | high | `PerforceHistory`: show revisions in a list with an action menu (Open, Diff against have, Diff against previous). |

---

## 5. Theme colours (OPEN_QUESTIONS #19; assets/themes/vs-dark.txt)

Authoritative source: Microsoft's "Color value reference for Visual Studio" ([S34], updated 2026-05-15 and published for VS 2022):
the Dark column is the VS 2017-2022 palette. Editor/syntax values: Microsoft's own "Visual Studio 2019 Dark (C++)" VS Code
theme ([S35], vscode-cpptools) which was ported from VS. Where neither covers a value it is marked "not found".
**Important:** `#1F1F1F` (tenfold `color.background`, `menuBackground`, `tab` bodies) does **not** appear in the VS 2022
palette; it is VS Code's *Dark Modern* theme ([S36]). VS 2022's Dark shell is `#2D2D30` (title/menu/tool bar, tab strip), tool
windows `#252526`, document well `#1E1E1E`.

### 5.1 Corrections to vs-dark.txt (key: tenfold value -> VS value, source, confidence)

| tenfold key | now | VS 2022 Dark | Source / note | Conf. |
|---|---|---|---|---|
| `color.background` | #1F1F1F | #2D2D30 (IDE background, title bar, menu bar, toolbar) | [S34] "IDE and unselected preview tab backgrounds", "Title bar, menu bar, and command bar backgrounds" | high |
| `color.panelBackground` / `popupBackground` / `treeBackground` | #252526 | #252526 tool window background (confirmed). Popups/combo drop-downs: #1B1B1C ("Drop-down list ... drop-down menu background") | [S34] | high |
| `color.tabStripBackground` | #2D2D30 | #2D2D30 ✓ (document well/tab strip = IDE background) | [S34] | high |
| `color.tabInactive` | #2D2D30 | #2D2D30 ✓ "Unselected document window tab background" | [S34] | high |
| `color.tabActive` | #007ACC | #007ACC ✓ (focused document tab bg and border; text #FFFFFF) | [S34] | high |
| (new) selected tab in an unfocused group | - | bg #2D2D30? Learn: "Unfocused document tab, preview tab, and tool window title bar background" #2D2D30 with border #3F3F46; selected-unfocused hover #555555 | [S34] | medium |
| `color.tabHover` | #3E3E40 | **#1C97EA** for document tabs ("Background document tabs: background and border on hover"); #3E3E40 is the *tool window* tab hover | [S34] | high |
| (new) preview tab | none | the active **preview (provisional) tab is purple #68217A** (bg and border); unselected preview tab bg #2D2D30; its buttons #442359 on mouse down | [S34] | high |
| `color.tabAccent` | #1C97EA | no accent line in VS; the focused tab is the solid #007ACC. #1C97EA is the hover colour. Keep if the 2 px line is wanted, else drop. | [S34] | medium |
| `color.tabInactiveText` | #D0D0D0 | #D0D0D0 ✓ ("Auto-hide tab text, unfocused tool window title bar text, and tool window tab text"); document tab text #F1F1F1 on hover; disabled #656565 | [S34] | medium |
| `color.closeHover` | #E81123 | **#52B0EF** ("Document and tool window buttons: active backgrounds on hover"), glyph #FFFFFF; mouse-down bg #0E6198. Red #E81123 is the Windows caption button, not the tab. | [S34] | high |
| `color.dirtyMarker` | #DCDCAA | not found (VS shows a filled dot/"pencil" glyph in the tab text colour). Keep. | - | - |
| `color.splitter`, `panelBorder`, `popupBorder`, `menuBorder` | #3F3F46 | #3F3F46 ✓ (tool window border, unselected-tab separator); default borders/command-bar separators #434346 | [S34] | high |
| `color.scrollbarTrack` | #3E3E42 | #3E3E42 ✓ | [S34] | high |
| `color.scrollbarThumb` | #686868 | #686868 ✓; hover **#9E9E9E**; pressed **#EFEBEF**; arrow glyph hover #1C97EA, pressed #007ACC | [S34] | high |
| `color.statusBackground` | #007ACC | #007ACC ✓ (text #FFFFFF) | [S34] | high |
| `color.statusBackgroundDebugging` | #CA5100 | #CA5100 (orange; not in the Learn table; widely used; VS 2026 changed to #7A2101) | memory; [S49] says 2026 differs | medium-low |
| (new) status bar, no solution/folder loaded | none | purple #68217A (VS 2015-2022; VS Code copied it as `statusBar.noFolderBackground`) | memory; [S49] | medium-low |
| `color.statusHover` | #1C97EA | not found; keep | - | - |
| `color.menuBackground` | #1F1F1F | **#1B1B1C** (default menu bg, drop-down menu bg, icon channel) | [S34] | high |
| `color.menuSelection` | #3E3E40 | **#333334** ("Menu items: background on hover") | [S34] | high |
| `color.menuSeparator` | #3F3F46 | **#333337** ("Menu bar menu group separators") | [S34] | high |
| (new) menu text / glyph / disabled | - | text #F1F1F1, glyphs #999999, hover glyph #007ACC, disabled text #656565 | [S34] | high |
| `color.text` | #F1F1F1 | #F1F1F1 ✓ ("Most text") | [S34] | high |
| `color.textDim` | #999999 | #999999 ✓ ("Some instances of unfocused text"); disabled #656565 | [S34] | high |
| `color.selection` (list/picker selection) | #094771 | **#3399FF** focused selected row with #FFFFFF text; unfocused selected row **#3F3F46** with #F1F1F1 text (tree views and the Properties grid) | [S34] | high |
| `color.treeSelection` | #094771 | #3399FF (focused) | [S34] | high |
| `color.treeSelectionInactive` | #3F3F46 | #3F3F46 ✓ | [S34] | high |
| (new) input field | - | bg #333337, border #434346, disabled bg #2D2D30, hover bg #333337; text-input selection #66007ACC | [S34] | high |
| (new) hyperlink / search clear / selected tab text in tool windows | - | #0097FB | [S34] | high |
| `color.diagnosticError` | #F14C4C | not found. Docs: "Red squiggles denote syntax errors, blue denotes compiler errors, green denotes warnings, and purple denotes other types of errors"; Stop/error glyph #E51400. Suggest red #E51400-family; keep until sampled. | [S10], [S34] | low |
| `color.diagnosticWarning` | #4CAF50 | green (docs) ✓ in spirit; exact hex not found | [S10] | low |
| `color.diagnosticInfo` | #75BEFF | VS draws *suggestions* as three grey dots, "messages" as blue? exact not found | memory | low |
| `color.debugBreakpoint` | #E51400 | #E51400 ✓ (the "Stop, error" colour) - remove the `[?]` | [S34] | high |
| `color.debugCurrentArrow` / line | #FFEE62 | #FFEE62 (yellow arrow & statement highlight) - from memory | memory | low |
| `color.gutterText` (line numbers) | #2B91AF | #2B91AF ✓ (also the active line number) | [S35] | medium-high |
| `color.editorBackground` | #1E1E1E | #1E1E1E ✓ | [S35] | medium-high |
| `token.identifier`, editor foreground | #DCDCDC | **#DADADA** (Plain Text in VS 2019/2022 Dark) | [S35] `editor.foreground` | medium |
| `color.selectionInactive` | #3A3D41 | #3A3D41 ✓ (inactive selection) | [S35] | medium |
| `color.selectionActive` | #264F78 | #264F78 (Selected Text) - from memory, matches Dark+ | memory | medium-low |
| `color.currentLineBorder` | #464646 | "displays a gray box around the line" - an outline, no fill ✓; exact grey not found | [S10] | medium (shape), low (hex) |
| `color.bracketMatch`, find highlight, whitespace dots, column guide | various | not found (VS: brace matching is a grey filled rectangle with outline; find highlight is a translucent amber; whitespace glyphs grey). Keep. | - | - |
| `color.diffAdded/Modified/Deleted` | #57A64A / #1C97EA / #F14C4C | Git margin: green / blue / red wedge (names: Fonts and Colors "Track additions/deletions/modifications in documents under source control"); Track-changes: yellow (unsaved) -> green (saved). Hex not found. | [S32], [S10] | low |
| `token.keyword` | #569CD6 | #569CD6 ✓ | [S35] | high |
| `token.type/class/struct` | #4EC9B0 | #4EC9B0 ✓ | [S35] | high |
| `token.function/method` | #DCDCAA | #DCDCAA ✓ (functions and user literals) | [S35] | high |
| `token.comment` | #57A64A | #57A64A ✓ | [S35] | high |
| `token.number`, `enumMember` | #B5CEA8 / #B8D7A3 | #B5CEA8 for both in [S35]; VS 2019 Enumerator is #B8D7A3 (memory) | [S35] | medium |
| `token.string` | #D69D85 | [S35] has #CE9178 (VS Code colour); VS Fonts and Colors "String" #D69D85 (memory). Keep #D69D85, escape sequences #FFD68F ✓, string delimiters #E8C9BB | [S35] | medium-low |
| `token.operator`, `punctuation` | #B4B4B4 / #DCDCDC | **#B4B4B4 for both** (punctuation, operators, `*`/`&`) | [S35] | high |
| `token.preprocessor` | #9B9B9B | #9A9A9A (preprocessor keyword/directive) | [S35] | high |
| `token.parameter` | #C8C8C8 | **#9A9A9A** | [S35] | medium |
| `token.property`, member variables | #C8C8C8 | **#DADADA** (property) | [S35] | medium |
| `token.namespace`, global, constants, `meta.preprocessor` | #C8C8C8 | #C8C8C8 ✓ | [S35] | high |
| `token.macro` | #BD63C5 | [S35] colours function-like macro invocations #BEB7FF; VS 2019/2022 "Macros" is #BD63C5 (memory). Conflict: keep #BD63C5. | [S35] | low |
| control-flow keywords | (not expressible) | #D8A0DF (`if for while return switch break goto try catch throw`, `using`, `new`/`delete` are #569CD6) | [S35] | high |
| inlay hints | n/a | bg #3E3E3E, fg #A9A8A7 | [S35] | medium |
| `color.terminal*` | Campbell | not found for VS's terminal. Its palette is the Windows console/ConPTY one; background follows the theme's terminal colour, not documented. Keep. | - | - |
| tooltips (Quick Info) | popupBackground | not found; tool-tip colours follow the "Tooltip" category (dark #2D2D30-ish with #3F3F46 border) - not verified | - | - |
| find-in-file bar | - | VS 2022: overlay at the top right of the editor, tool-window colours (#252526? not documented) | - | - |

### 5.2 Theme-level decisions

* Rebase `color.background` (shell: title bar, menu bar, status of "window" areas), `menuBackground`, `tabHover`,
  `closeHover`, `selection`, `treeSelection`, token colours above; remove `[?]` from the rows marked high.
* Add keys: `color.tabPreviewActive` (#68217A), `color.tabInactiveFocusBorder` (#3F3F46), `color.scrollbarThumbHover`
  (#9E9E9E), `color.scrollbarThumbPressed` (#EFEBEF), `color.inputBackground` (#333337), `color.inputBorder` (#434346),
  `color.statusBackgroundNoSolution` (#68217A), `color.link` (#0097FB), `color.menuText/menuGlyph/menuDisabled`.

### 5.3 Implementation status (W10-THEME)

Applied to `assets/themes/vs-dark.txt` (the `[?]` marker is removed where a source backs the value; sources are cited in
the file): `background`, `popupBackground` (#1B1B1C), `closeHover`, `menuBackground`, `menuSelection`, `menuSeparator`,
`selection`, `treeSelection`, `debugBreakpoint`, `gutterText`, `selectionInactive`, `editorBackground`, and the tokens
`identifier` #DADADA, `punctuation` / `operator` #B4B4B4, `preprocessor` #9A9A9A, `parameter` #9A9A9A, `property`
#DADADA, control-flow keywords #D8A0DF. Rows marked "✓" already matched. New optional keys, painted: `tabDocumentHover`,
`scrollbarThumbHover`, `scrollbarThumbPressed` (editor, explorer and terminal scrollbars; hover on the terminal bar is
not tracked), `inputBackground`, `inputBorder`, `inputSelection`, `menuText`, `menuGlyph`, `menuDisabled`,
`statusBackgroundNoSolution` (status bar when the workspace root is empty). `tabPreviewActive` / `tabPreviewInactive` paint the
preview tab (VS-TABS, section 7 T2). Defined but not painted yet: `link` (no links). New token kind `controlKeyword` (line lexer; falls back to `keyword`).
Differences from the proposal: `tabHover` stays #3E3E40 (it also colours hovered list and tree rows; VS's #1C97EA applies to
document tabs only, hence `tabDocumentHover`); `tabInactiveFocusBorder` and white text on selected rows are not implemented
(no unfocused tab-group state; the F1F1F1 text is kept on #3399FF). Not changed (low confidence or not found, `[?]`):
squiggles, Git margin, find highlight, bracket match, whitespace, column guide, `selectionActive`, `currentLineBorder` hex,
`dirtyMarker`, `statusHover`, debugging orange, no-solution purple, string / enum / macro, terminal palette.

---

## 6. Symbol search preview (OPEN_QUESTIONS #6)

| Item | tenfold now | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| P1 Preview of the target - **implemented (VS-NAV)**: a "Preview: on/off" switch in the header of the symbol search and the function picker (click or Alt+P), kept for the session, `picker.preview` stays the start value | `picker.preview` on (preview of the target line) | Go To All (Ctrl+T / Ctrl+,): the classic window has **"Use preview tab"**: "Display the selected item immediately in the IDE's preview tab" - as you change the selection (mouse or arrows) the result opens in the italic **preview tab**. In VS 2022 Ctrl+T opens the new **Code Search** (All-In-One Search) which has a **preview pane** (eye icon toggles it, 17.12+) and a "Show details" option; docking possible; recent files shown first when the query is empty (17.13+). | [S16], [S17], [S18] | high | Keep `picker.preview` on and add the eye toggle (`picker.preview` setting already exists); preview in a split pane (target file with the line highlighted) rather than a real tab. |
| P2 Filters/prefixes - **not done (optional row)** | tenfold's own | `:` line, `f` file, `r` recent, `t` type, `m` member, `#` symbol, `?` help; camel-case matching ("CVM" finds CredentialViewModel); scope: current document / project / solution / external. | [S16], [S17], [S19] | high | Optional: prefix filters in the symbol picker. |

---

## 7. Tab well (OPEN_QUESTIONS #5 "still open" and W8f)

| Item | tenfold now | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| T1 MRU / Ctrl+Tab order | switch panel over all files (10x); MRU order guessed | Ctrl+Tab opens the **IDE Navigator** ("Active Files" + "Active Tool Windows"); Tab repeatedly moves down the list; Ctrl+Shift+Tab reverses; the list is most-recently-used order (first Ctrl+Tab goes to the previous document). Ctrl+- / Ctrl+Shift+- also step through documents in MRU order. | [S25] (snippet), [S50] | medium | Switch panel: most recently used first, selection starting on the previous file; releasing Ctrl commits. **Implemented (VS-TABS)**: `SwitchPanel` (a list of the active panel's tabs, MRU first; Ctrl+Tab selects the previous file, Ctrl+Shift+Tab the oldest; Tab / Shift+Tab / Up / Down move; releasing Ctrl, Enter or a click commits; Escape cancels; nothing is switched meanwhile). 10x's binding and command names are unchanged. Not done: the "Active Tool Windows" column, Ctrl+- stepping. |
| T2 Preview tab | none (single click selects, double click opens) | A single click on a file in Solution Explorer, Go To All results, Find Results, and debugger step-into open it in the one **preview tab**, shown at the right end of the tab well (italic/purple when active); it becomes a normal tab when you edit it, double-click it, or choose Open from the context menu. Preview tabs cannot be moved/pinned. Option: Tools > Options > Environment > Tabs and Windows > "Allow new files to be opened in the preview tab", "Preview selected files in Find Results", Solution Explorer toolbar "Preview Selected Items" toggle. Historically on by default. | [S23], [S18], [S51] (snippet) | high (mechanism), medium (default on) | Implement a preview tab (one per panel? VS: one per window). Single click in the explorer tree and picker selection open it; edit/double-click promotes. **Implemented (VS-TABS)**: one preview tab per window, italic (synthetic slant, there is no italic face) in `tabPreviewActive` / `tabPreviewInactive` colours (fallback `tabActive` / `tabInactive`), always the last tab, replaced by the next preview, never pinned or reordered. Opened by a single click in the explorer (keys stay in the tree), by a mouse click on a Find in Files / Find All References row (Enter and F8 open normal tabs) and by the debugger stepping into an unopened file. Promoted by an edit, a double click on the tab or the item, the explorer's Open / Enter, or opening the file by name. Settings `tabs.previewTab` (true), `tabs.previewFromFindResults` (true). Not done: the explorer toolbar toggle, preview from Go To All and from arrow-key selection in the tree. |
| T3 Middle-click | middle-click closes | Middle-click on a tab closes it (also Ctrl+F4 closes the active document). | memory | medium | Keep. |
| T4 Tab context menu | Close, Close All But This, Close All But Pinned, Pin/Unpin, Copy Full Path, Open Containing Folder, Show in Workspace Tree | VS tab menu, from memory in this order: Save, Close, **Close All But This**, **Close All But Pinned**, Close All to the Left, Close All to the Right, Copy Full Path, Open Containing Folder, Open in Terminal, **Pin Tab**, Set Tab Layout >, Set Tab Color >, Float, Dock as Tabbed Document, New Horizontal/Vertical Tab Group, Move to Next/Previous Tab Group, Restore Closed Tab. Hanselman lists "close all", "close all but this" and "close all but pinned". | [S38] (snippet), [S23] (Set Tab Layout/Color, Restore Closed Tab), memory | medium-low | Add Save, Close All to the Left/Right, Open in Terminal, Restore Closed Tab (already a command). |
| T5 "Close All But This" and pinned tabs | closes other unpinned tabs; pinned stay | Not documented. From memory "Close All But This" also closes pinned tabs? (the existence of a separate "Close All But Pinned" suggests the first closes everything but the clicked tab). | [S38] | low | Keep tenfold's behaviour (pinned survive) until V8. |
| T6 Closing dirty tabs in bulk | "close all" items leave dirty tabs open | VS shows **one** dialog "Save changes to the following items?" listing all modified documents with Yes / No / Cancel (Yes saves the checked items; Cancel aborts the whole close); single document: "Do you want to save changes to <file>?" Save / Don't Save / Cancel. On exit and Close Solution the same list dialog. | [S52] (Save Changes dialog), memory for button labels | medium | For Close All / Close All But This / quit: one list dialog; items checked by default; Cancel aborts. (tenfold's Save All / Don't Save / Cancel matches.) **Implemented (VS-TABS)**: `UnsavedPrompt` shows "Save changes to the following items?" for two or more modified documents (quit, window close, Close All But This / Pinned, new `Workbench.CloseAllDocuments` and `Workbench.ClosePanel`): `[x]` rows (Space / Enter / click toggles), Yes saves the checked items and closes the unchecked ones without saving, No closes without saving, Cancel aborts; a failed or encoding-refused save (SaveRefusedDialog) aborts the close. One modified document keeps Save / Don't Save / Cancel. |
| T7 Last tab of a split / emptied panel | auto-closes after drag, not after manual close | In VS an emptied *tab group* (split document well) **disappears** (the groups merge), whether the last tab was dragged out or closed. The Window > Split command splits the same document in two views, "Remove Split" restores. | [S23] (Split), memory for groups | medium-low | Auto-close an emptied panel in both cases (unless it is the only panel). |
| T8 Split semantic | split duplicates the active tab | Window > Split = two panes on the same document; New Horizontal/Vertical Tab Group *moves* the document to a new group. 10x's DuplicatePanel (answered) stays. | [S23] | medium | no change (10x documented). |
| T9 Files changed on disk | auto-reload when clean; prompt when dirty (`editor.reloadExternalChanges`=auto) | Tools > Options > Environment > Documents: **"Detect when file is changed outside the environment"** (on): a message notifies you and lets you reload; **"Reload modified files unless there are unsaved changes"** (off by default): if on, no prompt and the document is reloaded silently. With unsaved changes VS always prompts. The prompt is a dialog "The file ... has been modified outside of the source editor. Do you want to reload it?" with Yes / Yes to All / No / No to All. | [S1] | high (options), medium (dialog text) | Change `editor.reloadExternalChanges` default to `prompt` (VS default) and add the silent mode for clean files as `auto`; dialog: Reload / Reload All / Keep / Keep All. **Implemented (VS-TABS)** with VS's own names instead: `editor.detectExternalChanges` (true) and `editor.reloadUnlessUnsaved` (false), dialog "The file X has been modified outside of the source editor. Do you want to reload it?" with Yes / Yes to All / No / No to All (dirty documents always ask, default No). `editor.reloadExternalChanges` is kept as an optional override (empty by default; auto / prompt / never map onto the two new settings). |
| T10 Tab drag between panels | implemented | VS drag-docks document tabs into groups, with a guide diamond; Ctrl-drag doesn't snap; Ctrl+double-click toggles floating. | [S23] | high | no change. |
| T11 Pinned tabs | pinned tabs stay | Pinned tabs show in the first part of the well (or a separate row when "Show pinned tabs in separate row"); preview tabs can't be pinned; movement preserves the pinned/unpinned boundary. | [S23], [S24] | high | Keep pinned left of unpinned; block moves across the boundary. **Implemented (VS-TABS)**: tests for strip moves, mouse drags, `MoveNextPanelTab` and drops into another panel; a preview tab is never pinned. Not done: the separate row for pinned tabs. |
| T12 Close/restore | RestoreRecentlyClosedFile stack | **Restore Closed Tab** (Ctrl+K, Ctrl+Z, Window menu, tab menu) reopens the last closed tab. | [S23] | high | Add Ctrl+K,Ctrl+Z only as a tenfold addition (10x key map is authoritative). **Implemented (VS-TABS)**: `Control K, Control Z: RestoreRecentlyClosedFile` in the tenfold additions of the default key map (the chord is free in 10x's map); same command as `Control Shift T`. |
| T13 Tab keyboard | - | 18.11 (VS 2026): Ctrl+Shift+PgUp/PgDn move tab, Ctrl+NumPad1-9 activate - not in 17.x. | [S24] | high | skip. |

---

## 8. Context menus

| Item | tenfold now | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| C1 Editor menu (C++) | Cut/Copy/Paste, GoTo..., Rename, Quick Info, Switch Header/Source, Format Selection, Toggle Comment, Select All, Toggle Breakpoint, Run To Cursor, Revert/Show Change, VS, Show in Workspace Tree, Open Containing Folder, Copy Full Path | VS 2022 C++ editor menu in order, from memory: *Quick Actions and Refactorings...* (Ctrl+.), *Rename* (Ctrl+R,R), *Peek Definition* (Alt+F12), *Go To Definition* (F12), *Go To Declaration* (Ctrl+F12), *Find All References* (Shift+F12), *View Call Hierarchy* (Ctrl+K,Ctrl+T), *Toggle Header/Code File* (Ctrl+K,Ctrl+O), --- *Breakpoint >* (Insert Breakpoint, Insert Tracepoint, ...), *Add Watch*, *Add Parallel Watch*, *QuickWatch*, *Run To Cursor* (Ctrl+F10), *Set Next Statement*, --- *Snippet >* (Insert Snippet, Surround With), *Create Unit Tests*, --- *Cut, Copy, Paste*, *Outlining >*, *Source Control/Git >*. Debug items appear only while debugging or for startup projects. Learn confirms: Go To Definition (F12), Go To Declaration (Ctrl+F12), Peek Definition, Find All References, Toggle Header/Code File, Go To Document on `#include`. | [S19], [S53] (cppblog), memory | medium-low (order from memory) | Reorder to VS: refactor/rename, peek, go to def/decl, FAR, call hierarchy, toggle header, breakpoint group, run-to-cursor, snippet group, cut/copy/paste, outlining submenu, then tenfold extras at the end. `#include` line: add "Go To Document". |
| C2 Right-click moves the caret | moves unless inside a selection | Yes: right-click places the caret at the click (Go To Definition then acts on that symbol); a click inside the selection keeps it. | memory ([S20]: "Right-click the symbol name and select Go To Definition") | medium | keep. |
| C3 Shift+F10 / Menu key | opens at caret, selects first item | The menu opens at the caret; the first item is *not* pre-selected (Windows menus select on arrow); Esc closes. | memory | low | Do not pre-select the first item; Down selects the first enabled item. |
| C4 Disabled items | greyed; arrows skip them | Windows menus: disabled items are greyed and **skipped** by arrow keys in Win32 menus, but VS's WPF menus let the selection rest on disabled items without activating them. | memory | low | keep skipping (simpler). |
| C5 Tab menu | see T4 | see T4 | | | |
| C6 Solution Explorer: solution node | Open, Collapse All, Open Containing Folder, Copy Full Path | Build Solution, Rebuild Solution, Clean Solution, Run Code Analysis; Batch Build..., Configuration Manager...; Manage NuGet Packages (C#); Restore NuGet; **Project Dependencies / Build Order**, **Set Startup Projects...**, Git > ..., Add > New Project / Existing Project / New Solution Folder, Paste, Rename, Open Folder in File Explorer, Open in Terminal, Collapse All Descendants, **Properties** (Alt+Enter), Show on Code Map. | [S27] (describes the Solution menu; screenshots only) | medium-low | Add Build/Rebuild/Clean Solution, Configuration Manager..., Set Startup Project(s), Open in Terminal, Properties (a read-only page showing solution configuration). **Partly (VS-TREE): Open in Terminal (VS-TERM) and Properties; the rest skipped (medium-low).** |
| C7 Project node | same | Build, Rebuild, Clean, Project Only > (Build Only/Rebuild Only/Clean Only), Add > (New Item, Existing Item, New Filter, Class...), Scope to This, New Solution Explorer View, Debug > (Start New Instance / Step Into New Instance), Set as Startup Project, Cut/Copy/Paste, Remove, Rename, Unload Project, Open Folder in File Explorer, Open in Terminal, **Properties** (Alt+Enter), Source control. | [S27] | medium-low | Add Build/Rebuild/Clean (project), Set as Startup Project, Properties (opens the project's configuration summary). **Partly (VS-TREE): Properties and the file operations of the folder; the rest skipped (medium-low).** |
| C8 File node | Open, Collapse All, Open Containing Folder, Copy Full Path, Build Current File | Open, Open With..., View Code / View Designer, **Compile** (Ctrl+F7, C++ files), Scope to This, New Solution Explorer View, Exclude From Project / Include In Project, Cut/Copy/Paste, Remove, Rename (F2), Copy Full Path, Open Containing Folder, Open in Terminal, Source control (Git/P4VS: Checkout, Mark for Add, Revisions >, ...), **Properties** (Alt+Enter). | [S27], [S31] | medium-low | Rename the existing item "Build Current File" -> "Compile" in the explorer menu; add Open With..., Properties (path, size, configuration-specific "Excluded From Build"). **Partly (VS-TREE): Properties, New File/Folder, Rename, Delete; "Compile" rename, Open With... and Exclude/Include skipped (medium-low).** |
| C9 Folder / filter node | Expand/Collapse, Collapse All, Open Containing Folder, Copy Full Path | Add >, Scope to This, New Solution Explorer View, Cut/Copy/Paste, Remove, Rename, Open Folder in File Explorer, Open in Terminal, Properties. | [S27] | medium-low | same as C8 subset. **Partly (VS-TREE): Properties, New File/Folder, Rename, Delete, Open in Terminal; Scope to This and the rest skipped (medium-low).** |
| C10 Properties | not offered | **Properties** (Alt+Enter) opens the Properties window/page; in Solution Explorer it exists for solution, project, folder and file nodes. | [S27] | high | Add "Properties" opening a read-only info page (path, size, project, configuration) until a settings dialog exists (there is none, OPEN_QUESTIONS #12). **Implemented (VS-TREE):** `Explorer.Properties` and the menu item on every node (name, type, path; file: size, project, configuration, Excluded From Build). No key: Alt+Enter is 10x's ToggleFullscreen. |

---

## 9. Code outlining

| Item | tenfold now | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| O1 Regions in C++ | `{}` blocks of >= 2 lines, multi-line `/* */`, `#pragma region`; not `#if`, not `//` runs | Settings (Tools > Options > Text Editor > C/C++ > View > Outlining): **Enable Outlining** ("Enter outlining mode when a file opens"), **Outline Pragma Regions**, **Outline Statement Blocks** - all on by default. So: statement blocks (functions, classes, namespaces, control-flow bodies), `#pragma region`. Multi-line `/* */` comments are collapsible in the editor (memory, low); `#if/#endif` and `//` runs are not outlined in C++. User-defined regions via Hide Selection (Ctrl+M,Ctrl+H). | [S14], [S15], [S54] (cppblog: "outlines are added around code blocks") | high (settings), medium (comments), medium (#if) | Add the three settings (`outline.enabled`, `outline.pragmaRegions`, `outline.statementBlocks`); hide-selection region type optional. **Implemented (W10-OUTLINE)**: the three `outline.*` settings; hide-selection regions not done. |
| O2 Collapsed text | head line + `{...}` marker, text after `}` hidden | Collapsed block shows the line up to the opening brace followed by a **`...` box** (`{...}` for braces); hovering shows the hidden contents in a tooltip; `#pragma region NAME` shows the name; collapsed comments show the first words. | [S14] (tooltip), memory | medium | Add the tooltip with the hidden text. **Implemented (W10-OUTLINE)**: the hover tooltip with the hidden code. |
| O3 Outlining margin visible by default | `editor.showFoldGlyphs` default **false** | Outlining margin is shown whenever outlining is enabled: the `[-]`/`[+]` boxes with connecting lines (expander glyph `⊟`/`⊞`), hover highlights the region (colour: Fonts and Colors > Collapsible Region); double-click on the margin collapses a region. | [S14] | high | **Make `editor.showFoldGlyphs` default true** (VS default), accept the golden-image churn (OPEN_QUESTIONS W8f detail already flags the trade-off). **Implemented (W10-OUTLINE)**: default true, boxed +/- margin with guides; goldens regenerated. |
| O4 Ctrl+M Ctrl+M | CollapseRegion / Toggle at caret | **Toggle Outlining Expansion**: "Reverses the current hidden or expanded state of the innermost outlining section when the cursor is in a nested collapsed section" (acts on the innermost section containing the caret). | [S14] | high | `ToggleCollapseExpandRegion`: innermost region around the caret (collapsed or not) toggled; tenfold's "repeating walks outwards" differs. **Already matched** (toggle on a collapsed head opens, else collapses the innermost open region); unchanged. |
| O5 Ctrl+M Ctrl+L | collapses all unless all collapsed | **Toggle All Outlining**: "Sets all regions to the same collapsed or expanded state. If some regions are expanded and some collapsed, the collapsed regions are expanded." So: mixed -> expand all; all expanded -> collapse all; all collapsed -> expand all. | [S14] | high | **Change**: mixed state must *expand* (tenfold collapses). **Implemented (W10-OUTLINE)**. |
| O6 Ctrl+M Ctrl+O | n/a | **Collapse to Definitions**: "Collapses the members of all types" (functions/methods collapsed, types left open - not all regions). Also: Ctrl+M,Ctrl+P Stop Outlining; Ctrl+M,Ctrl+U Stop Hiding Current; Ctrl+M,Ctrl+H Hide Selection; Edit > Outlining > Start Automatic Outlining. | [S14] | high | Add `CollapseToDefinitions` (collapse every region that is a member body: function bodies, nested at any depth inside classes/namespaces; leave namespaces/classes open). **Implemented (W10-OUTLINE)** as `Editor.CollapseToDefinitions`, key `Control M, Control O` (free in 10x); Stop Outlining / Hide Selection not done. |
| O7 State across sessions | not saved | Not documented; outlining is recomputed when a file opens (all regions *expanded* unless the document was in Collapse-to-Definitions mode; the Learn page says copy/paste/drag-drop do not keep the collapsed state, pasted regions are expanded, and edits may destroy a region's end). VS C# restores nothing per file either. | [S14] | medium-low | Keep "not persisted". **Followed** (not persisted). |
| O8 Caret in collapsed region | expands regions that hide it | Find, Go To, Find References, build-error navigation expand the collapsed regions around the target; typing in a collapsed region isn't possible (caret cannot sit inside). | memory | medium | keep. **Followed** (unchanged). |
| O9 Undo of folding | not undoable | "The Undo and Redo commands on the Edit menu affect these actions" (collapse/expand are undoable). | [S14] | high | Optional: make fold toggles undo units (low value). Not done (optional). |

---

## 10. Find All References / Go To Definition / Peek / Go To Implementation (C++)

| Item | tenfold now | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| N1 Go To Definition / Declaration keys - **implemented (VS-NAV)**: F12 / Ctrl+F12 / Shift+F12 already are the 10x keys that match VS; Go To Document on `#include` added (F12, Ctrl+F12 and the 10x middle-click on the directive open the file) | F12-like defs | F12 = Go To Definition; Ctrl+F12 = **Go To Declaration** (C++ docs; in C# Ctrl+F12 is Go To Implementation); Ctrl+Click = Go To Definition (modifier Ctrl/Alt/Ctrl+Alt, "Open definition in peek view" option); **Go To Document** on an `#include`. | [S19], [S20], [S10] | high | keep 10x keys; add Go To Document on `#include`. |
| N2 F12 on a definition | goes to declaration | Not documented. From memory in C++, F12 on a function **definition** (already at the definition) jumps to the declaration; F12 on a declaration jumps to the definition (toggle). | memory | low | Keep; test V9. |
| N3 Multiple results | picker (decision: choosers for alias/ctor only) | Not documented for F12. Peek Definition (Alt+F12) shows several definitions with a pager ("1 of N") and a breadcrumb path; Find All References / Find Symbol Results list in a tool window. From memory F12 with several candidate definitions (e.g. a class in two files, overloads of equal rank) opens the **Find Symbol Results**? list - unverified. | [S20], [S19] | low | Keep the picker; use it for ambiguous results. V9. |
| N4 Peek | Peek exists | Alt+F12 embeds the definition in an inline window, editable, with breadcrumb navigation (peek from a peek), Esc closes; closing closes breadcrumb files. | [S20] | high | keep. |
| N5 Find All References window - **partly implemented (VS-NAV)**: the list is grouped (Definition first, then one heading per file), shows Line:Col, the source line and Read / Write / Definition / Other; no Group-by menu, scope drop-down, filter box, keep results or checkbox column (the picker is a modal list, not a tool window); Confirmed / Disconfirmed does not exist in clangd | picker with results | Tool window "<symbol> references" with columns (checkbox, Kind/Read-Write, File, Line, Col, Code Preview), toolbar scope drop-down (open docs/changed docs/project/solution), Group by (Project then Definition [default], Definition Only, Definition then Project, Definition then Path, Definition-Project-Path), filter by column, search box, Keep Results, tree vs list; **incremental**; results classified **Confirmed / Disconfirmed / Unprocessed** (Disconfirmed hidden by default); C++ column Read/Write/Other; F8/Shift+F8 or Enter/double-click navigates; hovering previews. Preview in editor on selection controlled by "Preview selected files in Find Results". | [S21], [S22], [S19] | high | Result list: group by definition then file; add Read/Write/Other classification (clangd gives `references` with role only partially); keep hidden "disconfirmed" out. |
| N6 FAR of a virtual function | whole override family up/down one level | Not documented. From memory the C++ engine returns references to the exact function and its overrides' declarations if you start on the base; starting on an override also lists the base's... (unverified). | memory | low | Keep clangd's behaviour (>=). V10. |
| N7 Implicit range-for begin/end | clangd: counted as references (xfail) | Not documented; from memory VS does not list the implicit `begin()/end()` calls of a range-for as references. | memory | low | Treat as not references (xfail stays; the expectation file is right). |
| N8 Go To Implementation - **no change by design (VS-NAV)** | `impl` one level (clangd) | In C++ Ctrl+F12 is *Go To Declaration* (no separate implementation command in the C++ docs); C#/VB have Go To Implementation (Ctrl+F12) listing implementing/overriding members in a list. | [S19] | medium | Keep `impl`; document that VS has no C++ equivalent (no change). |
| N9 Definition vs declaration of a variable with several tentative definitions | any | C++ `int g; int g; int g = 100;`: F12 goes to the first definition found; unverified. | memory | low | keep. |

---

## 11. Solution Explorer / workspace tree

| Item | tenfold now | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| X1 Sort | folders/projects first then files, case-insensitive | Alphabetical; within a C++ project, filters (virtual folders) first then files; "External Dependencies" node in C++ projects; solution folders before projects. | [S27], memory | medium | keep; optionally add "External Dependencies" (header include set). **Sort kept (VS-TREE); External Dependencies not added (optional).** |
| X2 Filters vs folders ("Show All Files") | filters from `.vcxproj.filters`, else folders | Default Solution view shows project items by **filters**; the toolbar **Show All Files** shows the folders/files on disk (including not-in-project, dimmed); **Switch Views** toggles Solution view / Folder view (on-disk folder tree). | [S27] | high | Add a view toggle: Solution (filters) <-> Folder (disk) and Show All Files (dim files not in the project). **Implemented (VS-TREE):** `View.ToggleExplorerFolderView`, `View.ToggleExplorerShowAllFiles`, context-menu items; Show All Files applies to the Solution view and uses the workspace scan's files. |
| X3 Single vs double click | single click selects, double click opens | Single click selects and, with the "Preview Selected Items" toggle (historically on), opens the file in the **preview tab**; double-click opens it permanently; Enter opens; double-clicking a project node opens the project file only if the option "Open project files with double-click or the Enter key when supported" is set (otherwise expands). | [S26], [S27], [S23], [S51] | medium | Implement T2; double-click on a project/solution node expands. **Implemented (T2 by VS-TABS; the project/solution double click by VS-TREE: it expands, a project file is never opened).** |
| X4 Track active item | `explorer.trackActiveFile` false (10x) | Option "Track active item in Solution Explorer" is **enabled by default per the 2026-revised Learn page** ("This option is enabled by default"); a 2018 blog says it is not the default in VS (2017-2019) and VS 2022 shows a toolbar button **Sync with Active Document** (Ctrl+[, S). Conflict. | [S26] vs [S55] | low | 10x documents `TrackActiveFileInWorkspaceTree` default false -> 10x answer wins; offer the sync command (`ShowInWorkspaceTree` already). No change. **Skipped (low confidence; unchanged).** |
| X5 Keyboard | typing filters the tree | The tree view uses standard **type-ahead**: typing jumps to the next node whose name starts with the typed characters (it does not filter); Ctrl+; moves to the Solution Explorer search box (filters results, Back/Forward over results); Left/Right collapse/expand; * expands all; Enter opens; F2 renames; Delete removes (Delete key: Remove vs Delete from disk per node type, Shift+Delete deletes). | [S27] (search bar), memory | medium | Change plain typing to **jump** (type-ahead with 1 s reset); keep filtering behind `Ctrl+;` / a filter key. **Implemented (VS-TREE):** jump; Ctrl+; filters (10x documents neither). Left/Right, Enter as before; F2 renames and Delete deletes while the tree is focused; `*` (expand all) was not added. |
| X6 Drag and drop | not implemented | Drag files/folders within a project to move them (confirmation "Prompt before moving files to a new location", option on by default); drag a file onto the editor to open it; drop files from Explorer to add; Ctrl-drag copies. | [S26] | high (prompt option), medium (rest) | Implement move within the tree with the confirmation; `EnableWorkspaceTreeDragDrop` (10x) stays the on/off switch. **Implemented (VS-TREE):** move with the confirmation (`explorer.promptBeforeMoving`), Ctrl-drag copies, `explorer.dragDrop` is the switch. Not done: drag onto the editor to open, drop from the file manager (medium), drop onto a filter. |
| X7 File operations | none | Add > New Item / Existing Item / New Folder (or Filter), Rename (F2; with "symbolic renaming" prompt for code files: "Enable symbolic renaming when renaming files" / "Prompt for symbolic renaming"), Delete/Remove, Cut/Copy/Paste, Open Folder in File Explorer, Open in Terminal. | [S26] | high | Phase after the tree: New file/folder, Rename, Delete (to the recycle bin with confirm), Reveal. **Implemented (VS-TREE):** New File / New Folder / Rename (F2) / Delete (to the Recycle Bin / Trash, confirmed; permanent only where there is no trash, with that wording) that keep `.vcxproj` / `.filters` Include values in step; Reveal was already "Open Containing Folder". "Symbolic renaming" prompts are not done. |
| X8 Open by default | closed at start | Solution Explorer is docked top-right and open by default (Ctrl+Alt+L). | [S27] | high | Default open when a solution is loaded (`layout.json` still remembers). **Implemented (VS-TREE):** only when no layout was restored; `explorer.openWithSolution`. |
| X9 Restore tree state | not saved | Option "Restore Solution Explorer project hierarchy state on solution load" (on by default). | [S26] | high | Persist expansion state in `.tenfold/layout.json`. **Implemented (VS-TREE)** in `.tenfold/explorer.json` (see OPEN_QUESTIONS "VS-TREE detail"). |
| X10 Search | filter | Search bar over names; dropdown "Search within file contents", "Search within external items"; results list with Back/Forward. | [S27] | high | optional. **Not done (optional):** the search dropdown and result list; the fuzzy filter (Ctrl+;) stays. |

---

## 12. Integrated terminal

| Item | tenfold now | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| M1 Copy/paste keys | Ctrl+C copies a selection (and clears it), no selection = ^C; Ctrl+V pastes; right click pastes/copies | Microsoft (Developer Community reply, VS 2022 17.10): **Ctrl+C is SIGINT** and Ctrl+V a literal character in the PTY; **Ctrl+Shift+C / Ctrl+Shift+V copy / paste**; "right mouse click" pastes; configurable under Tools > Options > Environment > Keyboard (commands such as `View.Terminal`; copy/paste in "Terminal" scope). Toolbar has Copy/Paste buttons. | [S28] (snippet) | medium | **Change**: Ctrl+C always goes to the program (^C); copy = Ctrl+Shift+C; paste = Ctrl+Shift+V (and Shift+Insert/right click); the user decision "follow Visual Studio's terminal" in OPEN_QUESTIONS (W8f terminal detail) is wrong for Ctrl+C. Optionally "Ctrl+C copies when a selection exists" like Windows Terminal, as a setting (default off). **Implemented (VS-TERM)**: `Terminal.Copy` / `Terminal.Paste` in the key context `terminal`; setting `terminal.ctrlCCopiesSelection`. |
| M2 Right-click | pastes/copies | right mouse click pastes. | [S28] | medium | Right click = paste (copy via Ctrl+Shift+C). With a selection, do not copy on right-click. **Implemented (VS-TERM).** |
| M3 Default shell | `cmd.exe` setting = $SHELL elsewhere | VS lists two built-in profiles - **Developer Command Prompt** and **Developer PowerShell** (Windows PowerShell 5) - and you can add your own shells (path/arguments) and pick the default under Tools > Options > Environment > Terminal. Default: Developer PowerShell. | [S29] | medium | Default `terminal.command` to PowerShell on Windows (`pwsh.exe` if found, else `powershell.exe`); keep the 10x setting TerminalCommand authoritative when set (10x doc'd). **Implemented (VS-TERM)**: `terminal.command` now defaults to empty = this choice (`Pty::chooseTerminalShell`). |
| M4 Working directory | workspace root | "opens in repository root by default"; "Open in Terminal" on a project/folder node opens in that folder. | [S29] | medium | Add "Open in Terminal" to explorer/tab menus (already `Terminal` per node path). **Implemented (VS-TERM)** for the explorer menu (`Terminal.OpenAt`); the tab menu is not done. |
| M5 Process exit | tab stays with final screen + `[process exited with code N]`; `terminal.closeOnExit` false | Not documented; from memory the terminal tab stays open showing the shell's last output, and a new instance is started by "New Terminal"; typing after exit does nothing. | memory | low | keep. |
| M6 Keys that stay with VS | terminal, Workbench.*, quit, palette, toggles | VS terminal passes most keys to the shell but keeps VS shortcuts that are registered with the "Terminal" scope: Ctrl+Tab (IDE Navigator), Alt+... menu accelerators, Ctrl+Shift+C/V copy/paste, Ctrl+` open. | [S28], memory | low | keep. |
| M7 Palette/background | Campbell, #1E1E1E | not documented; keep. | - | - | none |

---

## 13. IntelliSense evaluation (OPEN_QUESTIONS #2 and W9 detail)

| Item | tenfold now | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| I1 Project mapped but not "Build" ticked | evaluated with the mapped configuration | Not documented. The IntelliSense compiler is driven by **IntelliSense-only (design-time) builds** that "are launched as background tasks in response to certain changes made in the project settings or source files"; it "doesn't produce binaries". The Build checkbox governs `Build`, not design-time builds; loaded projects get IntelliSense for the **active solution configuration's mapped project configuration**. So the default of W9-FIX-vs is the best reading. | [S30], [S56] (cppblog: log shows `Configuration Name: Debug\|Win32`) | medium-low | keep. Unloaded projects (Unload Project) get none. V11. |
| I2 Switching configuration | restart clangd, keep indexes for 4 configs | VS re-runs the IntelliSense build for the new configuration, updating squiggles/IntelliSense; "force a rescan ... Rescan Solution". | [S30] | medium | keep. |
| I3 Include paths and macros - **already there** (View > compile command info, D4; `tenfold-cli compdb`) | MSBuild evaluated | Same: IntelliSense uses `/I`, `/D`, `/FI` of the project and logs them (Tools > Options > Text Editor > C/C++ > Advanced > Diagnostic Logging); unevaluated `$(Var)` removed from the path. | [S30], [S56] | high | Provide a command printing the compile command for a file (`tenfold-cli compdb` exists). |
| I4 Headers / non-self-contained `.inl`/`.tpp` - **not done (medium-low; the includer-context part exists since W9-FIX-B; the no-squiggles part is I5)** | parsed standalone (STRESS_RESULTS F6) | VS parses a header in the context of a translation unit that includes it (the Navigation Bar shows the project/context drop-down; a header in no project is "Miscellaneous Files" with single-file IntelliSense and no squiggles by default). A non-self-contained file that is only included mid-way (inside a class) shows spurious errors when viewed on its own, unless a TU includes it in the right place; the usual advice is self-contained includes. | [S30] (single-file IntelliSense, Navigation Bar), [S57] (forum) | medium-low | Supply the includer's compile command: for a file not in the database that is `#include`d by one that is, use the first includer's command and `-include` the includer chain (STRESS_RESULTS question 1/5); show no squiggles for files in no TU (VS "Miscellaneous Files" rule). |
| I5 Squiggles in single files - **implemented (VS-NAV)**: `diagnostics.miscFiles` (default false) | - | "When you open a file that isn't included in a project, Visual Studio provides some IntelliSense support, but by default no error squiggles are shown." | [S30] | high | For files outside every TU, suppress diagnostics (setting `diagnostics.miscFiles`, default off). |
| I6 Error cap | - | IntelliSense produces at most 1,000 errors; over that, one squiggle at the file start. | [S30] | high | cosmetic |

---

## 14. Other items from the lists (OPEN_QUESTIONS detail sections, W8b-W8f, #15, #17, #18)

| Item | tenfold now | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| Z1 Auto-close quotes (`editor.autoCloseQuotes` off, tenfold-only) | off | VS C++ has **Automatic Brace Completion** (Tools > Options > Text Editor > C/C++ > General > "Automatic brace completion", on by default) completing `( [ { " '` and typing the closer steps over it; Backspace in an empty pair deletes both. (10x defines `AutoAddCloseBrace/Bracket` false - 10x wins for those two; quotes are docs-silent.) | memory; [S11] ("Auto list members" etc.) | low | Set `editor.autoCloseQuotes` default **true** for `"` (not `'` after identifier chars) to follow VS; keep braces/brackets per 10x. **implemented (VS-EDIT)**: **not enabled by default** (Conf. low): the pairing code and `editor.autoCloseQuotes` already exist and stay off; verification item V12. |
| Z2 Wrap selection | `bracketSelectedText` true (10x) | VS 2022 17.5+: typing `(`, `[`, `{`, `"`, `'` with a selection surrounds it ("Surround selection on typing quote or brace"). | memory | low | n/a (10x documented). Not changed (10x documented). |
| Z3 Line comment | `//` at minimum indentation | VS Comment Selection (Ctrl+K,Ctrl+C) / Uncomment (Ctrl+K,Ctrl+U) put `//` in front of the text of the selected lines; the exact column (column 0 vs common indentation) could not be established; newer 17.x builds also have Toggle Line Comment (Ctrl+/). | memory | low | keep. Not changed (low confidence). |
| Z4 Copy/cut with no selection | CutLine command | VS: copies/cuts the **whole line incl. EOL**; blank lines only if the option (default on) is set. | [S11] | high | `Copy` with no selection copies the line (when `editor.copyLineWhenEmpty`, default true). **implemented (VS-EDIT)**: copy and cut take the line with its terminator (done earlier by W10-LE); new `editor.copyLineWhenEmpty` (true) and `editor.copyBlankLines` (true, VS "Apply Cut or Copy commands to blank lines when there is no selection"). |
| Z5 Highlight current line | on (outline) | "displays a gray box around the line" - default on. | [S10] | high | ✓ no change. Nothing to do. |
| Z6 Word wrap default | true (user decision #4) | VS default is **off**; "Show visual glyphs for word wrap" return arrow; "Virtual space" disabled with wrap. | [S11] | high | Decision #4 stays (user); if "match VS" applies, set false - flag to the user. **Not changed**: user decision #4 (word wrap ON). |
| Z7 Scroll wheel | 3 lines | "Vertical scrolling sensitivity (lines per scroll) ... default value is 3"; horizontal 'characters per scroll'. | [S33] | high | ✓ Already `editor.wheelLines` = 3; nothing to change (VS-EDIT checked). |
| Z8 Zoom | scales whole UI | VS zooms only the **editor text** (Ctrl+scroll), shown as a "100 %" box in the editor margin; per window; not persisted. | memory | medium | Make ZoomIn/ZoomOut scale only editor text (not tabs/status). **implemented (VS-EDIT)**: only the editor text (and the terminal's default font) is zoomed; the status bar shows "N %" while zoomed (VS shows "100 %" permanently; hidden at 100 % so no golden changes); click resets; Control + wheel zooms (10x MouseWheelZoom, `editor.mouseWheelZoom`). 10x ZoomIn/ZoomOut keys and the one-point step are unchanged. |
| Z9 Find/replace | bar with Alt+C/W/R | VS Find (Ctrl+F) is an overlay at the top right of the editor, options: Match case, Match whole word, Use regular expressions (Alt+C / Alt+W / Alt+E), scope selector; Replace All is **one undo unit**; Enter next, Shift+Enter previous, Esc closes; Find in Files shows the Find Results window. | memory, [S19] | medium | Alt+R -> Alt+E for regex in the bar (tenfold already supports Alt+E). **Not changed**: 10x documents Alt+R for regex (Alt+E already also works). |
| Z10 Bookmarks | persist in `.tenfold/bookmarks.json`, next/prev wrap | VS bookmarks: toggle Ctrl+K,Ctrl+K, next Ctrl+K,Ctrl+N, previous Ctrl+K,Ctrl+P (they wrap through the whole solution), Bookmark window (Ctrl+K,Ctrl+W) with folders; stored per solution in the user options file (.suo) - survive sessions. | memory | medium | ✓ (10x documented command set). Nothing to do. |
| Z11 Paste history | last 20, picker | VS **Clipboard Ring**: holds the last **20** items; Ctrl+Shift+V cycles through them in place (repeated presses replace the pasted text with the next older item). | memory | medium | keep the picker (10x `PasteHistory` shows a dropdown); optional cycling command. Already 20 (`editor.pasteHistorySize`); the picker stays (Ctrl+Shift+V is 10x's PasteHistory, so VS's in-place cycling on the same key would conflict) - nothing implemented. |
| Z12 Key-sequence recording | tenfold recorder | VS has no keyboard macro recorder since VS 2012 (macros removed; extensions only). | memory | medium | "no VS equivalent - keep default". Not changed (user instruction: the 10x recorder stays). |
| Z13 Focus mode | panel only | VS **Full Screen** (Shift+Alt+Enter) hides toolbars and tool windows, keeps the editor; Window > Auto Hide All. | memory | medium | keep (10x concept) Not changed. |
| Z14 NewTextFile | "Untitled N" in workspace | VS File > New > File: "Text File" -> `TextFile1.txt`, "C++ File (.cpp)" -> `Source1.cpp`, "Header File (.h)" -> `Header1.h`; Save As dialog on first save. | memory | low | name by type: `TextFile<N>.txt` when no extension chosen. **built behind `editor.textFileNames` (default false)** (Conf. low): `TextFile<N>.txt`, lowest free N, always `.txt` (no file-type chooser); default stays `Untitled N`; verification item V13. |
| Z15 Startup attach to VS (#15) | attaches to a running VS | No VS-side behaviour to match; 10x documents VisualStudioSync. | - | - | keep default (no VS equivalent). |
| Z16 Perforce commands (#17) | fake p4 only | P4VS uses `p4 info`, `p4 where`, `p4 fstat`, `p4 edit`... through the P4API (not the CLI); can't be compared. | [S31] | - | keep; user must check on a server. |
| Z17 Licence (#18) | none | n/a | - | - | n/a |
| Z18 Multi-caret | 10x commands | VS 2022 multi-caret: Ctrl+Alt+click adds a caret, Alt+click-drag box selection, Ctrl+Alt+drag...; Shift+Alt+. selects next occurrence, Shift+Alt+; all occurrences; Esc returns to one caret. 10x commands are answered by docs. | [S33] (box selection option), memory | low | n/a Not changed (low confidence; Ctrl/Alt-click add caret and Alt-drag box selection already exist). |
| Z19 Trailing whitespace | off | VS does not trim on save by default; `.editorconfig` `trim_trailing_whitespace` is honoured when formatting; Edit > Advanced > Delete Horizontal White Space. | [S9], [S11] | medium | keep off; read `.editorconfig` `trim_trailing_whitespace` / `insert_final_newline` as extra defaults. **implemented (VS-EDIT)**: `core/EditorConfig.*` reads `trim_trailing_whitespace` and `insert_final_newline`; applied on save only with `editor.useEditorConfig` (default **false**: no Microsoft source found that VS applies them on save rather than on Format Document / Code Cleanup; V14); `editor.trimTrailingWhitespace` stays false. |
| Z20 Indent | Tab inserts a unit; Shift+Tab outdents | Same in VS; with a multi-line selection Tab indents lines; "Insert spaces / Keep tabs", tab size default 4, indent size 4, Smart indenting for C++. | [S11] | high | ✓ Nothing to do (already so). |

---

## 15. Quick actions, call hierarchy, inlay hints (W11-GUI-LSP)

| Item | tenfold | VS 2022 | Source | Conf. | Change |
|---|---|---|---|---|---|
| Q1 Quick Actions key and menu | `Editor.QuickActions` on **Ctrl+.**, first item of the editor context menu "Quick Actions and Refactorings..." | "Right-click a line of code and choose Quick Actions and Refactorings ... Select Ctrl + period (.) anywhere on a line." VS also lists Alt+Enter; Alt+Enter is 10x's `ToggleFullscreen`, so tenfold does not bind it. Ctrl+. is not bound in 10x's map. | [S58] | high | **Implemented (W11-GUI-LSP).** Ctrl+. works anywhere on the line: tenfold asks clangd for the caret's line (trimmed of blanks) or the selection. |
| Q2 Light bulb | a light bulb in the gutter, left of the line number, on the caret's line when clangd has a quick fix; click opens the list; red when the line has an error | Icons beside the line: **screwdriver** (suggested action to improve code), **yellow light bulb** (recommended action for noncritical issues), **error light bulb** (critical action to fix an error). "Move the insertion cursor into a line of code, and check for Quick Action icons that appear inline" | [S58] | high (icons), low (exact glyph / hex colours) | **Implemented** for the yellow and the red bulb. **Not done:** the screwdriver: clangd answers a refactoring ("Extract subexpression to variable") for almost any line, so it would be everywhere; Ctrl+. still lists those actions. Colours `lightBulb` #FFCC00 and `lightBulbError` #F14C4C are guesses (`[?]`). |
| Q3 Applying an action | one undo step per file | "To apply the recommended change to your code, select the fix from the menu." Undo of a Quick Action is one step in VS (a single edit transaction). | [S58], memory | medium | **Implemented** (`Editor::applyCodeAction`, one undo group per document). No preview of the change (VS has "Preview changes" for some actions): not done. |
| Q4 Call Hierarchy | **Ctrl+K, Ctrl+T** (`View.CallHierarchy`), the Hierarchy panel in the bottom dock, editor menu item "View Call Hierarchy" | "Right-click on the name of a method ... select View Call Hierarchy. The member name appears in a tree view pane in the Call Hierarchy window. If you expand the member node, **Calls To** *member*, and for C++, **Calls From** *member*, subnodes appear ... You can then expand each calling member to see its Calls To, and for C++, Calls From nodes." For virtual / abstract members an **Overrides** node appears. C++ implicit calls (constructors, conversions) may be missing. Shortcut Ctrl+K, Ctrl+T (editor menu in section 8 C1). | [S59], [S19] | high | **Implemented.** Every entry has Calls To and Calls From nodes (type entries have Base / Derived), lazily asked. Not done: the Overrides / Implements nodes and VS's details pane of call sites. The first group is opened at once (VS shows both collapsed). |
| Q5 Type hierarchy | `View.TypeHierarchy`, command palette and editor menu, no key | Visual Studio has no C++ type hierarchy window (Class View and Object Browser show bases, not this). | memory | medium | **Implemented as a tenfold addition** in the same panel; no key because no free key is obvious. |
| Q6 Inlay hints (C++) | `editor.inlayHints` = `off` / `on` / `while-pressed`, default `while-pressed`; parameter names and deduced types; dim text; the setting description names the VS option Tools > Options > Text Editor > C/C++ > IntelliSense > Inlay Hints | The C++ inlay hint options live under Text Editor > C/C++ > IntelliSense (a shorter list than C#'s). Hints are off until asked; **Alt+F1** shows them while it is held (a Stack Overflow answer for 17.2 Preview also names pressing Ctrl twice, and says Alt+F1 is Visual Assist's). The Learn page of the option could not be found (404) and the C++ hints page does not exist; this row rests on a forum answer and the user's description. | search result (Stack Overflow, "inline hints on/off shortcut in visual studio 2022"), user brief | low | **Implemented.** Default `while-pressed`: nothing is shown until Alt+F1 is held, which is how VS behaves out of the box; `off` also disables the key, `on` shows them always. Verify in a real VS 2022 (checklist V15). |
| Q7 Inlay hint colours | `inlayHint` #A9A8A7 on `inlayHintBackground` #3E3E3E in vs-dark; other themes derive them from the text colour | #3E3E3E background, #A9A8A7 foreground (the cpptools "Visual Studio 2019 Dark (C++)" theme; section 5.1) | [S35] | medium | **Implemented** (theme keys with fallbacks). |
| Q8 Inlay hints as text | virtual: they push the text right but are not part of the document or of any column; a caret never stops inside one | VS draws hints inline between the characters; the caret skips them, they are not text. | memory | medium | **Implemented** (`InlayMap`, `TextView::hintShift`). The caret before a parameter hint is drawn after the hint; a caret after a type hint's anchor is drawn before it. |

## Proposed implementation tasks (grouped; one task = one area)

| Task | Area | Files likely touched | Depends |
|---|---|---|---|
| **VS-LE** | Line endings per line (sec. 1.3): per-line terminators, Enter/paste/copy rules, load consistency dialog, Advanced Save Options dialog, status bar + menu, `.editorconfig` `end_of_line`, VCS text compare | `src/core/TextBuffer.*`, `src/core/Document.*`, `src/app/EditorEditing.cpp`, `src/app/Editor.*` (paste/copy, setLineEnding), `src/ui/shell/EditorApp.*` (dialog, status segment), `src/semantic/SemanticEngine.cpp` (`lspLineEnding`), `src/vcs/*`, settings, tests | none; coordinate with the encodings task (W10-refuse-unencodable) on `Document::save` |
| **VS-ENC** (done) | Encoding widget, Reopen/Save with Encoding, auto-detect UTF-8 option, `GetACP()` fallback, `.editorconfig charset` | `src/core/Document.*`, `src/core/Encoding*`, `src/ui/shell/EditorApp.*`, `src/app/Editor.*` | shares the Save-with dialog with VS-LE |
| **VS-UNDO** | Undo grouping: typing-run groups (no timer), multi-caret per keystroke | `src/core/UndoHistory*`/`TextBuffer`, `src/app/Editor*.cpp`, settings `undoGrouping` | verify V6 first |
| **VS-THEME** | `vs-dark.txt` corrections (sec. 5), new keys, preview-tab purple, hover colours | `assets/themes/vs-dark.txt`, theme loader/keys, `src/ui/*` painting of tabs/menus/scrollbars | none |
| **VS-TABS** (done: T1, T2, T6, T9, T11, T12; the rest of the row is T3/T4/T5/T7/T8/T10/T13, see section 7) | Preview tab, MRU Ctrl+Tab behaviour, tab menu items, one list save dialog for bulk close, emptied panel auto-close, external-change prompt default | `src/ui/shell/EditorApp.*`, `src/ui/widgets/Tab*`, `src/app/Editor.*`, settings | VS-TREE for single-click preview |
| **VS-MENUS** | Editor/explorer/tab context menu contents and order, Properties item, "Compile" | `src/ui/shell/EditorApp.cpp` (explorerMenu etc.), command registry | none |
| **VS-OUTLINE** | Fold glyphs default on, `ToggleAll` semantics, Collapse to Definitions, hover tooltip, outlining settings | `src/ui/views/*Fold*`, `src/app/Editor*Fold*`, settings, golden images | none |
| **VS-NAV** (done: P1, N1 Go To Document, N5 partly, I5; not done: P2 optional, N2/N3/N6/N7/N9 low, N4/N8 no change, I1 medium-low, I4 medium-low, I6 cosmetic) | Symbol search preview pane toggle, FAR grouping/columns, Go To Document on `#include`, misc-files diagnostics rule, non-self-contained header context | `src/ui/features/*`, `src/semantic/SemanticEngine.*`, `src/compdb/*` | clangd |
| **VS-TREE** (implemented: X2, X3, X5, X6, X7, X8, X9, C10; skipped: X1 optional, X4 low, X10 optional, C6-C9 medium-low) | Solution Explorer: type-ahead, view toggle (Solution/Folder, Show All Files), default-open, state persistence, drag-drop, file operations, preview on click | `src/ui/features/Explorer*`, `src/ui/features/WorkspaceExplorer.*`, `src/ui/widgets/TreeView.*` | VS-TABS |
| **VS-TERM** | Terminal key bindings (Ctrl+C = SIGINT, Ctrl+Shift+C/V), default shell, Open in Terminal | `src/ui/features/Terminal*`, `src/term/*`, key map defaults | none |
| **VS-VCS** (implemented: VC3, VC5, VC6; VC1 and VC4 need no change; skipped: VC2 low) | Marks for untracked/added in Perforce (blue ?, red plus, red check, yellow triangle), `perforce.promptOnEdit`, P4 history actions | `src/vcs/*`, `src/ui/features/VcsUi.cpp`, `src/ui/features/PerforceUi.cpp`, `src/ui/widgets/VcsBadge.h`, tab markers | V7 (Git untracked) |
| **VS-EDIT** (done: Z4, Z8; built but off by default pending verification: Z1, Z14, Z19; Z7, Z11, Z20 already matched; skipped Z2, Z3, Z6, Z9, Z12, Z13, Z18 and the rows without a change) | Auto-close quotes default, copy line, zoom editor-only | `src/app/EditorEditing.cpp`, `src/ui/shell/EditorApp.*`, settings | none |

## Items where Visual Studio's behaviour could not be established

1. Which existing line supplies the ending on Enter (caret line / previous line / first line) - only the option's semantics are documented (L6).
2. Whether paste into an LF file preserves CRLF in VS 2022 (L8); whether copy puts LF or CRLF on the clipboard.
3. Pre-selected entry, exact title, the "do not ask again" check box and the NEL entry of the inconsistent-line-endings dialog (L4).
4. What clicking the line-ending indicator does in 17.x, and whether a mixed file shows a distinct label (L14).
5. Undo grouping rules (U1, U2): no documentation; memory only.
6. Whether VS paints an untracked file as wholly added (V1/V2): documentation and a Microsoft blog comment indicate it does not.
7. Exact hex of: selection (active), current-line outline, brace match, find highlight, squiggles, Git margin colours, tooltips, VS terminal palette, debugging/no-solution status bar (sec. 5).
8. F12 behaviour on a definition and with several candidates; FAR on virtual functions; range-for begin/end (N2, N3, N6, N7).
9. "Close All But This" with pinned tabs; the Ctrl+Tab MRU order details (T1, T5).
10. "Track active item" default (X4: sources conflict), terminal exit behaviour (M5), whether the Build checkbox affects IntelliSense (I1).
11. Outlining of `/* */` comments and `#if` blocks in C++ and whether collapse state is remembered (O1, O7).

## V. Verification checklist (10 minutes in a real Visual Studio 2022)

* **V1** Open a file with lines 1-3 = `a\r\n`, `b\n`, `c\r` -> does the dialog appear (title, entries, pre-selection)? What does the bottom margin show? Click it.
* **V2** In a CRLF file press Enter at the end of line 1, of a middle LF line, and on the last unterminated line; hex-dump. Repeat in an LF-only file and in an empty new file.
* **V3** Copy two lines from Notepad (CRLF) and paste into an LF file; save; hex-dump. Copy two lines out of the LF file in VS and paste into Notepad.
* **V4** Save a mixed file with plain Save: unchanged? Then Save As > Save with Encoding > Line endings: Unix (LF): are all lines LF? Does the next plain Save keep LF?
* **V5** `.editorconfig` with `end_of_line = lf` next to an LF project: Enter in a new file; Enter in an existing CRLF file.
* **V6** Type `hello world`, Enter, `foo`; press Ctrl+Z repeatedly and note each step; also type, move the caret with an arrow, type again; type with two carets.
* **V7** `git init`, add an untracked `.cpp` to the solution; look at the editor margin, the Solution Explorer glyph and the Git Changes window. Perforce: same with a file not in the depot. (VS-VCS left VC2, the Git glyphs, alone until this is answered; for Perforce also compare the P4VS badge colours with `tabVcsOpenedForAdd` / `NotInDepot` / `OutOfDate` / `Conflict` in vs-dark.txt, and check whether P4VS asks again at save after you declined the first edit.)
* **V8** Pin two tabs, "Close All But This" on an unpinned tab: do the pinned tabs close? Dirty tab + Close All: one dialog?
* **V9** F12 on a function definition; F12 on a name with two definitions; screenshot the result UI.
* **V10** Find All References on `Base::f` with overrides, and on a range-for container.
* **V12** (Z1) C++ file: type `"` and `'` (after a space and after a letter), type the closing quote, Backspace in an empty pair, a quote over a selection: which pair, which step over?
* **V13** (Z14) File > New > File: exact name offered for a Text File, a C++ File and a Header File (`TextFile1.txt`, `Source1.cpp`, `Header1.h`?), and the first-save dialog.
* **V14** (Z19) `.editorconfig` with `trim_trailing_whitespace = true` and `insert_final_newline = true`: does a plain Ctrl+S change the file, or only Format Document / Code Cleanup?
* **V15** (Q6) Tools > Options > Text Editor > C/C++ > IntelliSense: is there an Inlay Hints option, what is its default, does Alt+F1 show hints when the option is off, and is the hold a toggle?
* **V11** Uncheck Build for a project in Configuration Manager; open one of its files; confirm IntelliSense still shows.

## S. Sources

* [S1] https://learn.microsoft.com/en-us/visualstudio/ide/reference/documents-environment-options-dialog-box?view=vs-2022
* [S2] https://learn.microsoft.com/en-us/visualstudio/ide/encodings-and-line-breaks?view=vs-2022
* [S3] https://learn.microsoft.com/en-ca/dotnet/api/microsoft.visualstudio.text.editor.defaultoptions.replicatenewlinecharacteroptionid?view=visualstudiosdk-2022
* [S4] https://learn.microsoft.com/en-us/dotnet/api/microsoft.visualstudio.text.editor.defaultoptions.newlinecharacteroptionid?view=visualstudiosdk-2022
* [S5] https://stackoverflow.com/questions/553548/what-does-visual-studio-mean-by-normalize-inconsistent-line-endings (read through a mirror)
* [S6] https://stackoverflow.com/questions/3994909/stop-visual-studio-from-mixing-line-endings-in-files (read through a mirror)
* [S7] https://manski.net/articles/csharp-dotnet/line-endings
* [S8] https://learn.microsoft.com/en-gb/answers/questions/1280150/visual-studio-2022-still-save-files-with-mixed-lin
* [S9] https://learn.microsoft.com/en-us/visualstudio/ide/create-portable-custom-editor-options?view=vs-2022
* [S10] https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2019/ide/reference/options-text-editor-general
* [S11] https://learn.microsoft.com/en-us/visualstudio/ide/writing-code-in-the-code-and-text-editor?view=vs-2022 (editor margin, customisation) and the All Languages General page (VS 2017 text) https://learn.microsoft.com/bg-bg/visualstudio/ide/reference/options-text-editor-all-languages?view=vs-2022 (English content)
* [S12] https://learn.microsoft.com/en-ca/previous-versions/visualstudio/visual-studio-2017/ide/how-to-save-and-open-files-with-encoding?view=vs-2017
* [S13] https://devblogs.microsoft.com/visualstudio/set-the-default-file-encoding/
* [S14] https://learn.microsoft.com/en-us/visualstudio/ide/outlining?view=vs-2022
* [S15] https://learn.microsoft.com/en-us/visualstudio/ide/reference/options-text-editor-c-cpp-view?view=vs-2022
* [S16] https://learn.microsoft.com/en-us/visualstudio/ide/go-to?view=vs-2022
* [S17] https://learn.microsoft.com/en-us/visualstudio/ide/visual-studio-search?view=vs-2022
* [S18] https://ardalis.com/finding-things-in-visual-studio-2017/
* [S19] https://learn.microsoft.com/en-us/cpp/ide/navigate-code-cpp?view=msvc-170
* [S20] https://learn.microsoft.com/en-us/visualstudio/ide/go-to-and-peek-definition?view=vs-2022
* [S21] https://learn.microsoft.com/en-us/visualstudio/ide/finding-references?view=vs-2022
* [S22] https://devblogs.microsoft.com/cppblog/find-all-references-re-designed-for-larger-searches/
* [S23] https://learn.microsoft.com/en-us/visualstudio/ide/customizing-window-layouts-in-visual-studio?view=vs-2022
* [S24] https://devblogs.microsoft.com/visualstudio/organize-and-revisit-tabs-with-the-keyboard/
* [S25] https://learn.microsoft.com/en-us/visualstudio/ide/how-to-move-around-in-the-visual-studio-ide?view=vs-2022 (search summary only)
* [S26] https://learn.microsoft.com/uk-ua/visualstudio/ide/reference/projects-and-solutions-options-dialog-box?view=vs-2022 (English content)
* [S27] https://learn.microsoft.com/en-us/visualstudio/ide/use-solution-explorer?view=vs-2022
* [S28] https://developercommunity.visualstudio.com/t/Copy-Paste-in-Terminal-Window/10585114 (search summary only; page needs JavaScript)
* [S29] https://www.damirscorner.com/blog/posts/20250117-TerminalProfilesInIdes.html
* [S30] https://learn.microsoft.com/en-us/visualstudio/ide/visual-cpp-intellisense-configuration?view=vs-2022
* [S31] https://ftp.perforce.com/perforce/r20.2/doc/manuals/p4vs.pdf (P4VS User Guide 2020.2; pdftotext)
* [S32] https://learn.microsoft.com/en-us/visualstudio/version-control/git-line-staging?view=vs-2022 and https://devblogs.microsoft.com/visualstudio/git-line-staging-released/
* [S33] https://learn.microsoft.com/en-us/visualstudio/ide/reference/options-text-editor-advanced?view=vs-2022
* [S34] https://learn.microsoft.com/en-us/visualstudio/extensibility/ux-guidelines/color-value-reference-for-visual-studio?view=vs-2022
* [S35] https://github.com/microsoft/vscode-cpptools/blob/main/Themes/themes/cpptools_dark_vs_new.json (name in file: "Visual Studio 2019 Dark (C++)")
* [S36] https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/dark_modern.json (origin of #1F1F1F)
* [S37] https://learn.microsoft.com/en-us/previous-versions/ms181372(v=vs.80) (TFS glyph table)
* [S38] https://www.hanselman.com/blog/visual-studios-most-useful-and-underused-tips (search summary)
* [S40] https://learn.microsoft.com/pl-pl/previous-versions/visualstudio/visual-studio-2008/66d2abf2(v=vs.90) (Advanced Save Options, VS 2008)
* [S41] https://github.com/rolfwr/endoftheline (extension README via search summary)
* [S42] https://developercommunity.visualstudio.com/t/editorconfig-end_of_line-doesnt-work-9/10481082 (search summary)
* [S43] https://learn.microsoft.com/en-us/visualstudio/ide/reference/options-text-editor-general (current page, "Show editing context in the editor")
* [S44] https://learn.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-1-c4819
* [S45] https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2010/ee696312(v=vs.100) (IMergeTextUndoTransactionPolicy) and ITextUndoTransaction pages (search summary)
* [S46] https://dofactory.com/visual-studio-shortcuts (search summary)
* [S47] https://devblogs.microsoft.com/visualstudio/git-line-staging-released/ (comment about new files)
* [S48] https://learn.microsoft.com/en-us/answers/questions/5613304/all-files-from-a-forked-github-repo-are-locked-(bl (search summary)
* [S49] https://learn.microsoft.com/en-us/visualstudio/extensibility/ux-guidelines/theme-color-token-reference?view=visualstudio (VS 2026 Fluent tokens; debugging status bar #7A2101 there; not 2022)
* [S50] https://learn.microsoft.com/en-us/visualstudio/ide/how-to-move-around-in-the-visual-studio-ide?view=vs-2022 (same as S25)
* [S51] https://learn.microsoft.com/en-my/archive/blogs/zainnab/visual-studio-2012-new-features-preview-tab (search summary; single-click preview on by default in 2012/2013)
* [S52] https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2008/fwxk5dad(v=vs.90) (Save Changes dialog; search summary)
* [S53] https://devblogs.microsoft.com/cppblog/c-code-editing-and-navigation-in-visual-studio/
* [S54] same as S53
* [S55] https://mattferderer.com/show-current-active-file-in-vs-studio-explorer (2018)
* [S56] https://devblogs.microsoft.com/cppblog/configuring-c-intellisense-and-browsing/
* [S58] https://learn.microsoft.com/en-us/visualstudio/ide/quick-actions?view=vs-2022 (Quick Actions, light bulbs, and screwdrivers; fetched with curl, read 2026-11-07)
* [S59] https://learn.microsoft.com/en-us/visualstudio/ide/call-hierarchy?view=vs-2022 (View call hierarchy; fetched with curl, read 2026-11-07)
* [S57] https://forum.juce.com/t/intellisense-broken-for-juce-cpp-files-in-solutions/53002 and the Unreal answers thread (search summary)

**Environment note.** `learn.microsoft.com` is blocked for the WebFetch tool in this session ("has to be allowed in the
session's network settings"); the Learn pages were fetched with `curl` through the agent proxy and converted to text.
`github.com` HTML and `api.github.com` were not reachable; `raw.githubusercontent.com` was.
