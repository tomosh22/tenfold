#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/widgets/ListPopup.h"

namespace tf::ui {

class Workbench;

// Source lines for previews: from the open Document when there is one (unsaved edits included), else from disk
// (cached per file, at most kMaxFiles files and 4 MiB per file). UI thread only.
class LineSource {
 public:
  explicit LineSource(Editor& ed) : ed_(ed) {}
  // Lines [from, from + count) of `file` (clamped to the file); false if it cannot be read.
  bool lines(const std::filesystem::path& file, int from, int count, std::vector<std::string>& out, int& firstLine);
  std::string line(const std::filesystem::path& file, int n);  // empty if unreadable
  void clearCache() { cache_.clear(); }

 private:
  static constexpr size_t kMaxFiles = 32;
  Editor& ed_;
  std::map<std::string, std::vector<std::string>> cache_;
};

// What the pickers share; owned by EditorApp.
struct PickerEnv {
  RootWidget& root;
  Editor& editor;
  LineSource lines;
  std::function<int64_t()> clock;                   // monotonic ms (the app's event clock)
  std::function<void(const std::string&)> message;  // status-bar message (may be empty)
  // Saves the engine refused because the file's encoding cannot hold a character: the app shows SaveRefusedDialog
  // (the characters, why, go to the first one) and a status-bar message. May be empty (headless).
  std::function<void(std::vector<SaveRefusal>)> saveRefused;
  bool preview = true;         // setting picker.preview
  Workbench* bench = nullptr;  // for the preview tab of result lists (openResult); null without a Workbench
  PickerEnv(RootWidget& r, Editor& e) : root(r), editor(e), lines(e) {}
  void say(const std::string& m) const {
    if (message) message(m);
  }
  void refused(std::vector<SaveRefusal> r) const {
    if (!r.empty() && saveRefused) saveRefused(std::move(r));
  }
};

// Opens a result of a find / references list: `open` navigates (Editor::gotoLocation and friends). When the result was
// clicked with the mouse (`clicked`) and the settings allow it (tabs.previewTab, tabs.previewFromFindResults), a file
// that has no tab yet opens in the preview tab; Enter and F8 open normal tabs (VS_BEHAVIOUR T2).
void openResult(PickerEnv& env, bool clicked, const std::function<void()>& open);
// Workspace-relative generic path ("src/a.cpp"), or the absolute generic path when outside the root.
std::string relativePath(const std::filesystem::path& root, const std::filesystem::path& p);
// Kind icon (text glyph) and the theme token that colours it, for SymbolLocation::kind strings.
void kindIcon(const std::string& kind, std::string& icon, std::string& token);
// Leading/trailing blanks removed, tabs to spaces, cut at `max` bytes (UTF-8 safe).
std::string trimmedLine(std::string s, size_t max = 160);
// Row for a SymbolLocation-style result list: identity key independent of confidence.
std::string locationKey(const SymbolLocation& l);
// Header badge text for a result set: "verifying..." while an approximate answer awaits clangd, "approximate (why)"
// when no authoritative answer will follow, empty when authoritative (or the error text).
std::string confidenceBadge(Confidence c, const std::string& err);

}  // namespace tf::ui
