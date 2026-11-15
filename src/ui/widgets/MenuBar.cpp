#include "ui/widgets/MenuBar.h"

#include <cctype>

namespace tf::ui {

void MenuBar::setMenus(std::vector<Menu> menus) {
  leave();
  menus_ = std::move(menus);
  if (RootWidget* r = root()) r->requestLayout();
}

void MenuBar::setHidden(bool h) {
  if (hidden_ == h) return;
  hidden_ = h;
  revealed_ = false;
  if (RootWidget* r = root()) r->requestLayout();
}

float MenuBar::barHeight() const {
  const RootWidget* r = root();
  return r && !menus_.empty() ? r->text().lineHeight() + 2 * theme().verticalPad : 0;
}

float MenuBar::reservedHeight() const { return hidden_ ? 0 : barHeight(); }

void MenuBar::reveal(bool on) {
  if (!hidden_ || revealed_ == on || active()) return;
  revealed_ = on;
  if (RootWidget* r = root()) r->requestLayout();
}

RectF MenuBar::titleRect(int i) const {
  const RootWidget* r = root();
  if (!r || i < 0 || i >= static_cast<int>(menus_.size())) return {0, 0, 0, 0};
  const RectF& b = bounds();
  float x = b.x;
  for (int k = 0; k <= i; ++k) {
    const float w = r->text().measure(menus_[static_cast<size_t>(k)].title) + 2 * theme().padding;
    if (k == i) return {x, b.y, w, b.h};
    x += w;
  }
  return {0, 0, 0, 0};
}

int MenuBar::titleAt(float x, float y) const {
  if (!visible()) return -1;
  for (int i = 0; i < static_cast<int>(menus_.size()); ++i)
    if (ui::contains(titleRect(i), x, y)) return i;
  return -1;
}

ContextMenu* MenuBar::dropdown() const {
  RootWidget* r = root();
  return menu_ && r && r->modal() == menu_ ? menu_ : nullptr;
}

int MenuBar::openIndex() const { return dropdown() ? open_ : -1; }

void MenuBar::setActive() {
  const bool now = active();
  if (now == wasActive_) return;
  wasActive_ = now;
  if (hidden_ && !now) revealed_ = false;
  if (RootWidget* r = root()) r->requestLayout();
  if (onActiveChanged) onActiveChanged();
}

void MenuBar::open(int i, bool keyboard) {
  RootWidget* r = root();
  if (!r || i < 0 || i >= static_cast<int>(menus_.size())) return;
  if (ContextMenu* cur = dropdown()) cur->close();
  open_ = -1;
  std::vector<MenuItem> items =
      menus_[static_cast<size_t>(i)].build ? menus_[static_cast<size_t>(i)].build() : std::vector<MenuItem>{};
  if (items.empty()) {
    focus_ = i;
    setActive();
    return;
  }
  auto m = std::make_unique<ContextMenu>(std::move(items));
  ContextMenu* raw = m.get();
  const RectF t = titleRect(i);
  raw->outsideFilter = [this](const InputEvent& e) { return outside(e); };
  raw->onSideKey = [this](int dir) {
    const int n = static_cast<int>(menus_.size());
    open(((open_ + dir) % n + n) % n, true);
  };
  raw->onClosed([this, raw] {
    if (menu_ == raw) {
      menu_ = nullptr;
      open_ = -1;
      focus_ = -1;
      setActive();
    }
  });
  open_ = i;
  focus_ = -1;
  menu_ = ContextMenu::open(*r, std::move(m), {t.x, t.y, t.w, t.h}, keyboard);
  wasActive_ = false;
  setActive();
  r->requestLayout();
}

bool MenuBar::outside(const InputEvent& e) {
  const int t = titleAt(e.x, e.y);
  if (e.type == InputType::MouseMove) {
    if (t >= 0 && t != open_) {
      open(t, false);
      return true;
    }
    return false;
  }
  if (e.type == InputType::MouseDown && t >= 0) {
    if (t == open_) {
      if (ContextMenu* cur = dropdown()) cur->close();
    } else {
      open(t, false);
    }
    return true;
  }
  return false;
}

bool MenuBar::hasAccess(char letter) const {
  const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
  for (const Menu& m : menus_)
    if (m.access && std::tolower(static_cast<unsigned char>(m.access)) == c) return true;
  return false;
}

void MenuBar::openByAccess(char letter) {
  const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
  for (int i = 0; i < static_cast<int>(menus_.size()); ++i)
    if (menus_[static_cast<size_t>(i)].access &&
        std::tolower(static_cast<unsigned char>(menus_[static_cast<size_t>(i)].access)) == c) {
      open(i, true);
      return;
    }
}

void MenuBar::focus(int i) {
  if (menus_.empty()) return;
  if (ContextMenu* cur = dropdown()) cur->close();
  focus_ = i < 0 ? 0 : i % static_cast<int>(menus_.size());
  setActive();
  markDirty();
}

void MenuBar::leave() {
  if (ContextMenu* cur = dropdown()) cur->close();
  focus_ = -1;
  open_ = -1;
  setActive();
  markDirty();
}

bool MenuBar::handleKey(const InputEvent& e) {
  if (focus_ < 0 || e.type != InputType::KeyDown) return false;
  const int n = static_cast<int>(menus_.size());
  switch (e.key) {
    case Key::Left:
    case Key::Right:
      focus_ = ((focus_ + (e.key == Key::Left ? -1 : 1)) % n + n) % n;
      markDirty();
      return true;
    case Key::Down:
    case Key::Up:
    case Key::Enter:
    case Key::KpEnter: open(focus_, true); return true;
    case Key::Escape: leave(); return true;
    default: break;
  }
  if (e.key == Key::Unknown) return true;  // a modifier going down
  if (!(e.mods & (ModCtrl | ModShift)) && e.key >= static_cast<Key>('A') && e.key <= static_cast<Key>('Z') &&
      hasAccess(static_cast<char>(e.key))) {
    openByAccess(static_cast<char>(e.key));
    return true;
  }
  leave();
  return false;
}

bool MenuBar::onEvent(const InputEvent& e) {
  if (e.type == InputType::MouseMove) {
    const int t = titleAt(e.x, e.y);
    if (t != hot_) {
      hot_ = t;
      markDirty();
    }
    if (focus_ >= 0 && t >= 0 && t != focus_) {
      focus_ = t;
      markDirty();
    }
    return t >= 0;
  }
  if (e.type == InputType::MouseDown && e.button == MouseButton::Left) {
    const int t = titleAt(e.x, e.y);
    if (t < 0) return false;
    open(t, false);
    return true;
  }
  return e.type == InputType::MouseDown || e.type == InputType::MouseUp ? titleAt(e.x, e.y) >= 0 : false;
}

void MenuBar::onHover(bool entered) {
  if (!entered && hot_ >= 0) {
    hot_ = -1;
    markDirty();
  }
}

void MenuBar::paint(PaintContext& ctx) {
  if (menus_.empty() || !revealed()) return;
  const Theme& t = ctx.theme;
  const RectF& b = bounds();
  const Color bg = Theme::orElse(t.menuBarBackground, t.background);
  const Color fg = Theme::orElse(t.menuBarText, t.text);
  const Color hover = Theme::orElse(t.menuBarHover, t.tabHover);
  const Color pressed = Theme::orElse(t.menuBarPressed, Theme::orElse(t.menuBackground, t.popupBackground));
  ctx.fill(b, bg);
  const int open = openIndex();
  const int kb = open >= 0 ? -1 : focus_;
  for (int i = 0; i < static_cast<int>(menus_.size()); ++i) {
    const RectF r = titleRect(i);
    if (i == open) ctx.fill(r, pressed);
    else if (i == kb || (i == hot_ && !active())) ctx.fill(r, hover);
    const Menu& m = menus_[static_cast<size_t>(i)];
    const float ty = r.y + (r.h - ctx.lineHeight()) / 2;
    const float tx = r.x + t.padding;
    ctx.text(tx, ty, m.title, fg, r);
    if (showAccess() && m.access) {  // the access key, underlined
      const size_t at = m.title.find(m.access);
      if (at != std::string::npos) {
        const float x0 = tx + ctx.measure(m.title.substr(0, at));
        const float w = ctx.measure(m.title.substr(at, 1));
        ctx.fill({x0, ty + ctx.lineHeight() - 1, w, 1}, fg);
      }
    }
  }
}

}  // namespace tf::ui
