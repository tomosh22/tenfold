#include "ui/shell/EditorApp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string_view>

#include "ui/commands/CommandNames.h"
#include "ui/commands/EditorCommands.h"
#include "ui/features/ParserCommands.h"
#include "ui/platform/Clipboard.h"
#include "ui/settings/Settings.h"
#include "ui/widgets/TerminalView.h"
#include "ui/widgets/TextView.h"
#include "vcs/VcsExec.h"

namespace tf::ui {

namespace {

constexpr int kMessageMs = 6000, kDiagMs = 12000;

bool anySelection(const Editor& ed) {
  for (const Selection& s : ed.selections())
    if (!s.empty()) return true;
  return false;
}

// Lines of the carets (distinct, document order).
std::vector<int> caretLines(const Editor& ed) {
  std::set<int> lines;
  for (const Selection& s : ed.selections()) lines.insert(s.head.line);
  return {lines.begin(), lines.end()};
}

// Selected text (multi-caret: selections joined with '\n'); without a selection the caret lines (one caret: the line
// plus its break, several: joined with '\n' so that pasting into the same number of carets round-trips).
std::string copyText(Editor& ed) {
  if (anySelection(ed)) return ed.selectedText();
  const TextBuffer& b = ed.currentDocument()->buffer();
  std::string out;
  const std::vector<int> lines = caretLines(ed);
  for (size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + b.line(lines[i]);
  if (lines.size() == 1) out += '\n';
  return out;
}

using SetClipboard = std::function<bool(const std::string&)>;

bool doCopy(Editor& ed, const SetClipboard& set) { return set(copyText(ed)); }

bool doCut(Editor& ed, const SetClipboard& set) {
  if (!doCopy(ed, set)) return false;
  if (anySelection(ed)) {
    ed.insertText("");  // replaces every selection with nothing; empty carets are untouched
    return true;
  }
  const TextBuffer& b = ed.currentDocument()->buffer();
  const std::vector<int> lines = caretLines(ed);
  for (size_t i = lines.size(); i-- > 0;) {  // bottom-up so earlier line numbers stay valid
    const int l = lines[i];
    ed.deleteRange(l + 1 < b.lineCount() ? Range{{l, 0}, {l + 1, 0}}
                                         : Range{{l, 0}, {l, static_cast<int>(b.line(l).size())}});
  }
  return true;
}

// N carets and exactly N lines on the clipboard (a final line break does not count): one line per caret, in document
// order. Anything else pastes the whole text at every caret.
bool pasteText(Editor& ed, std::string t) {
  t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());
  if (t.empty()) return false;
  const size_t carets = ed.selections().size();
  if (carets > 1) {
    std::vector<std::string> lines;
    size_t from = 0;
    for (size_t nl; (nl = t.find('\n', from)) != std::string::npos; from = nl + 1)
      lines.push_back(t.substr(from, nl - from));
    if (from < t.size() || lines.empty()) lines.push_back(t.substr(from));
    if (lines.size() == carets) {
      ed.insertTextPerCaret(lines);
      return true;
    }
  }
  ed.insertText(t);
  return true;
}

bool doPaste(Editor& ed) { return pasteText(ed, clipboardGet()); }

}  // namespace

EditorApp::EditorApp(RootWidget& root, Editor& editor, Workbench* bench, const std::filesystem::path& userDir)
    : root_(root), editor_(editor), bench_(bench), pickerEnv_(root, editor) {
  pickerEnv_.clock = [this] { return now_; };
  pickerEnv_.message = [this](const std::string& m) { setMessage(m, kMessageMs); };
  symbols_ = std::make_unique<SymbolSearch>(pickerEnv_);
  navResults_ = std::make_unique<NavResultPicker>(pickerEnv_);
  find_ = std::make_unique<FindInFiles>(pickerEnv_);
  find_->setMarkersFn([this](const std::filesystem::path& f, const std::vector<Range>& r) {
    forEachView([&](TextView& v) {
      if (v.document() && v.document()->path() == f) v.setSearchMarkers(r);
    });
  });
  replaceInFiles_ = std::make_unique<ReplaceInFiles>(pickerEnv_);
  diagPanel_ = std::make_unique<DiagnosticsPanel>(pickerEnv_);
  findReplace_ = std::make_unique<FindReplace>(pickerEnv_);
  findReplace_->setViewsFn([this](const std::function<void(TextView&)>& fn) { forEachView(fn); });
  findReplace_->setAreaFn([this] {
    RectF r = root_.bounds();
    forEachView([&](TextView& v) {
      if (v.document() == editor_.currentDocument() && v.visible()) r = v.bounds();
    });
    return r;
  });
  goToLine_ = std::make_unique<GoToLine>(pickerEnv_);
  configPicker_ = std::make_unique<ConfigurationPicker>(pickerEnv_);
  bookmarkList_ = std::make_unique<BookmarkList>(pickerEnv_);
  breakpointList_ = std::make_unique<BreakpointList>(pickerEnv_);
  compileInfo_ = std::make_unique<CompileInfo>(pickerEnv_);
  completion_ = std::make_unique<CompletionPopup>(pickerEnv_);
  hover_ = std::make_unique<HoverTooltip>(pickerEnv_);
  sigHelp_ = std::make_unique<SignatureHelp>(pickerEnv_);
  rename_ = std::make_unique<RenameInput>(pickerEnv_);
  rename_->onClosed([this] {  // give the keys back to the editor view
    forEachView([&](TextView& v) {
      if (v.isCurrent() && v.visible()) v.setFocus();
    });
  });
  functions_ = std::make_unique<FunctionPicker>(pickerEnv_);
  if (bench_) files_ = std::make_unique<FileSearch>(pickerEnv_, *bench_);
  unsaved_ = std::make_unique<UnsavedPrompt>(pickerEnv_);
  saveAs_ = std::make_unique<SaveAsPrompt>(pickerEnv_);
  saveRefused_ = std::make_unique<SaveRefusedDialog>(pickerEnv_);
  pickerEnv_.saveRefused = [this](std::vector<SaveRefusal> r) {  // the file's encoding cannot hold a character
    setMessage(SaveRefusedDialog::statusText(r, editor_.workspace().root()), kDiagMs);
    saveRefused_->show(std::move(r));
  };
  saveAs_->onSaved = [this](const std::filesystem::path& from, const std::filesystem::path& to) {
    untitledNames_.erase(from.generic_string());
    if (bench_) bench_->documentRenamed(from, to);
    setMessage("Saved " + to.filename().string(), kMessageMs);
  };
  external_ = std::make_unique<ExternalChanges>(pickerEnv_, bench_);
  {
    CommandPalette::Hooks ph;
    ph.binding = [this](const std::string& id) { return bindingFor(id); };
    ph.run = [this](const std::string& id) {
      CommandContext cc;
      cc.editor = &editor_;
      cc.context = activeContext();
      const Command* c = registry_.find(id);
      if (c && !c->isEnabled(cc)) setMessage(c->name + " is not available right now", kMessageMs);
      else runCommandId(id, "", activeContext());
    };
    palette_ = std::make_unique<CommandPalette>(pickerEnv_, registry_, std::move(ph));
  }
  {
    VcsUi::Hooks vh;
    vh.activeView = [this]() -> TextView* {
      if (auto* tv = dynamic_cast<TextView*>(root_.focused())) return tv;
      TextView* found = nullptr;
      forEachView([&](TextView& v) {
        if (!found && v.isCurrent() && v.visible()) found = &v;
      });
      return found;
    };
    vh.message = [this](const std::string& text, int ttlMs) { setMessage(text, ttlMs); };
    vcsUi_ = std::make_unique<VcsUi>(pickerEnv_, std::move(vh));
  }
  {
    PasteHistory::Hooks hh;
    hh.paste = [this](const std::string& t) { return editor_.currentDocument() && pasteText(editor_, t); };
    hh.clipboardGet = [] { return clipboardGet(); };
    hh.clipboardSet = [](const std::string& t) { return clipboardSet(t); };
    pasteHistory_ = std::make_unique<PasteHistory>(pickerEnv_, std::move(hh));
    PerforceUi::Hooks ph;
    ph.message = [this](const std::string& text, int ttlMs) { setMessage(text, ttlMs); };
    ph.launcher = [this]() -> const OsLauncher& { return launcher_; };
    perforceUi_ = std::make_unique<PerforceUi>(pickerEnv_, std::move(ph));
  }
  registerEditorCommands(registry_);
  if (bench_) {
    terminals_ = std::make_unique<TerminalPanel>(editor_, *bench_);
    terminals_->message = [this](const std::string& m) { setMessage(m, kMessageMs); };
  }
  bindUiCommands();
  bindParserCommands();
  keyOpts_.workspaceRoot = editor.workspace().root();
  keyOpts_.userDir = userDir;
  if (!userDir.empty() && userDir == userConfigDir()) keyOpts_.import10xFile = default10xKeyMappingsPath();
  reloadKeyBindings();
  if (bench_) {
    bench_->status().addSegment("message", StatusBar::Align::Left);
    bench_->status().addSegment("config", StatusBar::Align::Left, [this](const std::string&) {
      runCommandId("Build.SelectConfiguration", "", activeContext());
    });
    bench_->status().setOnClick(
        "flags", [this](const std::string&) { runCommandId("Help.ShowCompileCommand", "", activeContext()); });
    bench_->status().addSegment("navcounts", StatusBar::Align::Right);
    bench_->status().addSegment("recording", StatusBar::Align::Right);
    bench_->confirmClose = [this](int id) {  // the tab's x / middle click on the last view of a dirty document
      if (!confirmUnsaved_) return false;
      closePickers();
      unsaved_->ask({bench_->tabPath(id)}, [this, id](bool discarded) { bench_->closeTab(id, discarded); });
      return true;
    };
    NavigationUi::Hooks hooks;
    hooks.run = [this](const std::string& id) {
      runCommandId(id, "", activeContext());
      return true;
    };
    hooks.shortcut = [this](const std::string& id) { return shortcutFor(id); };
    nav_ = std::make_unique<NavigationUi>(*bench_, editor_, std::move(hooks));
    auto bp = std::make_unique<BuildPanel>(editor_);
    build_ = bp.get();
    build_->onClose = [this] { bench_->hideBottomPanel(); };
    build_->onMessage = [this](const std::string& m) { setMessage(m, kMessageMs); };
    build_->onJumped = [this] {  // reveal the target's tab and give the editor the keys back
      bench_->sync();
      bench_->setActivePanel(bench_->activePanel());
    };
    bench_->addBottomPanel("Build", std::move(bp));
    bench_->status().setOnClick("build", [this](const std::string&) { bench_->toggleBottomPanel("Build"); });
    WorkspaceExplorer::Hooks eh;
    eh.message = [this](const std::string& m) { setMessage(m, kMessageMs); };
    eh.focusEditor = [this] { bench_->setActivePanel(bench_->activePanel()); };
    explorer_ = std::make_unique<WorkspaceExplorer>(editor_, *bench_, std::move(eh));
    addContextMenuProvider([this](Widget& target, float x, float y, bool keyboard, ContextMenuRequest& out) {
      return explorerMenu(target, x, y, keyboard, out);
    });
  }
  showDiagnostics();
  // Clangd semantic tokens go to every view of the document they describe (they replace the previous set).
  editor_.onSemanticTokens([this](Document& d, const std::vector<TokenSpan>& spans) {
    const LspSemanticTokensLegend legend = editor_.semantic().tokenLegend();
    forEachView([&](TextView& v) {
      if (v.document() == &d) v.setSemanticTokens(spans, legend);
    });
  });
}

EditorApp::~EditorApp() {
  if (bench_) bench_->confirmClose = nullptr;
  explorer_.reset();  // its tree callbacks point into this object
  vcsUi_.reset();     // unhooks VcsManager::onError, closes its confirmation
  perforceUi_.reset();
  closePickers();  // their callbacks point into this object
  palette_.reset();
  external_.reset();  // unhooks Editor::onExternalChange
  unsaved_.reset();
  saveAs_.reset();
  saveRefused_.reset();
  if (build_) {
    build_->onClose = nullptr;
    build_->onMessage = nullptr;
    build_->onJumped = nullptr;
  }
  findReplace_.reset();
  completion_.reset();
  hover_.reset();
  sigHelp_.reset();
  rename_.reset();
  functions_.reset();
  life_.reset();
  editor_.onSemanticTokens(nullptr);
  if (settings_) {
    settings_->settings().removeOnChanged(settingsToken_);
    settings_->onThemeChanged(nullptr);
  }
}

void EditorApp::reloadKeyBindings() {
  diags_.clear();
  keys_ = loadKeyBindings(keyOpts_, diags_);
}

void EditorApp::setKeyImport10x(bool enabled) {
  if (keyOpts_.import10x == enabled) return;
  keyOpts_.import10x = enabled;
  reloadKeyBindings();
  showDiagnostics();
}

void EditorApp::setKeyImportFile(const std::filesystem::path& file) {
  keyOpts_.import10xFile = file;
  reloadKeyBindings();
  showDiagnostics();
}

bool EditorApp::commandRunnable(const std::string& id) const {
  const Command* c = registry_.find(id);
  return c && c->handler;
}

// Runs a build in the Build panel (shown first). The panel reports a command that cannot be found or started.
bool EditorApp::startBuild(BuildOverrides o) {
  bench_->showBottomPanel("Build");
  std::string err;
  if (!build_->start(o, &err)) setMessage("Build failed to start: " + err, kMessageMs);
  return true;
}

// Alt R / Shift+Alt R are ReplaceNextInFile / ReplaceAllInFile in 10x and the regex toggle of tenfold's pickers and
// find bar. The widget keeps the key in an open picker and in the find-only bar; the replace row (Ctrl+H) gives it to
// the commands.
bool EditorApp::widgetOwnsKey(const std::string& commandId) const {
  if (commandId != "Editor.ReplaceNextInFile" && commandId != "Editor.ReplaceAllInFile") return false;
  if (root_.modal()) return true;
  return findReplace_->focusInBar() && !findReplace_->replaceRowShown();
}

void EditorApp::forEachView(const std::function<void(TextView&)>& fn) {
  std::function<void(Widget&)> walk = [&](Widget& w) {
    if (auto* v = dynamic_cast<TextView*>(&w)) fn(*v);
    for (const auto& c : w.children()) walk(*c);
  };
  walk(root_);
}

// The first key-binding or settings / theme diagnostic, "+N more" for the rest.
void EditorApp::showDiagnostics() {
  std::vector<KeyBindingDiag> all = diags_;
  if (settings_) {
    const auto sd = settings_->diagnostics();
    all.insert(all.end(), sd.begin(), sd.end());
  }
  if (!all.empty())
    setMessage(formatDiag(all[0]) + (all.size() > 1 ? " (+" + std::to_string(all.size() - 1) + " more)" : ""), kDiagMs);
}

void EditorApp::applyEditorSettings() {
  if (!settings_) return;
  const EditorSettings es = editorSettings(settings_->settings());
  applyEditingSettings(settings_->settings());
  setNavStatusCounts(settings_->settings().getBool("nav.statusCounts"));
  applySettings(settings_->settings());
  forEachView([&](TextView& v) {
    v.applySettings(es);
    if (wrapOverride_) v.setWordWrap(*wrapOverride_);
    if (whitespaceOverride_) v.setShowWhitespace(*whitespaceOverride_);
    v.settingsGen = settingsGen_;
  });
}

// editor.indentUnit: "tab" or a number of spaces (1..16); anything else keeps four spaces.
void EditorApp::applyEditingSettings(const Settings& s) {
  Editor::EditingOptions& o = editor_.editing();
  std::string unit = s.getString("editor.indentUnit");
  for (char& c : unit) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const int n = std::atoi(unit.c_str());
  o.indentUnit = unit == "tab" ? "\t" : std::string(static_cast<size_t>(n >= 1 && n <= 16 ? n : 4), ' ');
  o.detectIndent = s.getBool("editor.detectIndent");
  o.autoIndent = s.getBool("editor.autoIndent");
  o.autoIndentBraces = s.getBool("editor.autoIndentBraces");
  o.formatBracesOnEnterSplit = s.getBool("editor.formatBracesOnEnterSplit");
  o.autoCloseBrace = s.getBool("editor.autoAddCloseBrace");
  o.autoCloseBracket = s.getBool("editor.autoAddCloseBracket");
  o.autoCloseQuotes = s.getBool("editor.autoCloseQuotes");
  o.bracketSelectedText = s.getBool("editor.bracketSelectedText");
  o.trimTrailingWhitespace = s.getBool("editor.trimTrailingWhitespace");
  o.commentBlockUsesLineComments = s.getBool("editor.commentBlockUsesLineComments");
  o.tabWidth = std::max(1, s.getInt("editor.tabWidth"));
}

void EditorApp::attachSettings(SettingsService& svc, float displayScale, std::function<bool()> rebuildFont) {
  if (settings_) settings_->settings().removeOnChanged(settingsToken_);
  settings_ = &svc;
  scale_ = displayScale;
  rebuildFont_ = std::move(rebuildFont);
  svc.applyTo(root_, scale_);
  svc.onThemeChanged([this](const Theme&) {
    settings_->applyTo(root_, scale_);
    showDiagnostics();
  });
  settingsToken_ = svc.settings().onChanged([this](const std::set<std::string>& keys) {
    bool font = false;
    for (const std::string& k : keys) {
      font |= k.rfind("font.", 0) == 0;
      if (k == "editor.wordWrap") wrapOverride_.reset();  // the file has spoken again
      if (k == "editor.showWhitespace") whitespaceOverride_.reset();
    }
    if (font && rebuildFont_) rebuildFont_();
    ++settingsGen_;
    applyEditorSettings();
    showDiagnostics();
  });
  ++settingsGen_;
  applyEditorSettings();
  showDiagnostics();
}

// One path for settings: attachSettings and every change go through applyEditorSettings, which also feeds the picker
// settings (applySettings stays public for callers without a SettingsService).
void EditorApp::applySettings(const Settings& s) {
  pickerEnv_.preview = s.getBool("picker.preview");
  // keys.import10x joins the settings schema in a later change; until it exists the import stays on.
  for (const SettingInfo& si : settingsSchema())
    if (std::string_view(si.key) == "keys.import10x") setKeyImport10x(s.getBool("keys.import10x"));
  confirmUnsaved_ = s.getBool("editor.confirmUnsaved");
  {
    std::string eol = s.getString("editor.defaultLineEnding");
    for (char& c : eol) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    defaultEol_ = eol == "LF" ? LineEnding::LF : LineEnding::CRLF;  // 10x: CRLF unless told otherwise
    searchCommand_ = s.getString("editor.internetSearchCommand");
    if (searchCommand_.empty()) searchCommand_ = "https://www.google.com/search?q=";
    autoNameUntitled_ = s.getBool("editor.autoNameUntitledFiles");
  }
  external_->setMode(ExternalChanges::parseMode(s.getString("editor.reloadExternalChanges")));
  vcsUi_->applySettings(s);
  if (terminals_) terminals_->applySettings(s, scale_);
  if (explorer_) explorer_->setTrackActiveFile(s.getBool("explorer.trackActiveFile"));
  pasteHistory_->applySettings(s);
  perforceUi_->applySettings(s);
}

void EditorApp::closePickers() {
  if (ContextMenu* m = contextMenu()) m->close();
  symbols_->close();
  navResults_->close();
  find_->close();
  replaceInFiles_->close();
  diagPanel_->close();
  goToLine_->close();
  configPicker_->close();
  bookmarkList_->close();
  breakpointList_->close();
  compileInfo_->close();
  functions_->close();
  palette_->close();
  unsaved_->close();
  saveAs_->close();
  saveRefused_->close();
  external_->close();
  if (files_) files_->close();
  if (vcsUi_) vcsUi_->closeConfirm();
  if (pasteHistory_) pasteHistory_->close();
  if (perforceUi_) perforceUi_->close();
}

void EditorApp::setNavStatusCounts(bool on) {
  if (nav_) nav_->setShowCounts(on);
  if (!on && bench_) bench_->status().setText("navcounts", "");
}

std::string EditorApp::shortcutFor(const std::string& command) const {
  for (const KeyBinding& b : keys_.bindings())  // first keyboard binding in the global context
    if (b.command == command && b.context == "global" && b.arg.empty() && !b.chords.empty() && !b.chords[0].mouse)
      return formatChords(b.chords);
  return {};
}

// The first key binding of a command in any context (global ones first), arguments excluded; for the palette.
std::string EditorApp::bindingFor(const std::string& command) const {
  if (std::string g = shortcutFor(command); !g.empty()) return g;
  for (const KeyBinding& b : keys_.bindings())
    if (b.command == command && b.arg.empty() && !b.chords.empty() && !b.chords[0].mouse) return formatChords(b.chords);
  for (const KeyBinding& b : keys_.bindings())
    if (b.command == command && b.arg.empty() && !b.chords.empty()) return formatChords(b.chords);
  return {};
}

bool EditorApp::requestQuit() {
  if (quit_) return true;
  std::vector<std::filesystem::path> dirty;
  if (confirmUnsaved_)
    for (Document* d : editor_.documents())
      if (d->dirty()) dirty.push_back(d->path());
  if (dirty.empty()) return quit_ = true;
  closePickers();
  unsaved_->ask(dirty, [this](bool) { quit_ = true; });
  return false;
}

// The 10x editing commands that need the UI (W8a): the clipboard, views and the find bar.
void EditorApp::bindEditingCommands() {
  const CommandEnabled hasDoc = [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); };
  registry_.bind(
      "Editor.CutLine",
      [this](const CommandContext& c, std::string_view) {
        const std::string text = c.editor->cutLines();
        return text.empty() || clipboardSetter()(text);
      },
      hasDoc);
  registry_.bind(
      "File.SaveAll",
      [this](const CommandContext& c, std::string_view) {
        std::string err;
        std::vector<SaveRefusal> refused;
        if (!c.editor->saveAll(&err, &refused)) {
          // Every other document was saved. Characters a file's encoding cannot hold get the dialog (and its own
          // status message); other failures (read-only files) the first reason.
          if (!err.empty()) setMessage("Cannot save all files: " + err, kDiagMs);
          pickerEnv_.refused(std::move(refused));  // after the message: its own status text wins
        }
        return true;
      },
      [](const CommandContext& c) { return c.editor != nullptr; });
  registry_.bind("View.ToggleWhitespace", [this](const CommandContext&, std::string_view) {
    bool on = false, seen = false;
    forEachView([&](TextView& v) {
      if (!seen || (v.isCurrent() && v.visible())) on = v.settings().showWhitespace;
      seen = true;
    });
    if (!seen) return false;
    whitespaceOverride_ = !on;
    forEachView([&](TextView& v) { v.setShowWhitespace(!on); });
    setMessage(on ? "Whitespace hidden" : "Whitespace visible", kMessageMs);
    return true;
  });
  for (bool down : {false, true})
    registry_.bind(
        down ? "Editor.ScrollLineDown" : "Editor.ScrollLineUp",
        [this, down](const CommandContext&, std::string_view) {
          TextView* target = nullptr;
          forEachView([&](TextView& v) {
            if (v.isCurrent() && v.visible() && (!target || v.hasFocus())) target = &v;
          });
          if (!target) return false;
          target->scrollLines(down ? 1 : -1);
          return true;
        },
        hasDoc);
  registry_.bind(
      "Editor.SelectAllOccurrences",
      [this](const CommandContext& c, std::string_view) {
        std::string err;
        size_t n = 0;
        if (findReplace_->isOpen() && !findReplace_->query().empty()) {  // "the find results": the bar's text
          const SearchOptions o = findReplace_->options();
          n = c.editor->selectAllOccurrences(&o, &err);
        } else {
          n = c.editor->selectAllOccurrences();
        }
        setMessage(!err.empty() ? err
                   : n == 0     ? "No occurrences to select"
                                : std::to_string(n) + " occurrences selected",
                   kMessageMs);
        return true;
      },
      hasDoc);
  registry_.bind(
      "Editor.AddCaretAtMouse",
      [this](const CommandContext&, std::string_view) {
        TextView* tv = textViewAt(mouseX_, mouseY_);
        if (!tv) return false;
        tv->addCaretAtPoint(mouseX_, mouseY_);
        return true;
      },
      hasDoc);
  // Code folding (W8f): the 10x CollapseRegions commands act on the focused (else the current visible) text view.
  static const std::pair<const char*, TextView::FoldOp> kFoldCommands[] = {
      {"Editor.CollapseRegion", TextView::FoldOp::Collapse},
      {"Editor.ExpandRegion", TextView::FoldOp::Expand},
      {"Editor.ToggleCollapseExpandRegion", TextView::FoldOp::Toggle},
      {"Editor.ToggleCollapseExpandAllRegions", TextView::FoldOp::ToggleAll},
      {"Editor.CollapseAllRegions", TextView::FoldOp::CollapseAll},
      {"Editor.ExpandAllRegions", TextView::FoldOp::ExpandAll},
      {"Editor.CollapseAllRegionsRecursive", TextView::FoldOp::CollapseAllRecursive},
      {"Editor.ExpandAllRegionsRecursive", TextView::FoldOp::ExpandAllRecursive}};
  for (const auto& [id, op] : kFoldCommands)
    registry_.bind(
        id,
        [this, op = op](const CommandContext&, std::string_view) {
          TextView* target = nullptr;
          forEachView([&](TextView& v) {
            if (v.isCurrent() && v.visible() && (!target || v.hasFocus())) target = &v;
          });
          if (!target) return false;
          if (!target->foldingActive()) {
            setMessage("Code folding is available in C/C++ files", kMessageMs);
            return true;
          }
          target->foldCommand(op);
          return true;
        },
        hasDoc);
  for (bool fwd : {true, false})
    registry_.bind(
        fwd ? "Editor.FindNextCurrentWord" : "Editor.FindPrevCurrentWord",
        [this, fwd](const CommandContext&, std::string_view) { return findReplace_->stepCurrentWord(fwd); }, hasDoc);
}

