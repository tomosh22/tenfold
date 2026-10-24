#include "core/Document.h"

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

void Document::applyRaw(Range r, std::string_view text, UndoRecord* rec) {
  r.start = buf_.clamp(r.start);
  r.end = buf_.clamp(r.end);
  if (r.end < r.start) std::swap(r.start, r.end);
  std::string old = buf_.textInRange(r);
  auto res = buf_.replace(r, text);
  if (rec) {
    rec->range = res.inserted;  // to undo, replace the inserted range with old text
    rec->oldText = old;
    rec->newText = std::string(text);
  }
  ++version_;
  dirty_ = true;
  Change ch{r, std::string(text), res.lineDelta};
  auto copy = listeners_;  // listeners may add/remove listeners
  for (auto& [id, l] : copy) l(*this, ch);
}

void Document::replace(Range r, std::string_view text) {
  UndoRecord rec;
  applyRaw(r, text, &rec);
  undo_.push_back(std::move(rec));
  redo_.clear();
}

bool Document::undo() {
  if (undo_.empty()) return false;
  UndoRecord rec = std::move(undo_.back());
  undo_.pop_back();
  UndoRecord inverse;
  applyRaw(rec.range, rec.oldText, &inverse);
  // inverse.range now covers oldText; redo replaces it with newText.
  redo_.push_back({inverse.range, rec.oldText, rec.newText});
  return true;
}

bool Document::redo() {
  if (redo_.empty()) return false;
  UndoRecord rec = std::move(redo_.back());
  redo_.pop_back();
  UndoRecord fwd;
  applyRaw(rec.range, rec.newText, &fwd);
  undo_.push_back({fwd.range, rec.oldText, rec.newText});
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
