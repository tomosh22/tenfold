#include "app/Editor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

namespace tf {

namespace fs = std::filesystem;

namespace {
// Key of an open document in docs_ / diskHash_: canonical and, on Windows, case/separator-insensitive.
std::string docKey(const fs::path& p) {
  std::error_code ec;
  return pathKey(fs::weakly_canonical(p, ec));
}

// Hash of a file's raw bytes; 0 when it cannot be read.
size_t hashFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return 0;
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string bytes = ss.str();
  return std::hash<std::string>{}(bytes) ^ (bytes.size() * 0x9e3779b97f4a7c15ULL);
}

bool isIdentByte(unsigned char c) { return std::isalnum(c) || c == '_' || c == '$' || c >= 0x80; }  // CppLexer rule
bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Start of the UTF-8 character that ends at byte `col` (col > 0).
int prevCharStart(const std::string& l, int col) {
  --col;
  while (col > 0 && isContinuation(l[static_cast<size_t>(col)])) --col;
  return col;
}
// End of the UTF-8 character that starts at byte `col` (col < size).
int nextCharEnd(const std::string& l, int col) {
  const int n = static_cast<int>(l.size());
  ++col;
  while (col < n && isContinuation(l[static_cast<size_t>(col)])) ++col;
  return col;
}
// Column in code points of byte offset `col`, and back (clamped to the line end, never inside a character).
int codepointColumn(const std::string& l, int col) {
  int n = 0;
  for (int i = 0; i < col && i < static_cast<int>(l.size()); ++i)
    if (!isContinuation(l[static_cast<size_t>(i)])) ++n;
  return n;
}
int byteColumn(const std::string& l, int codepoints) {
  int i = 0;
  const int n = static_cast<int>(l.size());
  while (codepoints > 0 && i < n) {
    i = nextCharEnd(l, i);
    --codepoints;
  }
  return i;
}
bool isSingleCodepoint(const std::string& t) {
  if (t.empty()) return false;
  unsigned char c = static_cast<unsigned char>(t[0]);
  size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
  return len != 0 && t.size() == len;
}
bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

// 0 = whitespace, 1 = identifier characters, 2 = punctuation.
int charClass(const std::string& l, int i) {
  const unsigned char c = static_cast<unsigned char>(l[static_cast<size_t>(i)]);
  return isBlank(static_cast<char>(c)) ? 0 : isIdentByte(c) ? 1 : 2;
}
Position wordRight(const TextBuffer& b, Position p) {
  const std::string& l = b.line(p.line);
  const int n = static_cast<int>(l.size());
  if (p.column >= n) return p.line + 1 < b.lineCount() ? Position{p.line + 1, 0} : p;
  int c = p.column;
  const int k = charClass(l, c);
  if (k != 0)
    while (c < n && charClass(l, c) == k) ++c;
  while (c < n && charClass(l, c) == 0) ++c;
  return {p.line, c};
}
Position wordLeft(const TextBuffer& b, Position p) {
  if (p.column <= 0) return p.line > 0 ? Position{p.line - 1, static_cast<int>(b.line(p.line - 1).size())} : p;
  const std::string& l = b.line(p.line);
  int c = std::min(p.column, static_cast<int>(l.size()));
  while (c > 0 && charClass(l, c - 1) == 0) --c;
  if (c == 0) return {p.line, 0};
  const int k = charClass(l, c - 1);
  while (c > 0 && charClass(l, c - 1) == k) --c;
  return {p.line, c};
}

// Where position `p` (at or after `endPre`) ends up when the text up to `endPre` was replaced such that `endPre`
// became `endPost`. This composes the per-edit remapping of TextBuffer::EditResult for a whole bottom-to-top batch.
Position remapAfter(Position p, Position endPre, Position endPost) {
  if (p.line == endPre.line) return {endPost.line, endPost.column + (p.column - endPre.column)};
  return {p.line + (endPost.line - endPre.line), p.column};
}
}  // namespace

Editor::Editor(fs::path root) : Editor(std::move(root), Options()) {}

Editor::Editor(fs::path root, Options opt) : ws_(std::move(root)), opt_(std::move(opt)), nav_(opt_.navigation) {
  if (!opt_.semantic.log) opt_.semantic.log = opt_.log;
  ws_.loadSettings();
}

Editor::~Editor() { shutdown(); }

bool Editor::start(std::string* error) {
  auto t0 = std::chrono::steady_clock::now();
  ws_.scan();
  if (opt_.log) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    opt_.log("workspace scan: " + std::to_string(ws_.fileCount()) + " files in " + std::to_string(ms) + " ms");
  }
  semantic_ = std::make_unique<SemanticEngine>(ws_, opt_.semantic);
  semantic_->onDiagnostics([this](const fs::path& file, std::vector<Diagnostic> ds) {
    post([this, file, ds = std::move(ds)]() mutable { diagnostics_[file.generic_string()] = std::move(ds); });
  });
  if (ws_.settings().watchFiles) startWatching();
  bool ok = semantic_->start(error);
  // Builds often live in an ignored directory (build/); watch just that folder
  // so a regenerated compile_commands.json is noticed.
  if (watcher_) {
    fs::path dir = semantic_->status().compdbPath.parent_path();
    if (!dir.empty() && dir != ws_.cacheDir() && dir != ws_.root() && ws_.isIgnoredDir(dir))
      watcher_->addDirectory(dir);
  }
  return ok;
}