// Everything tenfold puts on the clipboard also goes to the paste history.
std::function<bool(const std::string&)> EditorApp::clipboardSetter() {
  return [this](const std::string& t) {
    if (!clipboardSet(t)) return false;
    pasteHistory_->record(t);
    return true;
  };
}

// W8f: PasteHistory, RecordKeySequence / PlaybackKeySequence, PerforceHistory, PerforceShowInP4v.
void EditorApp::bindMiscCommands() {
  const CommandEnabled hasDoc = [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); };
  registry_.bind(
      "Editor.PasteHistory",
      [this](const CommandContext&, std::string_view) {
        closePickers();
        pasteHistory_->open();
        return true;
      },
      hasDoc);
  registry_.bind("App.RecordKeySequence", [this](const CommandContext&, std::string_view) {
    if (keySeq_.recording()) {
      const bool kept = keySeq_.stop();
      setMessage(kept ? "Key sequence recorded (" + std::to_string(keySeq_.steps().size()) + " steps)"
                      : "Nothing recorded: the previous key sequence is kept",
                 kMessageMs);
    } else {
      closePickers();
      keySeq_.start();
      setMessage("Recording key sequence... (Control Shift R stops)");
    }
    if (bench_) bench_->status().setText("recording", keySeq_.recording() ? "REC" : "");
    return true;
  });
  registry_.bind("App.PlaybackKeySequence", [this](const CommandContext&, std::string_view) {
    if (keySeq_.recording()) {
      setMessage("Stop recording first (Control Shift R)", kMessageMs);
      return true;
    }
    if (keySeq_.steps().empty()) {
      setMessage("No key sequence recorded (Control Shift R records one)", kMessageMs);
      return true;
    }
    playKeySequence();
    return true;
  });
  registry_.bind(
      "Vcs.PerforceHistory",
      [this](const CommandContext&, std::string_view) {
        closePickers();
        perforceUi_->history();
        return true;
      },
      hasDoc);
  registry_.bind(
      "Vcs.ShowInP4v",
      [this](const CommandContext&, std::string_view) {
        perforceUi_->showInP4v();
        return true;
      },
      hasDoc);
}

