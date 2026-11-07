#include "ui/features/LineEndingDialog.h"

#include <algorithm>

namespace tf::ui {

namespace {

std::vector<std::string> wrapText(const gfx::TextRenderer& tr, const std::string& s, float width) {
  std::vector<std::string> lines;
  std::string cur;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = s.find(' ', i);
    if (j == std::string::npos) j = s.size();
    const std::string word = s.substr(i, j - i);
    const std::string tryLine = cur.empty() ? word : cur + " " + word;
    if (!cur.empty() && tr.measure(tryLine) > width) {
      lines.push_back(cur);
      cur = word;
    } else {
      cur = tryLine;
    }
    i = j + 1;
  }
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}

}  // namespace

LineEndingDialog::LineEndingDialog() {
  auto dd = std::make_unique<DropDown>();
  dd->setEnterOpens(false);  // Enter is the default button
  dropDown_ = static_cast<DropDown*>(addChild(std::move(dd)));
  auto cb = std::make_unique<CheckBox>();
  cb->setLabel("Do not show this message again");
  checkBox_ = static_cast<CheckBox*>(addChild(std::move(cb)));
}

void LineEndingDialog::setCaption(std::string s) {
  caption_ = std::move(s);
  markDirty();
}
void LineEndingDialog::setMessage(std::string s) {
  message_ = std::move(s);
  if (root()) root()->requestLayout();
  markDirty();
}
void LineEndingDialog::setFiles(std::vector<File> files) {
  files_ = std::move(files);
  firstFile_ = 0;
  if (root()) root()->requestLayout();
  markDirty();
}
void LineEndingDialog::setLabel(std::string s) {
  label_ = std::move(s);
  markDirty();
}

LineEndingDialog* LineEndingDialog::open(RootWidget& root, std::unique_ptr<LineEndingDialog> d) {
  LineEndingDialog* raw = d.get();
  raw->closedFired_ = false;
  raw->answered_ = false;
  raw->buttonFocus_ = 2;
  root.showModal(std::move(d), raw);
  return raw;
}

void LineEndingDialog::close() {
  RootWidget* r = root();
  if (!r || r->modal() != this) return;
  r->closeModal();
  if (!closedFired_) {
    closedFired_ = true;
    if (closed_) closed_();
  }
}

void LineEndingDialog::answer(bool yes) {
  if (answered_) return;
  answered_ = true;
  const int entry = dropDown_->selected();
  const bool never = checkBox_->checked();
  close();  // first: the handler may open another popup
  if (answer_) answer_(yes, entry, never);
}

int LineEndingDialog::focusIndex() const {
  if (dropDown_->hasFocus()) return 0;
  if (checkBox_->hasFocus()) return 1;
  return buttonFocus_;
}

void LineEndingDialog::focusControl(int index) {
  index = ((index % 4) + 4) % 4;
  if (index == 0) dropDown_->setFocus();
  else if (index == 1) checkBox_->setFocus();
  else {
    buttonFocus_ = index;
    setFocus();
  }
  markDirty();
}

void LineEndingDialog::press(int button) { answer(button == 2); }

