#include "ui/features/FindInFiles.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace tf::ui {

namespace {
constexpr size_t kLineMax = 220;

// "*.cpp, !third_party" -> include / exclude lists (separators: comma and space).
void splitGlobs(const std::string& s, std::vector<std::string>& inc, std::vector<std::string>& exc) {
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && (s[i] == ',' || s[i] == ' ')) ++i;
    size_t j = i;
    while (j < s.size() && s[j] != ',' && s[j] != ' ') ++j;
    if (j > i) {
      const bool neg = s[i] == '!';
      if (j > i + (neg ? 1 : 0)) (neg ? exc : inc).push_back(s.substr(i + (neg ? 1 : 0), j - i - (neg ? 1 : 0)));
    }
    i = j;
  }
}
}  // namespace

// ListPopup plus the three option toggles (Alt+C / Alt+W / Alt+R), which the InputLine does not handle.
class FindInFiles::Popup : public ListPopup {
 public:
  std::function<void(char)> toggle;
  bool onEvent(const InputEvent& e) override {
    if (e.type == InputType::KeyDown && (e.mods & ModAlt) && !(e.mods & ModCtrl)) {
      const char k = static_cast<char>(e.key);
      if (k == 'C' || k == 'W' || k == 'R') {
        if (toggle) toggle(k);
        return true;
      }
    }
    return ListPopup::onEvent(e);
  }
};

FindInFiles::~FindInFiles() {
  alive_.reset();
  cancel();
}

SearchOptions FindInFiles::parse(const std::string& text, bool cs, bool word, bool regex) {
  SearchOptions o;
  const size_t sep = text.find(" :: ");
  o.query = text.substr(0, sep);
  o.caseSensitive = cs;
  o.wholeWord = word;
  o.regex = regex;
  if (sep != std::string::npos) splitGlobs(text.substr(sep + 4), o.includeGlobs, o.excludeGlobs);
  return o;
}

void FindInFiles::open(const std::string& initialQuery) {
  close();
  auto p = std::make_unique<Popup>();
  p->input()->setPlaceholder("Find in files   (Alt+C case, Alt+W word, Alt+R regex;  query :: *.cpp, !third_party)");
  p->setMaxVisibleRows(22);
  p->setEmptyText("Type to search the workspace");
  p->onQuery([this](const std::string&) { queryChanged(); });
  p->onAccept([this](int row) { accept(row); });
  p->onClosed([this] {
    popup_ = nullptr;
    pending_ = false;
    cancel();
  });
  p->toggle = [this](char k) {
    (k == 'C' ? cs_ : k == 'W' ? word_ : regex_) ^= true;
    popup_->setStatus(statusText());
    queryChanged();
  };
  popup_ = ListPopup::open(env_.root, std::move(p));
  popup_->setStatus(statusText());
  if (!initialQuery.empty()) {
    popup_->input()->setText(initialQuery, true);
    queryChanged();
  } else if (!all_.empty()) rebuild();  // the previous results stay visible until the query changes
}

void FindInFiles::close() {
  if (popup_) popup_->close();
}

void FindInFiles::cancel() {
  if (running_ && handle_.valid()) handle_.cancel();
  running_ = false;
  ++seq_;  // batches still queued for the cancelled search are dropped
}

void FindInFiles::queryChanged() {
  pending_ = true;
  lastEdit_ = env_.clock ? env_.clock() : 0;
  cancel();
}

int FindInFiles::tick(int64_t now) {
  int wait = -1;
  auto soonest = [&](int64_t due) {
    const int ms = static_cast<int>(std::max<int64_t>(1, due - now));
    wait = wait < 0 ? ms : std::min(wait, ms);
  };
  if (popup_ && pending_) {
    if (now - lastEdit_ >= debounceMs_) start();
    else soonest(lastEdit_ + debounceMs_);
  }
  if (dirty_) {
    if (now - lastRebuild_ >= rebuildMs_) rebuild();
    else soonest(lastRebuild_ + rebuildMs_);
  }
  return wait;
}

void FindInFiles::start() {
  pending_ = false;
  const std::string text = popup_ ? popup_->input()->text() : std::string();
  cancel();
  all_.clear();
  order_.clear();
  summary_ = {};
  cur_ = -1;
  dirty_ = true;
  SearchOptions o = parse(text, cs_, word_, regex_);
  if (o.query.empty()) {
    running_ = false;
    rebuild();
    return;
  }
  running_ = true;
  const uint64_t seq = ++seq_;
  std::weak_ptr<int> alive = alive_;
  auto onBatch = [this, seq, alive](const std::vector<SearchMatch>& b) {
    if (!alive.expired()) this->onBatch(seq, b);
  };
  auto onDone = [this, seq, alive](const SearchSummary& s) {
    if (!alive.expired()) this->onDone(seq, s);
  };
  handle_ = search_ ? search_(o.query, o, onBatch, onDone) : env_.editor.findInWorkspace(o.query, o, onBatch, onDone);
  rebuild();
}

void FindInFiles::onBatch(uint64_t seq, const std::vector<SearchMatch>& b) {
  if (seq != seq_) return;  // superseded or cancelled
  all_.insert(all_.end(), b.begin(), b.end());
  dirty_ = true;
}