// Replays the last recording as ONE undo step of the document that is current when it starts (edits to other
// documents the sequence switches to are their own steps).
void EditorApp::playKeySequence() {
  Document* doc = editor_.currentDocument();
  if (doc) doc->beginGroup(editor_.selections());
  KeySequence::PlayHooks h;
  h.command = [this](const std::string& id, const std::string& arg) { runCommandId(id, arg, activeContext()); };
  h.input = [this](const InputEvent& e) { root_.dispatch(e); };
  const size_t n = keySeq_.play(h);
  if (doc && editor_.document(doc->path()) == doc) {
    doc->endGroup(editor_.currentDocument() == doc ? editor_.selections() : std::vector<Selection>{});
  }
  if (editor_.currentDocument()) editor_.setSelections(editor_.selections());  // ends typing coalescing
  setMessage("Played " + std::to_string(n) + " recorded steps", kMessageMs);
}

void EditorApp::bindUiCommands() {
  vcsUi_->bind(registry_);
  bindMiscCommands();
  compileInfo_->bind(registry_, [this] { closePickers(); });
  registry_.bind("App.ShowContextMenu", [this](const CommandContext&, std::string_view) {
    if (ContextMenu* m = contextMenu()) {  // pressing the key again dismisses the menu
      m->close();
      return true;
    }
    if (root_.modal()) return true;  // a picker owns the keys
    if (!openContextMenuForFocus()) setMessage("No context menu for this element", kMessageMs);
    return true;
  });
  registry_.bind(
      "Search.SymbolSearch",
      [this](const CommandContext&, std::string_view) {
        closePickers();
        symbols_->open();
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->started(); });
  registry_.bind("Search.FindInWorkspace", [this](const CommandContext& c, std::string_view) {
    std::string initial;  // a single-line selection of the primary caret seeds the query
    if (c.editor->currentDocument() && !c.editor->selections().empty() && !c.editor->selections()[0].empty() &&
        c.editor->selections()[0].anchor.line == c.editor->selections()[0].head.line)
      initial = c.editor->currentDocument()->buffer().textInRange(c.editor->selections()[0].range());
    closePickers();
    find_->open(initial);
    return true;
  });
  registry_.bind("Search.NewFindInFiles", [this](const CommandContext& c, std::string_view) {
    std::string initial;  // like Search.FindInWorkspace: a single-line selection seeds the query
    if (c.editor->currentDocument() && !c.editor->selections().empty() && !c.editor->selections()[0].empty() &&
        c.editor->selections()[0].anchor.line == c.editor->selections()[0].head.line)
      initial = c.editor->currentDocument()->buffer().textInRange(c.editor->selections()[0].range());
    closePickers();
    find_->openNew(initial);
    return true;
  });
  registry_.bind(
      "Search.ReplaceInFiles",
      [this](const CommandContext& c, std::string_view) {
        std::string initial;
        if (c.editor->currentDocument() && !c.editor->selections().empty() && !c.editor->selections()[0].empty() &&
            c.editor->selections()[0].anchor.line == c.editor->selections()[0].head.line)
          initial = c.editor->currentDocument()->buffer().textInRange(c.editor->selections()[0].range());
        closePickers();
        replaceInFiles_->open(initial);
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->started(); });
  // GotoNextResult / GotoPrevResult walk the results of the last search or build operation, whichever started last.
  for (bool fwd : {true, false})
    registry_.bind(fwd ? "Search.NextResult" : "Search.PrevResult",
                   [this, fwd](const CommandContext&, std::string_view) {
                     if (editor_.lastResultSource() == Editor::ResultSource::Build) return stepBuildResult(fwd);
                     find_->step(fwd);  // says "No search results" / "Result i of n" itself
                     return true;
                   });
  registry_.bind("View.ShowDiagnostics", [this](const CommandContext&, std::string_view) {
    closePickers();
    diagPanel_->open();
    return true;
  });
  registry_.bind("Editor.GotoLine", [this](const CommandContext&, std::string_view) {
    closePickers();
    goToLine_->open();
    return true;
  });
  registry_.bind("Editor.ListBookmarks", [this](const CommandContext&, std::string_view) {
    closePickers();
    bookmarkList_->open();
    return true;
  });
  registry_.bind("Build.SelectConfiguration", [this](const CommandContext&, std::string_view) {
    closePickers();
    configPicker_->open();
    return true;
  });
  registry_.bind(
      "Debug.OpenInVisualStudio",
      [this](const CommandContext&, std::string_view) {
        if (!editor_.debug().hasBackend()) {
          setMessage("Open in Visual Studio: no Visual Studio debugger backend on this platform", kMessageMs);
          return true;
        }
        if (!editor_.openInVisualStudio())  // the backend's own failure arrives through lastError (shown by tick)
          setMessage("Open in Visual Studio failed" +
                         (editor_.debug().lastError().empty() ? std::string() : ": " + editor_.debug().lastError()),
                     kMessageMs);
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind("Debug.ShowBreakpoints", [this](const CommandContext&, std::string_view) {
    closePickers();
    breakpointList_->open();
    return true;
  });
  registry_.bind(
      "Debug.EditCondition",
      [this](const CommandContext&, std::string_view) {
        closePickers();
        breakpointList_->openCondition();
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  for (bool replace : {false, true})
    registry_.bind(
        replace ? "Editor.Replace" : "Editor.Find",
        [this, replace](const CommandContext&, std::string_view) {
          closePickers();
          findReplace_->open(replace);
          return true;
        },
        [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind(
      "Editor.ReplaceNextInFile",
      [this](const CommandContext&, std::string_view) {
        findReplace_->replaceNext();  // says what it did (or why not) in the status bar
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind(
      "Editor.ReplaceAllInFile",
      [this](const CommandContext&, std::string_view) {
        findReplace_->replaceAllInFile();
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  for (bool fwd : {true, false})
    registry_.bind(
        fwd ? "Editor.FindNext" : "Editor.FindPrev",
        [this, fwd](const CommandContext&, std::string_view) { return findReplace_->step(fwd); },
        [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind("App.Quit", [this](const CommandContext&, std::string_view) {
    requestQuit();  // with unsaved changes this opens the prompt; quitRequested() follows the answer
    return true;
  });
  registry_.bind("App.CommandPalette", [this](const CommandContext&, std::string_view) {
    closePickers();
    palette_->open();
    return true;
  });
  registry_.bind(
      "Clangd.Restart",
      [this](const CommandContext&, std::string_view) {
        std::string err;
        if (!editor_.restartClangd(&err)) {
          setMessage(
              "Restart clangd failed: " + (err.empty() ? std::string("clangd is disabled or not available") : err),
              kDiagMs);
          return true;
        }
        setMessage("clangd restarted", kMessageMs);
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->started(); });
  // File.Save: the engine's reason (read-only file, locked file, ...) reaches the status bar instead of a bare failure.
  registry_.bind(
      "File.Save",
      [this](const CommandContext& c, std::string_view) {
        Document* d = c.editor->currentDocument();
        if (!d) return false;
        if (d->isUntitled()) {  // no file yet: ask where it goes
          closePickers();
          saveAs_->open(d->path(), untitledSuggestion(*d));
          return true;
        }
        std::string err;
        UnencodableReport rep;
        if (c.editor->save(*d, &err, &rep)) return true;
        if (!rep.empty()) {  // the file's encoding cannot hold a character: show it, nothing was written
          pickerEnv_.refused({{d->path(), std::move(rep)}});
          return true;
        }
        setMessage("Cannot save " + d->path().filename().string() + (err.empty() ? std::string() : ": " + err),
                   kDiagMs);
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind(
      "Editor.Copy", [this](const CommandContext& c, std::string_view) { return doCopy(*c.editor, clipboardSetter()); },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind(
      "Editor.Cut", [this](const CommandContext& c, std::string_view) { return doCut(*c.editor, clipboardSetter()); },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind(
      "Editor.Paste", [](const CommandContext& c, std::string_view) { return doPaste(*c.editor); },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind("View.ToggleWordWrap", [this](const CommandContext&, std::string_view) {
    bool on = true, seen = false;
    forEachView([&](TextView& v) {
      if (!seen || (v.isCurrent() && v.visible())) on = v.settings().wordWrap;
      seen = true;
    });
    if (!seen) return false;
    wrapOverride_ = !on;
    forEachView([&](TextView& v) { v.setWordWrap(!on); });
    setMessage(on ? "Word wrap off" : "Word wrap on", kMessageMs);
    return true;
  });
  bindViewCommands();
  bindEditingCommands();
  if (!bench_) return;
  bindPanelCommands();
  bindTerminalCommands();
  registry_.bind("File.OpenFile", [this](const CommandContext&, std::string_view) {
    closePickers();
    files_->open();
    return true;
  });
  for (bool fwd : {true, false})
    registry_.bind(fwd ? "Workbench.CycleMruNext" : "Workbench.CycleMruPrev",
                   [this, fwd](const CommandContext&, std::string_view) {
                     if (!bench_->activePanel()) return false;
                     bench_->cycleMru(bench_->activePanel(), fwd);
                     cycling_ = true;  // ends when Ctrl is released
                     return true;
                   });
  registry_.bind("Workbench.ReopenClosedTab", [this](const CommandContext&, std::string_view) {
    if (bench_->reopenClosedTab()) return true;
    setMessage("No recently closed file to reopen", kMessageMs);
    return true;
  });
  registry_.bind("Build.Build", [this](const CommandContext&, std::string_view arg) {
    BuildOverrides o;
    o.command = std::string(arg);  // optional: a custom shell command instead of the detected build
    return startBuild(std::move(o));
  });
  registry_.bind("Build.Rebuild", [this](const CommandContext&, std::string_view arg) {
    BuildOverrides o;
    o.kind = BuildOverrides::Kind::Rebuild;
    o.rebuildCommand = std::string(arg);  // optional: a custom shell command instead of the detected rebuild
    return startBuild(std::move(o));
  });
  registry_.bind(
      "Build.BuildCurrentFile",
      [this](const CommandContext& c, std::string_view arg) {
        BuildOverrides o;
        o.kind = BuildOverrides::Kind::CurrentFile;
        if (!arg.empty()) {  // a file given by the caller (the explorer tree's menu)
          o.file = pathFromUtf8(std::string(arg));
          return startBuild(std::move(o));
        }
        const Document* d = c.editor->currentDocument();
        if (!d) return false;
        if (d->isUntitled()) {
          setMessage(d->path().filename().string() + " has not been saved yet", kMessageMs);
          return true;
        }
        o.file = d->path();
        return startBuild(std::move(o));
      },
      [](const CommandContext& c) { return c.editor != nullptr; });
  registry_.bind(
      "Build.Cancel",
      [this](const CommandContext&, std::string_view) {
        build_->cancel();
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->buildRunning(); });
  for (bool fwd : {true, false})
    registry_.bind(fwd ? "Build.NextError" : "Build.PrevError",
                   [this, fwd](const CommandContext&, std::string_view) { return stepBuildResult(fwd); });
  const CommandEnabled hasActiveTab = [this](const CommandContext&) {
    return bench_->activePanel() && bench_->activePanel()->activeId() >= 0;
  };
  registry_.bind(
      "Workbench.CloseTab",
      [this](const CommandContext&, std::string_view) {
        if (!bench_->activePanel() || bench_->activePanel()->activeId() < 0) return false;
        bench_->requestCloseTab(bench_->activePanel()->activeId());
        return true;
      },
      hasActiveTab);
  // W8f: CloseAllOtherTabs / CloseAllButPinnedTabs close without asking, except tabs that hold the last view of a
  // modified document: those stay open (closing them is a decision for the user, tab by tab).
  registry_.bind(
      "Workbench.CloseOtherTabs",
      [this](const CommandContext&, std::string_view) {
        Panel* p = bench_->activePanel();
        std::vector<int> ids;
        for (const auto& t : p->tabs().tabs())
          if (t.id != p->activeId() && !t.pinned) ids.push_back(t.id);
        closeTabs(ids);
        return true;
      },
      hasActiveTab);
  registry_.bind(
      "Workbench.CloseAllButPinned",
      [this](const CommandContext&, std::string_view) {
        std::vector<int> ids;
        for (const auto& t : bench_->activePanel()->tabs().tabs())
          if (!t.pinned) ids.push_back(t.id);
        closeTabs(ids);
        return true;
      },
      hasActiveTab);
  for (bool toggle : {false, true})
    registry_.bind(
        toggle ? "Workbench.TogglePinTab" : "Workbench.PinTab",
        [this, toggle](const CommandContext&, std::string_view) {
          Panel* p = bench_->activePanel();
          const int id = p->activeId(), i = p->tabs().indexOf(id);
          if (i < 0) return false;
          bench_->setPinned(id, toggle ? !p->tabs().tabs()[static_cast<size_t>(i)].pinned : true);
          return true;
        },
        hasActiveTab);
  registry_.bind("View.ToggleBuildPanel", [this](const CommandContext&, std::string_view) {
    bench_->toggleBottomPanel("Build");
    return true;
  });
  registry_.bind("View.ToggleWorkspaceExplorer", [this](const CommandContext&, std::string_view) {
    if (!explorer_) return false;
    explorer_->toggle();
    return true;
  });
  registry_.bind("View.ShowInWorkspaceTree", [this](const CommandContext&, std::string_view) {
    if (!explorer_) return false;
    explorer_->showCurrentFile(false);
    return true;
  });
  registry_.bind("View.ShowInWorkspaceTreeWithToggle", [this](const CommandContext&, std::string_view) {
    if (!explorer_) return false;
    explorer_->showCurrentFile(true);
    return true;
  });
  registry_.bind("Workbench.FocusPanel", [this](const CommandContext&, std::string_view arg) {
    const auto ps = bench_->panels();
    const int n = std::atoi(std::string(arg).c_str());  // 1-based
    if (n < 1 || n > static_cast<int>(ps.size())) return false;
    bench_->setActivePanel(ps[static_cast<size_t>(n - 1)]);
    return true;
  });
}

// ---------------------------------------------------------------------------------------------------------------
// W8b: view, file and panel commands
// ---------------------------------------------------------------------------------------------------------------

bool EditorApp::zoomBy(float stepPt) {
  constexpr float kMin = -8, kMax = 40;  // points added to the configured size; the font builder clamps the total
  const float next = std::clamp(zoomPt_ + stepPt, kMin, kMax);
  if (next == zoomPt_) {
    setMessage(stepPt > 0 ? "Already at the largest zoom" : "Already at the smallest zoom", kMessageMs);
    return true;
  }
  const float before = zoomPt_;
  zoomPt_ = next;
  if (!rebuildFont_ || !rebuildFont_()) {
    zoomPt_ = before;
    setMessage("Cannot change the font size", kMessageMs);
    return false;
  }
  std::string txt = "Zoom reset";
  if (zoomPt_ != 0) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "Zoom %c%g pt", zoomPt_ > 0 ? '+' : '-', std::abs(zoomPt_));
    txt = buf;
  }
  setMessage(txt, kMessageMs);
  return true;
}

void EditorApp::setFullscreen(bool on) {
  fullscreen_ = on;
  if (window_.setFullscreen) window_.setFullscreen(on);
}

// The name an untitled document offers the Save As prompt: the first word typed (editor.autoNameUntitledFiles) or
// "Untitled N".
std::string EditorApp::untitledSuggestion(const Document& d) const {
  const auto it = untitledNames_.find(d.path().generic_string());
  if (autoNameUntitled_ && it != untitledNames_.end() && !it->second.word.empty()) return it->second.word;
  return d.path().filename().string();
}

// editor.autoNameUntitledFiles: an untitled document is named after the first word typed in it. The word follows the
// typing until it is complete (whitespace after it), then it is kept. Only the tab title and the Save As suggestion
// change; the document stays untitled.
void EditorApp::updateUntitledNames() {
  if (!bench_) return;
  std::set<std::string> alive;
  for (Document* d : editor_.documents()) {
    if (!d->isUntitled()) continue;
    const std::string key = d->path().generic_string();
    alive.insert(key);
    if (!autoNameUntitled_) continue;
    UntitledName& n = untitledNames_[key];
    if (n.frozen) continue;
    std::string word;
    bool complete = false;
    const TextBuffer& b = d->buffer();
    for (int l = 0; l < b.lineCount() && l < 20 && !complete; ++l) {
      const std::string& line = b.line(l);
      size_t i = line.find_first_not_of(" \t\r");
      if (i == std::string::npos) {
        if (!word.empty()) complete = true;
        continue;
      }
      for (; i < line.size(); ++i) {
        const char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r') {
          complete = true;
          break;
        }
        if (static_cast<unsigned char>(c) >= 32 && std::string_view("<>:\"/\\|?*").find(c) == std::string_view::npos &&
            word.size() < 48)
          word += c;
      }
      if (i >= line.size() && !word.empty() && l + 1 < b.lineCount()) complete = true;  // a line break ends the word
      break;
    }
    n.word = word;
    n.frozen = complete && !word.empty();
    bench_->setTitleOverride(d->path(), word);
  }
  for (auto it = untitledNames_.begin(); it != untitledNames_.end();) {
    if (alive.count(it->first)) {
      ++it;
    } else {
      bench_->setTitleOverride(it->first, "");
      it = untitledNames_.erase(it);
    }
  }
}

void EditorApp::bindViewCommands() {
  registry_.bind("View.ZoomIn", [this](const CommandContext&, std::string_view) { return zoomBy(1); });
  registry_.bind("View.ZoomOut", [this](const CommandContext&, std::string_view) { return zoomBy(-1); });
  registry_.bind("View.ToggleFullscreen", [this](const CommandContext&, std::string_view) {
    if (!window_.setFullscreen) {
      setMessage("Fullscreen is not available here", kMessageMs);
      return true;
    }
    setFullscreen(!fullscreen_);
    return true;
  });
  registry_.bind("View.ToggleFocusMode", [this](const CommandContext&, std::string_view) {
    if (!bench_) return false;
    bench_->setFocusMode(!bench_->focusMode());
    return true;
  });
  registry_.bind("View.ToggleFullscreenFocusMode", [this](const CommandContext&, std::string_view) {
    if (!bench_) return false;
    // Both on, unless both already are: then both off.
    const bool on = !(fullscreen_ && bench_->focusMode());
    bench_->setFocusMode(on);
    if (window_.setFullscreen) setFullscreen(on);
    return true;
  });
  registry_.bind(
      "Editor.InternetSearch",
      [this](const CommandContext& c, std::string_view arg) {
        std::string term(arg);
        if (term.empty()) {
          Document* d = c.editor->currentDocument();
          if (!d) return false;
          const auto& sels = c.editor->selections();
          if (!sels.empty() && !sels[0].empty()) term = d->buffer().textInRange(sels[0].range());
          else term = d->buffer().wordAt(c.editor->cursor().pos).text;
        }
        const std::string url = internetSearchUrl(searchCommand_, term);
        if (url.empty()) {
          setMessage("Nothing to search for: put the caret on a word or select some text", kMessageMs);
          return true;
        }
        std::string err;
        if (!launcher_.openUrl || !launcher_.openUrl(url, &err)) {
          setMessage("Cannot open the browser" + (err.empty() ? std::string() : ": " + err), kMessageMs);
          return true;
        }
        setMessage("Searching the web: " + url, kMessageMs);
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind(
      "File.CopyPath",
      [this](const CommandContext& c, std::string_view arg) {
        if (!arg.empty()) {  // a path given by the caller (the explorer tree's menu)
          std::filesystem::path p = pathFromUtf8(std::string(arg));
          if (!clipboardSet(p.make_preferred().string())) return false;
          setMessage("Copied " + p.string(), kMessageMs);
          return true;
        }
        Document* d = c.editor->currentDocument();
        if (!d) return false;
        if (d->isUntitled()) {
          setMessage(d->path().filename().string() + " has not been saved yet", kMessageMs);
          return true;
        }
        std::filesystem::path p = d->path();
        if (!clipboardSet(p.make_preferred().string())) return false;
        setMessage("Copied " + p.string(), kMessageMs);
        return true;
      },
      [](const CommandContext& c) { return c.editor != nullptr; });
  registry_.bind(
      "File.ShowInFolder",
      [this](const CommandContext& c, std::string_view arg) {
        if (!arg.empty()) {
          std::string err;
          if (!launcher_.showInFolder || !launcher_.showInFolder(pathFromUtf8(std::string(arg)), &err))
            setMessage("Cannot open the file manager" + (err.empty() ? std::string() : ": " + err), kMessageMs);
          return true;
        }
        Document* d = c.editor->currentDocument();
        if (!d) return false;
        if (d->isUntitled()) {
          setMessage(d->path().filename().string() + " has not been saved yet", kMessageMs);
          return true;
        }
        std::string err;
        if (!launcher_.showInFolder || !launcher_.showInFolder(d->path(), &err))
          setMessage("Cannot open the file manager" + (err.empty() ? std::string() : ": " + err), kMessageMs);
        return true;
      },
      [](const CommandContext& c) { return c.editor != nullptr; });
  registry_.bind("File.New", [this](const CommandContext&, std::string_view) {
    if (!bench_) return false;
    Document* d = editor_.newUntitled(defaultEol_);
    if (!d) return false;
    closePickers();
    bench_->openDocument(*d);
    return true;
  });
}

void EditorApp::bindPanelCommands() {
  const auto hasPanel = [this](const CommandContext&) { return bench_->activePanel() != nullptr; };
  struct SideCmd {
    const char* id;
    Workbench::Side side;
    const char* word;
  };
  static const SideCmd sides[] = {{"Left", Workbench::Side::Left, "left"},
                                  {"Right", Workbench::Side::Right, "right"},
                                  {"Up", Workbench::Side::Up, "above"},
                                  {"Down", Workbench::Side::Down, "below"}};
  for (const SideCmd& sc : sides) {
    registry_.bind(
        std::string("Workbench.FocusPanel") + sc.id,
        [this, sc](const CommandContext&, std::string_view) {
          if (!bench_->focusPanelToward(sc.side)) setMessage(std::string("No panel ") + sc.word, kMessageMs);
          return true;
        },
        hasPanel);
    registry_.bind(
        std::string("Workbench.MovePanel") + sc.id,
        [this, sc](const CommandContext&, std::string_view) {
          if (!bench_->movePanelToward(sc.side))
            setMessage(std::string("No panel ") + sc.word + " to swap with", kMessageMs);
          return true;
        },
        hasPanel);
  }
  struct DupCmd {
    const char* id;
    bool after, focusNew;
  };
  static const DupCmd dups[] = {{"Workbench.DuplicatePanel", true, false},
                                {"Workbench.DuplicatePanelLeft", false, true},
                                {"Workbench.DuplicatePanelRight", true, true}};
  for (const DupCmd& dc : dups)
    registry_.bind(
        dc.id,
        [this, dc](const CommandContext&, std::string_view) {
          if (!bench_->duplicatePanel(bench_->activePanel(), dc.after, dc.focusNew))
            setMessage("Cannot duplicate the panel: too many panels", kMessageMs);
          return true;
        },
        hasPanel);
  struct TabCmd {
    const char* id;
    int dir;
    bool move;
  };
  static const TabCmd tabs[] = {{"Workbench.PrevTab", -1, false},
                                {"Workbench.NextTab", 1, false},
                                {"Workbench.MoveTabPrev", -1, true},
                                {"Workbench.MoveTabNext", 1, true}};
  for (const TabCmd& tc : tabs)
    registry_.bind(
        tc.id,
        [this, tc](const CommandContext&, std::string_view) {
          if (tc.move) bench_->moveActiveTab(bench_->activePanel(), tc.dir);
          else bench_->stepTab(bench_->activePanel(), tc.dir);
          return true;
        },
        hasPanel);
}

// W8f: the terminal commands. A failure to start (unknown program, no ConPTY) is reported in the status bar.
void EditorApp::bindTerminalCommands() {
  struct TermCmd {
    const char* id;
    TerminalPanel::Kind kind;
    bool show;
  };
  static const TermCmd cmds[] = {{"Terminal.New", TerminalPanel::Kind::Shell, false},
                                 {"Terminal.Show", TerminalPanel::Kind::Shell, true},
                                 {"Terminal.NewClaude", TerminalPanel::Kind::Claude, false},
                                 {"Terminal.NewCursor", TerminalPanel::Kind::Cursor, false}};
  for (const TermCmd& tc : cmds)
    registry_.bind(tc.id, [this, tc](const CommandContext&, std::string_view) {
      closePickers();
      std::string err;
      const int id = tc.show ? terminals_->show(&err) : terminals_->create(tc.kind, &err);
      if (id < 0) setMessage("Terminal: " + err, kMessageMs);
      return true;
    });
  // VS-TERM: copy / paste of the focused terminal (keys from the key map, context "terminal").
  registry_.bind("Terminal.Copy", [this](const CommandContext&, std::string_view) {
    auto* tv = dynamic_cast<TerminalView*>(root_.focused());
    return tv && tv->copySelection(false);
  });
  registry_.bind("Terminal.Paste", [this](const CommandContext&, std::string_view) {
    auto* tv = dynamic_cast<TerminalView*>(root_.focused());
    if (!tv) return false;
    tv->paste();
    return true;
  });
  // VS-TERM: "Open in Terminal" on an explorer node: a terminal in that folder (a file: its folder).
  registry_.bind("Terminal.OpenAt", [this](const CommandContext&, std::string_view arg) {
    closePickers();
    std::filesystem::path dir = pathFromUtf8(std::string(arg));
    std::error_code ec;
    if (!dir.empty() && !std::filesystem::is_directory(dir, ec)) dir = dir.parent_path();
    std::string err;
    if (terminals_->create(TerminalPanel::Kind::Shell, &err, dir) < 0) setMessage("Terminal: " + err, kMessageMs);
    return true;
  });
}

// The focused terminal sends nearly every key to its program. These are the strokes that still run app commands: the
// terminal commands themselves, tab and panel switching / moving / closing, quit, the command palette and the window
// toggles. Everything else (Ctrl+K, Ctrl+F, Ctrl+O, Alt+letters, F-keys ...) belongs to the shell or the full-screen
// program. A stroke counts when any global binding that starts with it runs one of these commands.
bool EditorApp::terminalSkipsShell(const KeyChord& chord) const {
  static const std::set<std::string_view> exact = {"App.Quit",
                                                   "App.CommandPalette",
                                                   "View.ToggleWorkspaceExplorer",
                                                   "View.ToggleBuildPanel",
                                                   "View.ToggleFullscreen",
                                                   "View.ToggleFocusMode",
                                                   "View.ToggleFullscreenFocusMode",
                                                   "View.ZoomIn",
                                                   "View.ZoomOut"};
  for (const KeyBinding& b : keys_.bindings()) {
    if ((b.context != "global" && b.context != "terminal") || b.chords.empty() || !(b.chords[0] == chord)) continue;
    const std::string id = resolveCommandName(b.command).id;
    if (id.rfind("Terminal.", 0) == 0 || id.rfind("Workbench.", 0) == 0 || exact.count(id)) return true;
  }
  return false;
}

const char* EditorApp::activeContext() const {
  if (root_.modal()) return "picker";
  if (rename_ && rename_->isOpen()) return "popup";  // the rename box: Enter is SubmitRenameSymbol
  if (dynamic_cast<TerminalView*>(root_.focused())) return "terminal";
  return dynamic_cast<TextView*>(root_.focused()) ? "editor" : "global";
}

void EditorApp::showNavResults(uint64_t token, const std::string& title, size_t minResults, Confidence c,
                               const std::vector<SymbolLocation>& locs, const std::string& err) {
  if (token != navToken_) return;  // a newer navigation command superseded this answer
  if (locs.size() >= minResults) {
    if (root_.modal() && !navResults_->isOpen()) return;  // the user opened another picker meanwhile
    navResults_->show(title, c, locs, err, minResults);
    return;
  }
  navResults_->close();
  if (!err.empty()) setMessage(err, kMessageMs);
  else if (locs.empty() && c == Confidence::Authoritative) setMessage("No results", kMessageMs);
}

void EditorApp::setMessage(const std::string& text, int ttlMs, bool pending) {
  message_ = text;
  ttlMs_ = text.empty() ? 0 : ttlMs;
  expiry_ = 0;
  pendingShown_ = pending && !text.empty();
  if (bench_) bench_->status().setText("message", text);
}

bool EditorApp::handleEvent(const InputEvent& e, int64_t nowMs) {
  now_ = nowMs;
  if (e.type == InputType::MouseMove || e.type == InputType::MouseDown || e.type == InputType::MouseUp) {
    mouseX_ = e.x;
    mouseY_ = e.y;
  }
  if (e.type == InputType::MouseDown && rename_->isOpen() && !ui::contains(rename_->box()->bounds(), e.x, e.y))
    rename_->cancel();  // a click elsewhere abandons the rename; the click itself still goes through
  if (root_.modal() || rename_->isOpen()) {  // a picker or the rename box owns the keys: the editor popups step aside
    completion_->close();
    sigHelp_->close();
    hover_->dismiss();
  } else if (e.type == InputType::KeyDown) {
    hover_->dismiss();
    if (completion_->handleKey(e) || sigHelp_->handleKey(e)) return true;
  } else if (e.type == InputType::MouseMove) {
    hover_->onMouse(e);
  } else if (e.type == InputType::MouseDown) {
    hover_->dismiss();
  }
  const bool r = handleEventImpl(e, nowMs);
  if (!root_.modal() && !rename_->isOpen()) {
    if (e.type == InputType::Text) {
      completion_->onTyped(e.text);
      sigHelp_->onTyped(e.text);
    }
    completion_->update();
    sigHelp_->update();
  }
  return r;
}

TextView* EditorApp::textViewAt(float x, float y) {
  for (Widget* w = root_.hitTest(x, y); w; w = w->parent())
    if (auto* tv = dynamic_cast<TextView*>(w)) return tv;
  return nullptr;
}

bool EditorApp::handleEventImpl(const InputEvent& e, int64_t nowMs) {
  if (nav_) nav_->setNow(nowMs);
  if (ttlMs_ > 0) {
    expiry_ = nowMs + ttlMs_;
    ttlMs_ = 0;
  }
  if (e.type == InputType::FocusGained) {
    vcsUi_->refreshOnFocus();  // the engine does not watch .git / the p4 client
    pasteHistory_->syncFromClipboard();
  }
  // Right click: the context menu of whatever is under the pointer (W8f). The widgets ignore the right button, so
  // where there is no menu the click goes on to them as before.
  if (e.type == InputType::MouseDown && e.button == MouseButton::Right && !root_.modal() && !root_.captured() &&
      openContextMenuAt(e.x, e.y))
    return true;
  if (terminals_ && (e.type == InputType::FocusGained || e.type == InputType::FocusLost))
    terminals_->setWindowFocused(e.type == InputType::FocusGained);
  if (e.type == InputType::KeyUp || (e.type == InputType::KeyDown && !(e.mods & ModCtrl))) {
    if (cycling_ && !(e.mods & ModCtrl)) {  // Ctrl released: the Ctrl+Tab cycle is over
      cycling_ = false;
      if (bench_) bench_->endMruCycle();
    }
  }
  KeyChord chord;
  if (e.type == InputType::KeyDown && e.key != Key::Unknown)  // a lone modifier press is not a stroke
    chord = chordFromKey(e.key, e.mods);
  else if (e.type == InputType::MouseDown &&
           (e.button == MouseButton::X1 || e.button == MouseButton::X2 || e.button == MouseButton::Middle))
    chord = chordFromMouse(e.button, e.mods);
  else return dispatchUnbound(e);

  // A focused terminal owns the keyboard except for the strokes terminalSkipsShell names (and the rest of a chord that
  // began with one).
  if (e.type == InputType::KeyDown && !root_.modal() && !keys_.pending() &&
      dynamic_cast<TerminalView*>(root_.focused()) && !terminalSkipsShell(chord))
    return root_.dispatch(e);
  const char* ctx = activeContext();
  const KeyMap::Result r = keys_.feed(chord, ctx, nowMs);
  switch (r.status) {
    case KeyMap::Status::Pending: setMessage(r.pendingText, 0, true); return true;
    case KeyMap::Status::NoMatch:
      if (pendingShown_) setMessage("");
      return r.abortedChord || dispatchUnbound(e);  // an aborted chord's stroke is swallowed
    case KeyMap::Status::Matched: break;
  }
  // GotoSymbolDefinitionUnderMouse only takes the middle click over an identifier of a text view; anywhere else (a tab,
  // the gutter, blank space, a picker) the click goes to the widgets as before, so a middle click still closes tabs.
  if (chord.mouse && chord.button == MouseButton::Middle && r.binding->command == "Editor.GotoDefinitionUnderMouse" &&
      !identifierUnderMouse(e.x, e.y).first)
    return root_.dispatch(e);
  if (chord.mouse && chord.button == MouseButton::Middle &&
      resolveCommandName(r.binding->command).id == "Editor.AddCaretAtMouse" && !textViewAt(e.x, e.y))
    return root_.dispatch(e);
  if (!commandRunnable(r.binding->command)) {
    // The middle button also closes tabs and so on: while a middle-click command is not runnable, the click goes to
    // the widgets as before.
    if (chord.mouse && chord.button == MouseButton::Middle) return root_.dispatch(e);
    // A 10x binding for a command tenfold lacks must not take keys away from the focused input or the open picker
    // (Alt+R toggles regex in the find bar, ReplaceNextInFile is Alt+R in 10x): they get the key first.
    if (std::string_view(ctx) != "editor" && root_.dispatch(e)) return true;
  }
  if (widgetOwnsKey(resolveCommandName(r.binding->command).id)) {
    root_.dispatch(e);  // the picker / bar toggles its option; the key never reaches the editor behind
    return true;
  }
  setMessage("");
  runCommandId(r.binding->command, r.binding->arg, ctx);
  return true;
}

// Text and keys without a binding go to the widgets; typed text and plain keys that reach the editor view while a key
// sequence is recorded become steps of it.
bool EditorApp::dispatchUnbound(const InputEvent& e) {
  const bool record = keySeq_.recording() && (e.type == InputType::Text || e.type == InputType::KeyDown) &&
                      std::string_view(activeContext()) == "editor";
  const bool handled = root_.dispatch(e);
  if (record && handled) {
    if (e.type == InputType::Text) keySeq_.recordText(e.text);
    else keySeq_.recordKey(e);
  }
  return handled;
}

// F8 / Shift+F8 (and F4 / Shift+F4 after a build): the next / previous build error or warning with a location.
bool EditorApp::stepBuildResult(bool forward) {
  std::string err;
  const auto m = forward ? editor_.nextBuildMessage(&err) : editor_.prevBuildMessage(&err);
  if (!m) {
    setMessage(err.empty() ? "No build errors or warnings" : err, kMessageMs);
    return true;
  }
  if (build_) build_->selectMessage(*m);
  setMessage(std::string(m->severity == BuildSeverity::Error ? "error: " : "warning: ") + m->text, kMessageMs);
  return true;
}

void EditorApp::runCommandId(const std::string& written, const std::string& arg, const char* ctx) {
  if (written == kPythonCommand) {
    setMessage("Python statements are not supported: " + arg, kMessageMs);
    return;
  }
  // Bindings, the palette and tests may name a command the 10x way or by its tenfold id.
  const ResolvedCommand rc = resolveCommandName(written);
  if (rc.known10x && !rc.implemented) {
    setMessage("not implemented yet: " + rc.name10x, kMessageMs);
    return;
  }
  const std::string id = rc.id;
  if (keySeq_.recording() && std::string_view(ctx) == "editor" && id != "App.RecordKeySequence" &&
      id != "App.PlaybackKeySequence" && id != "App.CommandPalette")
    keySeq_.recordCommand(id, arg);
  CommandContext cc;
  cc.editor = &editor_;
  cc.context = ctx;
  if (auto* tv = dynamic_cast<TextView*>(root_.focused()))
    cc.moveCarets = [tv](Editor::Direction dir, Editor::Unit unit, bool extend) {
      return tv->moveCaretsVisual(dir, unit, extend);
    };
  const std::string& cmdId = id;
  if (cmdId == "Editor.CollapseCarets")  // Escape closes an open inline diff block first (C3c)
    if (auto* tv = dynamic_cast<TextView*>(root_.focused()))
      if (tv->closeInlineDiffs()) return;
  if (cmdId.rfind("Editor.Goto", 0) == 0 || cmdId == "Editor.FindReferences") {
    // The answer may arrive twice (approximate, then authoritative); both feed the same picker. The Editor jumps by
    // itself for a single definition/declaration, so only References lists one location.
    const uint64_t token = ++navToken_;
    navResults_->close();
    const Command* c = registry_.find(cmdId);
    const std::string title = c ? c->name : cmdId;
    const size_t minResults = cmdId == "Editor.FindReferences" ? 1 : 2;
    cc.onNavResult = [this, life = std::weak_ptr<int>(life_), token, title, minResults](
                         Confidence conf, const std::vector<SymbolLocation>& locs, const std::string& err) {
      if (!life.expired()) showNavResults(token, title, minResults, conf, locs, err);
    };
  }
  const RunResult rr = registry_.run(id, arg, cc);
  const Command* cmd = registry_.find(id);
  const std::string name = cmd ? cmd->name : id;
  switch (rr.status) {
    case RunStatus::Ok:
    case RunStatus::Disabled: break;  // e.g. Back with no history: nothing to say
    case RunStatus::Unbound: setMessage(name + " is not available yet", kMessageMs); break;
    case RunStatus::UnknownCommand: setMessage("Unknown command " + id, kMessageMs); break;
    case RunStatus::Failed:
      setMessage(name + " failed" + (rr.message.empty() ? "" : ": " + rr.message), kMessageMs);
      break;
  }
}

int EditorApp::tick(int64_t nowMs) {
  now_ = nowMs;
  if (ttlMs_ > 0) {
    expiry_ = nowMs + ttlMs_;
    ttlMs_ = 0;
  }
  if (bench_) bench_->sync();
  if (terminals_) terminals_->tick();
  updateUntitledNames();
  vcsUi_->tick();
  if (settings_)
    forEachView([&](TextView& v) {  // views created since the last settings change (new tabs)
      if (v.settingsGen != settingsGen_) {
        v.applySettings(editorSettings(settings_->settings()));
        if (wrapOverride_) v.setWordWrap(*wrapOverride_);
        if (whitespaceOverride_) v.setShowWhitespace(*whitespaceOverride_);
        v.settingsGen = settingsGen_;
      }
    });
  if (keys_.tick(nowMs) && pendingShown_) setMessage("");
  if (expiry_ && nowMs >= expiry_) setMessage("");
  int wait = root_.tick(nowMs);
  auto soonest = [&](int ms) { wait = wait < 0 ? ms : std::min(wait, ms); };
  if (const int due = symbols_->tick(nowMs); due >= 0) soonest(due);
  if (const int due = find_->tick(nowMs); due >= 0) soonest(due);
  if (const int due = replaceInFiles_->tick(nowMs); due >= 0) soonest(due);
  findReplace_->tick(nowMs);
  soonest(external_->tick(nowMs));
  if (const int due = hover_->tick(nowMs); due >= 0) soonest(due);
  diagPanel_->refresh();
  if (explorer_) {
    if (const int due = explorer_->tick(nowMs); due >= 0) soonest(due);
  }
  if (bench_) bench_->status().setText("config", configPicker_->statusText());
  breakpointList_->refresh();
  if (const DebugSession& dbg = editor_.debug(); dbg.generation() != debugErrorGen_) {  // C2c: session errors
    debugErrorGen_ = dbg.generation();
    if (!dbg.lastError().empty() && dbg.lastError() != debugErrorShown_) setMessage(dbg.lastError(), kMessageMs);
    debugErrorShown_ = dbg.lastError();
  }
  if (build_)
    bench_->status().setText(
        "build", build_->state() == BuildPanel::State::Idle ? std::string() : "build: " + build_->statusText());
  if (nav_) {
    const int w = nav_->tick(nowMs);
    if (w >= 0) soonest(w);
    bench_->status().setText("navcounts", nav_->showCounts() ? nav_->countsText() : std::string());
  }
  if (keys_.pending()) soonest(100);
  if (expiry_) soonest(static_cast<int>(std::max<int64_t>(1, expiry_ - nowMs)));
  return wait;
}

// ---------------------------------------------------------------------------------------------------------------
// W8c: the 10x parser commands. They wire the existing engine features (clangd rename, formatting, hover, signature
// help, completion, document symbols) to commands. Only FindFunction and Autocomplete have a fast-tier fallback, and
// both show which tier answered; rename and formatting say so when clangd is missing.
// ---------------------------------------------------------------------------------------------------------------

std::pair<TextView*, Position> EditorApp::identifierUnderMouse(float x, float y) {
  Widget* w = root_.hitTest(x, y);
  while (w && !dynamic_cast<TextView*>(w)) w = w->parent();
  auto* v = static_cast<TextView*>(w);
  if (!v || !v->document()) return {nullptr, {}};
  if (const auto p = v->identifierAt(x, y)) return {v, *p};
  return {nullptr, {}};
}

void EditorApp::bindParserCommands() {
  const auto hasDoc = [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); };
  // The features report their outcome in the status bar, so these handlers report success and keep that text.
  registry_.bind(
      "Editor.RenameSymbol",
      [this](const CommandContext&, std::string_view) {
        closePickers();
        completion_->close();
        sigHelp_->close();
        hover_->dismiss();
        rename_->open();
        return true;
      },
      hasDoc);
  registry_.bind(
      "Editor.SubmitRenameSymbol", [this](const CommandContext&, std::string_view) { return rename_->submit(); },
      [this](const CommandContext&) { return rename_->isOpen(); });
  registry_.bind(
      "Editor.Autocomplete",
      [this](const CommandContext&, std::string_view) {
        hover_->dismiss();
        return completion_->trigger();
      },
      hasDoc);
  registry_.bind(
      "Editor.ShowFunctionArgs", [this](const CommandContext&, std::string_view) { return sigHelp_->trigger(); },
      hasDoc);
  registry_.bind(
      "Editor.ShowSymbolInfo",
      [this](const CommandContext&, std::string_view) {
        hover_->showAtCaret();
        return true;
      },
      hasDoc);
  registry_.bind(
      "Editor.FindFunction",
      [this](const CommandContext&, std::string_view) {
        closePickers();
        functions_->open();
        return true;
      },
      hasDoc);
  registry_.bind(
      "Editor.GotoDefinitionUnderMouse",
      [this](const CommandContext& c, std::string_view) {
        const auto [view, pos] = identifierUnderMouse(mouseX_, mouseY_);
        if (!view) {
          setMessage("No symbol under the mouse", kMessageMs);
          return true;
        }
        c.editor->gotoDefinitionAt(*view->document(), pos, c.onNavResult);
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->started(); });
  registry_.bind(
      "Editor.FormatSelection",
      [this](const CommandContext& c, std::string_view) {
        Editor& ed = *c.editor;
        if (!ed.started() || !ed.semantic().status().clangdInitialized) {
          setMessage("Format needs clangd, which is not running: it has no approximate tier", kMessageMs);
          return true;
        }
        Range r{};
        bool selected = false;
        for (const Selection& s : ed.selections())  // the primary selection, else the first other one
          if (!s.empty() && !selected) {
            r = s.range();
            selected = true;
          }
        if (!selected) r = formatScopeRange(ed.currentDocument()->buffer(), ed.selections()[0].head);
        const std::string what = selected ? "selection" : "scope";
        ed.formatRange(r, [this, life = std::weak_ptr<int>(life_), what](const size_t& n, const std::string& err) {
          if (life.expired()) return;
          if (!err.empty()) setMessage("Format failed: " + err, kMessageMs);
          else if (n == 0) setMessage("The " + what + " is already formatted", kMessageMs);
          else
            setMessage("Formatted the " + what + " (" + std::to_string(n) + (n == 1 ? " edit)" : " edits)"),
                       kMessageMs);
        });
        return true;
      },
      hasDoc);
}

// ---------------------------------------------------------------------------------------------------------------
// W8f: context menus. Every item runs a command through the registry; the menus are only lists of command names
// (ContextMenus.cpp). The widget under the pointer (right click) or the focused widget (ShowContextMenu) decides which.
// ---------------------------------------------------------------------------------------------------------------

ContextMenu* EditorApp::contextMenu() { return menu_ && root_.modal() == menu_ ? menu_ : nullptr; }

MenuCommandInfo EditorApp::menuInfo(const std::string& written) {
  MenuCommandInfo info;
  const ResolvedCommand rc = resolveCommandName(written);
  if (rc.known10x && !rc.implemented) return info;  // a 10x command tenfold lacks: left out
  const Command* c = registry_.find(rc.id);
  if (!c || !c->handler) return info;  // not bound by the front end (no Workbench, ...)
  if (rc.id == "Debug.OpenInVisualStudio" && !editor_.debug().hasBackend()) return info;
  CommandContext cc;
  cc.editor = &editor_;
  cc.context = activeContext();
  info.available = true;
  info.enabled = c->isEnabled(cc);
  info.name = c->name;
  info.shortcut = bindingFor(rc.id);
  return info;
}

std::vector<MenuItem> EditorApp::buildCommandMenu(const std::vector<MenuEntry>& entries, bool onVcsChange) {
  MenuHooks h;
  h.info = [this](const std::string& cmd) { return menuInfo(cmd); };
  h.run = [this](const std::string& cmd, const std::string& arg) { runCommandId(cmd, arg, activeContext()); };
  return buildMenu(entries, h, onVcsChange);
}

bool EditorApp::showContextMenu(std::vector<MenuItem> items, const RectF& anchor, bool keyboard) {
  if (items.empty()) return false;
  closePickers();
  completion_->close();
  sigHelp_->close();
  hover_->dismiss();
  auto m = std::make_unique<ContextMenu>(std::move(items));
  ContextMenu* raw = m.get();
  raw->onClosed([this, raw] {
    if (menu_ == raw) menu_ = nullptr;
  });
  menu_ = ContextMenu::open(root_, std::move(m), anchor, keyboard);
  return menu_ != nullptr;
}

bool EditorApp::menuRequestFor(Widget& target, float x, float y, bool keyboard, ContextMenuRequest& out) {
  for (size_t i = menuProviders_.size(); i-- > 0;)
    if (menuProviders_[i](target, x, y, keyboard, out) && !out.items.empty()) return true;
  for (Widget* w = &target; w; w = w->parent()) {
    if (auto* tv = dynamic_cast<TextView*>(w)) {
      if (!tv->document() || (!keyboard && w != &target)) return false;
      if (!keyboard) {  // like clicking: this view and its document become current, the caret goes under the pointer
        if (bench_)
          for (Widget* p = tv; p; p = p->parent())
            if (auto* panel = dynamic_cast<Panel*>(p)) {
              bench_->setActivePanel(panel);
              break;
            }
        tv->activate();
        const Position pos = tv->document()->buffer().clamp(tv->positionAt(x, y));
        bool inSelection = false;
        for (const Selection& s : editor_.selections())
          if (!s.empty() && s.range().contains(pos)) inSelection = true;
        if (!inSelection) editor_.setSelections({caretAt(pos)});
        out.anchor = {x, y, 0, 0};
      } else {
        RectF c = tv->caretRect(editor_.cursor().pos);
        const RectF& b = tv->bounds();  // a caret scrolled out of view: the nearest edge of the view
        c.x = std::clamp(c.x, b.x, std::max(b.x, b.x + b.w - 1));
        c.y = std::clamp(c.y, b.y, std::max(b.y, b.y + b.h - c.h));
        out.anchor = c;
      }
      bool onChange = false;
      if (tv->diffStripActive()) {
        const std::vector<DiffHunk> hunks = tv->currentHunks();
        onChange = TextView::hunkIndexAt(hunks, editor_.cursor().pos.line) >= 0;
      }
      out.items = buildCommandMenu(editorMenuEntries(), onChange);
      return !out.items.empty();
    }
    if (keyboard) continue;
    if (auto* strip = dynamic_cast<TabStrip*>(w)) {
      const int id = strip->tabAt(x, y);
      if (id < 0) return false;
      strip->setActive(id);  // the tab menu's commands act on the active tab
      if (strip->onActivate) strip->onActivate(id);
      const int i = strip->indexOf(id);
      out.items = buildCommandMenu(tabMenuEntries(i >= 0 && strip->tabs()[static_cast<size_t>(i)].pinned));
      out.anchor = {x, y, 0, 0};
      return !out.items.empty();
    }
    if (auto* bar = dynamic_cast<StatusBar*>(w)) {
      const std::string key = bar->keyAt(x, y);
      if (key.empty()) return false;
      out.items = buildCommandMenu(statusMenuEntries(key));
      out.anchor = {x, y, 0, 0};
      return !out.items.empty();
    }
  }
  return false;
}

bool EditorApp::openContextMenuAt(float x, float y) {
  Widget* target = root_.hitTest(x, y);
  if (!target) return false;
  ContextMenuRequest req;
  if (!menuRequestFor(*target, x, y, false, req)) return false;
  return showContextMenu(std::move(req.items), req.anchor, false);
}

bool EditorApp::openContextMenuForFocus() {
  Widget* f = root_.focused();
  if (!f)
    forEachView([&](TextView& v) {
      if (!f && v.isCurrent() && v.visible()) f = &v;
    });
  if (!f) return false;
  ContextMenuRequest req;
  if (!menuRequestFor(*f, 0, 0, true, req)) return false;
  return showContextMenu(std::move(req.items), req.anchor, true);
}

void EditorApp::closeTabs(const std::vector<int>& ids) {
  int kept = 0;
  for (int id : ids) {
    if (bench_->isLastViewOfDirty(id)) ++kept;
    else bench_->closeTab(id);
  }
  if (kept) setMessage(std::to_string(kept) + " modified tab(s) left open", kMessageMs);
}

// The explorer tree's menu (W8f): the node under the pointer, or the selected one from the keyboard. Commands take the
// node's path as their argument, so nothing has to be opened first.
bool EditorApp::explorerMenu(Widget& target, float x, float y, bool keyboard, ContextMenuRequest& out) {
  TreeView* tv = explorer_ ? explorer_->view() : nullptr;
  if (!tv) return false;
  bool inside = false;
  for (Widget* w = &target; w; w = w->parent())
    if (w == tv) inside = true;
  if (!inside) return false;
  const int node = keyboard ? tv->selectedNode() : tv->nodeAt(x, y);
  if (node <= 0) return false;
  if (!keyboard) {
    tv->select(node);  // right click selects first
    tv->setFocus();
  }
  const TreeNode n = tv->model().node(node);
  std::vector<MenuItem> items;
  const bool isFile = n.kind == TreeKind::File && !n.path.empty();
  if (isFile) {
    MenuItem open;
    open.label = "Open";
    open.action = [tv, node] {
      if (tv->onActivate) tv->onActivate(node);
    };
    items.push_back(std::move(open));
  }
  if (n.expandable()) {
    MenuItem m;
    m.label = tv->isExpanded(node) ? "Collapse" : "Expand";
    m.action = [tv, node] { tv->toggle(node); };
    items.push_back(std::move(m));
  }
  {
    MenuItem m;
    m.label = "Collapse All";
    m.action = [tv] { tv->expandAll(false); };
    items.push_back(std::move(m));
  }
  if (!n.path.empty()) {
    items.push_back(MenuItem::sep());
    std::vector<MenuEntry> entries = {{"File.ShowInFolder", "Open Containing Folder", false, n.path},
                                      {"File.CopyPath", "Copy Full Path", false, n.path},
                                      {"Terminal.OpenAt", "Open in Terminal", false, n.path}};
    std::string ext = std::filesystem::path(n.path).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (isFile && (ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx"))
      entries.push_back({"Build.BuildCurrentFile", "Build Current File", false, n.path});
    for (MenuItem& m : buildCommandMenu(entries)) items.push_back(std::move(m));
  }
  out.items = std::move(items);
  if (keyboard) {
    const int row = tv->selectedRow();
    const RectF r = row >= 0 ? tv->rowRect(row) : tv->bounds();
    out.anchor = {r.x + 24, r.y, 0, r.h};
  } else {
    out.anchor = {x, y, 0, 0};
  }
  return true;
}

}  // namespace tf::ui
