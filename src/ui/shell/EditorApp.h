#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/commands/CommandRegistry.h"
#include "ui/commands/KeyMap.h"
#include "ui/features/BookmarkList.h"
#include "ui/features/BuildPanel.h"
#include "ui/features/CompletionPopup.h"
#include "ui/features/DiagnosticsPanel.h"
#include "ui/features/FileSearch.h"
#include "ui/features/FindInFiles.h"
#include "ui/features/FindReplace.h"
#include "ui/features/GoToLine.h"
#include "ui/features/HoverTooltip.h"
#include "ui/features/NavResultPicker.h"
#include "ui/features/NavigationUi.h"
#include "ui/features/SignatureHelp.h"
#include "ui/features/SymbolSearch.h"
#include "ui/features/VcsUi.h"
#include "ui/settings/KeyBindings.h"
#include "ui/settings/Settings.h"
#include "ui/shell/Workbench.h"

namespace tf::ui {

class TextView;
class Settings;

// Glue between the window's input events and the command system (B4): maps KeyDown / thumb-button MouseDown to a
// KeyChord, picks the active context ("editor" when a TextView has focus, else "global"), runs KeyMap::feed and
// CommandRegistry::run, and reports through the status bar: the pending chord ("Ctrl+K..."), key-binding file
// diagnostics, "<command> is not available yet" for commands whose UI does not exist yet and failures. It also owns
// the pickers (B5a): symbol search (Search.SymbolSearch), open file (File.OpenFile) and the multi-result navigation
// list; while one is open the key context is "picker" and every key/mouse event reaches it first. B5b adds find in
// files (Search.FindInWorkspace, F8 / Shift+F8), the diagnostics list (View.ShowDiagnostics) and the non-modal popups
// over the editor: completion, hover and signature help. Those get the key first (Tab/Enter/Escape/arrows while the
// completion list is open, Escape for signature help), are told about typed text and see every other event afterwards
// (they close themselves when the caret leaves); no picker is open while they are. Everything else (text input,
// mouse, keys without a binding) goes to the widget tree unchanged. With a Workbench the Build commands (C1-UI:
// Build.Build [custom command], Build.Cancel, Build.NextError / PrevError, View.ToggleBuildPanel) drive a BuildPanel
// in the bottom dock and the status bar "build" segment shows the last result. View.ToggleWordWrap flips soft wrap in
// every view for the session; the caret movement commands offer the focused view the chance to move by visual row.
// C3c: the Vcs.* commands (VcsUi) are bound here; Escape first closes the inline diff blocks of the focused view, a
// regained window focus re-reads the version-control status, and VcsManager::onError messages reach the status bar.
//
// `bench` may be null (any other RootWidget with TextViews, e.g. ShellDemo or a test); then the workbench commands
// stay unbound and messages are only available through message(). Bindings come from loadKeyBindings(workspace root,
// userDir). The caller owns time: pass a monotonic millisecond clock to handleEvent and tick.
class EditorApp {
 public:
  EditorApp(RootWidget& root, Editor& editor, Workbench* bench = nullptr, const std::filesystem::path& userDir = {});
  ~EditorApp();
  EditorApp(const EditorApp&) = delete;
  EditorApp& operator=(const EditorApp&) = delete;

  // Reads picker.preview (and later picker settings); call again on Settings changes.
  void applySettings(const Settings& s);
  SymbolSearch& symbolSearch() { return *symbols_; }
  FileSearch* fileSearch() { return files_.get(); }  // null without a Workbench
  NavResultPicker& navResults() { return *navResults_; }
  FindInFiles& findInFiles() { return *find_; }
  DiagnosticsPanel& diagnosticsPanel() { return *diagPanel_; }
  // B8: find / replace in the current file (Ctrl+F / Ctrl+H), go to line (Ctrl+G), bookmark list.
  FindReplace& findReplace() { return *findReplace_; }
  GoToLine& goToLine() { return *goToLine_; }
  BookmarkList& bookmarkList() { return *bookmarkList_; }
  CompletionPopup& completion() { return *completion_; }
  HoverTooltip& hover() { return *hover_; }
  SignatureHelp& signatureHelp() { return *sigHelp_; }
  BuildPanel* buildPanel() { return build_; }  // the "Build" bottom-dock panel; null without a Workbench
  VcsUi& vcsUi() { return *vcsUi_; }           // C3c: Vcs.* commands, revert confirmation, external diff tool
  void closePickers();

