#include "ui/shell/EditorApp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string_view>

#include "ui/commands/CommandNames.h"
#include "ui/commands/EditorCommands.h"
#include "ui/platform/Clipboard.h"
#include "ui/settings/Settings.h"
#include "ui/widgets/TextView.h"

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

bool doCopy(Editor& ed) { return clipboardSet(copyText(ed)); }

bool doCut(Editor& ed) {
  if (!doCopy(ed)) return false;
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
bool doPaste(Editor& ed) {
  std::string t = clipboardGet();
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
  if (bench_) files_ = std::make_unique<FileSearch>(pickerEnv_, *bench_);
  unsaved_ = std::make_unique<UnsavedPrompt>(pickerEnv_);
  saveAs_ = std::make_unique<SaveAsPrompt>(pickerEnv_);
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
  registerEditorCommands(registry_);
  bindUiCommands();
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
  vcsUi_.reset();  // unhooks VcsManager::onError, closes its confirmation
  closePickers();  // their callbacks point into this object
  palette_.reset();
  external_.reset();  // unhooks Editor::onExternalChange
  unsaved_.reset();
  saveAs_.reset();
  if (build_) {
    build_->onClose = nullptr;
    build_->onMessage = nullptr;
    build_->onJumped = nullptr;
  }
  findReplace_.reset();
  completion_.reset();
  hover_.reset();
  sigHelp_.reset();
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
}

void EditorApp::closePickers() {
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
  palette_->close();
  unsaved_->close();
  saveAs_->close();
  external_->close();
  if (files_) files_->close();
  if (vcsUi_) vcsUi_->closeConfirm();
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
      [](const CommandContext& c, std::string_view) {
        const std::string text = c.editor->cutLines();
        return text.empty() || clipboardSet(text);
      },
      hasDoc);
  registry_.bind(
      "File.SaveAll",
      [this](const CommandContext& c, std::string_view) {
        std::string err;
        if (!c.editor->saveAll(&err)) setMessage("Cannot save all files: " + err, kDiagMs);
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
  for (bool fwd : {true, false})
    registry_.bind(
        fwd ? "Editor.FindNextCurrentWord" : "Editor.FindPrevCurrentWord",
        [this, fwd](const CommandContext&, std::string_view) { return findReplace_->stepCurrentWord(fwd); }, hasDoc);
}

void EditorApp::bindUiCommands() {
  vcsUi_->bind(registry_);
  compileInfo_->bind(registry_, [this] { closePickers(); });
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
  registry_.bind("Search.NextResult", [this](const CommandContext&, std::string_view) {
    find_->step(true);  // says "No search results" / "Result i of n" itself
    return true;
  });
  registry_.bind("Search.PrevResult", [this](const CommandContext&, std::string_view) {
    find_->step(false);
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
        if (c.editor->save(*d, &err)) return true;
        setMessage("Cannot save " + d->path().filename().string() + (err.empty() ? std::string() : ": " + err),
                   kDiagMs);
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind(
      "Editor.Copy", [](const CommandContext& c, std::string_view) { return doCopy(*c.editor); },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind(
      "Editor.Cut", [](const CommandContext& c, std::string_view) { return doCut(*c.editor); },
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
      [this](const CommandContext& c, std::string_view) {
        const Document* d = c.editor->currentDocument();
        if (d->isUntitled()) {
          setMessage(d->path().filename().string() + " has not been saved yet", kMessageMs);
          return true;
        }
        BuildOverrides o;
        o.kind = BuildOverrides::Kind::CurrentFile;
        o.file = d->path();
        return startBuild(std::move(o));
      },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind(
      "Build.Cancel",
      [this](const CommandContext&, std::string_view) {
        build_->cancel();
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->buildRunning(); });
  for (bool fwd : {true, false})
    registry_.bind(fwd ? "Build.NextError" : "Build.PrevError", [this, fwd](const CommandContext&, std::string_view) {
      std::string err;
      const auto m = fwd ? editor_.nextBuildMessage(&err) : editor_.prevBuildMessage(&err);
      if (!m) {
        setMessage(err.empty() ? "No build errors or warnings" : err, kMessageMs);
        return true;
      }
      build_->selectMessage(*m);
      setMessage(std::string(m->severity == BuildSeverity::Error ? "error: " : "warning: ") + m->text, kMessageMs);
      return true;
    });
  registry_.bind(
      "Workbench.CloseTab",
      [this](const CommandContext&, std::string_view) {
        if (!bench_->activePanel() || bench_->activePanel()->activeId() < 0) return false;
        bench_->requestCloseTab(bench_->activePanel()->activeId());
        return true;
      },
      [this](const CommandContext&) { return bench_->activePanel() && bench_->activePanel()->activeId() >= 0; });
  registry_.bind("View.ToggleBuildPanel", [this](const CommandContext&, std::string_view) {
    bench_->toggleBottomPanel("Build");
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
      "File.ShowInFolder",
      [this](const CommandContext& c, std::string_view) {
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
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
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

const char* EditorApp::activeContext() const {
  if (root_.modal()) return "picker";
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
  if (root_.modal()) {  // a picker owns the screen: the editor popups step aside
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
  if (!root_.modal()) {
    if (e.type == InputType::Text) {
      completion_->onTyped(e.text);
      sigHelp_->onTyped(e.text);
    }
    completion_->update();
    sigHelp_->update();
  }
  return r;
}

bool EditorApp::handleEventImpl(const InputEvent& e, int64_t nowMs) {
  if (nav_) nav_->setNow(nowMs);
  if (ttlMs_ > 0) {
    expiry_ = nowMs + ttlMs_;
    ttlMs_ = 0;
  }
  if (e.type == InputType::FocusGained) vcsUi_->refreshOnFocus();  // the engine does not watch .git / the p4 client
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
  else return root_.dispatch(e);

  const char* ctx = activeContext();
  const KeyMap::Result r = keys_.feed(chord, ctx, nowMs);
  switch (r.status) {
    case KeyMap::Status::Pending: setMessage(r.pendingText, 0, true); return true;
    case KeyMap::Status::NoMatch:
      if (pendingShown_) setMessage("");
      return r.abortedChord || root_.dispatch(e);  // an aborted chord's stroke is swallowed
    case KeyMap::Status::Matched: break;
  }
  if (!commandRunnable(r.binding->command)) {
    // The middle button also closes tabs and so on: while its 10x commands (GotoSymbolDefinitionUnderMouse,
    // AddCursorAtMousePos) are missing, the click goes to the widgets as before.
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

}  // namespace tf::ui
