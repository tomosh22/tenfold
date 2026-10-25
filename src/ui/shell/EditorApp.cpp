#include "ui/shell/EditorApp.h"

#include <algorithm>
#include <cstdlib>
#include <set>

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
  if (bench_) files_ = std::make_unique<FileSearch>(pickerEnv_, *bench_);
  registerEditorCommands(registry_);
  bindUiCommands();
  keys_ = loadKeyBindings(editor.workspace().root(), userDir, diags_);
  if (bench_) {
    bench_->status().addSegment("message", StatusBar::Align::Left);
    bench_->status().addSegment("navcounts", StatusBar::Align::Right);
    NavigationUi::Hooks hooks;
    hooks.run = [this](const std::string& id) {
      runCommandId(id, "", activeContext());
      return true;
    };
    hooks.shortcut = [this](const std::string& id) { return shortcutFor(id); };
    nav_ = std::make_unique<NavigationUi>(*bench_, editor_, std::move(hooks));
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
  closePickers();  // their callbacks point into this object
  life_.reset();
  editor_.onSemanticTokens(nullptr);
  if (settings_) {
    settings_->settings().removeOnChanged(settingsToken_);
    settings_->onThemeChanged(nullptr);
  }
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
  setNavStatusCounts(settings_->settings().getBool("nav.statusCounts"));
  applySettings(settings_->settings());
  forEachView([&](TextView& v) {
    v.applySettings(es);
    v.settingsGen = settingsGen_;
  });
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
    for (const std::string& k : keys) font |= k.rfind("font.", 0) == 0;
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
void EditorApp::applySettings(const Settings& s) { pickerEnv_.preview = s.getBool("picker.preview"); }

void EditorApp::closePickers() {
  symbols_->close();
  navResults_->close();
  if (files_) files_->close();
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

void EditorApp::bindUiCommands() {
  registry_.bind(
      "Search.SymbolSearch",
      [this](const CommandContext&, std::string_view) {
        closePickers();
        symbols_->open();
        return true;
      },
      [](const CommandContext& c) { return c.editor && c.editor->started(); });
  registry_.bind("App.Quit", [this](const CommandContext&, std::string_view) { return quit_ = true; });
  registry_.bind(
      "Editor.Copy", [](const CommandContext& c, std::string_view) { return doCopy(*c.editor); },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind(
      "Editor.Cut", [](const CommandContext& c, std::string_view) { return doCut(*c.editor); },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  registry_.bind(
      "Editor.Paste", [](const CommandContext& c, std::string_view) { return doPaste(*c.editor); },
      [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  if (!bench_) return;
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
  registry_.bind("Workbench.FocusPanel", [this](const CommandContext&, std::string_view arg) {
    const auto ps = bench_->panels();
    const int n = std::atoi(std::string(arg).c_str());  // 1-based
    if (n < 1 || n > static_cast<int>(ps.size())) return false;
    bench_->setActivePanel(ps[static_cast<size_t>(n - 1)]);
    return true;
  });
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
  if (nav_) nav_->setNow(nowMs);
  if (ttlMs_ > 0) {
    expiry_ = nowMs + ttlMs_;
    ttlMs_ = 0;
  }
  if (e.type == InputType::KeyUp || (e.type == InputType::KeyDown && !(e.mods & ModCtrl))) {
    if (cycling_ && !(e.mods & ModCtrl)) {  // Ctrl released: the Ctrl+Tab cycle is over
      cycling_ = false;
      if (bench_) bench_->endMruCycle();
    }
  }
  KeyChord chord;
  if (e.type == InputType::KeyDown && e.key != Key::Unknown)  // a lone modifier press is not a stroke
    chord = chordFromKey(e.key, e.mods);
  else if (e.type == InputType::MouseDown && (e.button == MouseButton::X1 || e.button == MouseButton::X2))
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
  setMessage("");
  runCommandId(r.binding->command, r.binding->arg, ctx);
  return true;
}

void EditorApp::runCommandId(const std::string& id, const std::string& arg, const char* ctx) {
  CommandContext cc;
  cc.editor = &editor_;
  cc.context = ctx;
  const std::string& cmdId = id;
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
  if (settings_)
    forEachView([&](TextView& v) {  // views created since the last settings change (new tabs)
      if (v.settingsGen != settingsGen_) {
        v.applySettings(editorSettings(settings_->settings()));
        v.settingsGen = settingsGen_;
      }
    });
  if (keys_.tick(nowMs) && pendingShown_) setMessage("");
  if (expiry_ && nowMs >= expiry_) setMessage("");
  int wait = root_.tick(nowMs);
  auto soonest = [&](int ms) { wait = wait < 0 ? ms : std::min(wait, ms); };
  if (const int due = symbols_->tick(nowMs); due >= 0) soonest(due);
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
