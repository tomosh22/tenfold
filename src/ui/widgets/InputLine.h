#pragma once
#include <functional>
#include <string>

#include "ui/widgets/Widget.h"

namespace tf::ui {

// Single-line editable text field (B5a): UTF-8 text, caret + selection (byte offsets, always on code point
// boundaries), Home/End, Ctrl+Left/Right word moves, Ctrl+Backspace/Delete word deletes, Ctrl+A/C/X/V through
// src/ui/platform/Clipboard, mouse caret placement, drag selection, double click = word, triple click = all.
// Up/Down/Enter/Escape/PageUp/PageDown and every other key are NOT handled, so they bubble to the parent (the
// ListPopup). Newlines in pasted text become spaces. The caret is drawn in the overlay pass (above glyphs).
class InputLine : public Widget {
 public:
  using ChangedFn = std::function<void(const std::string&)>;

  const std::string& text() const { return text_; }
  void setText(std::string s, bool selectAll = false);  // programmatic: does not call onChanged
  void setPlaceholder(std::string s);
  void onChanged(ChangedFn fn) { changed_ = std::move(fn); }  // user edits only

  size_t caret() const { return caret_; }
  size_t anchor() const { return anchor_; }
  void setCaret(size_t byteOffset, bool extendSelection = false);
  bool hasSelection() const { return caret_ != anchor_; }
  std::string selectedText() const;
  void selectAll();

  float preferredHeight() const;
  bool acceptsFocus() const override { return true; }
  bool onEvent(const InputEvent& e) override;

 protected:
  void paint(PaintContext& ctx) override;
  void paintOverlay(PaintContext& ctx) override;

 private:
  size_t prevWord(size_t i) const;
  size_t nextWord(size_t i) const;
  size_t offsetAt(float x) const;
  float caretX() const;  // relative to the text origin (unscrolled)
  void replaceSelection(const std::string& s);
  void deleteBackward(bool word);
  void deleteForward(bool word);
  void edited();
  float textLeft() const;

  std::string text_, placeholder_;
  size_t caret_ = 0, anchor_ = 0;
  mutable float scrollX_ = 0;
  ChangedFn changed_;
};

}  // namespace tf::ui
