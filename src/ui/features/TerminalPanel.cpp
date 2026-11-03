#include "ui/features/TerminalPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "ui/platform/Window.h"
#include "ui/shell/FontSetup.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool isCmdExe(const std::string& s) {
  const std::string l = lower(s);
  return l == "cmd" || l == "cmd.exe";
}

}  // namespace

TerminalPanel::TerminalPanel(Editor& editor, Workbench& bench) : editor_(editor), bench_(bench) {
  wake = [] { Window::wake(); };
}

TerminalPanel::~TerminalPanel() = default;

void TerminalPanel::applySettings(const Settings& s, float dpiScale) {
  command_ = s.getString("terminal.command");
  claude_ = s.getString("terminal.claudeCommand");
  cursor_ = s.getString("terminal.cursorCommand");
  scrollback_ = static_cast<size_t>(std::clamp(s.getInt("terminal.scrollbackLines"), 0, 1000000));
  closeOnExit_ = s.getBool("terminal.closeOnExit");
  wheelLines_ = std::max(1, s.getInt("editor.wheelLines"));
  for (const Info& i : infos_)
    if (TerminalView* v = view(i.id)) {
      v->wheelLines = wheelLines_;
      if (v->session()) v->session()->screen().setScrollbackLimit(scrollback_);
    }
  rebuildFont(s, dpiScale);
}

// A terminal.font / terminal.fontSize that differs from the editor's gets its own renderer, shared by all terminals.
void TerminalPanel::rebuildFont(const Settings& s, float dpiScale) {
  const FontSpec spec = s.getFont("terminal.font");
  const float size =
      s.getFloat("terminal.fontSize") > 0 ? static_cast<float>(s.getFloat("terminal.fontSize")) : spec.sizePt;
  const FontSpec& ed = s.getFont("font.editor");
  const float editorPt = ed.sizePt > 0 ? ed.sizePt : static_cast<float>(s.getFloat("font.size"));
  const std::string key = spec.family + "|" + std::to_string(size) + "|" + std::to_string(dpiScale) + "|" +
                          std::to_string(editorPt) + "|" +
                          (size > 0 || !spec.family.empty() ? bench_.text().fontPath() : "");
  if (key == fontKey_) return;
  fontKey_ = key;
  std::unique_ptr<gfx::TextRenderer> next;
  if (!spec.family.empty() || size > 0) {
    std::string file = spec.family.empty() ? bench_.text().fontPath() : resolveFontFile(spec.family);
    if (file.empty()) {
      if (message) message("terminal.font: no font file found for '" + spec.family + "', using the editor font");
      file = bench_.text().fontPath();
    }
    const float pt = size > 0 ? size : (editorPt > 0 ? editorPt : 10.0f);
    next = gfx::TextRenderer::create(file, std::clamp(pt, 6.0f, 72.0f) * 4.0f / 3.0f, dpiScale);
  }
  if (font_->tr) font_->retired.push_back(std::move(font_->tr));
  font_->tr = std::move(next);
  bench_.requestLayout();
}

std::vector<std::string> TerminalPanel::commandFor(Kind kind) const {
  const std::string& line = kind == Kind::Shell ? command_ : kind == Kind::Claude ? claude_ : cursor_;
  std::vector<std::string> parts = Pty::splitCommand(line);
#ifndef _WIN32
  // 10x's defaults are Windows command lines: `cmd.exe` is "the shell", `cmd /c agent` is "agent".
  if (parts.size() >= 2 && isCmdExe(parts[0]) && (lower(parts[1]) == "/c" || lower(parts[1]) == "/k"))
    parts.erase(parts.begin(), parts.begin() + 2);
  else if (parts.size() == 1 && isCmdExe(parts[0])) parts.clear();
#endif
  if (parts.empty()) {
    if (kind == Kind::Shell) return Pty::splitCommand(Pty::defaultShell());
    return {kind == Kind::Claude ? "claude" : "agent"};
  }
  return parts;
}

fs::path TerminalPanel::workingDirectory() const {
  std::error_code ec;
  const fs::path root = editor_.workspace().root();
  if (!root.empty() && fs::is_directory(root, ec)) return root;
  if (const Document* d = editor_.currentDocument()) {
    const fs::path dir = d->path().parent_path();
    if (!dir.empty() && fs::is_directory(dir, ec)) return dir;
  }
  for (const char* v : {"HOME", "USERPROFILE"})
    if (const char* h = std::getenv(v); h && *h && fs::is_directory(h, ec)) return h;
  return fs::current_path(ec);
}

std::string TerminalPanel::baseTitle(Kind k, const std::vector<std::string>& cmd, int serial) const {
  (void)serial;
  if (k == Kind::Claude) return "Claude Code";
  if (k == Kind::Cursor) return "Cursor";
  std::string name = fs::path(cmd.empty() ? "terminal" : cmd[0]).stem().string();
  return name.empty() ? "Terminal" : name;
}