void LineEndingDialog::onLayout() {
  const RectF& b = bounds();
  const gfx::TextRenderer& tr = root()->text();
  const Theme& t = theme();
  const float cell = tr.cellWidth(), lh = tr.lineHeight(), pad = 16;
  rowH_ = lh + t.verticalPad;
  const float w = std::min(b.w - 32, cell * 74);
  const float inner = w - 2 * pad;
  msgLines_ = wrapText(tr, message_, inner);
  const float capH = lh + 2 * t.verticalPad + 2;
  const int rows = std::max(1, std::min<int>(kMaxFileRows, static_cast<int>(files_.size())));
  const float listH = rowH_ * static_cast<float>(rows) + 2;
  const float ddH = dropDown_->preferredHeight(), cbH = checkBox_->preferredHeight();
  const float btnH = lh + 2 * t.verticalPad + 4;
  const float h = capH + pad + lh * static_cast<float>(msgLines_.size()) + pad * 0.75f + listH + pad * 0.75f + ddH +
                  pad * 0.5f + cbH + pad + btnH + pad;
  const float x = b.x + (b.w - w) / 2, y = b.y + std::max(16.0f, std::min(b.h * 0.18f, b.h - h - 16));
  dialog_ = {x, y, w, std::min(h, b.h - 16)};
  captionR_ = {x, y, w, capH};
  float cy = y + capH + pad;
  msgTop_ = cy;
  cy += lh * static_cast<float>(msgLines_.size()) + pad * 0.75f;
  list_ = {x + pad, cy, inner, listH};
  cy += listH + pad * 0.75f;
  const float labelW = tr.measure(label_) + cell;
  label_r_ = {x + pad, cy, labelW, ddH};
  float ddW = std::max(dropDown_->preferredWidth(), cell * 26);
  ddW = std::min(ddW, inner - labelW);
  dropDown_->layout({x + pad + labelW, cy, ddW, ddH});
  cy += ddH + pad * 0.5f;
  checkBox_->layout({x + pad, cy, std::min(inner, checkBox_->preferredWidth()), cbH});
  cy += cbH + pad;
  const float bw = std::max(cell * 10, tr.measure("Yes") + 4 * t.padding);
  no_ = {x + w - pad - bw, cy, bw, btnH};
  yes_ = {no_.x - 8 - bw, cy, bw, btnH};
  firstFile_ = std::clamp(firstFile_, 0, std::max(0, static_cast<int>(files_.size()) - kMaxFileRows));
}

bool LineEndingDialog::coveredByList(const RectF& r) const {
  if (!dropDown_->isOpen()) return false;
  const RectF l = dropDown_->listRect();
  return intersect(r, l).w > 0 && intersect(r, l).h > 0;
}

void LineEndingDialog::paintButton(PaintContext& ctx, const RectF& r, const char* text, int idx) {
  const Theme& t = ctx.theme;
  const bool focused = hasFocus() && buttonFocus_ == idx;
  ctx.fill(r, hover_ == idx ? t.tabHover : Theme::orElse(t.inputBackground, t.panelBackground));
  const Color bc = focused ? t.tabAccent : Theme::orElse(t.inputBorder, t.textDim);
  ctx.fill({r.x, r.y, r.w, 1}, bc);
  ctx.fill({r.x, r.y + r.h - 1, r.w, 1}, bc);
  ctx.fill({r.x, r.y, 1, r.h}, bc);
  ctx.fill({r.x + r.w - 1, r.y, 1, r.h}, bc);
  if (focused) {  // default / focused button: second frame line
    ctx.fill({r.x + 1, r.y + 1, r.w - 2, 1}, bc);
    ctx.fill({r.x + 1, r.y + r.h - 2, r.w - 2, 1}, bc);
  }
  if (coveredByList(r)) return;  // the open drop-down list lies on top: its text would show through
  const float tw = ctx.measure(text);
  ctx.text(r.x + (r.w - tw) / 2, r.y + (r.h - ctx.lineHeight()) / 2, text, t.text, r);
}