void Editor::shutdown() {
  for (auto& h : searches_) h.cancel();
  for (auto& h : searches_) h.wait();
  searches_.clear();
  if (watcher_) {
    watcher_->stop();  // before the engine goes away: its callback uses it
    watcher_.reset();
  }
  if (semantic_) {
    semantic_->stop();
    semantic_.reset();
  }
}

// ---------------------------------------------------------------------------
// Documents
// ---------------------------------------------------------------------------

Document* Editor::document(const fs::path& path) {
  std::error_code ec;
  auto it = docs_.find(docKey(path));
  return it == docs_.end() ? nullptr : it->second.get();
}

std::vector<Document*> Editor::documents() {
  std::vector<Document*> out;
  for (auto& [k, d] : docs_) out.push_back(d.get());
  return out;
}

Document* Editor::open(const fs::path& pathIn, std::string* error) {
  std::error_code ec;
  // The stored path keeps the caller's spelling (first open wins); only the
  // lookup key is canonicalised and normalised, so cursor/history paths round-trip.
  fs::path path = pathIn.is_absolute() ? pathIn : ws_.root() / pathIn;
  if (Document* d = document(path)) return d;
  auto doc = std::make_unique<Document>(path);
  if (!doc->loadFromDisk(error)) return nullptr;
  Document* raw = doc.get();
  raw->addListener([this](Document& d, const Document::Change& ch) {
    int newLines = 1 + static_cast<int>(std::count(ch.newText.begin(), ch.newText.end(), '\n'));
    nav_.onLinesReplaced(d.path(), ch.removed.start.line, ch.removed.end.line, newLines);
    if (semantic_) semantic_->didChange(d, ch);
    if (current_ == &d && !inEdit_) clampCarets();
  });
  std::string key = docKey(path);
  docs_[key] = std::move(doc);
  diskHash_[key] = hashFile(path);
  if (semantic_) semantic_->didOpen(*raw);
  return raw;
}

bool Editor::close(const fs::path& path) {
  Document* d = document(path);
  if (!d) return false;
  if (semantic_) semantic_->didClose(*d);
  std::error_code ec;
  if (current_ == d) {
    current_ = nullptr;
    cursor_ = {};
    carets_.clear();
    wants_.clear();
    breakTyping();
  }
  std::string key = docKey(d->path());
  diskHash_.erase(key);
  docs_.erase(key);
  return true;
}

bool Editor::save(Document& doc, std::string* error) {
  if (!doc.saveToDisk(error)) return false;
  diskHash_[docKey(doc.path())] = hashFile(doc.path());
  if (semantic_) semantic_->didSave(doc);
  return true;
}