void FindInFiles::onDone(uint64_t seq, const SearchSummary& s) {
  if (seq != seq_) return;
  running_ = false;
  summary_ = s;
  rebuild();
}

std::string FindInFiles::statusText() const {
  std::string s = std::string(cs_ ? "[Aa]" : " Aa ") + (word_ ? "[W]" : " W ") + (regex_ ? "[.*]" : " .* ") + "  ";
  if (!summary_.error.empty()) return s + summary_.error;
  std::set<std::filesystem::path> files;
  for (const SearchMatch& m : all_) files.insert(m.file);
  if (all_.empty() && !running_ && !summary_.filesSearched) return s;
  s += std::to_string(all_.size()) + " matches in " + std::to_string(files.size()) + " files";
  if (summary_.truncated) s += " (truncated)";
  if (running_) s += " - searching\xE2\x80\xA6";
  return s;
}

void FindInFiles::rebuild() {
  dirty_ = false;
  lastRebuild_ = env_.clock ? env_.clock() : 0;
  const std::filesystem::path root = env_.editor.workspace().root();
  std::map<std::string, std::vector<size_t>> byFile;  // sorted by relative path
  for (size_t i = 0; i < all_.size(); ++i) byFile[relativePath(root, all_[i].file)].push_back(i);
  order_.clear();
  for (auto& [path, idx] : byFile) {
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
      return std::tie(all_[a].line, all_[a].column) < std::tie(all_[b].line, all_[b].column);
    });
    order_.insert(order_.end(), idx.begin(), idx.end());
  }
  setMarkers();
  if (!popup_) return;
  std::string keep = popup_->selectedRow() ? popup_->selectedRow()->key : std::string();
  std::vector<ListRow> rows;
  rowMatch_.clear();
  size_t pos = 0;
  for (auto& [path, idx] : byFile) {
    ListRow h;
    h.key = "f:" + path;
    h.primary = path;
    h.badge = std::to_string(idx.size());
    h.icon = "\xE2\x96\xBE";
    rows.push_back(h);
    rowMatch_.push_back(static_cast<int>(pos));
    for (size_t k = 0; k < idx.size(); ++k, ++pos) {
      const SearchMatch& m = all_[order_[pos]];
      std::string t = m.text;
      size_t lead = 0;
      while (lead < t.size() && (t[lead] == ' ' || t[lead] == '\t')) ++lead;
      t.erase(0, lead);
      std::replace(t.begin(), t.end(), '\t', ' ');
      ListRow r;
      r.key = "m:" + path + ":" + std::to_string(m.line) + ":" + std::to_string(m.column);
      r.detail = std::to_string(m.line + 1);
      const int from = m.column - m.textOffset - static_cast<int>(lead), to = from + m.length;
      if (t.size() > kLineMax) t.resize(kLineMax);
      for (int b = std::max(0, from); b < to && b < static_cast<int>(t.size()); ++b)
        if ((static_cast<unsigned char>(t[static_cast<size_t>(b)]) & 0xC0) != 0x80) r.match.push_back(b);
      r.primary = std::move(t);
      rows.push_back(std::move(r));
      rowMatch_.push_back(static_cast<int>(pos));
    }
  }
  popup_->setEmptyText(all_.empty() && !running_ && summary_.filesSearched ? "No matches"
                                                                           : "Type to search the workspace");
  popup_->setStatus(statusText());
  const int first = rows.size() > 1 ? 1 : -1;  // the first match, not the file header
  popup_->setRows(std::move(rows), first);
  if (!keep.empty()) {
    const int at = popup_->indexOfKey(keep);
    if (at >= 0) popup_->setSelected(at);
  }
}

void FindInFiles::setMarkers() {
  if (!markers_) return;
  std::map<std::filesystem::path, std::vector<Range>> per;
  for (size_t i : order_) {
    const SearchMatch& m = all_[i];
    per[m.file].push_back({{m.line, m.column}, {m.line, m.column + m.length}});
  }
  for (const auto& f : marked_)
    if (!per.count(f)) markers_(f, {});
  marked_.clear();
  for (auto& [f, r] : per) {
    markers_(f, r);
    marked_.insert(f);
  }
}

void FindInFiles::accept(int row) {
  if (row < 0 || row >= static_cast<int>(rowMatch_.size())) return;
  cur_ = rowMatch_[static_cast<size_t>(row)];
  std::string err;
  const SearchMatch& m = all_[order_[static_cast<size_t>(cur_)]];
  if (!env_.editor.gotoSearchMatch(m, &err)) env_.say("Cannot open " + m.file.string() + ": " + err);
}

bool FindInFiles::step(bool forward) {
  if (order_.empty()) {
    env_.say("No search results");
    return false;
  }
  const int n = static_cast<int>(order_.size());
  cur_ = cur_ < 0 ? (forward ? 0 : n - 1) : ((cur_ + (forward ? 1 : n - 1)) % n);
  std::string err;
  const SearchMatch& m = all_[order_[static_cast<size_t>(cur_)]];
  if (!env_.editor.gotoSearchMatch(m, &err)) {
    env_.say("Cannot open " + m.file.string() + ": " + err);
    return false;
  }
  env_.say("Result " + std::to_string(cur_ + 1) + " of " + std::to_string(n));
  return true;
}

}  // namespace tf::ui
