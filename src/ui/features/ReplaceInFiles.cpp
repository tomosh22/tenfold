#include "ui/features/ReplaceInFiles.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace tf::ui {

namespace {
constexpr size_t kLineMax = 200;
constexpr int kMaxRows = 16;

std::string count(size_t n, const char* noun) { return std::to_string(n) + " " + noun + (n == 1 ? "" : "s"); }
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The panel widget
// ---------------------------------------------------------------------------------------------------------------

class ReplaceInFiles::Panel : public Widget {
 public:
  explicit Panel(ReplaceInFiles& o) : o_(o), life_(o.alive_) {
    find_ = static_cast<InputLine*>(addChild(std::make_unique<InputLine>()));
    repl_ = static_cast<InputLine*>(addChild(std::make_unique<InputLine>()));
    find_->setPlaceholder("Find in files   (Alt+C case, Alt+W word, Alt+R regex;  query :: *.cpp, !third_party)");
    repl_->setPlaceholder("Replace with   (Tab switches fields, Insert checks a row, Alt+T all, Enter replaces)");
    find_->onChanged([this](const std::string&) { o_.queryChanged(); });
    repl_->onChanged([this](const std::string&) {
      o_.resetConfirm();
      markDirty();
    });
  }
  ~Panel() override {
    if (!life_.expired() && o_.panel_ == this) {
      o_.panel_ = nullptr;
      o_.cancel();
    }
  }

  InputLine* find_ = nullptr;
  InputLine* repl_ = nullptr;
  int selected_ = -1, first_ = 0, hover_ = -1;

  float rowH() const { return root()->text().lineHeight() + theme().verticalPad; }
  float inputH() const { return find_->preferredHeight(); }
  float gap() const { return theme().verticalPad; }
  float listTopOffset() const { return gap() + inputH() + gap() + inputH() + gap() + rowH() + gap(); }

  RectF popupRect() const {
    const RectF& b = bounds();
    const float cell = root()->text().cellWidth(), margin = 16;
    const float w = std::min(b.w - 2 * margin, cell * 100);
    const float h = std::min(b.h - 2 * margin, listTopOffset() + rowH() * kMaxRows + gap());
    return {b.x + (b.w - w) / 2, b.y + std::min(b.h * 0.1f, std::max(0.0f, b.h - h - margin)), w, h};
  }
  RectF listRect() const {
    const RectF p = popupRect();
    const float top = p.y + listTopOffset();
    return {p.x, top, p.w, std::max(0.0f, p.y + p.h - top - gap())};
  }
  int visibleRows() const { return std::max(1, static_cast<int>(listRect().h / rowH())); }
  RectF rowRect(int i) const {
    const RectF l = listRect();
    return {l.x, l.y + static_cast<float>(i - first_) * rowH(), l.w, rowH()};
  }
  RectF boxRect(int i) const {  // the check box of row i
    const RectF r = rowRect(i);
    return {r.x, r.y, theme().padding + 4 * root()->text().cellWidth(), r.h};
  }
  RectF statusRect() const {
    const RectF p = popupRect();
    return {p.x, p.y + gap() + inputH() + gap() + inputH() + gap(), p.w, rowH()};
  }
  int rowAt(float x, float y) const {
    const RectF l = listRect();
    if (!ui::contains(l, x, y)) return -1;
    const int i = first_ + static_cast<int>((y - l.y) / rowH());
    return i >= 0 && i < static_cast<int>(o_.rows_.size()) && i < first_ + visibleRows() ? i : -1;
  }

  void setSelected(int i) {
    const int n = static_cast<int>(o_.rows_.size());
    if (n == 0) {
      selected_ = -1;
      return;
    }
    selected_ = std::clamp(i, 0, n - 1);
    if (selected_ < first_) first_ = selected_;
    if (selected_ >= first_ + visibleRows()) first_ = selected_ - visibleRows() + 1;
    markDirty();
  }
  void scrollTo(int first) {
    first = std::clamp(first, 0, std::max(0, static_cast<int>(o_.rows_.size()) - visibleRows()));
    if (first != first_) {
      first_ = first;
      markDirty();
    }
  }