bool Editor::reloadFromDisk(Document& doc, std::string* error) {
  std::ifstream in(doc.path(), std::ios::binary);
  if (!in) {
    if (error) *error = "cannot open " + doc.path().string();
    return false;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  TextBuffer fresh(ss.str());
  const TextBuffer& old = doc.buffer();
  const int oldN = old.lineCount(), newN = fresh.lineCount();
  // Replace only the lines that differ so history entries and clangd keep their place.
  int i = 0;
  while (i < oldN && i < newN && old.line(i) == fresh.line(i)) ++i;
  int j = 0;
  while (j < oldN - i && j < newN - i && old.line(oldN - 1 - j) == fresh.line(newN - 1 - j)) ++j;
  if (!(i == oldN && i == newN)) {
    auto join = [&](int from, int to, bool terminate) {
      std::string t;
      for (int k = from; k < to; ++k) {
        t += fresh.line(k);
        if (terminate || k + 1 < to) t += '\n';
      }
      return t;
    };
    if (j > 0) {
      doc.replace({{i, 0}, {oldN - j, 0}}, join(i, newN - j, true));
    } else {
      int from = (i == oldN || i == newN) && i > 0 ? i - 1 : i;
      doc.replace({{from, 0}, {oldN - 1, static_cast<int>(old.line(oldN - 1).size())}}, join(from, newN, false));
    }
  }
  if (!doc.loadFromDisk(error)) return false;  // clean state, CRLF flag, empty undo stack
  diskHash_[docKey(doc.path())] = hashFile(doc.path());
  return true;
}

// ---------------------------------------------------------------------------
// File watching
// ---------------------------------------------------------------------------

void Editor::startWatching() {
  FileWatcher::Options wo;
  wo.log = opt_.log;
  wo.pollMs = opt_.watchPollMs;
  wo.forcePolling = opt_.watchForcePolling;
  watcher_ = std::make_unique<FileWatcher>(wo);
  bool ok = watcher_->start(
      ws_.root(), [this](const fs::path& dir) { return ws_.isIgnoredDir(dir); },
      [this](FileWatcher::Event ev) {
        // Watcher thread. Index maintenance is thread-safe and kept off the
        // pump thread; everything else goes through post() (constraint 1.2).
        using E = FileWatcher::Event;
        if (!ev.isDirectory) {
          if (ev.kind == E::Created || ev.kind == E::Modified) semantic_->fileChanged(ev.path);
          else if (ev.kind == E::Deleted) semantic_->fileRemoved(ev.path);
          else if (ev.kind == E::Renamed) {
            semantic_->fileRemoved(ev.oldPath);
            semantic_->fileChanged(ev.path);
          }
        }
        post([this, ev = std::move(ev)] { handleFileEvent(ev); });
      });
  if (!ok) {
    if (opt_.log) opt_.log("file watcher: could not start; workspace changes will not be tracked");
    watcher_.reset();
  } else if (opt_.log) {
    opt_.log("file watcher: " + watcher_->mode() + ", " + std::to_string(watcher_->watchCount()) + " directories");
  }
}

void Editor::checkExternalChange(Document& doc) {
  size_t h = hashFile(doc.path());
  if (h == 0) return;  // unreadable / vanished
  size_t& known = diskHash_[docKey(doc.path())];
  if (h == known) return;
  known = h;
  if (externalChange_) externalChange_(doc);
}

void Editor::refreshBuildSystem() {
  if (!semantic_) return;
  if (buildRefreshRunning_) {
    buildRefreshAgain_ = true;
    return;
  }
  buildRefreshRunning_ = true;
  semantic_->refreshCompileCommands(ws_.sourceFiles(), ws_.headerFiles(), [this](bool moved) {
    post([this, moved] {
      buildRefreshRunning_ = false;
      if (!semantic_) return;
      if (moved) {
        std::string err;
        if (!semantic_->restartClangd(&err) && opt_.log && !err.empty()) opt_.log("clangd restart failed: " + err);
      }
      if (buildRefreshAgain_) {
        buildRefreshAgain_ = false;
        refreshBuildSystem();
      }
    });
  });
}

void Editor::rescanWorkspace() {
  std::set<fs::path> before;
  for (auto& f : ws_.filesUnder(ws_.root())) before.insert(f);
  ws_.scan();
  std::set<fs::path> after;
  for (auto& f : ws_.filesUnder(ws_.root())) after.insert(f);
  for (auto& f : before)
    if (!after.count(f)) {
      semantic_->fileRemoved(f);
      nav_.onFileRemoved(f);
    }
  semantic_->indexFilesAsync(ws_.codeFiles());
  for (auto* d : documents()) checkExternalChange(*d);
}

void Editor::handleFileEvent(const FileWatcher::Event& ev) {
  if (!semantic_) return;  // shut down while the event was queued
  using E = FileWatcher::Event;
  auto touched = [&](const fs::path& p) {
    if (Document* d = document(p)) {
      checkExternalChange(*d);
      if (!d->dirty()) semantic_->fileChanged(p, true);
    }
  };
  fs::path buildFile = ev.path;
  switch (ev.kind) {
    case E::Rescan:
      rescanWorkspace();
      refreshBuildSystem();
      return;
    case E::Created:
    case E::Modified:
      if (ev.isDirectory) return;
      ws_.noteFileAdded(ev.path);
      touched(ev.path);
      break;
    case E::Deleted: {
      // A file, or (Windows reports no directory flag) a directory: drop everything at or below it.
      std::vector<fs::path> gone = ws_.filesUnder(ev.path);
      for (auto& f : gone) {
        ws_.noteFileRemoved(f);
        semantic_->fileRemoved(f);
        nav_.onFileRemoved(f);
      }
      nav_.onFileRemoved(ev.path);
      break;
    }
    case E::Renamed:
      if (ev.isDirectory) {
        std::vector<fs::path> moved;
        for (auto& f : ws_.filesUnder(ev.oldPath)) {
          fs::path to = ev.path / f.lexically_relative(ev.oldPath);
          ws_.noteFileRenamed(f, to);
          nav_.onFileRenamed(f, to);
          semantic_->fileRemoved(f);
          moved.push_back(to);
        }
        semantic_->indexFilesAsync(std::move(moved));
      } else {
        ws_.noteFileRenamed(ev.oldPath, ev.path);
        nav_.onFileRenamed(ev.oldPath, ev.path);
        touched(ev.path);  // editors save by renaming a temp file over the target
        if (ev.oldPath.filename() == "CMakeLists.txt" || ev.oldPath.filename() == "compile_commands.json")
          refreshBuildSystem();
      }
      break;
  }
  const std::string name = buildFile.filename().string();
  if (!ev.isDirectory && (name == "compile_commands.json" || name == "CMakeLists.txt")) refreshBuildSystem();
}

// ---------------------------------------------------------------------------
// Caret & navigation
// ---------------------------------------------------------------------------

void Editor::setCurrent(Document* d, Position p) {
  current_ = d;
  cursor_ = {d ? d->path() : fs::path(), d ? d->buffer().clamp(p) : Position{}};
  carets_.clear();
  wants_.clear();
  if (d) {
    carets_.push_back(caretAt(cursor_.pos));
    wants_.push_back(-1);
  }
  breakTyping();
}

void Editor::setPrimaryOnly(Position p) {
  carets_.assign(1, caretAt(current_->buffer().clamp(p)));
  wants_.assign(1, -1);
  cursor_.pos = carets_[0].head;
}

void Editor::moveCursor(Position p) {
  if (!current_) return;
  breakTyping();
  Location from = cursor_;
  setPrimaryOnly(p);
  nav_.onCaretMoved(from, cursor_);
}

void Editor::normalizeCarets(size_t primaryIndex) {
  if (carets_.empty()) return;
  wants_.resize(carets_.size(), -1);
  if (primaryIndex >= carets_.size()) primaryIndex = 0;
  if (carets_.size() > 1) {
    struct Item {
      Selection sel;
      int want;
      bool primary;
    };
    std::vector<Item> items;
    items.reserve(carets_.size());
    for (size_t i = 0; i < carets_.size(); ++i) items.push_back({carets_[i], wants_[i], i == primaryIndex});
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
      Position am = a.sel.min(), bm = b.sel.min();
      return am != bm ? am < bm : a.sel.max() < b.sel.max();
    });
    std::vector<Item> out;
    out.reserve(items.size());
    for (const Item& it : items) {
      if (!out.empty() && it.sel.min() <= out.back().sel.max()) {  // overlap or touch: merge
        Item& m = out.back();
        const Position lo = std::min(m.sel.min(), it.sel.min()), hi = std::max(m.sel.max(), it.sel.max());
        const bool reversed = it.primary ? it.sel.reversed() : m.sel.reversed();
        m.sel = reversed ? Selection{hi, lo} : Selection{lo, hi};
        if (it.primary) {
          m.want = it.want;
          m.primary = true;
        }
      } else {
        out.push_back(it);
      }
    }
    carets_.clear();
    wants_.clear();
    for (const Item& it : out)
      if (it.primary) {
        carets_.push_back(it.sel);
        wants_.push_back(it.want);
      }
    for (const Item& it : out)
      if (!it.primary) {
        carets_.push_back(it.sel);
        wants_.push_back(it.want);
      }
  }
  cursor_.pos = carets_[0].head;
}

