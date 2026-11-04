#include "ui/features/RenameInput.h"

#include <algorithm>
#include <cctype>

#include "ui/widgets/TextView.h"

namespace tf::ui {

namespace {

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || (c & 0x80); }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || (c & 0x80); }

bool validIdentifier(const std::string& s) {
  return !s.empty() && identStart(s[0]) && std::all_of(s.begin(), s.end(), [](char c) { return identChar(c); });
}

// The box: a bordered top-layer widget with a label and the input line. It keeps its own rectangle like PopupBox.
class RenameBox : public Widget {
 public:
  RenameBox(std::string label, std::function<void()> cancel) : label_(std::move(label)), cancel_(std::move(cancel)) {
    setTopLayer(true);
    auto in = std::make_unique<InputLine>();
    input = static_cast<InputLine*>(addChild(std::move(in)));
  }

  void showAt(const RectF& anchor) {
    anchor_ = anchor;
    place();
  }

  bool onEvent(const InputEvent& e) override {
    if (e.type == InputType::KeyDown) {
      if (e.key == Key::Escape && !(e.mods & (ModCtrl | ModAlt))) {
        if (cancel_) cancel_();
        return true;
      }
      // The autocomplete keys (Up, Down, PageUp, PageDown) mean nothing here and must not report "not implemented".
      return e.key == Key::Up || e.key == Key::Down || e.key == Key::PageUp || e.key == Key::PageDown;
    }
    return e.type == InputType::MouseDown || e.type == InputType::MouseUp || e.type == InputType::Wheel;
  }

  InputLine* input = nullptr;

 protected:
  void onLayout() override {
    const RectF& b = bounds();
    if (inLayout_ || (b.x == box_.x && b.y == box_.y && b.w == box_.w && b.h == box_.h)) return;
    inLayout_ = true;  // RootWidget gives every child the full window; the box keeps its own rectangle
    layout(box_);
    inLayout_ = false;
    inputRect();
  }

  void paint(PaintContext& ctx) override {
    const Theme& t = ctx.theme;
    ctx.fill({box_.x - 1, box_.y - 1, box_.w + 2, box_.h + 2}, Theme::orElse(t.popupBorder, t.splitterHot));
    ctx.fill(box_, Theme::orElse(t.popupBackground, t.tabInactive));
    const float ty = box_.y + (box_.h - ctx.lineHeight()) / 2;
    ctx.text(box_.x + t.padding, ty, label_, t.textDim, box_);
  }

 private:
  void place() {
    if (!root()) return;
    const float pad = theme().padding, cell = root()->text().cellWidth();
    labelW_ = root()->text().measure(label_);
    const RectF win = root()->bounds();
    const float h = input->preferredHeight() + 4;
    const float w = std::min(labelW_ + 2 * pad + cell * 30, std::max(60.0f, win.w - 8));
    const float x = std::clamp(anchor_.x, win.x + 4, std::max(win.x + 4, win.x + win.w - w - 4));
    float y = anchor_.y + anchor_.h + 2;
    if (y + h > win.y + win.h && anchor_.y - h - 2 >= win.y) y = anchor_.y - h - 2;
    box_ = {x, y, w, h};
    setVisible(true);
    layout(box_);
    inputRect();
    markDirty();
  }
  void inputRect() {
    const float pad = theme().padding;
    const float lx = box_.x + pad + labelW_ + pad;
    input->layout({lx, box_.y + 2, std::max(20.0f, box_.x + box_.w - lx - 2), box_.h - 4});
  }

  std::string label_;
  std::function<void()> cancel_;
  RectF anchor_{0, 0, 0, 0}, box_{0, 0, 0, 0};
  float labelW_ = 0;
  bool inLayout_ = false;
};

}  // namespace

RenameInput::RenameInput(PickerEnv& env) : env_(env) {}

RenameInput::~RenameInput() {
  alive_.reset();
  closed_ = nullptr;
  close();
}

