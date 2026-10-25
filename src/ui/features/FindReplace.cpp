#include "ui/features/FindReplace.h"

#include <algorithm>

#include "ui/widgets/TextView.h"

namespace tf::ui {

namespace {
// Where the caret ends up after `text` was inserted at `start`.
Position endOf(Position start, const std::string& text) {
  Position p = start;
  for (char c : text) {
    if (c == '\n') p = {p.line + 1, 0};
    else ++p.column;
  }
  return p;
}
}  // namespace

// The bar: two InputLines, three option toggles, a status text and the Replace / All buttons. Non-modal top layer
// widget; only the events inside its box reach it (keys come through the focused InputLine and bubble up).
class FindReplace::Bar : public Widget {
 public:
  explicit Bar(FindReplace& fr) : fr_(fr) {
    setTopLayer(true);
    setVisible(false);
    find_ = static_cast<InputLine*>(addChild(std::make_unique<InputLine>()));
    repl_ = static_cast<InputLine*>(addChild(std::make_unique<InputLine>()));
    find_->setPlaceholder("Find");
    repl_->setPlaceholder("Replace");
    find_->onChanged([this](const std::string& q) {
      fr_.query_ = q;
      fr_.refresh(true);
    });
    repl_->setVisible(false);
  }

  InputLine* find_ = nullptr;
  InputLine* repl_ = nullptr;
  RectF box_{0, 0, 0, 0};

  void place() { layout(computeBox()); }

  bool onEvent(const InputEvent& e) override {
    if (e.type == InputType::MouseDown && e.button == MouseButton::Left && ui::contains(box_, e.x, e.y)) {
      if (ui::contains(caseR_, e.x, e.y)) toggle('C');
      else if (ui::contains(wordR_, e.x, e.y)) toggle('W');
      else if (ui::contains(regexR_, e.x, e.y)) toggle('R');
      else if (fr_.replaceRow_ && ui::contains(replR_, e.x, e.y)) fr_.replaceCurrent();
      else if (fr_.replaceRow_ && ui::contains(allR_, e.x, e.y)) fr_.replaceAll();
      return true;
    }
    if (e.type != InputType::KeyDown) return e.type == InputType::MouseDown || e.type == InputType::Wheel;
    const bool ctrl = e.mods & ModCtrl, shift = e.mods & ModShift, alt = e.mods & ModAlt;
    switch (e.key) {
      case Key::Escape: fr_.close(); return true;
      case Key::Enter:
      case Key::KpEnter:
        if (fr_.replaceRow_ && repl_->hasFocus()) {
          if (ctrl) fr_.replaceAll();
          else fr_.replaceCurrent();
        } else {
          fr_.step(!shift);
        }
        return true;
      case Key::Tab:
        if (fr_.replaceRow_) (repl_->hasFocus() ? find_ : repl_)->setFocus();
        return true;
      default: break;
    }
    if (alt && !ctrl && static_cast<uint32_t>(e.key) < 256) {
      const char k = static_cast<char>(e.key);
      if (k == 'C' || k == 'W' || k == 'R') {
        toggle(k);
        return true;
      }
    }
    return false;
  }

 protected:
  // RootWidget gives every child the full window; the bar keeps its own box (top right of the editor area).
  void onLayout() override {
    if (inLayout_ || !root()) return;
    inLayout_ = true;
    box_ = computeBox();
    if (bounds().x != box_.x || bounds().y != box_.y || bounds().w != box_.w || bounds().h != box_.h) layout(box_);
    const RectF b = box_;
    const float cell = root()->text().cellWidth(), ih = find_->preferredHeight();
    const float right = cell * 24, left = b.x + 4, iw = std::max(0.0f, b.w - right - 8);
    find_->layout({left, b.y + 4, iw, ih});
    repl_->setVisible(fr_.replaceRow_);
    repl_->layout({left, b.y + 4 + ih + 4, iw, ih});
    const float tx = left + iw + 6;
    caseR_ = {tx, b.y + 4, cell * 4, ih};
    wordR_ = {tx + cell * 4.5f, b.y + 4, cell * 3, ih};
    regexR_ = {tx + cell * 8, b.y + 4, cell * 4, ih};
    replR_ = {tx, b.y + 4 + ih + 4, cell * 9, ih};
    allR_ = {tx + cell * 9.5f, b.y + 4 + ih + 4, cell * 4, ih};
    inLayout_ = false;
  }

