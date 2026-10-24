#include "app/Editor.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>

namespace tf {

namespace fs = std::filesystem;

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
  return semantic_->start(error);
}

void Editor::shutdown() {
  for (auto& h : searches_) h.cancel();
  for (auto& h : searches_) h.wait();
  searches_.clear();
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
  auto it = docs_.find(fs::weakly_canonical(path, ec).generic_string());
  return it == docs_.end() ? nullptr : it->second.get();
}

std::vector<Document*> Editor::documents() {
  std::vector<Document*> out;
  for (auto& [k, d] : docs_) out.push_back(d.get());
  return out;
}

Document* Editor::open(const fs::path& pathIn, std::string* error) {
  std::error_code ec;
  fs::path path = fs::weakly_canonical(pathIn.is_absolute() ? pathIn : ws_.root() / pathIn, ec);
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
  docs_[path.generic_string()] = std::move(doc);
  if (semantic_) semantic_->didOpen(*raw);
  return raw;
}

bool Editor::close(const fs::path& path) {
  Document* d = document(path);
  if (!d) return false;
  if (semantic_) semantic_->didClose(*d);
  if (current_ == d) { current_ = nullptr; cursor_ = {}; }
  docs_.erase(d->path().generic_string());
  return true;
}

bool Editor::save(Document& doc, std::string* error) {
  if (!doc.saveToDisk(error)) return false;
  if (semantic_) semantic_->didSave(doc);
  return true;
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
