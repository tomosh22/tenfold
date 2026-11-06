#include "ui/view/FoldModel.h"

#include <algorithm>
#include <cctype>

#include "ui/view/Highlighter.h"

namespace tf::ui {

namespace {

// "#pragma region" / "#pragma endregion" directive: 1 = region, 2 = endregion, 0 = anything else. `d` is the
// directive text from its '#'.
int pragmaKind(std::string_view d) {
  size_t i = 1;  // past '#'
  auto skipWs = [&] {
    while (i < d.size() && (d[i] == ' ' || d[i] == '\t')) ++i;
  };
  auto word = [&](std::string_view w) {
    if (d.substr(i, w.size()) != w) return false;
    const size_t e = i + w.size();
    if (e < d.size() && (std::isalnum(static_cast<unsigned char>(d[e])) || d[e] == '_')) return false;
    i = e;
    return true;
  };
  skipWs();
  if (!word("pragma")) return 0;
  skipWs();
  if (word("region")) return 1;
  if (word("endregion")) return 2;
  return 0;
}

// True when the text before a '{' (`head`, from the start of the line) opens the body of a namespace / class / struct /
// union / enum / extern "C": the last statement-ish segment holds one of those keywords and no '(' (a function
// returning `struct S` or taking a `class T` template parameter has one).
bool opensTypeBody(std::string_view head) {
  const size_t cut = head.find_last_of(";{}");
  if (cut != std::string_view::npos) head.remove_prefix(cut + 1);
  if (head.find('(') != std::string_view::npos) return false;
  if (head.find("//") != std::string_view::npos || head.find("/*") != std::string_view::npos) {
    // a trailing comment: only the code before it counts
    head = head.substr(0, std::min(head.find("//"), head.find("/*")));
  }
  size_t i = 0;
  while (i < head.size()) {
    if (!(std::isalpha(static_cast<unsigned char>(head[i])) || head[i] == '_')) {
      ++i;
      continue;
    }
    size_t e = i;
    while (e < head.size() && (std::isalnum(static_cast<unsigned char>(head[e])) || head[e] == '_')) ++e;
    const std::string_view w = head.substr(i, e - i);
    if (w == "namespace" || w == "class" || w == "struct" || w == "union" || w == "enum") return true;
    if (w == "extern") return head.find('"', e) != std::string_view::npos;
    i = e;
  }
  return false;
}

bool byStart(const FoldModel::Region& r, int line) { return r.start < line; }

}  // namespace

std::vector<FoldModel::Region> FoldModel::detect(const std::vector<std::string>& lines) {
  return detect(lines, Options{});
}

std::vector<FoldModel::Region> FoldModel::detect(const std::vector<std::string>& lines, const Options& opt) {
  std::vector<Region> out;
  if (!opt.enabled) return out;
  LexState st;
  std::vector<LexSpan> spans;
  struct Brace {
    int line;
    bool type;
  };
  std::vector<Brace> braces;
  std::vector<int> pragmas;
  int commentStart = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    const std::string& s = lines[static_cast<size_t>(i)];
    const LexState in = st;
    spans.clear();
    st = lexLine(s, in, spans);
    if (in.mode != LexState::Block && st.mode == LexState::Block) {
      commentStart = i;
    } else if (in.mode == LexState::Block && st.mode != LexState::Block) {
      if (commentStart >= 0 && commentStart < i) out.push_back({commentStart, i, Kind::Comment});
      commentStart = -1;
    }
    for (const LexSpan& sp : spans) {
      if (sp.kind == Lex::Punct && sp.end == sp.begin + 1) {
        const char c = s[sp.begin];
        if (c == '{') {
          std::string_view head = std::string_view(s).substr(0, sp.begin);
          bool blank = true;
          for (char h : head) blank &= h == ' ' || h == '\t' || h == '\r';
          // Allman style: the head is the closest earlier line with code
          for (int j = i - 1; blank && j >= 0 && j >= i - 4; --j) {
            head = lines[static_cast<size_t>(j)];
            blank = head.find_first_not_of(" \t\r") == std::string_view::npos;
          }
          braces.push_back({i, opensTypeBody(head)});
        } else if (c == '}' && !braces.empty()) {
          const Brace b = braces.back();
          braces.pop_back();
          if (b.line < i) out.push_back({b.line, i, Kind::Brace, b.type});
        }
      } else if (opt.pragmaRegions && sp.kind == Lex::Preprocessor && in.mode == LexState::None) {
        const int k = pragmaKind(std::string_view(s).substr(sp.begin, sp.end - sp.begin));
        if (k == 1) {
          pragmas.push_back(i);
        } else if (k == 2 && !pragmas.empty()) {
          const int b = pragmas.back();
          pragmas.pop_back();
          if (b < i) out.push_back({b, i, Kind::Pragma});
        }
      }
    }
  }
  // Outermost first among equal starts, then keep one region per start line.
  std::sort(out.begin(), out.end(),
            [](const Region& a, const Region& b) { return a.start != b.start ? a.start < b.start : a.end > b.end; });
  out.erase(std::unique(out.begin(), out.end(), [](const Region& a, const Region& b) { return a.start == b.start; }),
            out.end());
  if (!opt.statementBlocks) {  // keep the brace blocks that are not inside a function body
    std::vector<Region> kept;
    int funcEnd = -1;
    for (const Region& r : out) {
      if (r.kind == Kind::Brace && !r.type) {
        if (r.start <= funcEnd) continue;
        funcEnd = r.end;
      }
      kept.push_back(r);
    }
    out = std::move(kept);
  }
  return out;
}