  void paint(PaintContext& ctx) override {
    const Theme& t = ctx.theme;
    ctx.fill({box_.x - 1, box_.y - 1, box_.w + 2, box_.h + 2}, t.splitterHot);
    ctx.fill(box_, t.tabInactive);
    const float lh = ctx.lineHeight();
    auto label = [&](const RectF& r, const char* s, bool on) {
      if (on) ctx.fill(r, t.selection);
      ctx.text(r.x + (r.w - ctx.measure(s)) / 2, r.y + (r.h - lh) / 2, s, on ? t.text : t.textDim, r);
    };
    label(caseR_, "Aa", fr_.cs_);
    label(wordR_, "W", fr_.word_);
    label(regexR_, ".*", fr_.regex_);
    const std::string status = fr_.statusText();
    ctx.text(regexR_.x + regexR_.w + 8, regexR_.y + (regexR_.h - lh) / 2, status, t.textDim,
             {regexR_.x + regexR_.w + 8, box_.y, std::max(0.0f, box_.x + box_.w - regexR_.x - regexR_.w - 12), box_.h});
    if (fr_.replaceRow_) {
      label(replR_, "Replace", false);
      label(allR_, "All", false);
    }
  }

 private:
  RectF computeBox() {
    const RectF area = fr_.area_ ? fr_.area_() : root()->bounds();
    const float cell = root()->text().cellWidth(), ih = find_->preferredHeight();
    const float w = std::min(cell * 70, std::max(cell * 30, area.w - 2 * cell));
    const float h = (fr_.replaceRow_ ? 2 : 1) * (ih + 4) + 4;
    return {std::max(area.x, area.x + area.w - w - cell * 3), area.y + 4, w, h};
  }
  void toggle(char k) {
    (k == 'C' ? fr_.cs_ : k == 'W' ? fr_.word_ : fr_.regex_) ^= true;
    fr_.refresh(true);
    markDirty();
  }

