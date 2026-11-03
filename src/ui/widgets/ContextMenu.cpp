#include "ui/widgets/ContextMenu.h"

#include <algorithm>
#include <cctype>

namespace tf::ui {

namespace {
bool selectable(const MenuItem& m) { return !m.separator && m.enabled; }
char lowerFirst(const std::string& s) {
  for (unsigned char c : s)
    if (std::isalnum(c)) return static_cast<char>(std::tolower(c));
  return 0;
}
}  // namespace

ContextMenu::ContextMenu(std::vector<MenuItem> items) : items_(std::move(items)) {}

ContextMenu* ContextMenu::open(RootWidget& root, std::unique_ptr<ContextMenu> m, const RectF& anchor, bool keyboard) {
  if (!m || m->items_.empty()) return nullptr;
  ContextMenu* raw = m.get();
  raw->anchor_ = anchor;
  raw->levels_.push_back({&raw->items_, -1, anchor});
  if (keyboard) raw->levels_[0].sel = raw->step(0, -1, 1);
  root.showModal(std::move(m));
  return raw;
}

void ContextMenu::close() {
  RootWidget* r = root();
  if (!r || r->modal() != this) return;
  if (!closedFired_) {
    closedFired_ = true;
    r->closeModal();
    if (closed_) closed_();
  }
}

const MenuItem* ContextMenu::itemAt(size_t level, int i) const {
  if (level >= levels_.size() || i < 0 || i >= static_cast<int>(levels_[level].items->size())) return nullptr;
  return &(*levels_[level].items)[static_cast<size_t>(i)];
}

ContextMenu::Metrics ContextMenu::metrics(const std::vector<MenuItem>& items) const {
  const gfx::TextRenderer& t = root()->text();
  Metrics m;
  m.rowH = t.lineHeight() + theme().verticalPad;
  m.sepH = std::max(3.0f, t.lineHeight() / 2);
  m.pad = theme().padding;
  m.vpad = std::max(2.0f, theme().verticalPad / 2);
  float label = 0, key = 0;
  bool arrow = false;
  for (const MenuItem& it : items) {
    if (it.separator) continue;
    label = std::max(label, t.measure(it.label));
    key = std::max(key, t.measure(it.shortcut));
    arrow = arrow || !it.sub.empty();
  }
  const float cell = t.cellWidth();
  m.width = std::max(12 * cell, 2 * m.pad + label + (key > 0 ? 3 * cell + key : 0) + (arrow ? 2 * cell : 0));
  return m;
}

float ContextMenu::heightOf(const std::vector<MenuItem>& items, const Metrics& m) const {
  float h = 2 * m.vpad;
  for (const MenuItem& it : items) h += it.separator ? m.sepH : m.rowH;
  return h;
}

RectF ContextMenu::rectOf(size_t level) const {
  const RectF win = root() ? root()->bounds() : bounds();
  const Level& L = levels_[level];
  const Metrics m = metrics(*L.items);
  const float w = m.width, h = heightOf(*L.items, m);
  float x, y;
  if (level == 0) {
    x = L.anchor.x;
    y = L.anchor.y + L.anchor.h;
    if (x + w > win.x + win.w) x = win.x + win.w - w;
    if (y + h > win.y + win.h) y = L.anchor.y - h;  // no room below: above the anchor
  } else {
    const RectF parent = rectOf(level - 1);
    x = parent.x + parent.w - 2;
    if (x + w > win.x + win.w) x = parent.x - w + 2;
    y = L.anchor.y - m.vpad;
    if (y + h > win.y + win.h) y = win.y + win.h - h;
  }
  x = std::max(win.x, x);
  y = std::max(win.y, y);
  return {x, y, w, h};
}

RectF ContextMenu::menuRect(size_t level) const { return level < levels_.size() ? rectOf(level) : RectF{0, 0, 0, 0}; }

RectF ContextMenu::itemRect(size_t level, int i) const {
  if (!itemAt(level, i)) return {0, 0, 0, 0};
  const RectF r = rectOf(level);
  const Metrics m = metrics(*levels_[level].items);
  float y = r.y + m.vpad;
  for (int k = 0; k < i; ++k) y += (*levels_[level].items)[static_cast<size_t>(k)].separator ? m.sepH : m.rowH;
  const MenuItem& it = (*levels_[level].items)[static_cast<size_t>(i)];
  return {r.x, y, r.w, it.separator ? m.sepH : m.rowH};
}

bool ContextMenu::hit(float x, float y, size_t& level, int& item) const {
  for (size_t lv = levels_.size(); lv-- > 0;) {  // the innermost menu is on top
    if (!ui::contains(rectOf(lv), x, y)) continue;
    level = lv;
    item = -1;
    for (int i = 0; i < static_cast<int>(levels_[lv].items->size()); ++i)
      if (ui::contains(itemRect(lv, i), x, y)) item = i;
    return true;
  }
  return false;
}

int ContextMenu::step(size_t level, int from, int dir) const {
  const auto& v = *levels_[level].items;
  const int n = static_cast<int>(v.size());
  int i = from;
  for (int k = 0; k < n; ++k) {
    i = ((i + dir) % n + n) % n;
    if (selectable(v[static_cast<size_t>(i)])) return i;
  }
  return -1;
}

void ContextMenu::truncate(size_t d) {
  if (levels_.size() > d) levels_.resize(d);
}

void ContextMenu::setSelected(int i, size_t level) {
  if (level >= levels_.size()) return;
  const MenuItem* it = itemAt(level, i);
  const int sel = it && selectable(*it) ? i : -1;
  if (sel == levels_[level].sel && levels_.size() == level + 1) return;
  levels_[level].sel = sel;
  truncate(level + 1);
  markDirty();
}

void ContextMenu::openSub(size_t level) {
  const MenuItem* it = itemAt(level, levels_[level].sel);
  if (!it || it->sub.empty() || !it->enabled) return;
  truncate(level + 1);
  Level sub{&it->sub, -1, itemRect(level, levels_[level].sel)};
  levels_.push_back(sub);
  markDirty();
}

void ContextMenu::activate(size_t level, int i) {
  const MenuItem* it = itemAt(level, i);
  if (!it || !selectable(*it)) return;
  if (!it->sub.empty()) {
    levels_[level].sel = i;
    openSub(level);
    levels_.back().sel = step(levels_.size() - 1, -1, 1);
    return;
  }
  auto action = it->action;  // the action may open another popup, so the menu closes first
  close();
  if (action) action();
}

bool ContextMenu::onEvent(const InputEvent& e) {
  switch (e.type) {
    case InputType::KeyDown: {
      const size_t lv = levels_.size() - 1;
      Level& L = levels_[lv];
      switch (e.key) {
        case Key::Up:
        case Key::Down:
          L.sel = step(lv, L.sel < 0 && e.key == Key::Up ? 0 : L.sel, e.key == Key::Up ? -1 : 1);
          markDirty();
          return true;
        case Key::Home:
        case Key::End:
          L.sel = step(lv, e.key == Key::Home ? -1 : 0, e.key == Key::Home ? 1 : -1);
          markDirty();
          return true;
        case Key::Right:
          if (const MenuItem* it = itemAt(lv, L.sel); it && !it->sub.empty()) activate(lv, L.sel);
          return true;
        case Key::Left:
          if (lv > 0) {
            truncate(lv);
            markDirty();
          }
          return true;
        case Key::Enter:
        case Key::KpEnter:
        case Key::Space: activate(lv, L.sel); return true;
        case Key::Escape:
          if (lv > 0) {
            truncate(lv);
            markDirty();
          } else close();
          return true;
        default: return false;
      }
    }
    case InputType::Text: {
      if (e.text.size() != 1) return true;
      const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(e.text[0])));
      const size_t lv = levels_.size() - 1;
      const auto& v = *levels_[lv].items;
      const int n = static_cast<int>(v.size());
      for (int k = 1; k <= n; ++k) {
        const int i = ((levels_[lv].sel + k) % n + n) % n;
        if (selectable(v[static_cast<size_t>(i)]) && lowerFirst(v[static_cast<size_t>(i)].label) == c) {
          levels_[lv].sel = i;
          markDirty();
          break;
        }
      }
      return true;
    }
    case InputType::MouseMove: {
      size_t lv = 0;
      int it = -1;
      if (!hit(e.x, e.y, lv, it)) return true;
      const MenuItem* item = itemAt(lv, it);
      const int sel = item && selectable(*item) ? it : -1;
      const bool sameSub = sel >= 0 && sel == levels_[lv].sel && levels_.size() > lv + 1;
      if (sel != levels_[lv].sel || (levels_.size() > lv + 1 && !sameSub)) {
        levels_[lv].sel = sel;
        truncate(lv + 1);
        if (sel >= 0 && !item->sub.empty()) openSub(lv);
        markDirty();
      }
      return true;
    }
    case InputType::MouseDown: {
      size_t lv = 0;
      int it = -1;
      if (!hit(e.x, e.y, lv, it)) {
        close();
        return true;
      }
      if (e.button == MouseButton::Left && it >= 0) activate(lv, it);
      return true;
    }
    case InputType::MouseUp:
    case InputType::Wheel: return true;
    default: return false;
  }
}

