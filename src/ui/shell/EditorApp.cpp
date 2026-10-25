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
  find_ = std::make_unique<FindInFiles>(pickerEnv_);
  find_->setMarkersFn([this](const std::filesystem::path& f, const std::vector<Range>& r) {
    forEachView([&](TextView& v) {
      if (v.document() && v.document()->path() == f) v.setSearchMarkers(r);
    });
  });
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
  bookmarkList_ = std::make_unique<BookmarkList>(pickerEnv_);
  completion_ = std::make_unique<CompletionPopup>(pickerEnv_);
  hover_ = std::make_unique<HoverTooltip>(pickerEnv_);
  sigHelp_ = std::make_unique<SignatureHelp>(pickerEnv_);
  if (bench_) files_ = std::make_unique<FileSearch>(pickerEnv_, *bench_);
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
  vcsUi_.reset();  // unhooks VcsManager::onError, closes its confirmation
  closePickers();  // their callbacks point into this object
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
  o.autoClosePairs = s.getBool("editor.autoClosePairs");
  o.trimTrailingWhitespace = s.getBool("editor.trimTrailingWhitespace");
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
  vcsUi_->applySettings(s);
}

void EditorApp::closePickers() {
  symbols_->close();
  navResults_->close();
  find_->close();
  diagPanel_->close();
  goToLine_->close();
  bookmarkList_->close();
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

void EditorApp::bindUiCommands() {
  vcsUi_->bind(registry_);
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
  for (bool replace : {false, true})
    registry_.bind(
        replace ? "Editor.Replace" : "Editor.Find",
        [this, replace](const CommandContext&, std::string_view) {
          closePickers();
          findReplace_->open(replace);
          return true;
        },
        [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
  for (bool fwd : {true, false})
    registry_.bind(
        fwd ? "Editor.FindNext" : "Editor.FindPrev",
        [this, fwd](const CommandContext&, std::string_view) { return findReplace_->step(fwd); },
        [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); });
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
  registry_.bind("Build.Build", [this](const CommandContext&, std::string_view arg) {
    BuildOverrides o;
    o.command = std::string(arg);  // optional: a custom shell command instead of the detected build
    bench_->showBottomPanel("Build");
    std::string err;
    if (!build_->start(o, &err)) setMessage("Build failed to start: " + err, kMessageMs);
    return true;
  });
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
  vcsUi_->tick();
  if (settings_)
    forEachView([&](TextView& v) {  // views created since the last settings change (new tabs)
      if (v.settingsGen != settingsGen_) {
        v.applySettings(editorSettings(settings_->settings()));
        if (wrapOverride_) v.setWordWrap(*wrapOverride_);
        v.settingsGen = settingsGen_;
      }
    });
  if (keys_.tick(nowMs) && pendingShown_) setMessage("");
  if (expiry_ && nowMs >= expiry_) setMessage("");
  int wait = root_.tick(nowMs);
  auto soonest = [&](int ms) { wait = wait < 0 ? ms : std::min(wait, ms); };
  if (const int due = symbols_->tick(nowMs); due >= 0) soonest(due);
  if (const int due = find_->tick(nowMs); due >= 0) soonest(due);
  findReplace_->tick(nowMs);
  if (const int due = hover_->tick(nowMs); due >= 0) soonest(due);
  diagPanel_->refresh();
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
