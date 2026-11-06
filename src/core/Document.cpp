#include "core/Document.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/PathUtil.h"

namespace tf {

Document::Document(std::filesystem::path path) : path_(std::move(path)) {}

Document Document::fromText(std::filesystem::path path, std::string text) {
  Document d(std::move(path));
  d.buf_.setText(text);
  return d;
}

Document Document::untitled(std::filesystem::path path, LineEnding lineEnding) {
  Document d(std::move(path));
  d.buf_.setLineEnding(lineEnding);
  d.untitled_ = true;
  return d;
}

bool Document::loadFromDisk(std::string* error) {
  std::string bytes;
  if (!readRegularFile(path_, bytes, error)) return false;
  DecodedText dec = decodeFileBytes(bytes);
  buf_.setText(dec.text);
  encoding_ = dec.encoding;
  dirty_ = false;
  onDisk_ = true;
  undo_.clear();
  redo_.clear();
  open_ = UndoGroup{};
  depth_ = 0;
  restored_.clear();
  return true;
}

UnencodableReport Document::findUnencodable(Encoding e) const {
  UnencodableReport rep;
  rep.encoding = e;
  if (e == Encoding::Utf8) return rep;
  for (int l = 0; l < buf_.lineCount(); ++l) collectUnencodable(buf_.line(l), l, rep);
  return rep;
}

bool Document::saveToDisk(std::string* error, UnencodableReport* refused) {
  UnencodableReport rep = findUnencodable();
  if (!rep.empty()) {
    if (error) *error = pathUtf8(path_.filename()) + " was not saved: " + describeUnencodable(rep);
    if (refused) *refused = std::move(rep);
    return false;
  }
  std::string bytes;
  if (!encodeText(buf_.textForDisk(), encoding_, bytes)) {  // cannot happen: the scan above found nothing
    if (error)
      *error = pathUtf8(path_.filename()) + " was not saved: " + encodingName(encoding_) + " cannot hold its text";
    return false;
  }
  std::ofstream out(path_, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (error) *error = "cannot write " + pathUtf8(path_);
    return false;
  }
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  dirty_ = false;
  onDisk_ = true;
  return true;
}

void Document::convertLineEndings(LineEnding e) {
  std::vector<BatchEdit> edits;
  const int n = buf_.lineCount();
  for (int i = 0; i + 1 < n; ++i)
    if (buf_.endingAfter(i) != e)
      edits.push_back(
          {{{i, static_cast<int>(buf_.line(i).size())}, {i + 1, 0}}, std::string(TextBuffer::terminatorText(e))});
  if (edits.empty()) {
    if (n == 1) buf_.setLineEnding(e);
    return;
  }
  beginGroup();
  replaceMany(std::move(edits));
  endGroup();
  buf_.compact();
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
    olds[k] = buf_.textInRangeRaw(rs[k]);  // with the real line breaks, so undo restores them byte for byte
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
