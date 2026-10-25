#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"
#include "ui/features/PopupBox.h"

namespace tf::ui {

// Hover tooltip (B5b): after the mouse rested kDwellMs over an identifier (or over a diagnostic's range) of a
// TextView, the diagnostics' messages are shown at once (severity coloured) and Editor::hoverAt's text is added
// when it arrives (clangd; without clangd it is the fast index's guess, greyed and marked approximate, constraint
// 1.4). Moving to another word, a key press or a click dismisses it; moving onto the tooltip keeps it. EditorApp
// feeds mouse moves (onMouse), dismisses on keys / clicks and ticks it.
class HoverTooltip {
 public:
  using Done = std::function<void(const std::string& text, const std::string& error)>;
  using HoverFn = std::function<void(Document&, Position, Done)>;
  static constexpr int kDwellMs = 400;

  explicit HoverTooltip(PickerEnv& env);
  ~HoverTooltip();
  HoverTooltip(const HoverTooltip&) = delete;
  HoverTooltip& operator=(const HoverTooltip&) = delete;

  void setHoverFn(HoverFn fn) { hover_ = std::move(fn); }  // default: Editor::hoverAt
  void setApproximateFn(std::function<bool()> fn) { approx_ = std::move(fn); }
  void onMouse(const InputEvent& e);  // MouseMove events
  void dismiss();
  int tick(int64_t nowMs);  // fires the request once the dwell elapsed; ms until it is due, -1 = nothing pending
  bool shown() const { return box_->visible(); }
  bool pending() const { return pending_; }
  PopupBox* box() const { return box_; }

 private:
  void fire();
  void onHover(uint64_t seq, const std::string& text, const std::string& err);
  void show();

  PickerEnv& env_;
  HoverFn hover_;
  std::function<bool()> approx_;
  PopupBox* box_ = nullptr;
  std::filesystem::path file_;
  Range target_;
  Position pos_;
  float x_ = 0, y_ = 0;
  int64_t since_ = 0;
  bool pending_ = false, haveTarget_ = false;
  uint64_t seq_ = 0;
  std::vector<PopupLine> diag_;
  std::string hoverText_;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
