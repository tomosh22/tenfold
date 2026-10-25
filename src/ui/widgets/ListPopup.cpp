#include "ui/widgets/ListPopup.h"

#include <algorithm>
#include <cctype>

namespace tf::ui {

std::vector<int> fuzzyMatchPositions(std::string_view q, std::string_view c) {
  auto eq = [](char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
  };
  std::vector<int> out;
  if (q.empty()) return out;
  size_t qi = 0, end = 0;
  for (size_t ci = 0; ci < c.size() && qi < q.size(); ++ci)
    if (eq(q[qi], c[ci])) ++qi, end = ci + 1;
  if (qi < q.size()) return out;
  out.assign(q.size(), 0);
  size_t qj = q.size();
  for (size_t ci = end; ci-- > 0 && qj > 0;)  // tightest window: match backwards from the earliest end
    if (eq(q[qj - 1], c[ci])) out[--qj] = static_cast<int>(ci);
  return out;
}

ListPopup::ListPopup(bool withInput) {
  if (!withInput) return;
  auto in = std::make_unique<InputLine>();
  input_ = static_cast<InputLine*>(addChild(std::move(in)));
  input_->onChanged([this](const std::string& s) {
    if (query_) query_(s);
  });
}

float ListPopup::rowHeight() const { return root()->text().lineHeight() + theme().verticalPad; }
float ListPopup::headerHeight() const {
  if (input_) return input_->preferredHeight() + 2 * theme().verticalPad;
  return title_.empty() && status_.empty() ? theme().verticalPad : rowHeight() + theme().verticalPad;
}

RectF ListPopup::popupRect() const {
  const RectF& b = bounds();
  const float cell = root()->text().cellWidth(), margin = 16;
  const float w = std::min(b.w - 2 * margin, cell * (preview_ ? 120 : 80));
  const float h = std::min(b.h - 2 * margin, headerHeight() + rowHeight() * maxRows_ + theme().verticalPad);
  return {b.x + (b.w - w) / 2, b.y + std::min(b.h * 0.12f, std::max(0.0f, b.h - h - margin)), w, h};
}

RectF ListPopup::listRect() const {
  const RectF p = popupRect();
  const float top = p.y + headerHeight();
  return {p.x, top, preview_ ? p.w * 0.55f : p.w, p.y + p.h - top - theme().verticalPad};
}

RectF ListPopup::previewRect() const {
  const RectF p = popupRect(), l = listRect();
  return {l.x + l.w, l.y, p.w - l.w, l.h};
}

RectF ListPopup::rowRect(int i) const {
  const RectF l = listRect();
  return {l.x, l.y + static_cast<float>(i - first_) * rowHeight(), l.w, rowHeight()};
}

int ListPopup::rowAt(float x, float y) const {
  const RectF l = listRect();
  if (!ui::contains(l, x, y)) return -1;
  const int i = first_ + static_cast<int>((y - l.y) / rowHeight());
  return i >= 0 && i < static_cast<int>(rows_.size()) && i < first_ + maxRows_ ? i : -1;
}

void ListPopup::onLayout() {
  if (!input_) return;
  const RectF p = popupRect();
  const float pad = theme().padding, sw = status_.empty() ? 0 : root()->text().measure(status_) + pad;
  input_->layout({p.x + pad, p.y + theme().verticalPad, std::max(0.0f, p.w - 2 * pad - sw), input_->preferredHeight()});
}

void ListPopup::setMaxVisibleRows(int n) {
  maxRows_ = std::max(1, n);
  ensureVisible();
  if (root()) root()->requestLayout();
}

void ListPopup::setTitle(std::string s) {
  if (s == title_) return;
  title_ = std::move(s);
  if (root()) root()->requestLayout();
}

void ListPopup::setStatus(std::string s) {
  if (s == status_) return;
  status_ = std::move(s);
  if (root()) root()->requestLayout();
}

void ListPopup::setPreviewEnabled(bool on) {
  if (on == preview_) return;
  preview_ = on;
  if (root()) root()->requestLayout();
}

void ListPopup::setPreview(ListPreview p) {
  previewContent_ = std::move(p);
  markDirty();
}

const ListRow* ListPopup::selectedRow() const {
  return selected_ >= 0 && selected_ < static_cast<int>(rows_.size()) ? &rows_[static_cast<size_t>(selected_)]
                                                                      : nullptr;
}

int ListPopup::indexOfKey(const std::string& key) const {
  for (size_t i = 0; i < rows_.size(); ++i)
    if (rows_[i].key == key) return static_cast<int>(i);
  return -1;
}

void ListPopup::setRows(std::vector<ListRow> rows, int selected) {
  rows_ = std::move(rows);
  selected_ = rows_.empty() ? -1 : std::clamp(selected, -1, static_cast<int>(rows_.size()) - 1);
  hover_ = -1;
  first_ = std::min(first_, std::max(0, static_cast<int>(rows_.size()) - maxRows_));
  ensureVisible();
  markDirty();
  if (selection_) selection_(selected_);
}

void ListPopup::ensureVisible() {
  if (selected_ < 0) return;
  if (selected_ < first_) first_ = selected_;
  if (selected_ >= first_ + maxRows_) first_ = selected_ - maxRows_ + 1;
}

void ListPopup::scrollTo(int first) {
  first = std::clamp(first, 0, std::max(0, static_cast<int>(rows_.size()) - maxRows_));
  if (first == first_) return;
  first_ = first;
  markDirty();
}

void ListPopup::setSelected(int i) {
  if (rows_.empty()) return;
  i = std::clamp(i, 0, static_cast<int>(rows_.size()) - 1);
  if (i == selected_) return;
  selected_ = i;
  ensureVisible();
  markDirty();
  if (selection_) selection_(selected_);
}

void ListPopup::moveSelection(int delta) { setSelected(selected_ < 0 ? (delta > 0 ? 0 : -1) : selected_ + delta); }

ListPopup* ListPopup::open(RootWidget& root, std::unique_ptr<ListPopup> p) {
  ListPopup* raw = p.get();
  raw->closedFired_ = false;
  root.showModal(std::move(p), raw->input_);
  return raw;
}

void ListPopup::close() {
  RootWidget* r = root();
  if (!r || r->modal() != this) return;
  r->closeModal();
  if (!closedFired_) {
    closedFired_ = true;
    if (closed_) closed_();
  }
}

void ListPopup::accept(int row) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return;
  close();  // first: the handler may open another popup
  if (accept_) accept_(row);
}

