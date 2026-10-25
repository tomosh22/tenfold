#include "ui/widgets/TextView.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "ui/platform/Clipboard.h"

namespace tf::ui {

namespace {
Color mix(Color a, Color b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1};
}
}  // namespace

Layout TextView::geometry() const { return Layout(root()->text().textLayout()); }

int64_t TextView::now() const {
  if (settings_.clock) return settings_.clock();
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

RectF TextView::textArea() const {
  const Layout L = geometry();
  const Document* d = document();
  const float gw = L.gutterWidth(d ? d->buffer().lineCount() : 1);
  return intersect({bounds().x + gw, bounds().y, bounds().w - gw, bounds().h}, bounds());
}

int TextView::visibleLineCount() const {
  if (!root()) return 1;
  return std::max(1, static_cast<int>(bounds().h / root()->text().lineHeight()));
}

Position TextView::positionAt(float x, float y) const {
  const Document* d = document();
  if (!d || !root()) return {0, 0};
  const Layout L = geometry();
  const TextBuffer& b = d->buffer();
  const int line = L.lineAt(y - bounds().y, std::floor(vp_.y()), b.lineCount());
  const float lx = x - (bounds().x + L.textOffset(b.lineCount())) + std::floor(vp_.x());
  return {line, static_cast<int>(L.byteForX(b.line(line), lx))};
}

RectF TextView::caretRect(Position p) const {
  const Document* d = document();
  if (!d || !root()) return {0, 0, 0, 0};
  const Layout L = geometry();
  const TextBuffer& b = d->buffer();
  p = b.clamp(p);
  const float w = std::max(1.0f, std::floor(L.cellWidth() / 6));
  return {bounds().x + L.textOffset(b.lineCount()) - std::floor(vp_.x()) + L.xForByte(b.line(p.line), p.column),
          bounds().y + L.lineTop(p.line, std::floor(vp_.y())), w, L.lineHeight()};
}

std::string TextView::positionText() const {
  const Document* d = document();
  if (!d || !isCurrent() || !root()) return {};
  const Position p = editor_.selections()[0].head;
  return "Ln " + std::to_string(p.line + 1) + ", Col " +
         std::to_string(geometry().columnForByte(d->buffer().line(p.line), static_cast<size_t>(p.column)) + 1);
}

float TextView::maxScrollY() const {
  const Document* d = document();
  if (!d || !root()) return 0;
  return std::max(0.0f, static_cast<float>(d->buffer().lineCount()) * root()->text().lineHeight() - bounds().h * 0.5f);
}

void TextView::onLayout() {
  if (isCurrent()) editor_.setPageLines(visibleLineCount());
  if (pendingEnsure_) ensureCaretVisible(false);
}

void TextView::onFocusChanged(bool) {
  if (isCurrent()) editor_.setPageLines(visibleLineCount());
  caretOn_ = true;
  activityMs_ = now();
  markDirty();
}

void TextView::activate() {
  Document* d = document();
  if (!d) return;
  if (d != editor_.currentDocument()) editor_.gotoLocation({d->path(), lastCaret_});
  setFocus();
  editor_.setPageLines(visibleLineCount());
  syncState();
}

void TextView::ensureCaretVisible(bool animate) {
  if (!root() || !isCurrent() || bounds().h <= 0) {
    pendingEnsure_ = true;
    return;
  }
  pendingEnsure_ = false;
  const Layout L = geometry();
  const TextBuffer& b = document()->buffer();
  const Position p = b.clamp(editor_.selections()[0].head);
  const float lh = L.lineHeight(), cw = L.cellWidth(), h = bounds().h;
  const float margin = std::max(0.0f, std::min(2 * lh, std::floor((h - lh) / 2)));
  const float top = static_cast<float>(p.line) * lh;
  float ty = vp_.targetY();
  if (top - margin < ty) ty = top - margin;
  else if (top + lh + margin > ty + h) ty = top + lh + margin - h;
  ty = std::clamp(ty, 0.0f, maxScrollY());
  const float visW = bounds().w - L.textOffset(b.lineCount()), cx = L.xForByte(b.line(p.line), p.column);
  const float mx = std::min(3 * cw, visW / 4);
  float tx = vp_.targetX();
  if (cx - mx < tx) tx = cx - mx;
  else if (cx + cw + mx > tx + visW) tx = cx + cw + mx - visW;
  vp_.scrollTo(std::max(0.0f, tx), ty, animate && settings_.smoothScroll);
  markDirty();
}

void TextView::scrollBy(float dx, float dy) {
  const Document* d = document();
  if (!d || !root()) return;
  const Layout L = geometry();
  const TextBuffer& b = d->buffer();
  int first, last;
  L.visibleLines(vp_.targetY(), bounds().h, b.lineCount(), first, last);
  float widest = 0;
  for (int i = first; i < last; ++i) widest = std::max(widest, L.lineWidth(b.line(i)));
  const float maxX = std::max(0.0f, widest + 2 * L.cellWidth() - (bounds().w - L.textOffset(b.lineCount())));
  vp_.scrollTo(std::clamp(vp_.targetX() + dx, 0.0f, std::max(maxX, vp_.targetX())),
               std::clamp(vp_.targetY() + dy, 0.0f, maxScrollY()), settings_.smoothScroll);
  markDirty();
}

// Compares the Editor with what was last seen; on a change keeps the caret visible and requests a repaint.
bool TextView::syncState() {
  const Document* d = document();
  const bool cur = isCurrent();
  const int ver = d ? d->version() : -1;
  const bool docChanged = d != seenDoc_, selChanged = cur && editor_.selections() != seenSel_;
  if (!docChanged && !selChanged && ver == seenVersion_) return false;
  seenDoc_ = d;
  seenVersion_ = ver;
  if (cur) {
    seenSel_ = editor_.selections();
    lastCaret_ = seenSel_[0].head;
  } else {
    seenSel_.clear();
  }
  if (docChanged) vp_.jumpTo(0, 0);
  if (cur) ensureCaretVisible(!docChanged);
  caretOn_ = true;
  activityMs_ = now();
  markDirty();
  if (onStateChanged) onStateChanged();
  return true;
}

int TextView::onTick(int64_t nowMs) {
  syncState();
  int wait = -1;
  if (vp_.animating()) {
    const float dt = (lastTick_ < 0 || nowMs - lastTick_ > 100) ? 16.0f : static_cast<float>(nowMs - lastTick_);
    lastTick_ = nowMs;
    if (vp_.step(std::max(dt, 1.0f))) markDirty();
    if (vp_.animating()) wait = 16;
  } else {
    lastTick_ = -1;
  }
  if (settings_.caretBlink && hasFocus() && isCurrent()) {
    const int64_t ms = std::max(50, settings_.caretBlinkMs), el = std::max<int64_t>(0, nowMs - activityMs_);
    const bool on = (el / ms) % 2 == 0;
    if (on != caretOn_) {
      caretOn_ = on;
      markDirty();
    }
    const int next = static_cast<int>(ms - el % ms);
    wait = wait < 0 ? next : std::min(wait, next);
  }
  return wait;
}

void TextView::paint(PaintContext& ctx) {
  const RectF& b = bounds();
  const Theme& th = ctx.theme;
  ctx.fill(b, th.panelBackground);
  const Document* d = document();
  if (!d) return;
  const Layout L = geometry();
  const TextBuffer& buf = d->buffer();
  const int n = buf.lineCount();
  const float lh = L.lineHeight(), cw = L.cellWidth(), sx = std::floor(vp_.x()), sy = std::floor(vp_.y());
  const float gw = L.gutterWidth(n), textX = b.x + L.textOffset(n) - sx;
  const RectF clip = textArea(), gutterClip = intersect({b.x, b.y, gw, b.h}, b);
  int first, last;
  L.visibleLines(sy, b.h, n, first, last);
  ctx.fill(gutterClip, th.gutterBackground);

  const bool cur = isCurrent();
  if (cur) {
    const std::vector<Selection>& sels = editor_.selections();
    const int headLine = sels[0].head.line;
    if (settings_.highlightCurrentLine && headLine >= first && headLine < last)
      ctx.fill(intersect({clip.x, b.y + L.lineTop(headLine, sy), clip.w, lh}, clip),
               mix(th.panelBackground, th.text, 0.05f));
    Color selColor = th.tabAccent;
    selColor.a = 0.38f;
    for (const Selection& s : sels) {
      if (s.empty()) continue;
      const Range r = s.range();
      if (r.end.line < first || r.start.line >= last) continue;
      for (int l = std::max(first, r.start.line); l <= std::min(last - 1, r.end.line); ++l) {
        const std::string& text = buf.line(l);
        const float x0 = l == r.start.line ? L.xForByte(text, static_cast<size_t>(r.start.column)) : 0,
                    x1 = l == r.end.line ? L.xForByte(text, static_cast<size_t>(r.end.column)) : L.lineWidth(text) + cw;
        const RectF rc = intersect({textX + x0, b.y + L.lineTop(l, sy), x1 - x0, lh}, clip);
        if (rc.w > 0) ctx.fill(rc, selColor);
      }
    }
  }
  for (int i = first; i < last; ++i) {
    const float y = b.y + L.lineTop(i, sy);
    number_ = std::to_string(i + 1);
    ctx.text(b.x + gw - cw - static_cast<float>(number_.size()) * cw, y, number_, th.gutterText, gutterClip);
    ctx.text(textX, y, buf.line(i), th.text, clip);
  }
}

// Carets go in the overlay layer: all text is emitted after all rects, so they would otherwise be hidden by it.
void TextView::paintOverlay(PaintContext& ctx) {
  if (!isCurrent() || !(caretOn_ || !settings_.caretBlink || !hasFocus())) return;
  const Layout L = geometry();
  const TextBuffer& buf = document()->buffer();
  const float sy = std::floor(vp_.y()), sx = std::floor(vp_.x());
  int first, last;
  L.visibleLines(sy, bounds().h, buf.lineCount(), first, last);
  const RectF clip = textArea();
  const float w = std::max(1.0f, std::floor(L.cellWidth() / 6)), x0 = bounds().x + L.textOffset(buf.lineCount()) - sx;
  const Color c = hasFocus() ? ctx.theme.text : ctx.theme.textDim;
  for (const Selection& s : editor_.selections()) {
    const Position p = s.head;
    if (p.line < first || p.line >= last) continue;
    const RectF rc = intersect({x0 + L.xForByte(buf.line(p.line), static_cast<size_t>(p.column)),
                                bounds().y + L.lineTop(p.line, sy), w, L.lineHeight()},
                               clip);
    if (rc.w > 0) ctx.fill(rc, c);
  }
}

bool TextView::onEvent(const InputEvent& e) {
  if (!document() || !root()) return false;
  bool handled = false;
  switch (e.type) {
    case InputType::Wheel: {
      const Layout L = geometry();
      const float notch = static_cast<float>(settings_.wheelLines);
      if (e.mods & ModShift) scrollBy(-e.dy * notch * L.cellWidth(), 0);
      else scrollBy(-e.dx * notch * L.cellWidth(), -e.dy * notch * L.lineHeight());
      return true;
    }
    case InputType::MouseDown:
      handled = e.button == MouseButton::Left;
      if (handled) mouseDown(e);
      break;
    case InputType::MouseMove:
      handled = hasCapture() && drag_ != Drag::None;
      if (handled) mouseDrag(e);
      break;
    case InputType::MouseUp:
      handled = e.button == MouseButton::Left;
      drag_ = Drag::None;
      break;
    case InputType::KeyDown:
      if (!isCurrent()) activate();
      handled = applyBuiltinKey(e);
      break;
    case InputType::Text:
      if (!isCurrent()) activate();
      if (!e.text.empty()) editor_.insertText(e.text);
      handled = !e.text.empty();
      break;
    default: break;
  }
  if (handled) {
    caretOn_ = true;
    activityMs_ = now();
    markDirty();
    syncState();
  }
  return handled;
}

void TextView::mouseDown(const InputEvent& e) {
  if (!isCurrent()) activate();
  const TextBuffer& b = document()->buffer();
  const Position pos = b.clamp(positionAt(e.x, e.y));
  const bool shift = e.mods & ModShift, ctrl = e.mods & ModCtrl, alt = e.mods & ModAlt;
  drag_ = Drag::Char;
  anchor_ = anchorEnd_ = lastDragPos_ = pos;
  base_.clear();
  if (e.clicks >= 3) {  // whole line including its break (the last line: up to its end)
    anchor_ = {pos.line, 0};
    anchorEnd_ = pos.line + 1 < b.lineCount() ? Position{pos.line + 1, 0}
                                              : Position{pos.line, static_cast<int>(b.line(pos.line).size())};
    editor_.setSelections({{anchor_, anchorEnd_}});
  } else if (e.clicks == 2) {
    const Range w = b.wordAt(pos).range;
    anchor_ = w.start;
    anchorEnd_ = w.end;
    editor_.setSelections({{anchor_, anchorEnd_}});
  } else if (shift) {
    anchor_ = anchorEnd_ = editor_.selections()[0].anchor;
    editor_.setSelections({{anchor_, pos}});
  } else if (ctrl || alt) {  // add a caret; dragging then grows it (Ctrl) or turns into a column selection (Alt)
    base_ = editor_.selections();
    drag_ = alt ? Drag::Column : Drag::AddCaret;
    std::vector<Selection> v{caretAt(pos)};
    v.insert(v.end(), base_.begin(), base_.end());
    editor_.setSelections(std::move(v));
  } else {
    editor_.setSelections({caretAt(pos)});
  }
  captureMouse();
}

void TextView::mouseDrag(const InputEvent& e) {
  const Position pos = document()->buffer().clamp(positionAt(e.x, e.y));
  if (pos == lastDragPos_) return;
  lastDragPos_ = pos;
  if (drag_ == Drag::Column) {
    editor_.columnSelect(anchor_, pos);
  } else if (drag_ == Drag::AddCaret) {
    std::vector<Selection> v{{anchor_, pos}};
    v.insert(v.end(), base_.begin(), base_.end());
    editor_.setSelections(std::move(v));
  } else if (pos < anchor_) {  // grow from the clicked word / line (or point) in either direction
    editor_.setSelections({{anchorEnd_, pos}});
  } else if (anchorEnd_ < pos) {
    editor_.setSelections({{anchor_, pos}});
  } else {
    editor_.setSelections({{anchor_, anchorEnd_}});
  }
}

void TextView::copySelection() {
  std::string t = editor_.selectedText();
  if (t.empty()) t = document()->buffer().line(editor_.selections()[0].head.line) + "\n";  // no selection: the line
  clipboardSet(t);
}

// TEMPORARY fixed key map (B4's command system replaces this function). Every action is an Editor command.
bool TextView::applyBuiltinKey(const InputEvent& e) {
  using D = Editor::Direction;
  using U = Editor::Unit;
  const bool shift = e.mods & ModShift, ctrl = e.mods & ModCtrl, alt = e.mods & ModAlt;
  auto anySelection = [&] {
    for (const Selection& s : editor_.selections())
      if (!s.empty()) return true;
    return false;
  };
  switch (e.key) {
    case Key::Left: editor_.moveCarets(D::Left, ctrl ? U::Word : U::Char, shift); return true;
    case Key::Right: editor_.moveCarets(D::Right, ctrl ? U::Word : U::Char, shift); return true;
    case Key::Up:
      if (ctrl && alt) editor_.addCaretAbove();
      else editor_.moveCarets(D::Up, U::Line, shift);
      return true;
    case Key::Down:
      if (ctrl && alt) editor_.addCaretBelow();
      else editor_.moveCarets(D::Down, U::Line, shift);
      return true;
    case Key::Home: editor_.moveCarets(ctrl ? D::DocStart : D::LineStart, U::Char, shift); return true;
    case Key::End: editor_.moveCarets(ctrl ? D::DocEnd : D::LineEnd, U::Char, shift); return true;
    case Key::PageUp: editor_.moveCarets(D::PageUp, U::Page, shift); return true;
    case Key::PageDown: editor_.moveCarets(D::PageDown, U::Page, shift); return true;
    case Key::Backspace:
      if (ctrl && !anySelection()) editor_.moveCarets(D::Left, U::Word, true);
      editor_.backspace();
      return true;
    case Key::Delete:
      if (ctrl && !anySelection()) editor_.moveCarets(D::Right, U::Word, true);
      editor_.del();
      return true;
    case Key::Enter:
    case Key::KpEnter: editor_.insertText("\n"); return true;
    case Key::Tab: editor_.insertText("\t"); return true;
    case Key::Escape:
      if (editor_.selections().size() < 2) return false;
      editor_.collapseToPrimary();
      return true;
    default: break;
  }
  if (!ctrl) return false;
  const TextBuffer& b = document()->buffer();
  switch (static_cast<uint32_t>(e.key)) {
    case 'A':
      editor_.setSelections({{{0, 0}, {b.lineCount() - 1, static_cast<int>(b.line(b.lineCount() - 1).size())}}});
      return true;
    case 'Z': shift ? editor_.redo() : editor_.undo(); return true;
    case 'Y': editor_.redo(); return true;
    case 'S': editor_.save(*document()); return true;
    case 'C': copySelection(); return true;
    case 'X': {
      copySelection();
      if (anySelection()) editor_.backspace();
      else {
        const int l = editor_.selections()[0].head.line;
        editor_.deleteRange(l + 1 < b.lineCount() ? Range{{l, 0}, {l + 1, 0}}
                                                  : Range{{l, 0}, {l, static_cast<int>(b.line(l).size())}});
      }
      return true;
    }
    case 'V': {
      std::string t = clipboardGet();
      t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());
      if (!t.empty()) editor_.insertText(t);
      return true;
    }
    default: return false;
  }
}

}  // namespace tf::ui
