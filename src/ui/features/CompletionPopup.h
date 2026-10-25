#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"
#include "ui/features/PopupBox.h"

namespace tf::ui {

// Autocomplete (B5b). A non-modal PopupBox under the start of the word being typed; the keyboard stays with the
// TextView, EditorApp offers it the key first (handleKey: Up/Down/PageUp/PageDown select, Tab/Enter accept, Escape
// closes) and tells it about typed text (onTyped) and every other event (update).
//
// Triggers: an identifier character that makes the word at least 2 characters long, and `.`, `->`, `::`. The request
// goes to the completion backend (Editor::complete: clangd, the fast index when clangd is absent: those items are
// greyed and the side pane says "approximate", constraint 1.4); the unfiltered answer is kept and filtered on the
// client as the word grows or shrinks (fuzzy, prefix matches first, characters highlighted). Answers carry a sequence
// number and are dropped when a newer request was made, the popup was closed, or the caret left the word. Accept
// replaces the typed prefix with the item's insert text as ONE undo step. With several carets it is applied at every
// caret only when all of them are empty and share the same prefix, else at the primary caret only.
class CompletionPopup {
 public:
  using Done = std::function<void(const std::vector<LspCompletionItem>&, const std::string& error)>;
  using CompleteFn = std::function<void(Done)>;

  explicit CompletionPopup(PickerEnv& env);
  ~CompletionPopup();
  CompletionPopup(const CompletionPopup&) = delete;
  CompletionPopup& operator=(const CompletionPopup&) = delete;

  void setCompleteFn(CompleteFn fn) { complete_ = std::move(fn); }              // default: Editor::complete
  void setApproximateFn(std::function<bool()> fn) { approx_ = std::move(fn); }  // default: clangd not initialised

  void onTyped(const std::string& text);  // after text input was applied to the Editor
  void update();                          // after any other event: close when the caret left the word, else refilter
  bool handleKey(const InputEvent& e);    // true when consumed
  void close();
  bool isOpen() const { return open_; }
  PopupBox* box() const { return box_; }
  int selected() const { return sel_; }
  std::vector<std::string> shownLabels() const;
  bool accept();  // the selected item; false when closed

 private:
  struct Row {
    size_t item;
    bool prefix;
    int score;
  };
  void request(Position anchor);
  void onItems(uint64_t seq, const std::vector<LspCompletionItem>& items, const std::string& err);
  bool valid() const;
  std::string prefix() const;
  void refilter();
  void render();
  void place();

  PickerEnv& env_;
  CompleteFn complete_;
  std::function<bool()> approx_;
  PopupBox* box_ = nullptr;
  std::vector<LspCompletionItem> items_;
  std::vector<Row> shown_;
  Position anchor_{0, 0}, suppressed_{-1, -1};
  const Document* doc_ = nullptr;
  uint64_t seq_ = 0;
  int sel_ = 0;
  bool open_ = false, approximate_ = false, inFlight_ = false;
  RectF placed_{0, 0, 0, 0};
  int lastVersion_ = -1;
  Position lastCaret_{-1, -1};
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
