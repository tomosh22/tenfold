#include "ui/features/CompletionPopup.h"

#include <algorithm>
#include <cctype>

#include "ui/widgets/TextView.h"

namespace tf::ui {

namespace {
constexpr size_t kMaxShown = 200;
constexpr int kRows = 10;

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || (c & 0x80); }

int wordStart(const std::string& line, int col) {
  col = std::clamp(col, 0, static_cast<int>(line.size()));
  while (col > 0 && identChar(line[static_cast<size_t>(col - 1)])) --col;
  return col;
}

const char* kindLetter(int k) {
  switch (k) {
    case 2:
    case 3:
    case 4: return "f";
    case 5:
    case 10: return "m";
    case 6: return "v";
    case 7:
    case 8:
    case 22:
    case 25: return "C";
    case 9: return "N";
    case 14: return "k";
    case 20:
    case 21: return "e";
    default: return " ";
  }
}

bool startsWithNoCase(const std::string& s, const std::string& p) {
  if (p.size() > s.size()) return false;
  for (size_t i = 0; i < p.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(p[i]))) return false;
  return true;
}
}  // namespace

CompletionPopup::CompletionPopup(PickerEnv& env) : env_(env) {
  auto b = std::make_unique<PopupBox>();
  b->setMaxRows(kRows);
  box_ = static_cast<PopupBox*>(env_.root.addChild(std::move(b)));
  box_->onPick([this](int i) {
    sel_ = i;
    accept();
  });
}

CompletionPopup::~CompletionPopup() {
  alive_.reset();
  if (box_ && box_->root()) box_->root()->removeChild(box_);
}

void CompletionPopup::close() {
  ++seq_;  // answers in flight are stale now
  inFlight_ = false;
  if (!open_) return;
  open_ = false;
  box_->hide();
}

std::vector<std::string> CompletionPopup::shownLabels() const {
  std::vector<std::string> out;
  for (const Row& r : shown_) out.push_back(items_[r.item].label);
  return out;
}

bool CompletionPopup::valid() const {
  Document* d = env_.editor.currentDocument();
  if (!d || d != doc_ || env_.editor.selections().empty()) return false;
  const Position c = env_.editor.selections()[0].head;
  if (c.line != anchor_.line || c.column < anchor_.column) return false;
  const std::string line = d->buffer().line(c.line);
  if (c.column > static_cast<int>(line.size())) return false;
  for (int i = anchor_.column; i < c.column; ++i)
    if (!identChar(line[static_cast<size_t>(i)])) return false;
  return true;
}

std::string CompletionPopup::prefix() const {
  const Document* d = env_.editor.currentDocument();
  const Position c = env_.editor.selections()[0].head;
  return d->buffer().line(c.line).substr(static_cast<size_t>(anchor_.column),
                                         static_cast<size_t>(c.column - anchor_.column));
}

void CompletionPopup::onTyped(const std::string& text) {
  Document* d = env_.editor.currentDocument();
  if (!d || text.empty() || env_.editor.selections().empty()) return;
  const Position c = env_.editor.selections()[0].head;
  const std::string line = d->buffer().line(c.line);
  const int col = std::min(c.column, static_cast<int>(line.size()));
  const char last = text.back();
  const char before = col >= 2 ? line[static_cast<size_t>(col - 2)] : '\0';
  if (text == "." || (text == ">" && before == '-') || (text == ":" && before == ':')) {
    suppressed_ = {-1, -1};
    doc_ = d;
    request(c);
  } else if (identChar(last)) {
    const int ws = wordStart(line, col);
    const Position a{c.line, ws};
    if (col - ws < 2 || a == suppressed_) return;
    if ((open_ || inFlight_) && a == anchor_ && doc_ == d) return;  // update() refilters the answer
    doc_ = d;
    request(a);
  } else {
    close();
  }
}

