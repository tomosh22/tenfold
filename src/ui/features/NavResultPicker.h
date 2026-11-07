#pragma once
#include <optional>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Result list for the semantic navigation commands (GotoDefinition, FindReferences, ...) when the answer has several
// locations: rows "file:line" + the source line, greyed with a "verifying..." badge while the answer is Approximate,
// swapped (selection kept by location) when the Authoritative one arrives. Enter -> Editor::gotoLocation.
// Chooser (W10-CHOOSE): when the targets carry a SymbolLocation::label ("class Widget", "constructor Widget(int)",
// "using-declaration", "typedef Bar -> Foo") the rows show label, code line and file:line, the popup sits at the
// caret (setAnchor) and selects the `preferred` target; Enter jumps (history), Escape closes.
// References (VS-NAV, VS_BEHAVIOUR N5): when the locations carry a SymbolLocation::role the rows are grouped under
// headings (Definition, then one per file), show Line:Col, the source line and Read / Write / Definition / Other.
class NavResultPicker {
 public:
  explicit NavResultPicker(PickerEnv& env) : env_(env) {}

  // Shows or updates the picker for one command run. `title` names the command ("Go to Definition"). Fewer than
  // `minResults` locations closes an open picker instead (the Editor jumped by itself, or there is nothing to pick).
  void show(const std::string& title, Confidence c, const std::vector<SymbolLocation>& locs, const std::string& err,
            size_t minResults);
  // Where the next chooser appears (the caret or the mouse position); without one the popup is centred.
  void setAnchor(RectF r) { anchor_ = r; }
  void clearAnchor() { anchor_.reset(); }
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }

 private:
  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  std::vector<SymbolLocation> locs_;
  std::vector<int> rowLoc_;  // popup row -> index into locs_ (-1: a group heading)
  std::optional<RectF> anchor_;
};

}  // namespace tf::ui