bool ListPopup::onEvent(const InputEvent& e) {
  switch (e.type) {
    case InputType::KeyDown:
      switch (e.key) {
        case Key::Up: moveSelection(-1); return true;
        case Key::Down: moveSelection(1); return true;
        case Key::PageUp: moveSelection(-maxRows_); return true;
        case Key::PageDown: moveSelection(maxRows_); return true;
        case Key::Home:
        case Key::End:
          if (input_ && !(e.mods & ModCtrl)) return false;
          setSelected(e.key == Key::Home ? 0 : static_cast<int>(rows_.size()) - 1);
          return true;
        case Key::Enter:
        case Key::KpEnter: accept(selected_); return true;
        case Key::Escape: close(); return true;
        default: return false;
      }
    case InputType::MouseMove: {
      const int h = rowAt(e.x, e.y);
      if (h != hover_) {
        hover_ = h;
        markDirty();
      }
      return true;
    }
    case InputType::MouseDown:
      if (!ui::contains(popupRect(), e.x, e.y)) close();
      else if (e.button == MouseButton::Left) {
        const int r = rowAt(e.x, e.y);
        if (r >= 0) {
          setSelected(r);
          accept(r);
        }
      }
      return true;
    case InputType::MouseUp: return true;
    case InputType::Wheel:
      if (ui::contains(popupRect(), e.x, e.y)) scrollTo(first_ - static_cast<int>(e.dy * 3));
      return true;
    default: return false;
  }
}

