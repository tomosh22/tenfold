#include "ui/features/NavigationUi.h"

#include <algorithm>

namespace tf::ui {

namespace {

const char* commandOf(NavigationUi::Which w) { return w == NavigationUi::Which::Back ? "Nav.Back" : "Nav.Forward"; }

struct OverlayText {
  float x, y;
  std::string s;
  Color c;
  RectF clip;
};

// Overlay rects are drawn after the frame's text was flushed, so text that belongs on top of them is flushed again
// right here (a growing atlas drops the first attempt; the second cannot grow again).
void emitText(PaintContext& ctx, const std::vector<OverlayText>& texts) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    for (const OverlayText& t : texts) ctx.text(t.x, t.y, t.s, t.c, t.clip);
    if (ctx.textRenderer.flushText(ctx.renderer)) return;
  }
}

std::string fileLine(const Location& l) { return l.file.filename().string() + ":" + std::to_string(l.pos.line + 1); }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Toolbar: two buttons
// ---------------------------------------------------------------------------------------------------------------

class NavigationUi::Toolbar : public Widget {
 public:
  explicit Toolbar(NavigationUi& u) : ui_(u) {}

  RectF button(Which w) const {
    const RectF& b = bounds();
    const float pad = 4, size = b.h - 2 * pad, gap = 2;
    return {b.x + pad + (w == Which::Back ? 0 : size + gap), b.y + pad, size, size};
  }
  void stateChanged() { markDirty(); }

  bool onEvent(const InputEvent& e) override {
    switch (e.type) {
      case InputType::MouseMove: {
        const int h = hit(e.x, e.y);
        if (h != hover_) {
          hover_ = h;
          hoverMs_ = ui_.now_;
          ui_.shownTip_.clear();
          markDirty();
        }
        return false;
      }
      case InputType::MouseDown: {
        const int h = hit(e.x, e.y);
        if (h < 0) return false;
        const Which w = static_cast<Which>(h);
        ui_.shownTip_.clear();
        if (e.button == MouseButton::Right) {
          ui_.openDropdown(w);
          return true;
        }
        if (e.button != MouseButton::Left) return false;
        pressed_ = h;
        pressMs_ = ui_.now_;
        longFired_ = false;
        captureMouse();
        markDirty();
        return true;
      }
      case InputType::MouseUp: {
        if (pressed_ < 0 || e.button != MouseButton::Left) return false;
        const int p = pressed_;
        pressed_ = -1;
        markDirty();
        if (!longFired_ && hit(e.x, e.y) == p && ui_.enabled(static_cast<Which>(p)))
          ui_.hooks_.run(commandOf(static_cast<Which>(p)));
        return true;
      }
      default: return false;
    }
  }
  void onHover(bool entered) override {
    if (entered) return;
    hover_ = -1;
    ui_.shownTip_.clear();
    markDirty();
  }

  int wait(int64_t now) {
    ui_.now_ = now;
    int wait = -1;
    auto soonest = [&](int64_t ms) {
      const int v = static_cast<int>(std::max<int64_t>(1, ms));
      wait = wait < 0 ? v : std::min(wait, v);
    };
    if (pressed_ >= 0 && !longFired_) {
      if (now - pressMs_ >= kLongPressMs) {
        longFired_ = true;
        ui_.openDropdown(static_cast<Which>(pressed_));
      } else {
        soonest(pressMs_ + kLongPressMs - now);
      }
    }
    if (hover_ >= 0 && pressed_ < 0 && !ui_.open_ && ui_.shownTip_.empty()) {
      if (now - hoverMs_ >= kTooltipMs) {
        ui_.shownTip_ = ui_.tooltip(static_cast<Which>(hover_));
        markDirty();
      } else {
        soonest(hoverMs_ + kTooltipMs - now);
      }
    }
    return wait;
  }

 protected:
  void paint(PaintContext& ctx) override {
    const RectF& b = bounds();
    ctx.fill(b, ctx.theme.tabStripBackground);
    ctx.fill({b.x, b.y + b.h - 1, b.w, 1}, ctx.theme.panelBorder);
    for (Which w : {Which::Back, Which::Forward}) {
      const RectF r = button(w);
      const bool on = ui_.enabled(w), isHover = hover_ == static_cast<int>(w), isDown = pressed_ == static_cast<int>(w);
      if (on && (isHover || isDown)) ctx.fill(r, isDown ? ctx.theme.tabActive : ctx.theme.tabHover);
      const char* glyph = w == Which::Back ? "\xE2\x86\x90" : "\xE2\x86\x92";  // left / right arrow
      ctx.text(r.x + (r.w - ctx.measure(glyph)) / 2, r.y + (r.h - ctx.lineHeight()) / 2, glyph,
               on ? ctx.theme.text : ctx.theme.textDim, r);
    }
  }
  void paintOverlay(PaintContext& ctx) override {
    if (ui_.shownTip_.empty() || hover_ < 0) return;
    const RectF r = button(static_cast<Which>(hover_));
    const float w = ctx.measure(ui_.shownTip_) + 2 * ctx.theme.padding, h = ctx.lineHeight() + ctx.theme.verticalPad;
    const RectF tip{r.x, r.y + r.h + 2, w, h};
    ctx.fill({tip.x - 1, tip.y - 1, tip.w + 2, tip.h + 2}, ctx.theme.panelBorder);
    ctx.fill(tip, ctx.theme.tabInactive);
    emitText(ctx, {{tip.x + ctx.theme.padding, tip.y + ctx.theme.verticalPad / 2, ui_.shownTip_, ctx.theme.text, tip}});
  }

