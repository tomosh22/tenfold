#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Find function (10x FindFunction, Alt+M, W8c): a modal picker of the functions DEFINED in the current file (document
// symbols that are functions, methods, constructors or operators with a body, in source order). Typing filters by
// fuzzy match on the qualified name. The fast index answers first (greyed, "verifying...") and clangd's authoritative
// list replaces it with the selection kept (constraint 1.4: the badge always says which tier is shown). Enter goes
// through Editor::gotoLocation, so the jump lands in the navigation history.
class FunctionPicker {
 public:
  using SymbolsFn = std::function<void(Editor::NavResult)>;
  explicit FunctionPicker(PickerEnv& env) : env_(env) {}
  FunctionPicker(const FunctionPicker&) = delete;
  FunctionPicker& operator=(const FunctionPicker&) = delete;

  void setSymbolsFn(SymbolsFn fn) { symbols_ = std::move(fn); }  // default: Editor::documentSymbols
  bool open();                                                   // false (with a message) without a document
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }

 private:
  void onResult(uint64_t seq, Confidence c, const std::vector<SymbolLocation>& locs, const std::string& err);
  void refilter();
  void updatePreview();

  PickerEnv& env_;
  SymbolsFn symbols_;
  ListPopup* popup_ = nullptr;
  std::vector<SymbolLocation> all_;    // the defined functions of the last answer, source order
  std::vector<SymbolLocation> shown_;  // aligned with the popup rows
  Confidence confidence_ = Confidence::Approximate;
  std::string error_;
  uint64_t seq_ = 0;
  bool answered_ = false;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