bool RenameInput::open() {
  cancel();
  Document* d = env_.editor.currentDocument();
  if (!d || env_.editor.selections().empty()) return false;
  const bool ok =
      available_ ? available_() : env_.editor.started() && env_.editor.semantic().status().clangdInitialized;
  if (!ok) {
    env_.say("Rename needs clangd, which is not running: approximate renames would be wrong, so none is offered");
    return false;
  }
  const Position caret = env_.editor.selections()[0].head;
  const TextBuffer::Word w = d->buffer().wordAt(caret);
  if (w.text.empty() || !identStart(w.text[0])) {
    env_.say("No symbol at the caret to rename");
    return false;
  }
  old_ = w.text;
  at_ = w.range.start;
  showBox("Rename " + old_ + " to", old_);
  return true;
}

void RenameInput::showBox(const std::string& label, const std::string& text) {
  auto* view = dynamic_cast<TextView*>(env_.root.focused());
  auto box = std::make_unique<RenameBox>(label, [this] { cancel(); });
  RenameBox* raw = box.get();
  box_ = env_.root.addChild(std::move(box));
  input_ = raw->input;
  RectF anchor{0, 0, 0, 0};
  if (view) anchor = view->caretRect(at_);
  else anchor = {env_.root.bounds().x + 40, env_.root.bounds().y + 40, 1, 14};
  raw->showAt(anchor);
  input_->setText(text, true);
  input_->setFocus();
}

void RenameInput::cancel() {
  confirm_ = false;
  if (box_) close();
}

void RenameInput::close() {
  if (!box_) return;
  Widget* b = box_;
  box_ = nullptr;
  input_ = nullptr;
  if (b->root()) retired_ = b->root()->removeChild(b);  // drops the focus; freed later (Escape runs inside the box)
  if (closed_) closed_();
}

bool RenameInput::submit() {
  if (!box_) return false;
  const std::string name = input_->text();
  const std::string old = old_;
  const Position at = at_;
  if (name == old || name.empty()) {
    close();
    env_.say("Rename cancelled: the name is unchanged");
    return true;
  }
  if (!validIdentifier(name)) {
    env_.say("\"" + name + "\" is not a valid identifier");  // the box stays open for a correction
    return true;
  }
  const bool force = confirm_ && name == confirmName_;
  confirm_ = false;
  close();
  busy_ = true;
  env_.say("Renaming " + old + " to " + name + "...");
  auto cb = [this, name, alive = std::weak_ptr<int>(alive_)](const Editor::RenameResult& r, const std::string& err) {
    if (!alive.expired()) finish(r, err, name);
  };
  if (force && forceRename_) forceRename_(at, name, cb);
  else if (rename_) rename_(at, name, cb);
  else env_.editor.renameSymbol(at, name, cb, force);
  return true;
}

void RenameInput::finish(const Editor::RenameResult& r, const std::string& err, const std::string& newName) {
  busy_ = false;
  if (r.needsConfirmation) {
    std::string where;
    for (size_t i = 0; i < r.missed.size() && i < 3; ++i)
      where += (i ? ", " : "") + r.missed[i].file.filename().string() + ":" + std::to_string(r.missed[i].pos.line + 1);
    if (r.missedTotal > 3) where += ", ...";
    env_.say(
        "Rename of " + old_ + " NOT applied yet: " + std::to_string(r.missedTotal) + " other spelling" +
        (r.missedTotal == 1 ? "" : "s") + " of it (" + where +
        ") are not in clangd's edit set and may stop compiling. Press Enter again to rename anyway, Escape to cancel");
    confirm_ = true;
    confirmName_ = newName;
    showBox("Rename " + old_ + " to (may miss " + std::to_string(r.missedTotal) + " uses; Enter again to force)",
            newName);
    return;
  }
  const size_t files = r.updated.size() + r.written.size() + r.unsaved.size();
  if (files == 0) {
    env_.say("Rename failed: " + (err.empty() ? std::string("no edits") : err));
    return;
  }
  std::string msg = "Renamed " + old_ + " to " + newName + ": " + std::to_string(r.edits) + " edit" +
                    (r.edits == 1 ? "" : "s") + " in " + std::to_string(files) + " file" + (files == 1 ? "" : "s");
  if (!r.written.empty()) msg += ", " + std::to_string(r.written.size()) + " written to disk";
  if (!r.unsaved.empty()) msg += "; could not save " + std::to_string(r.unsaved.size()) + " (" + err + ")";
  if (r.missedTotal)
    msg += "; WARNING: " + std::to_string(r.missedTotal) + " other spelling(s) of " + old_ + " were not renamed";
  env_.say(msg);
}

}  // namespace tf::ui