 private:
  int hit(float x, float y) const {
    for (Which w : {Which::Back, Which::Forward})
      if (ui::contains(button(w), x, y)) return static_cast<int>(w);
    return -1;
  }
  NavigationUi& ui_;
  int hover_ = -1, pressed_ = -1;
  int64_t hoverMs_ = 0, pressMs_ = 0;
  bool longFired_ = false;
};

// ---------------------------------------------------------------------------------------------------------------
// Dropdown: a transparent full-window layer (so a click anywhere else closes it) with the list drawn on top
// ---------------------------------------------------------------------------------------------------------------

class NavigationUi::Dropdown : public Widget {
 public:
  explicit Dropdown(NavigationUi& u) : ui_(u) {}
  bool acceptsFocus() const override { return true; }
  void reset() { sel_ = -1; }

  bool onEvent(const InputEvent& e) override {
    switch (e.type) {
      case InputType::MouseMove: {
        const int r = rowAt(e.x, e.y);
        if (r != sel_) {
          sel_ = r;
          markDirty();
        }
        return true;
      }
      case InputType::MouseDown: {
        const int r = rowAt(e.x, e.y);
        if (r >= 0 && e.button == MouseButton::Left) ui_.chooseRow(static_cast<size_t>(r));
        else ui_.closeDropdown();
        return true;
      }
      case InputType::MouseUp:
      case InputType::Wheel: return true;
      case InputType::KeyDown: {
        const int n = static_cast<int>(ui_.rows_.size());
        if (e.key == Key::Escape) ui_.closeDropdown();
        else if (e.key == Key::Down || e.key == Key::Up)
          sel_ = std::clamp(sel_ + (e.key == Key::Down ? 1 : -1), 0, n - 1);
        else if ((e.key == Key::Enter || e.key == Key::KpEnter) && sel_ >= 0) ui_.chooseRow(static_cast<size_t>(sel_));
        else return false;
        markDirty();
        return true;
      }
      default: return false;
    }
  }

 protected:
  void paintOverlay(PaintContext& ctx) override {
    const NavigationUi& u = ui_;
    ctx.fill({u.listRect_.x - 1, u.listRect_.y - 1, u.listRect_.w + 2, u.listRect_.h + 2}, ctx.theme.panelBorder);
    ctx.fill(u.listRect_, ctx.theme.tabInactive);
    std::vector<OverlayText> texts;
    for (size_t i = 0; i < u.rows_.size(); ++i) {
      const RectF r = u.rowRect(i);
      if (static_cast<int>(i) == sel_) ctx.fill(r, ctx.theme.tabHover);
      if (u.rows_[i].current) ctx.fill({r.x, r.y, ctx.theme.accentHeight, r.h}, ctx.theme.tabAccent);
      const float x = r.x + ctx.theme.padding, y = r.y + (r.h - ctx.lineHeight()) / 2;
      const std::string main = (u.rows_[i].current ? "> " : "  ") + u.rows_[i].text;
      texts.push_back({x, y, main, ctx.theme.text, r});
      if (!u.rows_[i].detail.empty())
        texts.push_back({x + ctx.measure(main) + ctx.measure("  "), y, u.rows_[i].detail, ctx.theme.textDim, r});
    }
    emitText(ctx, texts);
  }

 private:
  int rowAt(float x, float y) const {
    for (size_t i = 0; i < ui_.rows_.size(); ++i)
      if (ui::contains(ui_.rowRect(i), x, y)) return static_cast<int>(i);
    return -1;
  }
  NavigationUi& ui_;
  int sel_ = -1;
};

// ---------------------------------------------------------------------------------------------------------------
// NavigationUi
// ---------------------------------------------------------------------------------------------------------------

NavigationUi::NavigationUi(Workbench& bench, Editor& editor, Hooks hooks)
    : bench_(bench), editor_(editor), hooks_(std::move(hooks)) {
  barH_ = bench.text().lineHeight() + 2 * bench.rootTheme().verticalPad;
  bar_ = static_cast<Toolbar*>(bench.addChild(std::make_unique<Toolbar>(*this)));
  drop_ = static_cast<Dropdown*>(bench.addChild(std::make_unique<Dropdown>(*this)));
  drop_->setVisible(false);
  bench.setTopBar(bar_, barH_);
}