  FindReplace& fr_;
  RectF caseR_{0, 0, 0, 0}, wordR_{0, 0, 0, 0}, regexR_{0, 0, 0, 0}, replR_{0, 0, 0, 0}, allR_{0, 0, 0, 0};
  bool inLayout_ = false;
};

FindReplace::FindReplace(PickerEnv& env) : env_(env) {
  auto bar = std::make_unique<Bar>(*this);
  bar_ = static_cast<Bar*>(env_.root.addChild(std::move(bar)));
}

FindReplace::~FindReplace() {
  if (bar_) env_.root.removeChild(bar_);
}

InputLine* FindReplace::findInput() const { return bar_->find_; }
InputLine* FindReplace::replaceInput() const { return bar_->repl_; }
RectF FindReplace::barRect() const { return bar_->box_; }
const std::string& FindReplace::query() const { return query_; }

SearchOptions FindReplace::options() const {
  SearchOptions o;
  o.query = query_;
  o.caseSensitive = cs_;
  o.wholeWord = word_;
  o.regex = regex_;
  return o;
}

void FindReplace::open(bool replace) {
  const Document* d = env_.editor.currentDocument();
  if (!d) return;
  replaceRow_ = replace;
  open_ = true;
  const Selection s = env_.editor.selections()[0];
  origin_ = s.min();
  if (!s.empty() && s.min().line == s.max().line) {
    query_ = d->buffer().textInRange(s.range());
    bar_->find_->setText(query_, true);
  } else {
    bar_->find_->selectAll();
  }
  bar_->setVisible(true);
  bar_->place();
  bar_->find_->setFocus();
  refresh(!query_.empty() && !s.empty());
  bar_->markDirty();
}

void FindReplace::close() {
  if (!open_) return;
  open_ = false;
  bar_->setVisible(false);
  matches_.clear();
  cur_ = -1;
  publish();
  if (views_)  // focus goes back to the editor
    views_([&](TextView& v) {
      if (v.document() == env_.editor.currentDocument() && v.acceptsFocus()) v.setFocus();
    });
}

void FindReplace::setQuery(const std::string& q) {
  bar_->find_->setText(q);
  query_ = q;
  refresh(true);
}
void FindReplace::setReplacement(const std::string& r) { bar_->repl_->setText(r); }
void FindReplace::setOptions(bool cs, bool word, bool regex) {
  cs_ = cs;
  word_ = word;
  regex_ = regex;
  refresh(true);
}

std::string FindReplace::statusText() const {
  if (!error_.empty()) return error_;
  if (query_.empty()) return {};
  if (matches_.empty()) return "No results";
  return (cur_ >= 0 ? std::to_string(cur_ + 1) : std::string("?")) + " of " + std::to_string(matches_.size());
}

void FindReplace::refresh(bool selectFirst) {
  doc_ = env_.editor.currentDocument();
  version_ = doc_ ? doc_->version() : -1;
  matches_.clear();
  error_.clear();
  cur_ = -1;
  if (doc_ && !query_.empty()) matches_ = env_.editor.findInDocument(options(), &error_);
  if (selectFirst && !matches_.empty()) {  // incremental: the first match at or after where the search started
    auto it = std::lower_bound(matches_.begin(), matches_.end(), origin_,
                               [](const Range& r, Position p) { return r.start < p; });
    select(static_cast<int>((it == matches_.end() ? matches_.begin() : it) - matches_.begin()), false);
    return;
  }
  if (doc_ && !env_.editor.selections().empty()) {
    const auto it = std::find(matches_.begin(), matches_.end(), env_.editor.selections()[0].range());
    if (it != matches_.end()) cur_ = static_cast<int>(it - matches_.begin());
  }
  publish();
}

void FindReplace::publish() {
  if (bar_) bar_->markDirty();
  if (!views_) return;
  views_([&](TextView& v) {
    if (open_ && v.document() == doc_ && !matches_.empty()) v.setFindMatches(matches_, cur_);
    else if (!v.findMatches().empty()) v.setFindMatches({}, -1);
  });
}

void FindReplace::select(int index, bool viaHistory) {
  cur_ = index;
  const Range r = matches_[static_cast<size_t>(index)];
  if (viaHistory) env_.editor.moveCursor(r.start);  // a far move lands in the navigation history
  env_.editor.setSelections({Selection{r.start, r.end}});
  publish();
}

bool FindReplace::step(bool forward) {
  if (!env_.editor.currentDocument()) return false;
  if (!open_ || env_.editor.currentDocument() != doc_ || env_.editor.currentDocument()->version() != version_)
    refresh(false);
  if (matches_.empty()) {
    env_.say(query_.empty() ? "Nothing to find (Ctrl+F)" : "No results for \"" + query_ + "\"");
    return false;
  }
  const Selection s = env_.editor.selections()[0];
  int idx;
  if (forward) {
    const Position from = s.max();
    auto it = std::lower_bound(matches_.begin(), matches_.end(), from,
                               [](const Range& r, Position p) { return r.start < p; });
    idx = static_cast<int>((it == matches_.end() ? matches_.begin() : it) - matches_.begin());
  } else {
    const Position from = s.min();
    auto it = std::lower_bound(matches_.begin(), matches_.end(), from,
                               [](const Range& r, Position p) { return r.start < p; });
    idx = static_cast<int>((it == matches_.begin() ? matches_.end() : it) - matches_.begin()) - 1;
  }
  select(idx, true);
  return true;
}

bool FindReplace::replaceCurrent() {
  if (!env_.editor.currentDocument()) return false;
  refresh(false);
  if (cur_ < 0) return step(true);  // the selection is not a match: select the next one first
  const Range r = matches_[static_cast<size_t>(cur_)];
  const std::string repl = bar_->repl_->text();
  env_.editor.replaceRange(r, repl);
  const Position end = endOf(r.start, repl);
  env_.editor.setSelections({caretAt(end)});
  origin_ = end;
  refresh(false);
  if (!matches_.empty()) step(true);
  return true;
}

size_t FindReplace::replaceAll() {
  if (!env_.editor.currentDocument()) return 0;
  const size_t n = env_.editor.replaceAll(options(), bar_->repl_->text(), &error_);
  env_.say("Replaced " + std::to_string(n) + (n == 1 ? " occurrence" : " occurrences"));
  refresh(false);
  return n;
}

void FindReplace::tick(int64_t) {
  if (!open_) return;
  const Document* d = env_.editor.currentDocument();
  if (d != doc_ || (d && d->version() != version_)) refresh(false);
}

}  // namespace tf::ui