void Editor::clampCarets() {
  if (!current_ || carets_.empty()) return;
  const TextBuffer& b = current_->buffer();
  for (Selection& s : carets_) {
    s.anchor = b.clamp(s.anchor);
    s.head = b.clamp(s.head);
  }
  normalizeCarets();
}

void Editor::commitMove(const Location& from) {
  normalizeCarets();
  nav_.onCaretMoved(from, cursor_);
}

void Editor::setSelections(std::vector<Selection> carets) {
  if (!current_) return;
  breakTyping();
  Location from = cursor_;
  if (carets.empty()) carets.push_back(caretAt(from.pos));
  const TextBuffer& b = current_->buffer();
  for (Selection& s : carets) {
    s.anchor = b.clamp(s.anchor);
    s.head = b.clamp(s.head);
  }
  carets_ = std::move(carets);
  wants_.assign(carets_.size(), -1);
  commitMove(from);
}

std::string Editor::selectedText() const {
  if (!current_) return {};
  std::vector<Range> rs;
  for (const Selection& s : carets_)
    if (!s.empty()) rs.push_back(s.range());
  std::sort(rs.begin(), rs.end());
  std::string out;
  for (size_t i = 0; i < rs.size(); ++i) {
    if (i) out += '\n';
    out += current_->buffer().textInRange(rs[i]);
  }
  return out;
}

