#pragma once
#include <functional>
#include <memory>
#include <string>

#include "ui/features/PickerUtil.h"
#include "ui/widgets/InputLine.h"

namespace tf::ui {

// Rename symbol (10x RenameSymbol / SubmitRenameSymbol, W8c): a small non-modal input box under the symbol at the
// caret, pre-filled with the symbol's name and focused. Enter (the "popup" key context's SubmitRenameSymbol binding)
// asks clangd for the rename (Editor::renameSymbol: edits across files, one undo step per file, open documents updated,
// closed files written) and Escape or a click elsewhere cancels. There is no approximate tier: approximate renames
// would be wrong (constraint 1.4), so without a running clangd the command says so and opens nothing.
class RenameInput {
 public:
  using Done = Editor::Done<Editor::RenameResult>;
  using RenameFn = std::function<void(Position at, const std::string& newName, Done)>;

  explicit RenameInput(PickerEnv& env);
  ~RenameInput();
  RenameInput(const RenameInput&) = delete;
  RenameInput& operator=(const RenameInput&) = delete;

  // Tests inject fakes: the rename backend and whether clangd is available (default: clangd initialised).
  void setRenameFn(RenameFn fn) { rename_ = std::move(fn); }
  void setAvailableFn(std::function<bool()> fn) { available_ = std::move(fn); }
  // Called after the box closed so the app can give the keys back to the editor.
  void onClosed(std::function<void()> fn) { closed_ = std::move(fn); }

  bool open();    // false (with a status message) when there is no symbol at the caret or no clangd
  bool submit();  // Enter: false when closed
  void cancel();
  bool isOpen() const { return box_ != nullptr; }
  InputLine* input() const { return input_; }
  Widget* box() const { return box_; }
  const std::string& symbol() const { return old_; }
  bool busy() const { return busy_; }

 private:
  void close();
  void finish(const Editor::RenameResult& r, const std::string& err, const std::string& newName);

  PickerEnv& env_;
  RenameFn rename_;
  std::function<bool()> available_;
  std::function<void()> closed_;
  Widget* box_ = nullptr;
  std::unique_ptr<Widget> retired_;  // the closed box, destroyed at the next close (it may still be on the call stack)
  InputLine* input_ = nullptr;
  std::string old_;
  Position at_{0, 0};
  bool busy_ = false;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
