#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

class TextView;

// Inlay hints (W11-GUI-LSP): clangd's parameter-name and deduced-type hints drawn as dim virtual text (TextView,
// InlayMap). Setting editor.inlayHints (Visual Studio: Tools > Options > Text Editor > C/C++ > IntelliSense > Inlay
// Hints, whose hold-to-show key is Alt+F1):
//   off            never shown, Alt+F1 does nothing
//   on             always shown
//   while-pressed  shown while Alt+F1 is held (the default: Visual Studio shows nothing until the key is held)
// Only the current document is asked, for the visible lines plus a margin, a short debounce after the last scroll or
// text change; an answer for an older request, version or document is dropped, and nothing is asked without clangd or
// in a file outside every translation unit (the Miscellaneous Files rule). Leaving the "shown" state removes the hints
// from every view at once.
class InlayHints {
 public:
  enum class Mode { Off, On, WhilePressed };
  static Mode parseMode(const std::string& s);  // unknown text: WhilePressed (the default)
  template <class T>
  using Done = Editor::Done<std::vector<T>>;
  struct Backend {
    std::function<void(Range, Done<LspInlayHint>)> fetch;
  };
  static Backend editorBackend(Editor& editor);
  using ViewsFn = std::function<void(const std::function<void(TextView&)>&)>;

  static constexpr int64_t kDebounceMs = 150;
  static constexpr int kMarginLines = 40;

  InlayHints(PickerEnv& env, ViewsFn views);
  ~InlayHints();
  InlayHints(const InlayHints&) = delete;
  InlayHints& operator=(const InlayHints&) = delete;

  void setBackend(Backend b) { backend_ = std::move(b); }
  void setAllowedFn(std::function<bool()> fn) { allowed_ = std::move(fn); }
  void setMode(Mode m);
  Mode mode() const { return mode_; }
  void setHeld(bool held);  // Alt+F1 pressed / released (or the window lost focus)
  bool held() const { return held_; }
  bool shown() const { return mode_ == Mode::On || (mode_ == Mode::WhilePressed && held_); }

  // Call from the app's tick: returns the ms until it wants to be called again (-1 = nothing pending).
  int tick(int64_t nowMs);

 private:
  void applyShown();

  PickerEnv& env_;
  ViewsFn views_;
  Backend backend_;
  std::function<bool()> allowed_;
  Mode mode_ = Mode::WhilePressed;
  bool held_ = false, appliedShown_ = false;
  // What was last asked.
  const Document* doc_ = nullptr;
  int version_ = -1, firstAsked_ = 0, lastAsked_ = -1;
  int64_t changedAt_ = 0;
  bool dirty_ = false;
  int seenFirst_ = -1, seenLast_ = -1;
  uint64_t seq_ = 0;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
