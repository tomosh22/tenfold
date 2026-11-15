#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "app/Editor.h"
#include "ui/gfx/Renderer.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/FileDialog.h"
#include "ui/settings/RecentSolutions.h"
#include "ui/settings/Settings.h"
#include "ui/shell/EditorApp.h"
#include "ui/shell/EmptyWindow.h"
#include "ui/shell/Workbench.h"

namespace tf::ui {

// Everything that belongs to ONE open workspace in the editor window, and the machinery to replace it with another one
// in the same window (SLN-OPEN, docs/VS_BEHAVIOUR.md section 16: Visual Studio closes the open solution and opens the
// new one in the same window). tenfold-ui creates one AppHost; tests create their own with fakes.
//
// A workspace is: the SettingsService (user settings + <root>/.tenfold/settings.txt, theme), the text renderer built
// from those settings, the Editor (documents, clangd, file watcher, version control, debugger session, build runner),
// the Workbench (tabs, panels, layout.json) and the EditorApp (commands, key bindings, pickers, terminals, explorer).
// Switching saves the old workspace's layout.json and bookmarks / breakpoints, shuts the Editor down (clangd stops,
// a running build is cancelled, terminals end with the EditorApp), destroys all four in reverse order and creates the
// new ones from the new root. Nothing is shared between workspaces except the window, the renderer and the recent
// list. The user config is read again (the new workspace may override settings and key bindings).
//
// CLOSE-SLN (docs/VS_BEHAVIOUR.md section 18): the window can also have NO workspace. `openEmpty()` (tenfold-ui without
// arguments, File > Close Solution) keeps only the settings service and the font and puts an EmptyWindow in the
// Workbench's place: no Editor exists, so no clangd, scan, watcher, build, debugger session or version control, and
// nothing writes into a workspace folder. Closing runs the same teardown as a switch (layout.json, bookmarks and
// breakpoints of the closing workspace are saved first); opening from the empty window is the ordinary open().
//
// The switch is requested from inside event handling (WorkspaceOpener hooks), which runs in code owned by the object
// being replaced, so it only records the request: the program calls applyPending() between two events.
class AppHost {
 public:
  struct Options {
    std::filesystem::path userDir;  // user config dir: settings.txt, KeyMappings, theme, recent.txt (empty: none)
    float pixelScale = 1.0f;
    std::function<void()> wake;                // the settings watcher asks the UI thread to poll()
    gfx::Renderer* renderer = nullptr;         // where replaced text renderers release their atlas textures
    Editor::Options editor;                    // per-workspace copy; `solution` is filled in from the target
    bool startEngine = true;                   // Editor::start (clangd, scan, watcher); false for screenshots
    bool watchSettings = true;                 // SettingsService::startWatching
    std::string defaultFile = "src/main.cpp";  // opened when the workspace has no saved layout and no file was given
    std::function<void(Editor&)> setupEditor;  // e.g. the Visual Studio debugger factory
    std::function<void(EditorApp&)> setupApp;  // e.g. the window hooks (fullscreen)
    std::function<void(bool on)> setFullscreen;  // the window's full screen (View.ToggleFullscreen in the empty window)
    // Builds the editor font from the settings at `zoomPt` extra points (default: makeEditorTextRenderer).
    std::function<std::unique_ptr<gfx::TextRenderer>(const Settings&, float zoomPt, std::string* err)> makeText;
    FileDialogBackend dialog;                        // File > Open dialogs (null: the dialog commands are unavailable)
    std::function<void(const std::string&)> report;  // start-up problems (default: stderr)
  };
  explicit AppHost(Options o);
  ~AppHost();
  AppHost(const AppHost&) = delete;
  AppHost& operator=(const AppHost&) = delete;

  // Opens `target` (a folder, a .sln / .slnx, or any file whose folder becomes the workspace; see
  // resolveWorkspaceTarget) as the first or the replacing workspace, with NO save prompt: the caller asked already.
  // `file`: optional document to open, relative to the workspace root (else to the current folder). `remember`: put
  // `target` on the recent list. False (`err`) when the new workspace could not be built; the previous one stays.
  bool open(const std::filesystem::path& target, const std::filesystem::path& file = {}, bool remember = true,
            std::string* err = nullptr);
  // Shows the empty window (no solution or folder): the first state of `tenfold-ui` without arguments, or what is left
  // after closing the workspace (saved first, no prompt: the caller asked already). False (`err`) when the settings or
  // the font cannot be built; the previous state stays.
  bool openEmpty(std::string* err = nullptr);
  // Called by the WorkspaceOpener hooks: replace the workspace at the next applyPending(). `file`: a document to open
  // in it (a plain file dropped on the empty window).
  void requestOpen(const std::filesystem::path& target, const std::filesystem::path& file = {});
  // File > Close Solution after the save prompt: show the empty window at the next applyPending().
  void requestClose();
  bool hasPending() const { return pending_.has_value() || closePending_; }
  // Performs the requested switch. True when the workspace was replaced (everything must be redrawn).
  bool applyPending(std::string* err = nullptr);
  // Saves layout.json and shuts the Editor down (program exit). Idempotent.
  void shutdown();

  bool hasWorkspace() const { return app_ != nullptr; }
  bool isEmpty() const { return empty_ != nullptr; }  // the empty window is showing
  EmptyWindow* empty() { return empty_.get(); }
  Editor* editor() { return editor_.get(); }
  Workbench* bench() { return bench_.get(); }
  EditorApp* app() { return app_.get(); }
  SettingsService* settings() { return settings_.get(); }
  RootWidget* root() { return bench_ ? static_cast<RootWidget*>(bench_.get()) : empty_.get(); }
  gfx::TextRenderer* text() { return text_.get(); }
  RecentSolutions& recent() { return recent_; }
  bool layoutRestored() const { return layoutRestored_; }
  const WorkspaceTarget& target() const { return target_; }  // what the open workspace was opened from
  int generation() const { return generation_; }             // +1 per workspace built (tests, redraw)

 private:
  bool buildEmpty(std::string* err);
  void attachEmptySettings();
  bool build(const WorkspaceTarget& t, const std::filesystem::path& file, std::string* err);
  void teardown();
  bool rebuildFont();
  void say(const std::string& s) const;

  Options o_;
  RecentSolutions recent_;
  std::unique_ptr<SettingsService> settings_;
  std::unique_ptr<gfx::TextRenderer> text_;
  std::unique_ptr<gfx::TextRenderer> editorText_;  // the zoomed editor font; null while not zoomed
  std::unique_ptr<Editor> editor_;
  std::unique_ptr<Workbench> bench_;
  std::unique_ptr<EditorApp> app_;
  std::unique_ptr<EmptyWindow> empty_;  // instead of editor_ / bench_ / app_ while no workspace is open
  WorkspaceTarget target_;
  std::optional<std::filesystem::path> pending_;
  std::filesystem::path pendingFile_;
  bool closePending_ = false;
  bool layoutRestored_ = false, down_ = false;
  int generation_ = 0;
};

}  // namespace tf::ui