void Editor::moveCarets(Direction dir, Unit unit, bool extend) {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  const Location from = cursor_;
  const TextBuffer& b = current_->buffer();
  const int lastLine = b.lineCount() - 1;
  for (size_t i = 0; i < carets_.size(); ++i) {
    Selection& s = carets_[i];
    int& want = wants_[i];
    Position h = s.head;
    const bool vertical =
        dir == Direction::Up || dir == Direction::Down || dir == Direction::PageUp || dir == Direction::PageDown;
    bool collapsed = false;
    switch (dir) {
      case Direction::Left:
      case Direction::Right: {
        const bool left = dir == Direction::Left;
        if (!extend && !s.empty() && unit != Unit::Word) {
          h = left ? s.min() : s.max();  // collapse to the selection edge
          collapsed = true;
        } else if (unit == Unit::Word) {
          h = left ? wordLeft(b, h) : wordRight(b, h);
        } else if (left) {
          if (h.column > 0) h.column = prevCharStart(b.line(h.line), h.column);
          else if (h.line > 0) h = {h.line - 1, static_cast<int>(b.line(h.line - 1).size())};
        } else {
          const std::string& l = b.line(h.line);
          if (h.column < static_cast<int>(l.size())) h.column = nextCharEnd(l, h.column);
          else if (h.line < lastLine) h = {h.line + 1, 0};
        }
        break;
      }
      case Direction::Up:
      case Direction::Down:
      case Direction::PageUp:
      case Direction::PageDown: {
        const bool up = dir == Direction::Up || dir == Direction::PageUp;
        const int n = (dir == Direction::PageUp || dir == Direction::PageDown || unit == Unit::Page) ? pageLines_ : 1;
        if (want < 0) want = codepointColumn(b.line(h.line), h.column);
        if (up && h.line == 0) {
          h = {0, 0};
        } else if (!up && h.line == lastLine) {
          h = {lastLine, static_cast<int>(b.line(lastLine).size())};
        } else {
          const int line = std::clamp(up ? h.line - n : h.line + n, 0, lastLine);
          h = {line, byteColumn(b.line(line), want)};
        }
        break;
      }
      case Direction::LineStart: h.column = 0; break;
      case Direction::LineEnd: h.column = static_cast<int>(b.line(h.line).size()); break;
      case Direction::DocStart: h = {0, 0}; break;
      case Direction::DocEnd: h = b.endPosition(); break;
    }
    if (!vertical) want = -1;
    s.head = h;
    if (!extend || collapsed) s.anchor = h;
  }
  commitMove(from);
}

void Editor::addCaretAbove() {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  const Location from = cursor_;
  const TextBuffer& b = current_->buffer();
  size_t top = 0;
  for (size_t i = 1; i < carets_.size(); ++i)
    if (carets_[i].head < carets_[top].head) top = i;
  Position h = carets_[top].head;
  if (h.line == 0) return;
  const int want = wants_[top] >= 0 ? wants_[top] : codepointColumn(b.line(h.line), h.column);
  wants_[top] = want;
  carets_.push_back(caretAt({h.line - 1, byteColumn(b.line(h.line - 1), want)}));
  wants_.push_back(want);
  commitMove(from);
}

void Editor::addCaretBelow() {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  const Location from = cursor_;
  const TextBuffer& b = current_->buffer();
  size_t bottom = 0;
  for (size_t i = 1; i < carets_.size(); ++i)
    if (carets_[bottom].head < carets_[i].head) bottom = i;
  Position h = carets_[bottom].head;
  if (h.line + 1 >= b.lineCount()) return;
  const int want = wants_[bottom] >= 0 ? wants_[bottom] : codepointColumn(b.line(h.line), h.column);
  wants_[bottom] = want;
  carets_.push_back(caretAt({h.line + 1, byteColumn(b.line(h.line + 1), want)}));
  wants_.push_back(want);
  commitMove(from);
}

void Editor::addCaretAtNextOccurrence() {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  const Location from = cursor_;
  const TextBuffer& b = current_->buffer();
  const Selection prim = carets_[0];
  if (prim.empty()) {
    auto w = b.wordAt(prim.head);
    if (w.text.empty()) return;
    carets_[0] = {w.range.start, w.range.end};
    wants_[0] = -1;
    commitMove(from);
    return;
  }
  const std::string needle = b.textInRange(prim.range());
  const bool multiline = needle.find('\n') != std::string::npos;
  std::string flat;
  if (multiline) flat = b.text();
  // First occurrence starting at or after `p`, wrapping once around the document.
  auto nextMatch = [&](Position p) -> std::optional<Position> {
    if (multiline) {
      size_t at = flat.find(needle, b.offsetOf(p));
      if (at == std::string::npos) at = flat.find(needle);
      if (at == std::string::npos) return std::nullopt;
      return b.positionAt(at);
    }
    for (int pass = 0; pass < 2; ++pass) {
      const int first = pass == 0 ? p.line : 0, last = pass == 0 ? b.lineCount() - 1 : p.line;
      for (int l = first; l <= last; ++l) {
        size_t at = b.line(l).find(needle, pass == 0 && l == p.line ? static_cast<size_t>(p.column) : 0);
        if (at != std::string::npos) return Position{l, static_cast<int>(at)};
      }
    }
    return std::nullopt;
  };
  auto endOf = [&](Position start) {
    if (!multiline) return Position{start.line, start.column + static_cast<int>(needle.size())};
    return b.positionAt(b.offsetOf(start) + needle.size());
  };
  std::optional<Position> firstSeen;
  Position p = prim.max();
  for (;;) {
    auto m = nextMatch(p);
    if (!m || (firstSeen && *m == *firstSeen)) return;
    if (!firstSeen) firstSeen = m;
    const Position e = endOf(*m);
    bool clash = false;  // overlapping or touching an existing selection would merge, so look further
    for (const Selection& s : carets_)
      if (*m <= s.max() && s.min() <= e) {
        clash = true;
        break;
      }
    if (!clash) {
      carets_.insert(carets_.begin(), Selection{*m, e});
      wants_.insert(wants_.begin(), -1);
      commitMove(from);
      return;
    }
    const std::string& l = b.line(m->line);
    p = {m->line, m->column < static_cast<int>(l.size()) ? nextCharEnd(l, m->column) : m->column + 1};
  }
}

