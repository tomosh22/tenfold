#include "ui/widgets/DropDown.h"

#include <algorithm>

namespace tf::ui {

float DropDown::rowHeight() const { return root() ? root()->text().lineHeight() + theme().verticalPad : 0; }

float DropDown::preferredHeight() const {
  return (root() ? root()->text().lineHeight() : 16.0f) + 2 * theme().verticalPad;
}

float DropDown::preferredWidth() const {
  float w = 0;
  if (root())
    for (const auto& it : items_) w = std::max(w, root()->text().measure(it.label));
  return w + 2 * theme().padding + 2 * (root() ? root()->text().cellWidth() : 8.0f);
}

void DropDown::setItems(std::vector<DropDownItem> items, int selected) {
  items_ = std::move(items);
  open_ = false;
  selected_ = -1;
  setSelected(selected);
  if (selected_ < 0) selected_ = firstEnabled();
  markDirty();
}

void DropDown::setEnabled(int index, bool enabled) {
  if (index < 0 || index >= static_cast<int>(items_.size())) return;
  items_[static_cast<size_t>(index)].enabled = enabled;
  markDirty();
}

std::string DropDown::selectedLabel() const {
  return selected_ >= 0 && selected_ < static_cast<int>(items_.size()) ? items_[static_cast<size_t>(selected_)].label
                                                                        : std::string();
}

void DropDown::setSelected(int index) {
  if (index < 0 || index >= static_cast<int>(items_.size()) || !items_[static_cast<size_t>(index)].enabled ||
      index == selected_)
    return;
  selected_ = index;
  markDirty();
}

int DropDown::step(int from, int dir) const {
  for (int i = from + dir; i >= 0 && i < static_cast<int>(items_.size()); i += dir)
    if (items_[static_cast<size_t>(i)].enabled) return i;
  return -1;
}

void DropDown::open() {
  if (open_ || items_.empty()) return;
  open_ = true;
  highlight_ = selected_;
  markDirty();
}

void DropDown::closeList() {
  if (!open_) return;
  open_ = false;
  highlight_ = -1;
  markDirty();
}

void DropDown::choose(int index) {
  closeList();
  if (index < 0 || index >= static_cast<int>(items_.size()) || !items_[static_cast<size_t>(index)].enabled) return;
  if (index == selected_) return;
  selected_ = index;
  markDirty();
  if (changed_) changed_(selected_);
}

RectF DropDown::listRect() const {
  if (!open_ || !root()) return {0, 0, 0, 0};
  const RectF& b = bounds();
  const float h = rowHeight() * static_cast<float>(items_.size()) + 2;
  const RectF win = root()->bounds();
  float y = b.y + b.h - 1;
  if (y + h > win.y + win.h && b.y - h + 1 >= win.y) y = b.y - h + 1;  // no room below: open upwards
  return {b.x, y, b.w, h};
}

RectF DropDown::itemRect(int i) const {
  const RectF l = listRect();
  return {l.x + 1, l.y + 1 + rowHeight() * static_cast<float>(i), l.w - 2, rowHeight()};
}

RectF DropDown::hitRect() const {
  if (!open_) return bounds();
  const RectF b = bounds(), l = listRect();
  const float x0 = std::min(b.x, l.x), y0 = std::min(b.y, l.y);
  const float x1 = std::max(b.x + b.w, l.x + l.w), y1 = std::max(b.y + b.h, l.y + l.h);
  return {x0, y0, x1 - x0, y1 - y0};
}

int DropDown::itemAt(float x, float y) const {
  if (!open_) return -1;
  for (int i = 0; i < static_cast<int>(items_.size()); ++i)
    if (ui::contains(itemRect(i), x, y)) return i;
  return -1;
}

void DropDown::onFocusChanged(bool gained) {
  if (!gained) closeList();
  markDirty();
}

void DropDown::onHover(bool entered) {
  if (!entered && (hoverBox_ || highlight_ >= 0)) {
    hoverBox_ = false;
    markDirty();
  }
}

bool DropDown::onEvent(const InputEvent& e) {
  switch (e.type) {
    case InputType::KeyDown: {
      const bool alt = e.mods & ModAlt, plain = !(e.mods & (ModCtrl | ModShift | ModAlt | ModSuper));
      if (open_) {
        switch (e.key) {
          case Key::Up:
            if (alt) {
              choose(highlight_);
              return true;
            }
            if (const int n = step(highlight_, -1); n >= 0) highlight_ = n;
            markDirty();
            return true;
          case Key::Down:
            if (const int n = step(highlight_, 1); n >= 0) highlight_ = n;
            markDirty();
            return true;
          case Key::PageUp:
          case Key::Home: highlight_ = firstEnabled(); markDirty(); return true;
          case Key::PageDown:
          case Key::End: highlight_ = lastEnabled(); markDirty(); return true;
          case Key::Enter:
          case Key::KpEnter:
          case Key::Space: choose(highlight_); return true;
          case Key::Escape: closeList(); return true;
          case Key::Tab: closeList(); return false;
          default: return false;
        }
      }
      switch (e.key) {
        case Key::Down:
          if (alt) {
            open();
            return true;
          }
          if (plain) {
            if (const int n = step(selected_, 1); n >= 0) choose(n);
            return true;
          }
          return false;
        case Key::Up:
          if (plain) {
            if (const int n = step(selected_, -1); n >= 0) choose(n);
            return true;
          }
          return false;
        case Key::Home:
        case Key::PageUp:
          if (plain) choose(firstEnabled());
          return plain;
        case Key::End:
        case Key::PageDown:
          if (plain) choose(lastEnabled());
          return plain;
        case Key::F4:
        case Key::Space:
          if (!plain && e.key == Key::Space) return false;
          open();
          return true;
        case Key::Enter:
        case Key::KpEnter:
          if (!enterOpens_ || !plain) return false;
          open();
          return true;
        default: return false;
      }
    }
    case InputType::MouseMove: {
      const int h = itemAt(e.x, e.y);
      const bool box = ui::contains(bounds(), e.x, e.y);
      bool dirty = box != hoverBox_;
      hoverBox_ = box;
      if (h >= 0 && h != highlight_ && items_[static_cast<size_t>(h)].enabled) {
        highlight_ = h;
        dirty = true;
      }
      if (dirty) markDirty();
      return true;
    }
    case InputType::MouseDown:
      if (e.button != MouseButton::Left) return false;
      if (open_) {
        if (const int i = itemAt(e.x, e.y); i >= 0) {
          if (items_[static_cast<size_t>(i)].enabled) choose(i);
        } else {
          closeList();  // the box again (or the border of the list)
        }
      } else {
        open();
      }
      return true;
    case InputType::MouseUp: return open_ || ui::contains(bounds(), e.x, e.y);
    default: return false;
  }
}

void DropDown::paint(PaintContext& ctx) {
  const Theme& t = ctx.theme;
  const RectF& b = bounds();
  const Color bg = Theme::orElse(t.inputBackground, t.panelBackground);
  const Color border = hasFocus() ? t.tabAccent : Theme::orElse(t.inputBorder, t.splitter);
  auto frame = [&](const RectF& r, Color c) {
    ctx.fill({r.x, r.y, r.w, 1}, c);
    ctx.fill({r.x, r.y + r.h - 1, r.w, 1}, c);
    ctx.fill({r.x, r.y, 1, r.h}, c);
    ctx.fill({r.x + r.w - 1, r.y, 1, r.h}, c);
  };
  ctx.fill(b, hoverBox_ && !hasFocus() ? t.tabHover : bg);
  frame(b, border);
  const float cell = ctx.textRenderer.cellWidth(), ty = b.y + (b.h - ctx.lineHeight()) / 2;
  const float arrowW = 2 * cell;
  ctx.text(b.x + t.padding, ty, selectedLabel(), t.text, {b.x, b.y, std::max(0.0f, b.w - arrowW - t.padding), b.h});
  const float ax = b.x + b.w - arrowW / 2 - 4, ay = b.y + b.h / 2;  // chevron
  ctx.line(ax - 4, ay - 2, ax, ay + 2, 1.5f, t.text);
  ctx.line(ax, ay + 2, ax + 4, ay - 2, 1.5f, t.text);
  if (!open_) return;
  const RectF l = listRect();
  ctx.fill(l, Theme::orElse(t.popupBackground, t.tabInactive));
  frame(l, Theme::orElse(t.popupBorder, t.splitterHot));
  for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
    const DropDownItem& it = items_[static_cast<size_t>(i)];
    const RectF r = itemRect(i);
    if (i == highlight_ && it.enabled) ctx.fill(r, t.selection);
    ctx.text(r.x + t.padding, r.y + (r.h - ctx.lineHeight()) / 2, it.label, it.enabled ? t.text : t.textDim, r);
  }
}

}  // namespace tf::ui