  // Settings and themes (B7): the editor.* settings reach every TextView in the tree (new ones on the next tick), a
  // theme change is applied to the root (scaled by `displayScale`), and settings / theme diagnostics go to the status
  // bar message like key-binding ones. `rebuildFont` (optional) is called when a font.* key changed; it swaps the
  // TextRenderer (RootWidget::setTextRenderer) and returns false when no font could be built. The app loop calls
  // `SettingsService::poll()` itself (on the wake the service posts).
  void attachSettings(SettingsService& svc, float displayScale = 1.0f, std::function<bool()> rebuildFont = {});

  // Runs a command by id like a key binding would (the active key context, status-bar reporting of failures).
  void runCommand(const std::string& id, const std::string& arg = {}) { runCommandId(id, arg, activeContext()); }

  // Returns true if the event was consumed (a binding, a swallowed chord stroke, or a widget).
  bool handleEvent(const InputEvent& e, int64_t nowMs);
  // Call after each event batch and when waiting times out: Workbench::sync, key-chord/message expiry, the root's
  // tick. Returns the ms until the next tick is wanted (-1 = none).
  int tick(int64_t nowMs);

  bool quitRequested() const { return quit_; }
  const std::string& message() const { return message_; }  // what the status bar "message" segment shows
  CommandRegistry& commands() { return registry_; }
  KeyMap& keys() { return keys_; }
  const std::vector<KeyBindingDiag>& diagnostics() const { return diags_; }
  // B6: back/forward toolbar + dropdown (null without a Workbench). The status-bar "← 3 | 1 →" segment follows the
  // nav.statusCounts setting; until settings are wired into the app, whoever owns the Settings calls this (default
  // off).
  NavigationUi* navigationUi() { return nav_.get(); }
  void setNavStatusCounts(bool on);

 private:
  void setMessage(const std::string& text, int ttlMs = 0, bool pending = false);
  void showDiagnostics();
  void applyEditorSettings();
  void applyEditingSettings(const Settings& s);
  void forEachView(const std::function<void(TextView&)>& fn);
  void bindUiCommands();
  bool handleEventImpl(const InputEvent& e, int64_t nowMs);
  void runCommandId(const std::string& id, const std::string& arg, const char* context);
  std::string shortcutFor(const std::string& command) const;
  const char* activeContext() const;
  void showNavResults(uint64_t token, const std::string& title, size_t minResults, Confidence c,
                      const std::vector<SymbolLocation>& locs, const std::string& err);

  RootWidget& root_;
  Editor& editor_;
  Workbench* bench_;
  CommandRegistry registry_;
  KeyMap keys_;
  std::vector<KeyBindingDiag> diags_;
  std::string message_;
  int ttlMs_ = 0;       // set by setMessage, converted to expiry_ on the next call that knows the time
  int64_t expiry_ = 0;  // 0 = never
  std::unique_ptr<NavigationUi> nav_;
  bool pendingShown_ = false, cycling_ = false, quit_ = false;
  std::optional<bool> wrapOverride_;  // View.ToggleWordWrap: wins over editor.wordWrap until that setting changes
  SettingsService* settings_ = nullptr;
  float scale_ = 1.0f;
  int settingsGen_ = 1, settingsToken_ = 0;
  std::function<bool()> rebuildFont_;
  int64_t now_ = 0;
  uint64_t navToken_ = 0;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);  // callbacks of in-flight engine requests check it
  PickerEnv pickerEnv_;
  std::unique_ptr<SymbolSearch> symbols_;
  std::unique_ptr<FileSearch> files_;
  std::unique_ptr<NavResultPicker> navResults_;
  std::unique_ptr<FindInFiles> find_;
  std::unique_ptr<DiagnosticsPanel> diagPanel_;
  std::unique_ptr<FindReplace> findReplace_;
  std::unique_ptr<GoToLine> goToLine_;
  std::unique_ptr<BookmarkList> bookmarkList_;
  std::unique_ptr<CompletionPopup> completion_;
  std::unique_ptr<HoverTooltip> hover_;
  std::unique_ptr<SignatureHelp> sigHelp_;
  std::unique_ptr<VcsUi> vcsUi_;
  BuildPanel* build_ = nullptr;  // owned by the Workbench's bottom dock
};

}  // namespace tf::ui
