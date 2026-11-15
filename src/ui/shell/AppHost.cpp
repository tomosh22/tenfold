#include "ui/shell/AppHost.h"

#include <cstdio>
#include <set>

#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "core/PathUtil.h"
#include "ui/shell/FontSetup.h"
#include "workspace/Workspace.h"

namespace tf::ui {

namespace fs = std::filesystem;

AppHost::AppHost(Options o)
    : o_(std::move(o)), recent_(o_.userDir.empty() ? fs::path() : defaultRecentFile(o_.userDir)) {
  if (!o_.makeText)
    o_.makeText = [scale = o_.pixelScale](const Settings& s, float zoomPt, std::string* err) {
      return makeEditorTextRenderer(s, scale, err, zoomPt);
    };
  recent_.load();
}

AppHost::~AppHost() { shutdown(); }

void AppHost::say(const std::string& s) const {
  if (o_.report) o_.report(s);
  else std::fprintf(stderr, "tenfold-ui: %s\n", s.c_str());
}

void AppHost::requestOpen(const fs::path& target, const fs::path& file) {
  pending_ = target;
  pendingFile_ = file;
  closePending_ = false;
}

void AppHost::requestClose() {
  closePending_ = true;
  pending_.reset();
}

bool AppHost::applyPending(std::string* err) {
  std::string e;
  if (closePending_) {
    closePending_ = false;
    if (openEmpty(&e)) return true;
    if (err) *err = e;
    if (app_) app_->notify("Cannot close the solution: " + e);
    return false;
  }
  if (!pending_) return false;
  const fs::path target = *pending_, file = pendingFile_;
  pending_.reset();
  pendingFile_.clear();
  if (open(target, file, true, &e)) return true;
  if (err) *err = e;
  const std::string msg = "Cannot open " + pathUtf8(target) + ": " + e;
  if (app_) app_->notify(msg);
  else if (empty_) empty_->status().setText("message", msg);
  return false;
}

bool AppHost::open(const fs::path& arg, const fs::path& file, bool remember, std::string* err) {
  const WorkspaceTarget t = resolveWorkspaceTarget(arg);
  if (!build(t, file, err)) return false;
  if (remember) {
    recent_.add(arg);
    recent_.save();
  }
  return true;
}

void AppHost::teardown() {
  empty_.reset();                    // before the text renderer it draws with
  if (bench_) bench_->saveLayout();  // the layout of the workspace being left; the new one restores its own
  if (editor_) editor_->shutdown();
  app_.reset();
  bench_.reset();
  editor_.reset();
  if (o_.renderer) {
    if (text_) text_->releaseTexture(*o_.renderer);
    if (editorText_) editorText_->releaseTexture(*o_.renderer);
  }
  editorText_.reset();
  text_.reset();
  settings_.reset();
#ifdef __GLIBC__
  // The closed workspace's fast index, documents and caches are freed, but glibc keeps the freed pages in the thread
  // arenas of the indexing threads: after a 20k-file workspace the process held 170 to 200 MiB it did not use (W9i,
  // 18 to 50 MiB after this call). Give them back before the next workspace is built.
  ::malloc_trim(0);
#endif
}

void AppHost::shutdown() {
  if (down_) return;
  down_ = true;
  if (bench_) bench_->saveLayout();
  if (editor_) editor_->shutdown();
}

bool AppHost::rebuildFont() {
  // A changed font.* setting rebuilds the TextRenderer; the old one lives until the root points at the new one.
  auto next = o_.makeText(settings_->settings(), 0, nullptr);
  if (!next) return false;
  if (empty_) {  // the empty window has one font and no zoom
    empty_->setTextRenderer(*next);
    if (o_.renderer) text_->releaseTexture(*o_.renderer);
    text_ = std::move(next);
    return true;
  }
  std::unique_ptr<gfx::TextRenderer> nextEditor;
  if (app_->fontZoomPt() != 0) {  // View.ZoomIn / ZoomOut: a second renderer at the zoomed size (VS_BEHAVIOUR Z8)
    nextEditor = o_.makeText(settings_->settings(), app_->fontZoomPt(), nullptr);
    if (!nextEditor) return false;
  }
  bench_->setTextRenderer(*next);
  bench_->setEditorTextRenderer(nextEditor.get());
  if (o_.renderer) {
    text_->releaseTexture(*o_.renderer);
    if (editorText_) editorText_->releaseTexture(*o_.renderer);
  }
  text_ = std::move(next);
  editorText_ = std::move(nextEditor);
  return true;
}

bool AppHost::openEmpty(std::string* err) { return buildEmpty(err); }

// Like build(): what can fail first (settings and font without a workspace layer), then the old state is left.
bool AppHost::buildEmpty(std::string* err) {
  SettingsService::Options so;
  so.userDir = o_.userDir;  // no workspace root: only the user's settings, key bindings and theme apply
  so.wake = o_.wake;
  auto settings = std::make_unique<SettingsService>(so);
  std::string fontErr;
  auto text = o_.makeText(settings->settings(), 0, &fontErr);
  if (!text) {
    if (err) *err = fontErr;
    return false;
  }
  const bool wasFullscreen = app_ ? app_->fullscreen() : (empty_ && empty_->fullscreen());
  teardown();  // saves layout.json, bookmarks and breakpoints of the workspace being left
  settings_ = std::move(settings);
  text_ = std::move(text);
  target_ = {};
  layoutRestored_ = false;
  ++generation_;
  EmptyWindow::Hooks h;
  h.open = [this](const fs::path& p) { requestOpen(p); };
  h.openFile = [this](const fs::path& f) { requestOpen(f.parent_path(), f.filename()); };
  h.dialog = o_.dialog;
  h.recent = &recent_;
  h.setFullscreen = o_.setFullscreen;
  empty_ = std::make_unique<EmptyWindow>(*text_, settings_->theme().scaled(o_.pixelScale), o_.userDir, std::move(h),
                                         wasFullscreen);
  attachEmptySettings();
  if (o_.startEngine && o_.watchSettings) settings_->startWatching();
  return true;
}

void AppHost::attachEmptySettings() {
  empty_->applySettings(settings_->settings());
  settings_->onThemeChanged([this](const Theme&) { settings_->applyTo(*empty_, o_.pixelScale); });
  settings_->settings().onChanged([this](const std::set<std::string>& keys) {
    for (const std::string& k : keys)
      if (k.rfind("font.", 0) == 0) {
        rebuildFont();
        break;
      }
    empty_->applySettings(settings_->settings());
  });
}

bool AppHost::build(const WorkspaceTarget& t, const fs::path& file, std::string* err) {
  // 1. What can fail comes first, so that a failure leaves the open workspace untouched.
  SettingsService::Options so;
  so.workspaceRoot = t.root;
  so.userDir = o_.userDir;
  so.wake = o_.wake;
  auto settings = std::make_unique<SettingsService>(so);
  std::string fontErr;
  auto text = o_.makeText(settings->settings(), 0, &fontErr);
  if (!text) {
    if (err) *err = fontErr;
    return false;
  }
  // 2. Leave the old workspace (fullscreen is the window's, not the workspace's: it carries over).
  const bool wasFullscreen = app_ ? app_->fullscreen() : (empty_ && empty_->fullscreen());
  teardown();
  settings_ = std::move(settings);
  text_ = std::move(text);
  target_ = t;
  ++generation_;
  // 3. The new one.
  Editor::Options eo = o_.editor;
  eo.solution = t.solution;
  editor_ = std::make_unique<Editor>(t.root, std::move(eo));
  if (o_.setupEditor) o_.setupEditor(*editor_);
  const Theme theme = settings_->theme().scaled(o_.pixelScale);
  bench_ = std::make_unique<Workbench>(*text_, *editor_, theme);
  layoutRestored_ = bench_->restoreLayout();
  if (!file.empty()) {  // relative to the workspace root, else to the current folder
    fs::path path = fs::absolute(t.root / file);
    std::error_code ec;
    if (!fs::exists(path, ec) && fs::exists(file, ec)) path = fs::absolute(file);
    if (bench_->openFile(path) < 0) say("cannot open " + pathUtf8(path));
  } else if (!layoutRestored_ && !o_.defaultFile.empty()) {
    bench_->openFile(o_.defaultFile);  // first run: don't start blank (no-op if absent)
  }
  bench_->sync();
  app_ = std::make_unique<EditorApp>(*bench_, *editor_, bench_.get(), o_.userDir);
  {
    WorkspaceOpener::Hooks h;
    h.open = [this](const fs::path& p) { requestOpen(p); };
    h.dialog = o_.dialog;
    h.recent = &recent_;
    h.close = [this] { requestClose(); };
    h.confirmUnsaved = [a = app_.get()] { return a->confirmUnsaved(); };
    app_->workspaceOpener().setHooks(std::move(h));
  }
  if (o_.startEngine) {
    std::string startErr;
    if (!editor_->start(&startErr)) say("editor start: " + startErr);
  }
  app_->attachSettings(*settings_, o_.pixelScale, [this] { return rebuildFont(); });
  if (o_.setupApp) o_.setupApp(*app_);
  if (wasFullscreen) app_->runCommand("View.ToggleFullscreen");
  if (!layoutRestored_ && o_.startEngine && app_->explorer()) app_->explorer()->openByDefault();  // VS-TREE X8
  if (o_.startEngine && o_.watchSettings) settings_->startWatching();
  return true;
}

}  // namespace tf::ui