void Editor::collapseToPrimary() {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  carets_.resize(1);
  wants_.resize(1);
  normalizeCarets();
}

void Editor::columnSelect(Position a, Position bPos) {
  if (!current_) return;
  breakTyping();
  const Location from = cursor_;
  const TextBuffer& b = current_->buffer();
  a = b.clamp(a);
  bPos = b.clamp(bPos);
  const int cpA = codepointColumn(b.line(a.line), a.column), cpB = codepointColumn(b.line(bPos.line), bPos.column);
  const int l0 = std::min(a.line, bPos.line), l1 = std::max(a.line, bPos.line);
  carets_.clear();
  wants_.clear();
  size_t primary = 0;
  for (int l = l0; l <= l1; ++l) {
    if (l == bPos.line) primary = carets_.size();
    carets_.push_back({{l, byteColumn(b.line(l), cpA)}, {l, byteColumn(b.line(l), cpB)}});
    wants_.push_back(cpB);
  }
  normalizeCarets(primary);
  nav_.onCaretMoved(from, cursor_);
}

bool Editor::gotoLocation(const Location& loc, std::string* error, const std::string& label) {
  Document* d = open(loc.file, error);
  if (!d) return false;
  Location from = cursor_;
  if (!from.file.empty()) nav_.record(from, NavigationHistory::Kind::Jump);
  setCurrent(d, loc.pos);
  nav_.record(cursor_, NavigationHistory::Kind::Jump, label);
  return true;
}

bool Editor::back() {
  auto target = nav_.back(cursor_);
  if (!target) return false;
  Document* d = open(target->file);
  if (!d) return false;
  setCurrent(d, target->pos);
  return true;
}

bool Editor::forward() {
  auto target = nav_.forward(cursor_);
  if (!target) return false;
  Document* d = open(target->file);
  if (!d) return false;
  setCurrent(d, target->pos);
  return true;
}

// ---------------------------------------------------------------------------
// Semantic commands
// ---------------------------------------------------------------------------

void Editor::autoJump(Confidence c, std::vector<SymbolLocation> locs, const std::string& err, NavResult done) {
  // Drop results that point at the caret itself (e.g. "definition" of a definition).
  if (locs.size() == 1 && !(locs[0].loc.file == cursor_.file && locs[0].loc.pos.line == cursor_.pos.line)) {
    std::string e;
    if (gotoLocation(locs[0].loc, &e, locs[0].name)) {
      if (done) done(c, locs, err);
      return;
    }
  }
  if (done) done(c, locs, err);
}

void Editor::gotoDefinition(NavResult done) {
  if (!current_) {
    if (done) done(Confidence::Approximate, {}, "no document");
    return;
  }
  semantic_->gotoDefinition(
      *current_, cursor_.pos, [this, done](Confidence c, std::vector<SymbolLocation> locs, const std::string& err) {
        post([this, c, locs = std::move(locs), err, done]() mutable { autoJump(c, std::move(locs), err, done); });
      });
}

void Editor::gotoDeclaration(NavResult done) {
  if (!current_) {
    if (done) done(Confidence::Approximate, {}, "no document");
    return;
  }
  semantic_->gotoDeclaration(
      *current_, cursor_.pos, [this, done](Confidence c, std::vector<SymbolLocation> locs, const std::string& err) {
        post([this, c, locs = std::move(locs), err, done]() mutable { autoJump(c, std::move(locs), err, done); });
      });
}

void Editor::findReferences(NavResult done) {
  if (!current_) {
    if (done) done(Confidence::Approximate, {}, "no document");
    return;
  }
  semantic_->findReferences(*current_, cursor_.pos,
                            [this, done](Confidence c, std::vector<SymbolLocation> locs, const std::string& err) {
                              post([c, locs = std::move(locs), err, done] {
                                if (done) done(c, locs, err);
                              });
                            });
}

void Editor::findSymbols(const std::string& query, NavResult done) {
  semantic_->workspaceSymbols(query,
                              [this, done](Confidence c, std::vector<SymbolLocation> locs, const std::string& err) {
                                post([c, locs = std::move(locs), err, done] {
                                  if (done) done(c, locs, err);
                                });
                              });
}

void Editor::hover(std::function<void(const std::string&, const std::string&)> done) {
  if (!current_) {
    if (done) done("", "no document");
    return;
  }
  semantic_->hover(*current_, cursor_.pos, [this, done](std::string text, const std::string& err) {
    post([text = std::move(text), err, done] {
      if (done) done(text, err);
    });
  });
}