void ListPopup::paintRow(PaintContext& ctx, int i, const RectF& r) {
  const ListRow& row = rows_[static_cast<size_t>(i)];
  const Theme& t = ctx.theme;
  if (i == selected_) ctx.fill(r, t.selection);
  else if (i == hover_) ctx.fill(r, t.tabHover);
  const float cell = ctx.textRenderer.cellWidth(), pad = t.padding, ty = r.y + (r.h - ctx.lineHeight()) / 2;
  float x = r.x + pad, right = r.x + r.w - pad - 4;
  const RectF full{r.x, r.y, r.w, r.h};
  if (!row.icon.empty())
    ctx.text(x, ty, row.icon, row.iconToken.empty() || row.dim ? t.textDim : t.tokenColor(row.iconToken), full);
  x += 3 * cell;
  if (!row.detail.empty()) {
    const float w = ctx.measure(row.detail);
    ctx.text(right - w, ty, row.detail, t.textDim, full);
    right -= w + 2 * cell;
  }
  if (!row.badge.empty()) {
    const float w = ctx.measure(row.badge);
    ctx.text(right - w, ty, row.badge, t.textDim, full);
    right -= w + 2 * cell;
  }
  const RectF clip{x, r.y, std::max(0.0f, right - x), r.h};
  const Color base = row.dim ? t.textDim : t.text;
  size_t from = 0, mi = 0;
  auto run = [&](size_t to, bool hit) {
    if (to <= from) return;
    const std::string_view s = std::string_view(row.primary).substr(from, to - from);
    ctx.text(x, ty, s, hit ? t.tabAccent : base, clip);
    x += ctx.measure(s);
    from = to;
  };
  while (from < row.primary.size()) {  // alternate unmatched / matched runs (per code point)
    const bool hit = mi < row.match.size() && static_cast<int>(from) == row.match[mi];
    size_t to = from + 1;
    while (to < row.primary.size() && (static_cast<unsigned char>(row.primary[to]) & 0xC0) == 0x80) ++to;
    if (hit) {
      ++mi;
      run(to, true);
    } else {
      size_t stop = to;
      while (stop < row.primary.size() && !(mi < row.match.size() && static_cast<int>(stop) == row.match[mi])) ++stop;
      run(stop, false);
    }
  }
  if (!row.secondary.empty()) ctx.text(x + cell, ty, row.secondary, t.textDim, clip);
}

void ListPopup::paintPreview(PaintContext& ctx, const RectF& r) {
  const Theme& t = ctx.theme;
  ctx.fill(r, t.panelBackground);
  const float lh = ctx.lineHeight(), cell = ctx.textRenderer.cellWidth(), pad = t.padding;
  float y = r.y + t.verticalPad;
  if (!previewContent_.title.empty()) {
    ctx.text(r.x + pad, y, previewContent_.title, t.textDim, r);
    y += lh + t.verticalPad;
  }
  const RectF clip{r.x + 4 * cell + pad, r.y, std::max(0.0f, r.w - 4 * cell - 2 * pad), r.h};
  for (size_t i = 0; i < previewContent_.lines.size() && y + lh <= r.y + r.h; ++i, y += lh) {
    if (static_cast<int>(i) == previewContent_.highlight) ctx.fill({r.x, y, r.w, lh}, t.currentLine);
    ctx.text(r.x + pad, y, std::to_string(previewContent_.firstLine + static_cast<int>(i)), t.gutterText, r);
    ctx.text(r.x + 4 * cell + pad, y, previewContent_.lines[i], t.text, clip);
  }
}

void ListPopup::paint(PaintContext& ctx) {
  const Theme& t = ctx.theme;
  const RectF p = popupRect();
  ctx.fill({p.x - 1, p.y - 1, p.w + 2, p.h + 2}, t.splitterHot);
  ctx.fill(p, t.tabInactive);
  const float hy = p.y + t.verticalPad + ((input_ ? input_->preferredHeight() : rowHeight()) - ctx.lineHeight()) / 2;
  if (!input_ && !title_.empty()) ctx.text(p.x + t.padding, hy, title_, t.text, p);
  if (!status_.empty()) {
    const float w = ctx.measure(status_);
    ctx.text(p.x + p.w - t.padding - w, hy, status_, t.textDim, p);
  }
  const RectF l = listRect();
  if (rows_.empty()) ctx.text(l.x + t.padding, l.y + t.verticalPad, empty_, t.textDim, l);
  for (int i = first_; i < static_cast<int>(rows_.size()) && i < first_ + maxRows_; ++i) paintRow(ctx, i, rowRect(i));
  const int n = static_cast<int>(rows_.size());
  if (n > maxRows_) {  // scrollbar thumb
    Color c = t.textDim;
    c.a = 0.5f;
    const float th = std::max(12.0f, l.h * maxRows_ / n);
    ctx.fill({l.x + l.w - 4, l.y + (l.h - th) * first_ / (n - maxRows_), 3, th}, c);
  }
  if (preview_) paintPreview(ctx, previewRect());
}

}  // namespace tf::ui