NavigationUi::~NavigationUi() {
  bench_.setTopBar(nullptr, 0);
  bench_.removeChild(drop_);
  bench_.removeChild(bar_);
}

bool NavigationUi::enabled(Which w) const {
  const NavigationHistory& n = editor_.navigation();
  return w == Which::Back ? n.canGoBack() : n.canGoForward();
}

std::string NavigationUi::tooltip(Which w) const {
  std::string t = w == Which::Back ? "Navigate Backward" : "Navigate Forward";
  const std::string sc = hooks_.shortcut ? hooks_.shortcut(commandOf(w)) : std::string();
  return sc.empty() ? t : t + " (" + sc + ")";
}

RectF NavigationUi::buttonRect(Which w) const { return bar_->button(w); }

int NavigationUi::tick(int64_t nowMs) {
  now_ = nowMs;
  const bool b = enabled(Which::Back), f = enabled(Which::Forward);
  if (b != lastBack_ || f != lastFwd_) {
    lastBack_ = b;
    lastFwd_ = f;
    bar_->stateChanged();
  }
  if (open_ && (drop_->bounds().w != bench_.bounds().w || drop_->bounds().h != bench_.bounds().h)) closeDropdown();
  return bar_->wait(nowMs);
}

size_t NavigationUi::currentIndex() const {
  const NavigationHistory& n = editor_.navigation();
  return std::min(n.cursor(), n.entries().size() - 1);
}

std::string NavigationUi::countsText() const {
  const NavigationHistory& n = editor_.navigation();
  const size_t size = n.entries().size();
  if (size == 0) return "\xE2\x86\x90 0 | 0 \xE2\x86\x92";
  const size_t fwd = n.cursor() < size ? size - 1 - n.cursor() : 0;
  return "\xE2\x86\x90 " + std::to_string(currentIndex()) + " | " + std::to_string(fwd) + " \xE2\x86\x92";
}

void NavigationUi::buildRows() {
  rows_.clear();
  const auto& entries = editor_.navigation().entries();
  if (entries.empty()) return;
  const size_t cur = currentIndex();
  auto add = [&](size_t i) {
    const NavigationHistory::Entry& e = entries[i];
    Row r;
    r.index = i;
    r.current = i == cur;
    r.text = e.label.empty() ? fileLine(e.loc) : e.label;
    if (!e.label.empty()) r.detail = fileLine(e.loc);
    rows_.push_back(std::move(r));
  };
  if (which_ == Which::Back) {
    for (size_t i = cur + 1; i-- > 0 && rows_.size() < static_cast<size_t>(kMaxRows);) add(i);
  } else {
    for (size_t i = cur; i < entries.size() && rows_.size() < static_cast<size_t>(kMaxRows); ++i) add(i);
  }
}

void NavigationUi::openDropdown(Which w) {
  if (!enabled(w)) return;
  which_ = w;
  buildRows();
  if (rows_.size() < 2) return;  // only the current entry: nothing to jump to
  const Theme& th = bench_.rootTheme();
  rowH_ = bench_.text().lineHeight() + th.verticalPad;
  float width = 0;
  for (const Row& r : rows_)
    width = std::max(width, bench_.text().measure("> " + r.text + (r.detail.empty() ? "" : "  " + r.detail)));
  width += 2 * th.padding;
  const RectF btn = buttonRect(w), win = bench_.bounds();
  width = std::min(width, win.w - 8);
  listRect_ = {std::clamp(btn.x, win.x + 4, std::max(win.x + 4, win.x + win.w - width - 4)), btn.y + btn.h + 2, width,
               rowH_ * static_cast<float>(rows_.size())};
  // Raise above everything the workbench re-created since (installTree appends its tree after us).
  bench_.addChild(bench_.removeChild(drop_));
  drop_->layout(win);
  drop_->reset();
  drop_->setVisible(true);
  prevFocus_ = bench_.focused();
  drop_->setFocus();
  shownTip_.clear();
  open_ = true;
  drop_->markDirty();
}

void NavigationUi::closeDropdown() {
  if (!open_) return;
  open_ = false;
  drop_->setVisible(false);  // drops focus
  // Give focus back if its widget is still in the tree.
  for (Widget* w = prevFocus_; w; w = w->parent())
    if (w == &bench_) {
      prevFocus_->setFocus();
      break;
    }
  prevFocus_ = nullptr;
  bench_.markDirty();
}

RectF NavigationUi::rowRect(size_t i) const {
  return {listRect_.x, listRect_.y + rowH_ * static_cast<float>(i), listRect_.w, rowH_};
}

void NavigationUi::chooseRow(size_t i) {
  if (i >= rows_.size()) return;
  const Row r = rows_[i];
  closeDropdown();
  if (!r.current) editor_.goToHistoryEntry(r.index);
}

}  // namespace tf::ui
