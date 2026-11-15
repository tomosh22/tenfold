#include "ui/shell/EmptyWindow.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "core/PathUtil.h"
#include "ui/commands/CommandNames.h"
#include "ui/features/ContextMenus.h"
#include "workspace/Workspace.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {
constexpr int kMessageMs = 4000;
}

bool EmptyWindow::availableWithoutWorkspace(const std::string& id) {
  static const std::set<std::string, std::less<>> ids = {"File.OpenSolution", "File.OpenFolder", "File.RecentSolutions",
                                                         "App.Quit", "View.ToggleFullscreen"};
  return ids.count(id) != 0;
}

EmptyWindow::EmptyWindow(gfx::TextRenderer& text, const Theme& theme, const fs::path& userDir, Hooks hooks,
                         bool fullscreen)
    : RootWidget(text, theme), hooks_(std::move(hooks)), fullscreen_(fullscreen) {
  auto sb = std::make_unique<StatusBar>();
  status_ = sb.get();
  addChild(std::move(sb));
  auto mb = std::make_unique<MenuBar>();
  menuBar_ = mb.get();
  addChild(std::move(mb));
  status_->addSegment("message", StatusBar::Align::Left);
  status_->setNoSolution(true);  // Visual Studio's purple bar without a solution
  opener_ = std::make_unique<WorkspaceOpener>(*this, [this](const std::string& m) { setMessage(m, kMessageMs); });
  WorkspaceOpener::Hooks oh;
  oh.open = hooks_.open;
  oh.dialog = hooks_.dialog;
  oh.recent = hooks_.recent;
  opener_->setHooks(std::move(oh));
  keyOpts_.userDir = userDir;
  if (!userDir.empty() && userDir == userConfigDir()) keyOpts_.import10xFile = default10xKeyMappingsPath();
  reloadKeys();
  buildMenus();
}

EmptyWindow::~EmptyWindow() = default;

void EmptyWindow::reloadKeys() {
  std::vector<KeyBindingDiag> diags;
  keys_ = loadKeyBindings(keyOpts_, diags);
}

void EmptyWindow::applySettings(const Settings& s) {
  menuBar_->setHidden(s.getBool("menu.hide"));
  menuShowOnHover_ = s.getBool("menu.showOnMouseHover");
  menuToggleOnAlt_ = s.getBool("menu.toggleOnAlt");
  if (s.getBool("keys.import10x") != keyOpts_.import10x) {
    keyOpts_.import10x = s.getBool("keys.import10x");
    reloadKeys();
  }
  requestLayout();
}

void EmptyWindow::onLayout() {
  const RectF& b = bounds();
  const float sh = status_->preferredHeight();
  // Like the Workbench: the bar reserves its height, except while hidden (menu.hide), when it floats over the top edge.
  const float mb = menuBar_->reservedHeight();
  const bool floating = menuBar_->hidden() && menuBar_->revealed();
  menuBar_->layout({b.x, b.y, b.w, floating ? menuBar_->barHeight() : mb});
  menuBar_->setTopLayer(floating);
  status_->layout({b.x, b.y + b.h - sh, b.w, sh});
}

void EmptyWindow::paint(PaintContext& ctx) {
  // The client area is empty, like a Workbench panel without a document.
  ctx.fill(bounds(), ctx.theme.panelBackground);
}

std::string EmptyWindow::shortcutFor(const std::string& command) const {
  for (const KeyBinding& b : keys_.bindings())
    if (b.command == command && b.context == "global" && b.arg.empty() && !b.chords.empty() && !b.chords[0].mouse)
      return formatChords(b.chords);
  for (const KeyBinding& b : keys_.bindings())
    if (b.command == command && b.arg.empty() && !b.chords.empty() && !b.chords[0].mouse) return formatChords(b.chords);
  return {};
}

void EmptyWindow::buildMenus() {
  MenuHooks h;
  h.info = [this](const std::string& written) {
    MenuCommandInfo info;
    const ResolvedCommand rc = resolveCommandName(written);
    if (rc.known10x && !rc.implemented) return info;
    info.available =
        true;  // every command of the editor window's menus is listed; those that need a workspace are greyed
    info.enabled = availableWithoutWorkspace(rc.id);
    info.name = rc.id;
    info.shortcut = shortcutFor(rc.id);
    return info;
  };
  h.run = [this](const std::string& cmd, const std::string&) { runCommand(cmd); };
  auto menu = [&h](const char* title, char access, std::vector<MenuSection> sections) {
    return MenuBar::Menu{title, access, [h, sections = std::move(sections)] { return buildMenuSections(sections, h); }};
  };
  menuBar_->setMenus({menu("File", 'F', fileMenuSections()), menu("Edit", 'E', editMenuSections()),
                      menu("View", 'V', viewMenuSections())});
}

bool EmptyWindow::runCommand(const std::string& id) {
  if (!availableWithoutWorkspace(id)) return false;
  opener_->closePicker();
  if (id == "File.OpenSolution") {
    opener_->openProjectOrSolutionDialog();
  } else if (id == "File.OpenFolder") {
    opener_->openFolderDialog();
  } else if (id == "File.RecentSolutions") {
    opener_->openRecentPicker();
  } else if (id == "App.Quit") {
    quit_ = true;  // nothing is modified: no prompt
  } else if (id == "View.ToggleFullscreen") {
    fullscreen_ = !fullscreen_;
    if (hooks_.setFullscreen) hooks_.setFullscreen(fullscreen_);
  }
  return true;
}

