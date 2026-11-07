#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/Document.h"
#include "lsp/LspClient.h"
#include "ui/gfx/TextLayout.h"

namespace tf::ui {

// Inlay hints of one document as virtual text (W11-GUI-LSP): clangd's parameter-name and deduced-type hints. A hint
// never enters the document: it sits between two characters, takes `cells + 1` cells of room on its row (the label
// plus one cell of padding) and moves everything to its right. Caret positions, selections and hit testing keep
// working in document columns; the view adds shift() to the x of a column and subtracts it again when it turns an x
// into a column, so a caret can never be inside a hint and the columns shown in the status bar do not change.
//
// A parameter hint (kind 2) precedes the character at its column: a caret at that column is drawn after the hint. A
// type hint (kind 1, ": int" after the name) follows the text before its column: a caret there is drawn before it.
// Editing a line drops the hints of the lines it touched and moves the hints below (the view asks clangd again).
class InlayMap {
 public:
  struct Item {
    int line = 0, col = 0;  // byte column, where the hint sits
    std::string text;
    bool prefix = false;  // drawn before the character at `col` (parameter hints); else after the text before it
    int cells = 0;        // display cells of `text`
    int width() const { return cells + 1; }
  };

  void set(const std::vector<LspInlayHint>& hints, const gfx::TextLayout& tl);
  void clear() { items_.clear(); }
  bool empty() const { return items_.empty(); }
  const std::vector<Item>& items() const { return items_; }
  // Document::Change: hints on the edited lines are dropped, those below move with the line numbers.
  void onChange(const Document::Change& c);

  // Indices [first, last) of the hints of `line` (sorted by column).
  std::pair<size_t, size_t> ofLine(int line) const;
  // True when `it` is drawn on the row [rs, re) of its line (re == lineLen for the last row). On an unwrapped line that
  // is every hint; at a wrap boundary a prefix hint goes to the later row and a suffix hint to the earlier one.
  static bool onRow(const Item& it, int rs, int re, int lineLen);
  // Cells the hints on the row take up left of the caret position `col`.
  // `end`: the position closes a range, a prefix hint at `col` is not counted.
  int shiftCells(int line, int rs, int re, int lineLen, int col, bool end = false) const;
  // Inverse of x = natural + shift: `x` is a pixel offset in the shifted row (cell width `cw`), `natural(col)` gives
  // the unshifted pixel offset of a column. Returns the unshifted x; a point inside a hint maps to the hint's own
  // column.
  template <class NaturalFn>
  float unshift(int line, int rs, int re, int lineLen, float x, float cw, NaturalFn natural) const {
    const auto [a, b] = ofLine(line);
    float s = 0;
    for (size_t i = a; i < b; ++i) {
      const Item& h = items_[i];
      if (!onRow(h, rs, re, lineLen)) continue;
      const float nat = natural(h.col), w = static_cast<float>(h.width()) * cw;
      const float hx = nat + s;
      if (x < hx) break;
      if (x < hx + w) return nat;
      s += w;
    }
    return x - s;
  }

 private:
  std::vector<Item> items_;  // sorted by (line, col)
};

}  // namespace tf::ui