void ContextMenu::paint(PaintContext& ctx) {
  const Theme& t = ctx.theme;
  const Color bg = Theme::orElse(t.menuBackground, Theme::orElse(t.popupBackground, t.tabInactive));
  const Color border = Theme::orElse(t.menuBorder, Theme::orElse(t.popupBorder, t.splitterHot));
  const Color hot = Theme::orElse(t.menuSelection, t.selection);
  const Color sepc = Theme::orElse(t.menuSeparator, t.splitter);
  const float cell = ctx.textRenderer.cellWidth();
  for (size_t lv = 0; lv < levels_.size(); ++lv) {
    const RectF r = rectOf(lv);
    ctx.fill({r.x - 1, r.y - 1, r.w + 2, r.h + 2}, border);
    ctx.fill(r, bg);
    const Metrics m = metrics(*levels_[lv].items);
    for (int i = 0; i < static_cast<int>(levels_[lv].items->size()); ++i) {
      const MenuItem& it = (*levels_[lv].items)[static_cast<size_t>(i)];
      const RectF ir = itemRect(lv, i);
      if (it.separator) {
        ctx.fill({ir.x + m.pad, ir.y + ir.h / 2, ir.w - 2 * m.pad, 1}, sepc);
        continue;
      }
      if (i == levels_[lv].sel) ctx.fill({ir.x + 1, ir.y, ir.w - 2, ir.h}, hot);
      const float ty = ir.y + (ir.h - ctx.lineHeight()) / 2;
      const Color fg = it.enabled ? t.text : t.textDim;
      ctx.text(ir.x + m.pad, ty, it.label, fg, ir);
      float right = ir.x + ir.w - m.pad;
      if (!it.sub.empty()) {
        ctx.text(right - ctx.measure(">"), ty, ">", fg, ir);
        right -= 2 * cell;
      }
      if (!it.shortcut.empty()) ctx.text(right - ctx.measure(it.shortcut), ty, it.shortcut, t.textDim, ir);
    }
  }
}

}  // namespace tf::ui
