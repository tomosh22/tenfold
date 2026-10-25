#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/widgets/InputLine.h"
#include "ui/widgets/Widget.h"

namespace tf::ui {

// One row of a ListPopup.
struct ListRow {
  std::string key;         // identity (selection survives setRows when the key is still present; see indexOfKey)
  std::string icon;        // short badge in the icon column ("f", "C", ...)
  std::string iconToken;   // Theme::tokenColor name for the icon ("function", "class"); empty = dim text
  std::string primary;     // main text
  std::string secondary;   // dim text after the primary one (scope, line preview)
  std::string detail;      // dim, right aligned (path:line)
  std::string badge;       // dim, right aligned left of the detail ("approx")
  std::vector<int> match;  // byte offsets inside `primary` drawn highlighted (fuzzy-matched characters)
  bool dim = false;        // greyed out (an approximate result that is still being verified)
};

// Text shown in the optional right-hand pane (preview of the selected row).
struct ListPreview {
  std::string title;
  std::vector<std::string> lines;
  int firstLine = 1;   // number shown next to lines[0]
  int highlight = -1;  // index into lines drawn on a highlighted background
};

// Byte offsets of a case-insensitive subsequence match of `query` in `candidate` (tightest window ending at the
// earliest end), empty when it is not a subsequence. For ListRow::match.
std::vector<int> fuzzyMatchPositions(std::string_view query, std::string_view candidate);

// Modal overlay list (B5a): an optional InputLine on top, rows below, an optional preview pane on the right. Shown
// with RootWidget::showModal (`open`), which paints it on the top layer above the editor's glyphs, routes every key
// and mouse event to it and restores the previous focus on close. Keys: Up/Down/PageUp/PageDown move the selection,
// Home/End do too when there is no input (Ctrl+Home/End always), Enter accepts, Escape closes; typing, caret keys and
// Backspace go to the InputLine. Mouse: hover highlights, a click on a row accepts it, a click outside closes, the
// wheel scrolls. The popup is a full-window widget whose visible box is popupRect().
class ListPopup : public Widget {
 public:
  explicit ListPopup(bool withInput = true);

  InputLine* input() const { return input_; }
  void setMaxVisibleRows(int n);
  void setTitle(std::string s);   // header text of an input-less popup (left)
  void setStatus(std::string s);  // dim text right of the input / title ("verifying...")
  void setEmptyText(std::string s) { empty_ = std::move(s); }
  void setPreviewEnabled(bool on);  // reserves the right pane (even while empty)
  void setPreview(ListPreview p);
  bool previewEnabled() const { return preview_; }
  const ListPreview& previewContent() const { return previewContent_; }
  const std::string& status() const { return status_; }

  // Replaces the rows; the selection becomes `selected` (clamped; -1 = none). Fires onSelection.
  void setRows(std::vector<ListRow> rows, int selected = 0);
  const std::vector<ListRow>& rows() const { return rows_; }
  int selected() const { return selected_; }
  const ListRow* selectedRow() const;
  int indexOfKey(const std::string& key) const;  // -1 if absent
  void setSelected(int i);                       // clamps, scrolls into view, fires onSelection on change
  int firstRow() const { return first_; }
  int visibleRows() const { return maxRows_; }
  int hoverRow() const { return hover_; }

  void onQuery(std::function<void(const std::string&)> fn) { query_ = std::move(fn); }
  void onAccept(std::function<void(int row)> fn) { accept_ = std::move(fn); }
  void onSelection(std::function<void(int row)> fn) { selection_ = std::move(fn); }
  void onClosed(std::function<void()> fn) { closed_ = std::move(fn); }

  // Shows `p` on `root` as the modal widget (focus in the input) and returns it; ownership moves to the tree.
  static ListPopup* open(RootWidget& root, std::unique_ptr<ListPopup> p);
  void close();  // closes the modal and fires onClosed once; no-op when it is not shown

  // Geometry (valid after layout).
  RectF popupRect() const;
  RectF listRect() const;  // where rows are drawn
  RectF rowRect(int i) const;
  RectF previewRect() const;

  bool onEvent(const InputEvent& e) override;
  bool acceptsFocus() const override { return !input_; }

 protected:
  void onLayout() override;
  void paint(PaintContext& ctx) override;

 private:
  float rowHeight() const;
  float headerHeight() const;
  int rowAt(float x, float y) const;
  void ensureVisible();
  void scrollTo(int first);
  void moveSelection(int delta);
  void accept(int row);
  void paintRow(PaintContext& ctx, int i, const RectF& r);
  void paintPreview(PaintContext& ctx, const RectF& r);

  InputLine* input_ = nullptr;
  std::vector<ListRow> rows_;
  int selected_ = -1, first_ = 0, hover_ = -1, maxRows_ = 12;
  bool preview_ = false, closedFired_ = false;
  std::string title_, status_, empty_ = "No results";
  ListPreview previewContent_;
  std::function<void(const std::string&)> query_;
  std::function<void(int)> accept_, selection_;
  std::function<void()> closed_;
};

}  // namespace tf::ui