void Editor::complete(std::function<void(const std::vector<LspCompletionItem>&, const std::string&)> done) {
  if (!current_) {
    if (done) done({}, "no document");
    return;
  }
  semantic_->completion(*current_, cursor_.pos,
                        [this, done](std::vector<LspCompletionItem> items, const std::string& err) {
                          post([items = std::move(items), err, done] {
                            if (done) done(items, err);
                          });
                        });
}

void Editor::switchSourceHeader(std::function<void(bool, const std::string&)> done) {
  if (!current_) {
    if (done) done(false, "no document");
    return;
  }
  semantic_->switchSourceHeader(*current_, [this, done](fs::path p, const std::string& err) {
    post([this, p, err, done] {
      if (p.empty()) {
        if (done) done(false, err.empty() ? "no counterpart" : err);
        return;
      }
      std::string e;
      bool ok = gotoLocation({p, {0, 0}}, &e);
      if (done) done(ok, e);
    });
  });
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

int64_t Editor::nowMs() const {
  if (opt_.clock) return opt_.clock();
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Applies one edit per caret (edits[i] belongs to carets_[i]) as a single undo group. Edits go bottom-to-top, so a
// caret's own range is never disturbed by the edits already made; afterwards every caret sits at the end of its
// inserted text, found by remapping through the EditResults. Carets that came together are merged.
void Editor::applyCaretEdits(std::vector<CaretEdit> edits, bool joinPrevious) {
  const size_t n = carets_.size();
  std::vector<size_t> order(n);
  for (size_t i = 0; i < n; ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return edits[a].range.start != edits[b].range.start ? edits[a].range.start < edits[b].range.start
                                                        : edits[a].range.end < edits[b].range.end;
  });
  for (size_t k = 1; k < n; ++k) {  // defensive: ranges must not overlap
    Range& r = edits[order[k]].range;
    const Position prevEnd = edits[order[k - 1]].range.end;
    if (r.start < prevEnd) {
      r.start = prevEnd;
      if (r.end < r.start) r.end = r.start;
    }
  }
  Document& doc = *current_;
  doc.beginGroup(carets_, joinPrevious);
  inEdit_ = true;
  std::vector<TextBuffer::EditResult> results(n);
  for (size_t k = n; k-- > 0;) {
    CaretEdit& e = edits[order[k]];
    if (e.range.empty() && e.text.empty()) results[k] = {e.range, e.range, 0};
    else results[k] = doc.replace(e.range, e.text);
  }
  inEdit_ = false;
  Position endPre{}, endPost{};
  for (size_t k = 0; k < n; ++k) {
    const TextBuffer::EditResult& res = results[k];
    const Position landed = k == 0 ? res.inserted.end : remapAfter(res.inserted.end, endPre, endPost);
    carets_[order[k]] = caretAt(landed);
    endPre = res.removed.end;
    endPost = landed;
  }
  wants_.assign(n, -1);
  normalizeCarets();
  doc.endGroup(carets_);
}

void Editor::insertText(const std::string& text) {
  if (!current_ || carets_.empty()) return;
  const bool anySelection = std::any_of(carets_.begin(), carets_.end(), [](const Selection& s) { return !s.empty(); });
  if (text.empty() && !anySelection) return;
  const Workspace::Settings& st = ws_.settings();
  const bool coalesce = isSingleCodepoint(text) && st.undoGrouping != "char";
  const int64_t now = nowMs();
  const bool join = coalesce && !anySelection && typingOpen_ && typingDoc_ == current_ &&
                    typingVersion_ == current_->version() && typingCarets_ == carets_ &&
                    now - typingTime_ <= st.undoGroupPauseMs && current_->canUndo();
  std::vector<CaretEdit> edits;
  edits.reserve(carets_.size());
  for (const Selection& s : carets_) edits.push_back({s.range(), text});
  applyCaretEdits(std::move(edits), join);
  if (coalesce) {
    typingOpen_ = !(text.size() == 1 && isBlank(text[0]));  // whitespace joins the step, then closes it
    typingDoc_ = current_;
    typingVersion_ = current_->version();
    typingTime_ = now;
    typingCarets_ = carets_;
  } else {
    breakTyping();
  }
}

void Editor::backspace() {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  const TextBuffer& b = current_->buffer();
  std::vector<CaretEdit> edits;
  edits.reserve(carets_.size());
  for (const Selection& s : carets_) {
    Range r = s.range();
    if (s.empty()) {
      const Position p = s.head;
      if (p.column > 0) r.start = {p.line, prevCharStart(b.line(p.line), p.column)};
      else if (p.line > 0) r.start = {p.line - 1, static_cast<int>(b.line(p.line - 1).size())};
    }
    edits.push_back({r, {}});
  }
  applyCaretEdits(std::move(edits), false);
}

void Editor::del() {
  if (!current_ || carets_.empty()) return;
  breakTyping();
  const TextBuffer& b = current_->buffer();
  std::vector<CaretEdit> edits;
  edits.reserve(carets_.size());
  for (const Selection& s : carets_) {
    Range r = s.range();
    if (s.empty()) {
      const Position p = s.head;
      const std::string& l = b.line(p.line);
      if (p.column < static_cast<int>(l.size())) r.end = {p.line, nextCharEnd(l, p.column)};
      else if (p.line + 1 < b.lineCount()) r.end = {p.line + 1, 0};
    }
    edits.push_back({r, {}});
  }
  applyCaretEdits(std::move(edits), false);
}

void Editor::replaceRange(Range r, const std::string& text) {
  if (!current_) return;
  breakTyping();
  current_->beginGroup(carets_);
  inEdit_ = true;
  current_->replace(r, text);
  inEdit_ = false;
  setPrimaryOnly(r.start);
  current_->endGroup(carets_);
}

void Editor::deleteRange(Range r) { replaceRange(r, {}); }

bool Editor::undo() {
  if (!current_) return false;
  breakTyping();
  inEdit_ = true;
  const bool ok = current_->undo();
  inEdit_ = false;
  if (ok) restoreCaretsAfterHistory();
  return ok;
}

bool Editor::redo() {
  if (!current_) return false;
  breakTyping();
  inEdit_ = true;
  const bool ok = current_->redo();
  inEdit_ = false;
  if (ok) restoreCaretsAfterHistory();
  return ok;
}

void Editor::restoreCaretsAfterHistory() {
  std::vector<Selection> restored = current_->restoredCarets();
  if (restored.empty()) restored.push_back(caretAt(cursor_.pos));
  carets_ = std::move(restored);
  wants_.assign(carets_.size(), -1);
  clampCarets();
}

// ---------------------------------------------------------------------------
// Queue
// ---------------------------------------------------------------------------

void Editor::post(std::function<void()> fn) {
  {
    std::lock_guard<std::mutex> lk(qmu_);
    queue_.push_back(std::move(fn));
  }
  qcv_.notify_all();
}

size_t Editor::pump() {
  std::deque<std::function<void()>> batch;
  {
    std::lock_guard<std::mutex> lk(qmu_);
    batch.swap(queue_);
  }
  for (auto& fn : batch) fn();
  return batch.size();
}

bool Editor::waitFor(std::function<bool()> pred, int timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (true) {
    pump();
    if (pred()) return true;
    std::unique_lock<std::mutex> lk(qmu_);
    if (queue_.empty()) {
      if (qcv_.wait_until(lk, deadline) == std::cv_status::timeout) {
        lk.unlock();
        pump();
        return pred();
      }
    }
  }
}

const std::vector<Diagnostic>& Editor::diagnostics(const fs::path& file) const {
  static const std::vector<Diagnostic> empty;
  auto it = diagnostics_.find(file.generic_string());
  return it == diagnostics_.end() ? empty : it->second;
}

// ---------------------------------------------------------------------------
// Find in workspace
// ---------------------------------------------------------------------------

SearchHandle Editor::findInWorkspace(const std::string& query, SearchOptions opts, SearchBatch onBatch,
                                     SearchDone onDone) {
  opts.query = query;
  SearchInput in;
  in.root = ws_.root();
  in.files = ws_.codeFiles();
  if (opts.includeOtherFiles) in.files.insert(in.files.end(), ws_.otherFiles().begin(), ws_.otherFiles().end());
  // Snapshot open documents on this thread; the search must not touch live buffers.
  for (Document* d : documents()) {
    // The workspace root is canonical while a document keeps the caller's spelling
    // (macOS /var -> /private/var, Windows 8.3 names and case), so canonicalise first.
    std::error_code ec;
    std::string rel =
        pathKey(fs::weakly_canonical(d->path(), ec).lexically_relative(ws_.root()).generic_string(), nativePathStyle());
    if (!rel.empty() && rel.compare(0, 2, "..") != 0) in.overrides[rel] = d->buffer().text();
  }
  searches_.erase(
      std::remove_if(searches_.begin(), searches_.end(), [](const SearchHandle& h) { return h.finished(); }),
      searches_.end());
  SearchHandle h = SearchHandle::create();  // queued batches check its cancel flag when they are pumped
  TextSearch::start(
      std::move(in), std::move(opts),
      [this, h, onBatch](std::vector<SearchMatch>&& batch) {
        post([h, onBatch, batch = std::move(batch)] {
          if (!h.cancelled() && onBatch) onBatch(batch);
        });
      },
      [this, onDone](const SearchSummary& s) {
        post([onDone, s] {
          if (onDone) onDone(s);
        });
      },
      h);
  searches_.push_back(h);
  return h;
}

bool Editor::gotoSearchMatch(const SearchMatch& m, std::string* error) {
  return gotoLocation({m.file, {m.line, m.column}}, error, m.text.substr(0, 80));
}

}  // namespace tf