  bool onEvent(const InputEvent& e) override {
    switch (e.type) {
      case InputType::KeyDown: return onKey(e);
      case InputType::MouseMove: {
        const int h = rowAt(e.x, e.y);
        if (h != hover_) {
          hover_ = h;
          markDirty();
        }
        return true;
      }
      case InputType::MouseDown:
        if (!ui::contains(popupRect(), e.x, e.y)) {
          o_.close();
        } else if (e.button == MouseButton::Left) {
          const int r = rowAt(e.x, e.y);
          if (r >= 0) {
            setSelected(r);
            if (ui::contains(boxRect(r), e.x, e.y)) o_.toggleRow(r);
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

 protected:
  void onLayout() override {
    const RectF p = popupRect();
    const float pad = theme().padding;
    find_->layout({p.x + pad, p.y + gap(), std::max(0.0f, p.w - 2 * pad), inputH()});
    repl_->layout({p.x + pad, p.y + gap() + inputH() + gap(), std::max(0.0f, p.w - 2 * pad), inputH()});
    first_ = std::min(first_, std::max(0, static_cast<int>(o_.rows_.size()) - visibleRows()));
  }

  void paint(PaintContext& ctx) override {
    const Theme& t = ctx.theme;
    const RectF p = popupRect();
    ctx.fill({p.x - 1, p.y - 1, p.w + 2, p.h + 2}, Theme::orElse(t.popupBorder, t.splitterHot));
    ctx.fill(p, Theme::orElse(t.popupBackground, t.tabInactive));
    const RectF s = statusRect();
    ctx.text(s.x + t.padding, s.y + (s.h - ctx.lineHeight()) / 2, o_.statusText(),
             o_.confirming_ ? t.buildWarning : t.textDim, s);
    const RectF l = listRect();
    if (o_.rows_.empty())
      ctx.text(l.x + t.padding, l.y + t.verticalPad,
               o_.summary_.filesSearched && !o_.running_ ? "No matches" : "Type to search the workspace", t.textDim, l);
    const int n = static_cast<int>(o_.rows_.size());
    for (int i = first_; i < n && i < first_ + visibleRows(); ++i) paintRow(ctx, i, rowRect(i));
    if (n > visibleRows()) {  // scrollbar thumb
      Color c = t.textDim;
      c.a = 0.5f;
      const int vis = visibleRows();
      const float th = std::max(12.0f, l.h * static_cast<float>(vis) / static_cast<float>(n));
      ctx.fill({l.x + l.w - 4, l.y + (l.h - th) * static_cast<float>(first_) / static_cast<float>(n - vis), 3, th}, c);
    }
  }

 private:
  bool onKey(const InputEvent& e) {
    const bool alt = e.mods & ModAlt, ctrl = e.mods & ModCtrl;
    switch (e.key) {
      case Key::Escape:
        if (o_.confirming_) {
          o_.resetConfirm();
          markDirty();
        } else {
          o_.close();
        }
        return true;
      case Key::Enter:
      case Key::KpEnter: o_.apply(); return true;
      case Key::Tab: (repl_->hasFocus() ? find_ : repl_)->setFocus(); return true;
      case Key::Up: setSelected(selected_ < 0 ? 0 : selected_ - 1); return true;
      case Key::Down: setSelected(selected_ + 1); return true;
      case Key::PageUp: setSelected(selected_ - visibleRows()); return true;
      case Key::PageDown: setSelected(selected_ + visibleRows()); return true;
      case Key::Home:
      case Key::End:
        if (!ctrl) return false;  // the field's own caret keys
        setSelected(e.key == Key::Home ? 0 : static_cast<int>(o_.rows_.size()) - 1);
        return true;
      case Key::Insert:
        if (ctrl || alt || (e.mods & ModShift)) return false;
        o_.toggleRow(selected_);
        return true;
      default: break;
    }
    if (alt && !ctrl && static_cast<uint32_t>(e.key) < 256) {
      const char k = static_cast<char>(e.key);
      if (k == 'C' || k == 'W' || k == 'R') {
        o_.setOptions(k == 'C' ? !o_.cs_ : o_.cs_, k == 'W' ? !o_.word_ : o_.word_, k == 'R' ? !o_.regex_ : o_.regex_);
        return true;
      }
      if (k == 'T') {
        o_.setAllChecked(o_.checkedCount() != o_.entries_.size());
        return true;
      }
    }
    return false;
  }

  void paintRow(PaintContext& ctx, int i, const RectF& r) {
    const ReplaceInFiles::Row& row = o_.rows_[static_cast<size_t>(i)];
    const Theme& t = ctx.theme;
    if (i == selected_) ctx.fill(r, t.selection);
    else if (i == hover_) ctx.fill(r, t.tabHover);
    const float cell = ctx.textRenderer.cellWidth(), pad = t.padding, ty = r.y + (r.h - ctx.lineHeight()) / 2;
    const bool on = row.header ? row.checked > 0 : o_.entries_[row.first].checked;
    const bool partial = row.header && row.checked > 0 && row.checked < row.count;
    ctx.text(r.x + pad, ty, partial ? "[-]" : on ? "[x]" : "[ ]", on ? t.text : t.textDim, r);
    float x = r.x + pad + 4 * cell;
    const RectF clip{x, r.y, std::max(0.0f, r.x + r.w - pad - 4 - x), r.h};
    if (row.header) {
      const std::string badge = std::to_string(row.count);
      ctx.text(r.x + r.w - pad - 4 - ctx.measure(badge), ty, badge, t.textDim, r);
      ctx.text(x, ty, row.text, t.text, clip);
      return;
    }
    const SearchMatch& m = o_.entries_[row.first].match;
    std::string text = m.text;
    size_t lead = 0;
    while (lead < text.size() && (text[lead] == ' ' || text[lead] == '\t')) ++lead;
    text.erase(0, lead);
    std::replace(text.begin(), text.end(), '\t', ' ');
    ctx.text(x, ty, std::to_string(m.line + 1), t.gutterText, clip);
    x += 6 * cell;
    const int from = m.column - m.textOffset - static_cast<int>(lead), to = from + m.length;
    const Color base = on ? t.text : t.textDim;
    if (text.size() > kLineMax) text.resize(kLineMax);
    if (from < 0 || to > static_cast<int>(text.size())) {
      ctx.text(x, ty, text, base, clip);
      return;
    }
    const std::string pre = text.substr(0, static_cast<size_t>(from));
    const std::string old = text.substr(static_cast<size_t>(from), static_cast<size_t>(m.length));
    const std::string post = text.substr(static_cast<size_t>(to));
    std::string repl = repl_->text();
    std::replace(repl.begin(), repl.end(), '\n', ' ');
    ctx.text(x, ty, pre, base, clip);
    x += ctx.measure(pre);
    ctx.text(x, ty, old, on ? t.buildError : t.textDim, clip);
    const float ow = ctx.measure(old);
    if (on) ctx.line(x, ty + ctx.lineHeight() / 2, x + ow, ty + ctx.lineHeight() / 2, 1, t.buildError);
    x += ow;
    ctx.text(x, ty, repl, on ? t.tabAccent : t.textDim, clip);
    x += ctx.measure(repl);
    ctx.text(x, ty, post, base, clip);
  }

  ReplaceInFiles& o_;
  std::weak_ptr<int> life_;  // the panel may outlive its owner (retired widgets are destroyed with the root)
};

// ---------------------------------------------------------------------------------------------------------------
// ReplaceInFiles
// ---------------------------------------------------------------------------------------------------------------

ReplaceInFiles::~ReplaceInFiles() {
  if (isOpen()) env_.root.closeModal();
  alive_.reset();
  cancel();
}

bool ReplaceInFiles::isOpen() const { return panel_ && env_.root.modal() == static_cast<const Widget*>(panel_); }

InputLine* ReplaceInFiles::findInput() const { return panel_ ? panel_->find_ : nullptr; }
InputLine* ReplaceInFiles::replaceInput() const { return panel_ ? panel_->repl_ : nullptr; }

void ReplaceInFiles::open(const std::string& initialQuery) {
  close();
  auto p = std::make_unique<Panel>(*this);
  panel_ = p.get();
  entries_.clear();
  rows_.clear();
  summary_ = {};
  confirming_ = false;
  running_ = pending_ = dirty_ = false;
  env_.root.showModal(std::move(p), panel_->find_);
  if (!initialQuery.empty()) {
    panel_->find_->setText(initialQuery, true);
    queryChanged();
  }
}

void ReplaceInFiles::close() {
  cancel();
  pending_ = false;
  if (isOpen()) env_.root.closeModal();
  panel_ = nullptr;
}

void ReplaceInFiles::cancel() {
  if (running_ && handle_.valid()) handle_.cancel();
  running_ = false;
  ++seq_;  // batches still queued for the cancelled search are dropped
}

void ReplaceInFiles::resetConfirm() {
  confirming_ = false;
  if (panel_) panel_->markDirty();
}

void ReplaceInFiles::queryChanged() {
  pending_ = true;
  lastEdit_ = env_.clock ? env_.clock() : 0;
  confirming_ = false;
  cancel();
}

int ReplaceInFiles::tick(int64_t now) {
  int wait = -1;
  auto soonest = [&](int64_t due) {
    const int ms = static_cast<int>(std::max<int64_t>(1, due - now));
    wait = wait < 0 ? ms : std::min(wait, ms);
  };
  if (panel_ && pending_) {
    if (now - lastEdit_ >= debounceMs_) start();
    else soonest(lastEdit_ + debounceMs_);
  }
  if (dirty_) {
    if (now - lastRebuild_ >= rebuildMs_) rebuild();
    else soonest(lastRebuild_ + rebuildMs_);
  }
  return wait;
}

void ReplaceInFiles::start() {
  pending_ = false;
  const std::string text = panel_ ? panel_->find_->text() : std::string();
  cancel();
  entries_.clear();
  summary_ = {};
  SearchOptions o = FindInFiles::parse(text, cs_, word_, regex_);
  if (o.query.empty()) {
    rebuild();
    return;
  }
  running_ = true;
  const uint64_t seq = ++seq_;
  std::weak_ptr<int> alive = alive_;
  auto onBatch = [this, seq, alive](const std::vector<SearchMatch>& b) {
    if (alive.expired() || seq != seq_) return;
    for (const SearchMatch& m : b) entries_.push_back({m, true});
    dirty_ = true;
  };
  auto onDone = [this, seq, alive](const SearchSummary& s) {
    if (alive.expired() || seq != seq_) return;
    running_ = false;
    summary_ = s;
    rebuild();
  };
  handle_ = search_ ? search_(o.query, o, onBatch, onDone) : env_.editor.findInWorkspace(o.query, o, onBatch, onDone);
  rebuild();
}

void ReplaceInFiles::rebuild() {
  dirty_ = false;
  lastRebuild_ = env_.clock ? env_.clock() : 0;
  const std::filesystem::path root = env_.editor.workspace().root();
  std::string keep;  // identity of the selected row
  auto rel = [&](const SearchMatch& m) { return relativePath(root, m.file); };
  auto keyOf = [&](const Row& r) {
    if (r.header) return "f:" + r.text;
    const SearchMatch& m = entries_[r.first].match;
    return "m:" + rel(m) + ":" + std::to_string(m.line) + ":" + std::to_string(m.column);
  };
  if (panel_ && panel_->selected_ >= 0 && panel_->selected_ < static_cast<int>(rows_.size()))
    keep = keyOf(rows_[static_cast<size_t>(panel_->selected_)]);
  // The rows reference entries by index: remember the key of the old selection first, then sort.
  std::map<std::string, std::string> relCache;
  auto relOf = [&](const SearchMatch& m) -> const std::string& {
    auto it = relCache.find(m.file.generic_string());
    if (it == relCache.end()) it = relCache.emplace(m.file.generic_string(), rel(m)).first;
    return it->second;
  };
  std::stable_sort(entries_.begin(), entries_.end(), [&](const Entry& a, const Entry& b) {
    const std::string &pa = relOf(a.match), &pb = relOf(b.match);
    if (pa != pb) return pa < pb;
    return std::tie(a.match.line, a.match.column) < std::tie(b.match.line, b.match.column);
  });
  rows_.clear();
  for (size_t i = 0; i < entries_.size();) {
    const std::string& path = relOf(entries_[i].match);
    Row h;
    h.header = true;
    h.text = path;
    h.first = i;
    size_t j = i;
    while (j < entries_.size() && relOf(entries_[j].match) == path) {
      if (entries_[j].checked) ++h.checked;
      ++j;
    }
    h.count = j - i;
    rows_.push_back(h);
    for (size_t k = i; k < j; ++k) {
      Row r;
      r.first = k;
      rows_.push_back(r);
    }
    i = j;
  }
  if (!panel_) return;
  int sel = rows_.size() > 1 ? 1 : -1;  // the first match, not the file header
  if (!keep.empty())
    for (size_t i = 0; i < rows_.size(); ++i)
      if (keyOf(rows_[i]) == keep) sel = static_cast<int>(i);
  panel_->first_ = std::min(panel_->first_, std::max(0, static_cast<int>(rows_.size()) - panel_->visibleRows()));
  panel_->setSelected(sel);
  panel_->markDirty();
}

size_t ReplaceInFiles::checkedCount() const {
  return static_cast<size_t>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.checked; }));
}

void ReplaceInFiles::setChecked(size_t entry, bool on) {
  if (entry >= entries_.size()) return;
  entries_[entry].checked = on;
  confirming_ = false;
  rebuild();
}

void ReplaceInFiles::setAllChecked(bool on) {
  for (Entry& e : entries_) e.checked = on;
  confirming_ = false;
  rebuild();
}

void ReplaceInFiles::toggleRow(int row) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return;
  const Row r = rows_[static_cast<size_t>(row)];
  if (r.header) {
    const bool on = r.checked < r.count;  // some unchecked: check them all, else uncheck them all
    for (size_t k = r.first; k < r.first + r.count; ++k) entries_[k].checked = on;
  } else {
    entries_[r.first].checked = !entries_[r.first].checked;
  }
  confirming_ = false;
  rebuild();
}

void ReplaceInFiles::setOptions(bool cs, bool word, bool regex) {
  cs_ = cs;
  word_ = word;
  regex_ = regex;
  if (panel_) {
    queryChanged();
    panel_->markDirty();
  }
}

std::string ReplaceInFiles::statusText() const {
  if (confirming_) {
    std::set<std::filesystem::path> closed;
    size_t n = 0;
    for (const Entry& e : entries_)
      if (e.checked && !env_.editor.document(e.match.file)) {
        closed.insert(e.match.file);
        ++n;
      }
    return "Write " + count(n, "replacement") + " to " + count(closed.size(), "file") +
           " on disk? Enter confirms, Escape goes back";
  }
  std::string s = std::string(cs_ ? "[Aa]" : " Aa ") + (word_ ? "[W]" : " W ") + (regex_ ? "[.*]" : " .* ") + "  ";
  if (!summary_.error.empty()) return s + summary_.error;
  std::set<std::filesystem::path> files;
  for (const Entry& e : entries_) files.insert(e.match.file);
  if (entries_.empty() && !running_ && !summary_.filesSearched) return s;
  s += count(entries_.size(), "match") + " in " + count(files.size(), "file") + ", " + std::to_string(checkedCount()) +
       " checked";
  if (summary_.truncated) s += " (truncated)";
  if (running_) s += " - searching\xE2\x80\xA6";
  return s;
}

bool ReplaceInFiles::apply() {
  if (!panel_) return false;
  std::vector<SearchMatch> chosen;
  bool anyClosed = false;
  for (const Entry& e : entries_) {
    if (!e.checked) continue;
    chosen.push_back(e.match);
    if (!env_.editor.document(e.match.file)) anyClosed = true;
  }
  if (chosen.empty()) {
    env_.say("No matches checked");
    return false;
  }
  if (anyClosed && !confirming_) {  // the one confirmation before files on disk are written
    confirming_ = true;
    panel_->markDirty();
    return false;
  }
  const std::string repl = panel_->repl_->text();
  close();
  last_ = env_.editor.replaceInFiles(chosen, repl);
  std::string msg = "Replaced " + count(last_.replaced, "occurrence") + " in " +
                    count(last_.openDocuments + last_.filesWritten, "file");
  if (last_.openDocuments) msg += " (" + std::to_string(last_.openDocuments) + " open, not saved yet)";
  if (last_.stale) msg += "; " + std::to_string(last_.stale) + " changed since the search and were skipped";
  if (!last_.errors.empty()) msg += "; " + last_.errors.front();
  env_.say(msg);
  env_.refused(last_.refused);
  return true;
}

}  // namespace tf::ui