void CompletionPopup::request(Position anchor) {
  anchor_ = anchor;
  inFlight_ = true;
  const uint64_t seq = ++seq_;
  auto cb = [this, seq, alive = std::weak_ptr<int>(alive_)](const std::vector<LspCompletionItem>& items,
                                                            const std::string& err) {
    if (!alive.expired()) onItems(seq, items, err);
  };
  if (complete_) complete_(cb);
  else if (env_.editor.started()) env_.editor.complete(cb);
}

void CompletionPopup::onItems(uint64_t seq, const std::vector<LspCompletionItem>& items, const std::string& err) {
  if (seq != seq_) return;  // a newer request, or the popup was closed meanwhile
  inFlight_ = false;
  if (!err.empty() || items.empty() || !valid()) {
    if (open_) close();
    return;
  }
  items_ = items;
  approximate_ = approx_ ? approx_() : !env_.editor.semantic().status().clangdInitialized;
  lastVersion_ = -1;
  refilter();
  if (shown_.empty()) {
    if (open_) close();
    return;
  }
  open_ = true;
  sel_ = 0;
  render();
  place();
}

void CompletionPopup::refilter() {
  const std::string p = prefix();
  shown_.clear();
  for (size_t i = 0; i < items_.size(); ++i) {
    const LspCompletionItem& it = items_[i];
    const std::string& f = it.filterText.empty() ? it.label : it.filterText;
    if (p.empty()) {
      shown_.push_back({i, true, 0});
      continue;
    }
    const std::vector<int> m = fuzzyMatchPositions(p, f);
    if (m.empty()) continue;
    shown_.push_back({i, startsWithNoCase(f, p), m.back() - m.front() + 2 * m.front()});
  }
  std::stable_sort(shown_.begin(), shown_.end(), [&](const Row& a, const Row& b) {
    if (a.prefix != b.prefix) return a.prefix;
    if (a.score != b.score) return a.score < b.score;
    return items_[a.item].sortText < items_[b.item].sortText;
  });
  if (shown_.size() > kMaxShown) shown_.resize(kMaxShown);
  sel_ = std::clamp(sel_, 0, std::max(0, static_cast<int>(shown_.size()) - 1));
}

void CompletionPopup::render() {
  const std::string p = prefix();
  std::vector<PopupLine> lines;
  using S = PopupSpan::Style;
  for (size_t i = 0; i < shown_.size(); ++i) {
    const LspCompletionItem& it = items_[shown_[i].item];
    PopupLine l;
    l.spans.push_back({std::string(kindLetter(it.kind)) + " ", S::Dim, false});
    const S base = approximate_ || it.deprecated ? S::Dim : S::Text;
    const std::vector<int> m = fuzzyMatchPositions(p, it.label);
    size_t from = 0, mi = 0;
    while (from < it.label.size()) {
      const bool hit = mi < m.size() && static_cast<int>(from) == m[mi];
      size_t to = from + 1;
      while (to < it.label.size() && (static_cast<unsigned char>(it.label[to]) & 0xC0) == 0x80) ++to;
      if (hit) ++mi;
      if (!l.spans.empty() && l.spans.back().style == (hit ? S::Hit : base) && l.spans.size() > 1)
        l.spans.back().text.append(it.label, from, to - from);
      else l.spans.push_back({it.label.substr(from, to - from), hit ? S::Hit : base, false});
      from = to;
    }
    l.right = it.detail.size() > 40 ? it.detail.substr(0, 40) : it.detail;
    lines.push_back(std::move(l));
  }
  std::vector<std::string> side;
  if (sel_ >= 0 && sel_ < static_cast<int>(shown_.size())) {
    const LspCompletionItem& it = items_[shown_[static_cast<size_t>(sel_)].item];
    if (!it.documentation.empty()) {
      side = PopupBox::wrap(it.label + (it.detail.empty() ? "" : "  ->  " + it.detail), 44);
      side.push_back("");
      for (const std::string& s : PopupBox::wrap(it.documentation, 44)) side.push_back(s);
    }
    if (approximate_) side.push_back("approximate (clangd not running)");
  }
  box_->setContent(std::move(lines), sel_, std::move(side));
}