TerminalView* TerminalPanel::view(int id) const {
  Panel* p = bench_.panelOf(id);
  return p ? dynamic_cast<TerminalView*>(p->content(id)) : nullptr;
}

void TerminalPanel::prune() {
  infos_.erase(std::remove_if(infos_.begin(), infos_.end(), [&](const Info& i) { return !view(i.id); }), infos_.end());
  if (lastShown_ >= 0 && !view(lastShown_)) lastShown_ = -1;
}

std::vector<int> TerminalPanel::terminals() const {
  std::vector<int> out;
  for (const Info& i : infos_)
    if (view(i.id)) out.push_back(i.id);
  return out;
}

int TerminalPanel::create(Kind kind, std::string* error) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    return -1;
  };
  if (!Pty::available()) return fail("terminals need Windows 10 version 1809 or later (ConPTY)");
  prune();
  const std::vector<std::string> cmd = commandFor(kind);
  PtyOptions o;
  o.program = cmd[0];
  o.args.assign(cmd.begin() + 1, cmd.end());
  o.cwd = workingDirectory();
  o.env.emplace_back("TERM_PROGRAM", "tenfold");

  auto own = std::make_unique<TerminalView>();
  TerminalView* v = own.get();
  v->wheelLines = wheelLines_;
  v->setFont(font_);
  const int serial = ++serial_;
  const std::string title = baseTitle(kind, cmd, serial);
  const int id = bench_.addWidgetTab(std::move(own), title);
  if (id < 0) return fail("no panel to put the terminal in");

  // The size the program starts with: what the panel's content area holds (lay out now, the next frame is too late).
  int rows = 24, cols = 80;
  if (bench_.bounds().w > 0 && bench_.bounds().h > 0) {
    bench_.layout(bench_.bounds());
    if (v->bounds().w > 0 && v->bounds().h > 0) std::tie(rows, cols) = v->gridFor(v->bounds());
  }
  TerminalSession::Hooks hooks;
  Editor* ed = &editor_;
  hooks.post = [ed](std::function<void()> fn) { ed->post(std::move(fn)); };
  hooks.wake = wake;  // a copy: the hooks must not point into this panel, which may die before the view does
  auto session = std::make_unique<TerminalSession>(std::move(hooks), rows, cols, scrollback_);
  TerminalSession* sp = session.get();
  v->setSession(std::move(session));
  std::string err;
  if (!sp->start(o, &err)) {
    bench_.closeTab(id);
    return fail(err.empty() ? "cannot start " + o.program : err);
  }
  infos_.push_back({id, kind, title, title, serial});
  focusTerminal(id);
  return id;
}

void TerminalPanel::focusTerminal(int id) {
  Panel* p = bench_.panelOf(id);
  if (!p) return;
  p->setActive(id);
  bench_.setActivePanel(p);
  if (TerminalView* v = view(id)) v->setFocus();
  lastShown_ = id;
}

int TerminalPanel::show(std::string* error) {
  prune();
  const std::vector<int> ids = terminals();
  if (ids.empty()) return create(Kind::Shell, error);
  int target = -1;
  Panel* ap = bench_.activePanel();
  const int active = ap ? ap->activeId() : -1;
  const auto it = std::find(ids.begin(), ids.end(), active);
  if (it != ids.end()) {
    TerminalView* v = view(*it);
    // The shown terminal already has the keys: go to the next one. Shown but not focused (the explorer or an editor
    // has the focus, or another panel is active): give it the focus first.
    const bool hasKeys = v && v->hasFocus() && ap == bench_.activePanel();
    target = hasKeys ? *(it + 1 == ids.end() ? ids.begin() : it + 1) : *it;
  } else if (lastShown_ >= 0 && std::find(ids.begin(), ids.end(), lastShown_) != ids.end()) {
    target = lastShown_;
  } else {
    target = ids.front();
  }
  focusTerminal(target);
  return target;
}

void TerminalPanel::tick() {
  prune();
  std::vector<int> done;
  for (Info& i : infos_) {
    TerminalView* v = view(i.id);
    if (!v || !v->session()) continue;
    v->syncFocusReport();
    if (closeOnExit_ && v->session()->exited()) {
      done.push_back(i.id);
      continue;
    }
    const std::string& t = v->session()->screen().title();
    const std::string want = t.empty() ? i.baseTitle : t;
    if (want != i.shownTitle) {
      i.shownTitle = want;
      if (Panel* p = bench_.panelOf(i.id)) p->tabs().setTitle(i.id, want);
    }
  }
  for (int id : done) bench_.closeTab(id);
  if (!done.empty()) prune();
}

void TerminalPanel::setWindowFocused(bool on) {
  for (const Info& i : infos_)
    if (TerminalView* v = view(i.id)) v->setWindowFocused(on);
}

void TerminalPanel::closeAll() {
  for (int id : terminals()) bench_.closeTab(id);
  prune();
}

}  // namespace tf::ui