void LineEndingDialog::paint(PaintContext& ctx) {
  const Theme& t = ctx.theme;
  const Color bg = Theme::orElse(t.popupBackground, t.tabInactive);
  const Color border = Theme::orElse(t.popupBorder, t.splitterHot);
  ctx.fill({dialog_.x - 1, dialog_.y - 1, dialog_.w + 2, dialog_.h + 2}, border);
  ctx.fill(dialog_, bg);
  ctx.fill(captionR_, t.tabStripBackground);
  const float lh = ctx.lineHeight(), pad = 16;
  ctx.text(captionR_.x + pad, captionR_.y + (captionR_.h - lh) / 2, caption_, t.text, captionR_);
  for (size_t i = 0; i < msgLines_.size(); ++i) {
    const RectF row{dialog_.x + pad, msgTop_ + lh * static_cast<float>(i), dialog_.w - 2 * pad, lh};
    if (!coveredByList(row)) ctx.text(row.x, row.y, msgLines_[i], t.text, row);
  }
  // the files
  const Color ib = Theme::orElse(t.inputBackground, t.panelBackground);
  ctx.fill(list_, ib);
  const Color lb = Theme::orElse(t.inputBorder, t.splitter);
  ctx.fill({list_.x, list_.y, list_.w, 1}, lb);
  ctx.fill({list_.x, list_.y + list_.h - 1, list_.w, 1}, lb);
  ctx.fill({list_.x, list_.y, 1, list_.h}, lb);
  ctx.fill({list_.x + list_.w - 1, list_.y, 1, list_.h}, lb);
  const int n = static_cast<int>(files_.size());
  for (int i = 0; i < kMaxFileRows && firstFile_ + i < n; ++i) {
    const File& f = files_[static_cast<size_t>(firstFile_ + i)];
    const RectF r{list_.x + 1, list_.y + 1 + rowH_ * static_cast<float>(i), list_.w - 2 - (n > kMaxFileRows ? 6 : 0),
                  rowH_};
    const float ty = r.y + (r.h - lh) / 2;
    float right = r.x + r.w - t.padding;
    if (!f.detail.empty()) {
      const float dw = ctx.measure(f.detail);
      ctx.text(right - dw, ty, f.detail, t.textDim, r);
      right -= dw + 2 * ctx.textRenderer.cellWidth();
    }
    ctx.text(r.x + t.padding, ty, f.label, t.text, {r.x, r.y, std::max(0.0f, right - r.x), r.h});
  }
  if (n > kMaxFileRows) {  // scroll thumb
    const float track = list_.h - 2, th = std::max(10.0f, track * kMaxFileRows / static_cast<float>(n));
    const float ty = list_.y + 1 + (track - th) * static_cast<float>(firstFile_) / static_cast<float>(n - kMaxFileRows);
    ctx.fill({list_.x + list_.w - 7, ty, 5, th}, t.scrollbarThumb);
  }
  ctx.text(label_r_.x, label_r_.y + (label_r_.h - lh) / 2, label_, t.text, label_r_);
  paintButton(ctx, yes_, "Yes", 2);
  paintButton(ctx, no_, "No", 3);
}

bool LineEndingDialog::onEvent(const InputEvent& e) {
  switch (e.type) {
    case InputType::KeyDown: {
      const bool shift = e.mods & ModShift;
      switch (e.key) {
        case Key::Tab: focusControl(focusIndex() + (shift ? -1 : 1)); return true;
        case Key::Escape: answer(false); return true;
        case Key::Enter:
        case Key::KpEnter: press(focusIndex() == 3 ? 3 : 2); return true;
        case Key::Space:
          if (focusIndex() >= 2) {
            press(focusIndex());
            return true;
          }
          return false;
        case Key::Left:
        case Key::Right:
          if (focusIndex() >= 2) {
            focusControl(focusIndex() == 2 ? 3 : 2);
            return true;
          }
          return false;
        default: return true;  // modal: no other key leaves the dialog
      }
    }
    case InputType::Text: return true;
    case InputType::MouseMove: {
      const int h = ui::contains(yes_, e.x, e.y) ? 2 : ui::contains(no_, e.x, e.y) ? 3 : -1;
      if (h != hover_) {
        hover_ = h;
        markDirty();
      }
      return true;
    }
    case InputType::MouseDown:
      if (e.button == MouseButton::Left) {
        pressed_ = ui::contains(yes_, e.x, e.y) ? 2 : ui::contains(no_, e.x, e.y) ? 3 : -1;
        if (pressed_ >= 0) focusControl(pressed_);
      }
      return true;
    case InputType::MouseUp: {
      const int p = pressed_;
      pressed_ = -1;
      if (p >= 0 && e.button == MouseButton::Left && ui::contains(p == 2 ? yes_ : no_, e.x, e.y)) press(p);
      return true;
    }
    case InputType::Wheel:
      if (ui::contains(list_, e.x, e.y) && static_cast<int>(files_.size()) > kMaxFileRows) {
        firstFile_ = std::clamp(firstFile_ - static_cast<int>(e.dy * 3), 0,
                                static_cast<int>(files_.size()) - kMaxFileRows);
        markDirty();
      }
      return true;
    default: return false;
  }
}

}  // namespace tf::ui
