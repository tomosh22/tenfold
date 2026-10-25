#include "app/Editor.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
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
    if (!dir.empty() && dir != ws_.cacheDir() && dir != ws_.root() && ws_.isIgnoredDir(dir)) watcher_->addDirectory(dir);
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
    if (current_ == &d) cursor_.pos = d.buffer().clamp(cursor_.pos);
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
  if (current_ == d) { current_ = nullptr; cursor_ = {}; }
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
}

void Editor::moveCursor(Position p) {
  if (!current_) return;
  Location from = cursor_;
  cursor_.pos = current_->buffer().clamp(p);
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
  if (!current_) { if (done) done(Confidence::Approximate, {}, "no document"); return; }
  semantic_->gotoDefinition(*current_, cursor_.pos, [this, done](Confidence c, std::vector<SymbolLocation> locs, const std::string& err) {
    post([this, c, locs = std::move(locs), err, done]() mutable { autoJump(c, std::move(locs), err, done); });
  });
}

void Editor::gotoDeclaration(NavResult done) {
  if (!current_) { if (done) done(Confidence::Approximate, {}, "no document"); return; }
  semantic_->gotoDeclaration(*current_, cursor_.pos, [this, done](Confidence c, std::vector<SymbolLocation> locs, const std::string& err) {
    post([this, c, locs = std::move(locs), err, done]() mutable { autoJump(c, std::move(locs), err, done); });
  });
}

void Editor::findReferences(NavResult done) {
  if (!current_) { if (done) done(Confidence::Approximate, {}, "no document"); return; }
  semantic_->findReferences(*current_, cursor_.pos, [this, done](Confidence c, std::vector<SymbolLocation> locs, const std::string& err) {
    post([c, locs = std::move(locs), err, done] { if (done) done(c, locs, err); });
  });
}

void Editor::findSymbols(const std::string& query, NavResult done) {
  semantic_->workspaceSymbols(query, [this, done](Confidence c, std::vector<SymbolLocation> locs, const std::string& err) {
    post([c, locs = std::move(locs), err, done] { if (done) done(c, locs, err); });
  });
}

void Editor::hover(std::function<void(const std::string&, const std::string&)> done) {
  if (!current_) { if (done) done("", "no document"); return; }
  semantic_->hover(*current_, cursor_.pos, [this, done](std::string text, const std::string& err) {
    post([text = std::move(text), err, done] { if (done) done(text, err); });
  });
}

void Editor::complete(std::function<void(const std::vector<LspCompletionItem>&, const std::string&)> done) {
  if (!current_) { if (done) done({}, "no document"); return; }
  semantic_->completion(*current_, cursor_.pos, [this, done](std::vector<LspCompletionItem> items, const std::string& err) {
    post([items = std::move(items), err, done] { if (done) done(items, err); });
  });
}

void Editor::switchSourceHeader(std::function<void(bool, const std::string&)> done) {
  if (!current_) { if (done) done(false, "no document"); return; }
  semantic_->switchSourceHeader(*current_, [this, done](fs::path p, const std::string& err) {
    post([this, p, err, done] {
      if (p.empty()) { if (done) done(false, err.empty() ? "no counterpart" : err); return; }
      std::string e;
      bool ok = gotoLocation({p, {0, 0}}, &e);
      if (done) done(ok, e);
    });
  });
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

void Editor::insertText(const std::string& text) {
  if (!current_) return;
  Position at = cursor_.pos;
  current_->insert(at, text);
  // Move caret to the end of the inserted text.
  int nl = static_cast<int>(std::count(text.begin(), text.end(), '\n'));
  if (nl == 0) cursor_.pos = {at.line, at.column + static_cast<int>(text.size())};
  else cursor_.pos = {at.line + nl, static_cast<int>(text.size() - text.rfind('\n') - 1)};
  cursor_.pos = current_->buffer().clamp(cursor_.pos);
}

void Editor::deleteRange(Range r) {
  if (!current_) return;
  current_->erase(r);
  cursor_.pos = current_->buffer().clamp(r.start);
}

void Editor::replaceRange(Range r, const std::string& text) {
  if (!current_) return;
  current_->replace(r, text);
  cursor_.pos = current_->buffer().clamp(r.start);
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
    std::string rel = d->path().lexically_relative(ws_.root()).generic_string();
    if (!rel.empty() && rel.compare(0, 2, "..") != 0) in.overrides[rel] = d->buffer().text();
  }
  searches_.erase(std::remove_if(searches_.begin(), searches_.end(), [](const SearchHandle& h) { return h.finished(); }),
                  searches_.end());
  SearchHandle h = SearchHandle::create();  // queued batches check its cancel flag when they are pumped
  TextSearch::start(
      std::move(in), std::move(opts),
      [this, h, onBatch](std::vector<SearchMatch>&& batch) {
        post([h, onBatch, batch = std::move(batch)] {
          if (!h.cancelled() && onBatch) onBatch(batch);
        });
      },
      [this, onDone](const SearchSummary& s) { post([onDone, s] { if (onDone) onDone(s); }); }, h);
  searches_.push_back(h);
  return h;
}

bool Editor::gotoSearchMatch(const SearchMatch& m, std::string* error) {
  return gotoLocation({m.file, {m.line, m.column}}, error, m.text.substr(0, 80));
}

}  // namespace tf