void CompletionPopup::place() {
  Widget* f = env_.root.focused();
  auto* v = dynamic_cast<TextView*>(f);
  if (!v) {
    close();
    return;
  }
  const RectF r = v->caretRect(anchor_);
  box_->showAt(r);
  placed_ = r;
}

void CompletionPopup::update() {
  if (!open_) return;
  if (!valid() || !dynamic_cast<TextView*>(env_.root.focused())) {
    close();
    return;
  }
  const Document* d = env_.editor.currentDocument();
  const Position c = env_.editor.selections()[0].head;
  if (d->version() != lastVersion_ || c != lastCaret_) {
    lastVersion_ = d->version();
    lastCaret_ = c;
    refilter();
    if (shown_.empty()) {
      close();
      return;
    }
    render();
  }
  auto* v = static_cast<TextView*>(env_.root.focused());
  const RectF r = v->caretRect(anchor_);
  if (r.x != placed_.x || r.y != placed_.y || !box_->visible()) place();
}

bool CompletionPopup::handleKey(const InputEvent& e) {
  if (!open_ || e.type != InputType::KeyDown || (e.mods & (ModCtrl | ModAlt))) return false;
  const int n = static_cast<int>(shown_.size());
  auto move = [&](int to) {
    sel_ = std::clamp(to, 0, n - 1);
    render();
  };
  switch (e.key) {
    case Key::Up: move(sel_ - 1); return true;
    case Key::Down: move(sel_ + 1); return true;
    case Key::PageUp: move(sel_ - kRows); return true;
    case Key::PageDown: move(sel_ + kRows); return true;
    case Key::Tab:
    case Key::Enter:
    case Key::KpEnter: return accept();
    case Key::Escape:
      suppressed_ = anchor_;
      close();
      return true;
    default: return false;
  }
}

bool CompletionPopup::accept() {
  if (!open_ || sel_ < 0 || sel_ >= static_cast<int>(shown_.size()) || !valid()) return false;
  const LspCompletionItem& it = items_[shown_[static_cast<size_t>(sel_)].item];
  std::string text = it.insertText;
  if (text.empty()) {
    text = it.label;
    text.erase(0, text.find_first_not_of(' '));
  }
  Editor& ed = env_.editor;
  const std::string typed = prefix();
  const TextBuffer& b = ed.currentDocument()->buffer();
  std::vector<Selection> sels = ed.selections();
  bool all = sels.size() > 1;
  for (const Selection& s : sels) {  // every caret must be empty and sit after the same prefix
    const std::string line = b.line(s.head.line);
    const int ws = wordStart(line, s.head.column);
    const int len = s.head.column - ws;
    if (!s.empty() || len != static_cast<int>(typed.size()) ||
        line.substr(static_cast<size_t>(ws), static_cast<size_t>(len)) != typed)
      all = false;
  }
  close();
  if (all) {
    for (Selection& s : sels) s.anchor = {s.head.line, s.head.column - static_cast<int>(typed.size())};
    ed.setSelections(sels);
    ed.insertText(text);
  } else {
    // Primary caret only: replaceRange collapses the caret set, so the other carets are put back (shifted when they
    // share the line after the replaced prefix).
    const Position head = sels[0].head;
    const int delta = static_cast<int>(text.size()) - static_cast<int>(typed.size());
    ed.replaceRange({anchor_, head}, text);
    sels[0] = caretAt({anchor_.line, anchor_.column + static_cast<int>(text.size())});
    for (size_t i = 1; i < sels.size(); ++i)
      for (Position* q : {&sels[i].anchor, &sels[i].head})
        if (q->line == head.line && q->column >= head.column) q->column += delta;
    ed.setSelections(sels);
  }
  return true;
}

}  // namespace tf::ui