void EmptyWindow::setMessage(const std::string& text, int ttlMs) {
  message_ = text;
  status_->setText("message", text);
  ttlMs_ = ttlMs;
  expiry_ = 0;
  markDirty();
}

// A solution, project or folder opens as the workspace (the SLN-OPEN flow); a plain file opens its folder with the
// file in a tab (what `tenfold-ui file` does: there is no workspace to hold the document otherwise).
bool EmptyWindow::dropFile(const std::string& text) {
  if (text.empty()) return false;
  const fs::path p = pathFromUtf8(text);
  std::error_code ec;
  std::string ext = pathUtf8(p.extension());
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (fs::is_directory(p, ec) || isSolutionFile(p) || ext == ".vcxproj") {
    opener_->request(p);
    return true;
  }
  if (fs::is_regular_file(p, ec) && hooks_.openFile) {
    opener_->closePicker();
    hooks_.openFile(p);
    return true;
  }
  setMessage("Cannot open " + pathUtf8(p), kMessageMs);
  return true;
}

// Alt pressed and released alone focuses the bar, Alt+F / Alt+E / Alt+V open the menus (EditorApp::menuBarEvent).
bool EmptyWindow::menuBarEvent(const InputEvent& e) {
  MenuBar& mb = *menuBar_;
  if (!mb.hasMenus()) return false;
  if (e.type == InputType::KeyDown) {
    altCandidate_ =
        menuToggleOnAlt_ && e.key == Key::Unknown && (e.mods & ModAlt) && !(e.mods & (ModCtrl | ModShift | ModSuper));
  } else if (e.type == InputType::Text || e.type == InputType::MouseDown) {
    altCandidate_ = false;
  } else if (e.type == InputType::KeyUp && e.key == Key::Unknown && altCandidate_) {
    altCandidate_ = false;
    if (mb.active()) mb.leave();
    else if (!modal() || mb.dropdown()) {
      opener_->closePicker();
      mb.focus(0);
    }
    return true;
  }
  if (e.type == InputType::MouseMove && mb.hidden()) mb.reveal(menuShowOnHover_ && e.y >= 0 && e.y < mb.barHeight());
  if (e.type == InputType::MouseDown && mb.active() && !mb.dropdown() && mb.titleAt(e.x, e.y) < 0) mb.leave();
  if (e.type != InputType::KeyDown) return false;
  if (mb.active() && !mb.dropdown() && mb.handleKey(e)) return true;
  if (e.mods == ModAlt && e.key >= static_cast<Key>('A') && e.key <= static_cast<Key>('Z') &&
      mb.hasAccess(static_cast<char>(e.key)) && (!modal() || mb.dropdown())) {
    const KeyChord c = chordFromKey(e.key, e.mods);
    for (const KeyBinding& b : keys_.bindings())
      if (!b.chords.empty() && b.chords[0] == c) return false;
    opener_->closePicker();
    mb.openByAccess(static_cast<char>(e.key));
    return true;
  }
  return false;
}

bool EmptyWindow::handleEvent(const InputEvent& e, int64_t nowMs) {
  now_ = nowMs;
  if (ttlMs_ > 0) {
    expiry_ = nowMs + ttlMs_;
    ttlMs_ = 0;
  }
  if (e.type == InputType::DropFile) return dropFile(e.text);
  if (menuBarEvent(e)) return true;
  if (modal()) return dispatch(e);  // a menu or the recent list owns the keys
  if (e.type != InputType::KeyDown || e.key == Key::Unknown) return dispatch(e);
  const KeyMap::Result r = keys_.feed(chordFromKey(e.key, e.mods), "global", nowMs);
  switch (r.status) {
    case KeyMap::Status::Pending:
      setMessage(r.pendingText);
      pendingShown_ = true;
      return true;
    case KeyMap::Status::NoMatch:
      if (pendingShown_) setMessage("");
      pendingShown_ = false;
      return r.abortedChord || dispatch(e);
    case KeyMap::Status::Matched: break;
  }
  if (pendingShown_) setMessage("");
  pendingShown_ = false;
  if (runCommand(resolveCommandName(r.binding->command).id)) return true;
  return dispatch(e);  // bound to something that needs a workspace: nothing to do
}

int EmptyWindow::tick(int64_t nowMs) {
  now_ = nowMs;
  if (ttlMs_ > 0) {
    expiry_ = nowMs + ttlMs_;
    ttlMs_ = 0;
  }
  if (keys_.tick(nowMs) && pendingShown_) {
    pendingShown_ = false;
    setMessage("");
  }
  if (expiry_ && nowMs >= expiry_) setMessage("");
  int wait = RootWidget::tick(nowMs);
  auto soonest = [&wait](int ms) { wait = wait < 0 ? ms : std::min(wait, ms); };
  if (expiry_) soonest(static_cast<int>(std::max<int64_t>(1, expiry_ - nowMs)));
  if (keys_.pending()) soonest(100);
  return wait;
}

}  // namespace tf::ui
