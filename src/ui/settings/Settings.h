#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "ui/settings/KeyBindings.h"  // KeyBindingDiag / formatDiag / userConfigDir are shared
#include "ui/widgets/Theme.h"

namespace tf {
class FileWatcher;
}

// Settings (B7). Text files of `key = value` lines (dotted keys, no [sections]); `#` starts a comment only as the
// first non-blank character (colours contain '#'). Types: bool (true/false), int, float, string (optional "quotes"),
// colour (#rrggbb / #rrggbbaa), font ("family-or-path[, size_pt]", empty = unset). Unknown keys, type errors and
// malformed lines are collected as diagnostics with file:line and skipped; nothing here throws or is fatal.
// Layers, later wins: embedded assets/settings.default.txt, `<userConfigDir>/settings.txt`,
// `<workspace>/.tenfold/settings.txt`. Undo grouping lives in .tenfold/workspace.json (A5), not here.
namespace tf::ui {

using SettingsDiag = KeyBindingDiag;

enum class SettingType { Bool, Int, Float, String, Color, Font };
struct FontSpec {
  std::string family;  // family name or file path; empty = unset
  float sizePt = 0;    // 0 = unset
  bool operator==(const FontSpec& o) const { return family == o.family && sizePt == o.sizePt; }
};
struct SettingValue {
  SettingType type = SettingType::String;
  bool b = false;
  long long i = 0;
  double f = 0;
  std::string s;
  gfx::Color c{0, 0, 0, 1};
  FontSpec font;
  bool operator==(const SettingValue& o) const;
};
struct SettingInfo {
  const char* key;
  SettingType type;
};
const std::vector<SettingInfo>& settingsSchema();  // every known key and its type

// Value parsers (shared with the theme loader). False + `error` on failure.
bool parseColor(std::string_view text, gfx::Color& out, std::string& error);
bool parseSettingValue(SettingType type, std::string_view text, SettingValue& out, std::string& error);
// The line grammar shared by settings and themes: calls `fn(line, key, value)` for each well-formed `key = value`
// line; malformed lines (no '=', bad key, [section]) become diagnostics.
void parseKeyValueLines(std::string_view text, std::string_view source, std::vector<SettingsDiag>& diags,
                        const std::function<void(int line, std::string_view key, std::string_view value)>& fn);
// Parses one layer into `into` (later duplicates win; a duplicate inside one text is a warning).
void parseSettings(std::string_view text, std::string_view source, std::map<std::string, SettingValue>& into,
                   std::vector<SettingsDiag>& diags);
std::string_view defaultSettingsText();  // embedded assets/settings.default.txt

class Settings {
 public:
  using ChangedFn = std::function<void(const std::set<std::string>& changedKeys)>;
  Settings();  // embedded defaults only

  // Rebuilds defaults -> user -> workspace (empty paths skipped, missing files silent) and notifies listeners with
  // exactly the keys whose value changed. Diagnostics of this load replace the previous ones.
  void load(const std::filesystem::path& workspaceRoot, const std::filesystem::path& userDir);
  // Same, from texts (tests, embedding).
  void loadTexts(std::string_view userText, std::string_view workspaceText);

  bool getBool(const std::string& key) const { return get(key).b; }
  int getInt(const std::string& key) const { return static_cast<int>(get(key).i); }
  double getFloat(const std::string& key) const { return get(key).f; }
  const std::string& getString(const std::string& key) const { return get(key).s; }
  gfx::Color getColor(const std::string& key) const { return get(key).c; }
  const FontSpec& getFont(const std::string& key) const { return get(key).font; }
  const std::vector<SettingsDiag>& diagnostics() const { return diags_; }

  int onChanged(ChangedFn fn);  // returns a token for removeOnChanged
  void removeOnChanged(int token);

 private:
  const SettingValue& get(const std::string& key) const;
  void apply(std::map<std::string, SettingValue> next, std::vector<SettingsDiag> diags);
  std::map<std::string, SettingValue> values_;
  std::vector<SettingsDiag> diags_;
  std::vector<std::pair<int, ChangedFn>> listeners_;
  int nextToken_ = 1;
};

// The editor-facing subset, read in one go (TextView/DocumentView call this on construction and on change).
struct EditorSettings {
  bool smoothScroll, wordWrap, caretBlink, highlightCurrentLine, showWhitespace;
  int caretBlinkMs, wheelLines, tabWidth, columnGuide;
};
EditorSettings editorSettings(const Settings& s);

class RootWidget;
// Loads and hot-reloads settings and the active theme. Threading: the file watcher thread only sets a flag (and calls
// `wake`, which must be thread-safe, e.g. post an SDL user event); `poll()` does the reload and fires all callbacks on
// the calling (UI) thread.
class SettingsService {
 public:
  struct Options {
    std::filesystem::path workspaceRoot, userDir;  // either may be empty
    int debounceMs = 200, pollMs = 2000;
    bool forcePolling = false;
    std::function<void()> wake;
  };
  explicit SettingsService(Options opt);
  ~SettingsService();
  SettingsService(const SettingsService&) = delete;
  SettingsService& operator=(const SettingsService&) = delete;

  Settings& settings() { return settings_; }
  const Theme& theme() const { return theme_; }
  std::vector<SettingsDiag> diagnostics() const;  // settings + theme

  // Starts watching `<workspace>/.tenfold` (only if it exists) and the user config dir (created if missing).
  void startWatching();
  void stopWatching();
  // Call from the file watcher the app already has, if it covers .tenfold; same effect as a watcher event.
  void notifyFileChanged(const std::filesystem::path& p);
  // Reloads if something changed since the last call. True when a reload ran.
  bool poll();
  void reloadNow();

  // Called (on the poll thread) with the new theme after `ui.theme` or the theme file changed.
  void onThemeChanged(std::function<void(const Theme&)> fn) { themeFn_ = std::move(fn); }
  // Applies the current theme (scaled by the display scale) to the root widget, which repaints.
  void applyTo(RootWidget& root, float displayScale = 1.0f) const;

 private:
  void loadTheme();
  Options opt_;
  Settings settings_;
  Theme theme_;
  std::string themeFingerprint_;
  std::vector<SettingsDiag> themeDiags_;
  std::function<void(const Theme&)> themeFn_;
  std::atomic<bool> pending_{false};
  std::vector<std::unique_ptr<::tf::FileWatcher>> watchers_;
};

}  // namespace tf::ui
