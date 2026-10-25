#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Workspace symbol picker (Ctrl+, by default). Typing issues Editor::findSymbols (debounced: the first keystroke after
// a pause goes out at once, later ones at most every debounceMs), answers are matched to requests by sequence number
// (a response for an older query is dropped). An Approximate answer is shown greyed with a "verifying..." badge and
// is replaced when the Authoritative one for the same query arrives; the selected symbol stays selected if it is
// still in the new list. The right-hand pane previews the target line (setting picker.preview, OPEN_QUESTIONS #6).
// Enter -> Editor::gotoLocation (navigation history); the Workbench reveals the tab on its next sync().
class SymbolSearch {
 public:
  using SearchFn = std::function<void(const std::string& query, Editor::NavResult)>;
  explicit SymbolSearch(PickerEnv& env);
  ~SymbolSearch();
  SymbolSearch(const SymbolSearch&) = delete;
  SymbolSearch& operator=(const SymbolSearch&) = delete;

  // Default: Editor::findSymbols, issued from a worker thread so that the (synchronous) fast-tier scan of a huge
  // index never blocks the UI thread; results still come back through Editor::post. A newer query replaces one
  // that has not started yet. An injected function runs on the calling (UI) thread instead (tests).
  void setSearchFn(SearchFn fn) { search_ = std::move(fn); }
  void setDebounceMs(int ms) { debounceMs_ = ms; }
  void open();
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  // Fires a debounced query that is due; returns ms until the next one is due (-1 = nothing pending).
  int tick(int64_t nowMs);

  int queriesIssued() const { return issued_; }

 private:
  void queryChanged();
  void fire();
  void onResult(uint64_t seq, Confidence c, const std::vector<SymbolLocation>& locs, const std::string& err);
  void updatePreview();
  void submit(const std::string& query, Editor::NavResult cb);
  void workerLoop();

  PickerEnv& env_;
  SearchFn search_;
  ListPopup* popup_ = nullptr;
  std::vector<SymbolLocation> locs_;  // aligned with the popup rows
  uint64_t seq_ = 0, shownSeq_ = 0;
  int issued_ = 0, debounceMs_ = 25;
  bool pending_ = false;
  int64_t lastFire_ = -1000000;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);  // callbacks in flight check it before touching `this`
  std::thread worker_;
  std::mutex mu_;
  std::condition_variable cv_;
  bool stop_ = false, hasJob_ = false;
  std::string jobQuery_;
  Editor::NavResult jobCb_;
};

}  // namespace tf::ui
