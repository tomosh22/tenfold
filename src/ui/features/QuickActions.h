#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Quick Actions and Refactorings (W11-GUI-LSP), Visual Studio's Ctrl+. : a small list at the caret with what clangd
// offers there (code actions: quick fixes for the diagnostics on the line, such as "add #include", fix-its, and
// tweaks such as "extract variable"). Enter or a click applies the action (Editor::applyCodeAction: its edits form ONE
// undo step per document); Escape closes. Nothing is offered without clangd, in a file outside every translation unit
// (Visual Studio's Miscellaneous Files rule) or when clangd has no action there: the status bar says so.
//
// The light bulb: while the caret rests on a line (a short debounce after the last caret or text change) the same
// request runs in the background; when it returns a quick fix the view shows a light bulb in the margin of that line
// (bulbLine()), and a click on it opens the list. Answers of an older request, line or document version are dropped.
class QuickActions {
 public:
  template <class T>
  using Done = Editor::Done<std::vector<T>>;
  struct Backend {
    std::function<void(Range, Done<CodeAction>)> fetch;
    std::function<void(const CodeAction&, std::function<void(bool ok, const std::string& error)>)> apply;
  };
  static Backend editorBackend(Editor& editor);

  explicit QuickActions(PickerEnv& env);
  ~QuickActions();
  QuickActions(const QuickActions&) = delete;
  QuickActions& operator=(const QuickActions&) = delete;

  void setBackend(Backend b) { backend_ = std::move(b); }
  // Whether clangd results may be asked for (clangd running, the file in a translation unit); default: always.
  void setAllowedFn(std::function<bool()> fn) { allowed_ = std::move(fn); }
  // Where the list appears (the caret); without one it is centred.
  void setAnchor(RectF r) { anchor_ = r; }

  // The range actions are asked for: the selection, else the caret's line without its surrounding blanks.
  static Range rangeFor(Editor& editor);

  // Ctrl+. : asks and shows the list. False (with a message) when there is no document or nothing may be asked.
  bool open();
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  const std::vector<CodeAction>& shown() const { return shown_; }

  // ---- light bulb ----
  static constexpr int64_t kBulbDebounceMs = 300;
  // Call from the app's tick: returns the ms until it wants to be called again (-1 = nothing pending).
  int tick(int64_t nowMs);
  // The line of the current document that has a quick fix, -1 for none.
  int bulbLine() const { return bulbLine_; }
  // Visual Studio draws an "error light bulb" (red) for a fix of an error and a yellow one for the rest.
  bool bulbIsError() const { return bulbError_; }
  const Document* bulbDocument() const { return bulbDoc_; }
  std::function<void()> onBulbChanged;
  void setBulbEnabled(bool on);

 private:
  void deliver(uint64_t seq, const std::vector<CodeAction>& as, const std::string& err);
  void clearBulb();

  PickerEnv& env_;
  Backend backend_;
  std::function<bool()> allowed_;
  std::optional<RectF> anchor_;
  ListPopup* popup_ = nullptr;
  std::vector<CodeAction> shown_;
  uint64_t seq_ = 0;
  // light bulb
  bool bulbOn_ = true;
  const Document* keyDoc_ = nullptr;
  int keyVersion_ = -1, keyLine_ = -1;
  int64_t keyAt_ = 0;
  uint64_t keyDiagSig_ = 0;
  bool needAsk_ = false;
  uint64_t bulbSeq_ = 0;
  int bulbLine_ = -1;
  bool bulbError_ = false;
  const Document* bulbDoc_ = nullptr;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