const FoldModel::Region* FoldModel::regionStartingAt(int line) const {
  auto it = std::lower_bound(regions_.begin(), regions_.end(), line, byStart);
  return it != regions_.end() && it->start == line ? &*it : nullptr;
}

const FoldModel::Region* FoldModel::innermostAt(int line) const {
  const Region* best = nullptr;
  for (const Region& r : regions_) {
    if (r.start > line) break;
    if (r.end >= line && !isCollapsedAt(r.start)) best = &r;  // sorted by start: a later match is nested deeper
  }
  return best;
}

std::vector<int> FoldModel::depths() const {
  std::vector<int> d(regions_.size(), 0);
  std::vector<int> ends;  // ends of the enclosing regions, innermost last
  for (size_t i = 0; i < regions_.size(); ++i) {
    while (!ends.empty() && ends.back() < regions_[i].start) ends.pop_back();
    d[i] = static_cast<int>(ends.size());
    ends.push_back(regions_[i].end);
  }
  return d;
}

const FoldModel::Region* FoldModel::collapsedAt(int line) const {
  auto it = std::lower_bound(collapsed_.begin(), collapsed_.end(), line, byStart);
  return it != collapsed_.end() && it->start == line ? &*it : nullptr;
}

bool FoldModel::isCollapsedAt(int line) const { return collapsedAt(line) != nullptr; }

bool FoldModel::isHidden(int line) const {
  auto it = std::upper_bound(hidden_.begin(), hidden_.end(), line,
                             [](int l, const WrapLayout::HiddenRange& h) { return l < h.first; });
  return it != hidden_.begin() && line <= (it - 1)->last;
}

int FoldModel::headOf(int line) const {
  auto it = std::upper_bound(hidden_.begin(), hidden_.end(), line,
                             [](int l, const WrapLayout::HiddenRange& h) { return l < h.first; });
  return it != hidden_.begin() && line <= (it - 1)->last ? (it - 1)->first - 1 : line;
}

void FoldModel::rebuildHidden() {
  hidden_.clear();
  for (const Region& r : collapsed_) {  // sorted by start
    const int a = r.start + 1, b = r.end;
    if (!hidden_.empty() && a <= hidden_.back().last + 1) hidden_.back().last = std::max(hidden_.back().last, b);
    else hidden_.push_back({a, b});
  }
  ++gen_;
}

bool FoldModel::setRegions(std::vector<Region> regions) {
  regions_ = std::move(regions);
  fresh_ = true;
  bool changed = false;
  for (size_t i = collapsed_.size(); i-- > 0;) {
    const Region* r = regionStartingAt(collapsed_[i].start);
    if (!r) {
      collapsed_.erase(collapsed_.begin() + static_cast<long>(i));
      changed = true;
    } else if (!(*r == collapsed_[i])) {
      collapsed_[i] = *r;
      changed = true;
    }
  }
  if (changed) rebuildHidden();
  return changed;
}

bool FoldModel::collapse(const Region& r) {
  auto it = std::lower_bound(collapsed_.begin(), collapsed_.end(), r.start, byStart);
  if (it != collapsed_.end() && it->start == r.start) return false;
  collapsed_.insert(it, r);
  rebuildHidden();
  return true;
}

