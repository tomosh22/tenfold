#include "core/Document.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace tf {

Document::Document(std::filesystem::path path) : path_(std::move(path)) {}

Document Document::fromText(std::filesystem::path path, std::string text) {
  Document d(std::move(path));
  d.buf_.setText(text);
  return d;
}

bool Document::loadFromDisk(std::string* error) {
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot open " + path_.string();
    return false;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  buf_.setText(ss.str());
  dirty_ = false;
  onDisk_ = true;
  undo_.clear();
  redo_.clear();
  open_ = UndoGroup{};
  depth_ = 0;
  restored_.clear();
  return true;
}

bool Document::saveToDisk(std::string* error) {
  std::ofstream out(path_, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (error) *error = "cannot write " + path_.string();
    return false;
  }
  out << buf_.textForDisk();
  dirty_ = false;
  onDisk_ = true;
  return true;
}

std::string Document::uri() const { return pathToUri(path_); }

Document::BatchResult Document::applyBatch(const std::vector<BatchEdit>& edits, Batch* rec, bool skipNoops) {
  const size_t n = edits.size();
  std::vector<Range> rs(n);
  for (size_t k = 0; k < n; ++k) {
    Range r{buf_.clamp(edits[k].range.start), buf_.clamp(edits[k].range.end)};
    if (r.end < r.start) std::swap(r.start, r.end);
    if (k > 0 && r.start < rs[k - 1].end) {  // contract violation: keep the edits disjoint
      r.start = rs[k - 1].end;
      if (r.end < r.start) r.end = r.start;
    }
    rs[k] = r;
  }
  std::vector<std::string> olds(n);
  std::vector<TextBuffer::Edit> be(n);
  for (size_t k = 0; k < n; ++k) {
    olds[k] = buf_.textInRange(rs[k]);
    be[k] = {rs[k], edits[k].text};
  }
  BatchResult out;
  out.edits = buf_.replaceMany(be);

  // Where each inserted text sits afterwards: remap through the preceding edits (everything at or after the end of
  // edit k-1 moves rigidly: lines shift, and columns on the end line follow the end of its inserted text).
  out.after.resize(n);
  Position endPre{}, endPost{};
  auto remap = [&](Position p, size_t k) {
    if (k == 0) return p;
    if (p.line == endPre.line) return Position{endPost.line, endPost.column + (p.column - endPre.column)};
    return Position{p.line + (endPost.line - endPre.line), p.column};
  };
  auto skipped = [&](size_t k) { return skipNoops && rs[k].empty() && edits[k].text.empty(); };
  size_t real = 0;
  for (size_t k = 0; k < n; ++k) {
    const Position s = remap(rs[k].start, k), e = remap(out.edits[k].inserted.end, k);
    out.after[k] = {s, e};
    endPre = rs[k].end;
    endPost = e;
    if (!skipped(k)) ++real;
  }
  if (rec)
    for (size_t k = 0; k < n; ++k)
      if (!skipped(k)) rec->push_back({out.after[k], edits[k].text, std::move(olds[k])});

  if (real == 0) return out;
  dirty_ = true;
  auto copy = listeners_;  // listeners may add/remove listeners
  int v = version_;
  for (size_t k = n; k-- > 0;) {  // one event per edit, last to first, so each range is valid when it is handled
    if (skipped(k)) continue;
    version_ = ++v;
    Change ch{rs[k], edits[k].text, out.edits[k].lineDelta};
    for (auto& [id, l] : copy) l(*this, ch);
  }
  version_ = v;
  return out;
}

void Document::record(Batch&& b) {
  if (b.empty()) return;
  if (depth_ > 0) {
    open_.batches.push_back(std::move(b));
  } else {
    UndoGroup g;
    g.batches.push_back(std::move(b));
    undo_.push_back(std::move(g));
  }
  redo_.clear();
}

TextBuffer::EditResult Document::replace(Range r, std::string_view text) {
  Batch rec;
  auto res = applyBatch({{r, std::string(text)}}, &rec, false);
  record(std::move(rec));
  return res.edits[0];
}

std::vector<Range> Document::replaceMany(std::vector<BatchEdit> edits) {
  Batch rec;
  auto res = applyBatch(edits, &rec, true);
  record(std::move(rec));
  return std::move(res.after);
}

void Document::beginGroup(std::vector<Selection> carets, bool joinPrevious) {
  if (depth_++ > 0) return;
  if (joinPrevious && !undo_.empty()) {
    open_ = std::move(undo_.back());  // keeps its before-carets
    undo_.pop_back();
  } else {
    open_ = UndoGroup{};
    open_.before = std::move(carets);
  }
}

void Document::endGroup(std::vector<Selection> carets) {
  if (depth_ == 0) return;
  if (!carets.empty()) open_.after = std::move(carets);
  if (--depth_ > 0) return;
  if (!open_.batches.empty()) undo_.push_back(std::move(open_));
  open_ = UndoGroup{};
}

// Reverts the group's batches last to first, each as one batched edit, and returns the group that reverts that.
Document::UndoGroup Document::flipGroup(UndoGroup&& g) {
  UndoGroup out;
  out.before = std::move(g.before);
  out.after = std::move(g.after);
  for (size_t i = g.batches.size(); i-- > 0;) {
    std::vector<BatchEdit> edits;
    edits.reserve(g.batches[i].size());
    for (UndoRec& r : g.batches[i]) edits.push_back({r.range, std::move(r.alt)});
    Batch flipped;
    applyBatch(edits, &flipped, false);
    out.batches.push_back(std::move(flipped));
  }
  return out;
}

bool Document::undo() {
  if (undo_.empty() || depth_ > 0) return false;
  UndoGroup g = std::move(undo_.back());
  undo_.pop_back();
  restored_ = g.before;
  redo_.push_back(flipGroup(std::move(g)));
  return true;
}

bool Document::redo() {
  if (redo_.empty() || depth_ > 0) return false;
  UndoGroup g = std::move(redo_.back());
  redo_.pop_back();
  restored_ = g.after;
  undo_.push_back(flipGroup(std::move(g)));
  return true;
}

int Document::addListener(Listener l) {
  int id = nextListener_++;
  listeners_.emplace_back(id, std::move(l));
  return id;
}

void Document::removeListener(int id) {
  std::erase_if(listeners_, [id](auto& p) { return p.first == id; });
}

std::string pathToUri(const std::filesystem::path& p) { return pathToUri(p.generic_string(), nativePathStyle()); }

std::filesystem::path uriToPath(const std::string& uri) {
  return std::filesystem::path(uriToPathString(uri, nativePathStyle()));
}

}  // namespace tf