bool FoldModel::collapseAt(int line, Region* done) {
  const Region* r = innermostAt(line);
  if (!r) return false;
  const Region copy = *r;
  if (done) *done = copy;
  return collapse(copy);
}

bool FoldModel::expandAt(int line) {
  auto it = std::lower_bound(collapsed_.begin(), collapsed_.end(), line, byStart);
  if (it == collapsed_.end() || it->start != line) return false;
  collapsed_.erase(it);
  rebuildHidden();
  return true;
}

bool FoldModel::toggleAt(int line, Region* collapsedNow) {
  if (isCollapsedAt(line)) return expandAt(line);
  return collapseAt(line, collapsedNow);
}

bool FoldModel::collapseAll(bool recursive) {
  const std::vector<int> d = depths();
  std::vector<Region> next;
  next.reserve(regions_.size());
  size_t c = 0;
  bool changed = false;
  for (size_t i = 0; i < regions_.size(); ++i) {
    while (c < collapsed_.size() && collapsed_[c].start < regions_[i].start) next.push_back(collapsed_[c++]);
    const bool was = c < collapsed_.size() && collapsed_[c].start == regions_[i].start;
    if (was) {
      next.push_back(collapsed_[c++]);
    } else if (recursive || d[i] == 0) {
      next.push_back(regions_[i]);
      changed = true;
    }
  }
  while (c < collapsed_.size()) next.push_back(collapsed_[c++]);
  if (!changed) return false;
  collapsed_ = std::move(next);
  rebuildHidden();
  return true;
}

bool FoldModel::expandAll(bool recursive) {
  if (collapsed_.empty()) return false;
  if (recursive) {
    collapsed_.clear();
  } else {
    std::vector<Region> keep;
    for (const Region& c : collapsed_)
      if (isHidden(c.start)) keep.push_back(c);
    if (keep.size() == collapsed_.size()) return false;
    collapsed_ = std::move(keep);
  }
  rebuildHidden();
  return true;
}

bool FoldModel::toggleAll() {
  if (regions_.empty() && collapsed_.empty()) return false;
  return collapsed_.empty() ? collapseAll(true) : expandAll(true);
}

bool FoldModel::collapseToDefinitions() {
  std::vector<Region> next;
  int funcEnd = -1;  // regions are sorted by start: anything starting inside the last member body is part of it
  for (const Region& r : regions_)
    if (r.kind == Kind::Brace && !r.type && r.start > funcEnd) {
      next.push_back(r);
      funcEnd = r.end;
    }
  if (next == collapsed_) return false;
  collapsed_ = std::move(next);
  rebuildHidden();
  return true;
}

bool FoldModel::expandCovering(int line) {
  bool changed = false;
  for (size_t i = collapsed_.size(); i-- > 0;)
    if (collapsed_[i].start < line && line <= collapsed_[i].end) {
      collapsed_.erase(collapsed_.begin() + static_cast<long>(i));
      changed = true;
    }
  if (changed) rebuildHidden();
  return changed;
}

bool FoldModel::onChange(const Document::Change& c) {
  const int s = c.removed.start.line, e = c.removed.end.line, delta = c.lineDelta;
  fresh_ = false;
  bool changed = false;
  size_t w = 0;
  for (size_t i = 0; i < collapsed_.size(); ++i) {
    Region r = collapsed_[i];
    if (r.end < s) {
      // before the edit: stays
    } else if (r.start > e) {
      r.start += delta;
      r.end += delta;
      changed |= delta != 0;
    } else {
      changed = true;  // the edit touches it: expanded
      continue;
    }
    collapsed_[w++] = r;
  }
  collapsed_.resize(w);
  w = 0;
  for (size_t i = 0; i < regions_.size(); ++i) {
    Region r = regions_[i];
    if (r.end < s) {
      // before the edit: stays
    } else if (r.start > e) {
      r.start += delta;
      r.end += delta;
    } else if (r.start < s && e < r.end) {
      r.end += delta;  // the edit is inside: the region keeps its head line and its end moves
    } else {
      continue;
    }
    regions_[w++] = r;
  }
  regions_.resize(w);
  if (changed) rebuildHidden();
  return changed;
}

void FoldModel::clear() {
  regions_.clear();
  fresh_ = false;
  if (!collapsed_.empty()) {
    collapsed_.clear();
    rebuildHidden();
  }
}

}  // namespace tf::ui
