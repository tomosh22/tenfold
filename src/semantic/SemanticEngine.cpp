#include "semantic/SemanticEngine.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>

#include "semantic/FastIndexStore.h"

namespace tf {

namespace fs = std::filesystem;

// Shared state for one "approximate first, authoritative later" query.
struct PendingState {
  std::mutex m;
  bool approxDelivered = false;
  bool finalDelivered = false;
  std::vector<SymbolLocation> approx;
  SemanticEngine::LocationsCallback cb;
};

namespace {

const char* lspSymbolKindName(int k) {
  switch (k) {
    case 1: return "file"; case 2: return "module"; case 3: return "namespace"; case 4: return "package";
    case 5: return "class"; case 6: return "method"; case 7: return "property"; case 8: return "field";
    case 9: return "constructor"; case 10: return "enum"; case 11: return "interface"; case 12: return "function";
    case 13: return "variable"; case 14: return "constant"; case 15: return "string"; case 16: return "number";
    case 17: return "boolean"; case 18: return "array"; case 19: return "object"; case 20: return "key";
    case 21: return "null"; case 22: return "enumerator"; case 23: return "struct"; case 24: return "event";
    case 25: return "operator"; case 26: return "type parameter"; default: return "symbol";
  }
}

std::string readLineOfFile(const fs::path& p, int line) {
  std::ifstream in(p, std::ios::binary);
  std::string s;
  for (int i = 0; i <= line && std::getline(in, s); ++i) {}
  if (!s.empty() && s.back() == '\r') s.pop_back();
  return s;
}

std::string languageIdFor(const fs::path& p) {
  std::string e = p.extension().string();
  for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (e == ".c") return "c";
  if (e == ".m") return "objective-c";
  if (e == ".mm") return "objective-cpp";
  if (e == ".cu" || e == ".cuh") return "cuda-cpp";
  return "cpp";
}

}  // namespace

SemanticEngine::SemanticEngine(Workspace& ws, Options opt) : ws_(ws), opt_(std::move(opt)) {
  if (opt_.logDir.empty()) opt_.logDir = ws_.cacheDir();
}

SemanticEngine::~SemanticEngine() { stop(); }

void SemanticEngine::log(const std::string& msg) const {
  if (opt_.log) opt_.log(msg);
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

void SemanticEngine::prepareCompileCommands() {
  std::error_code ec;
  fs::create_directories(ws_.cacheDir(), ec);
  const fs::path root = ws_.root();

  if (auto existing = CompileDatabase::find(root)) {
    // Our own generated database lives in .tenfold; prefer a "real" one elsewhere.
    compdbPath_ = *existing;
    compdbSource_ = existing->parent_path() == ws_.cacheDir() ? "generated(cached)" : "existing";
  } else if (ws_.hasCMake() && ws_.settings().runCMake) {
    std::string cmakeLog;
    if (auto p = compdb::fromCMake(root, ws_.cacheDir() / "cmake-build", &cmakeLog)) {
      compdbPath_ = *p;
      compdbSource_ = "cmake";
    } else {
      log("cmake configure failed:\n" + cmakeLog);
    }
  }
  if (compdbPath_.empty()) {
    auto slns = ws_.solutionFiles();
    if (!slns.empty()) {
      compdb::VcxprojOptions vo;
      vo.configuration = ws_.settings().vsConfiguration;
      vo.platform = ws_.settings().vsPlatform;
      std::vector<CompileCommand> all;
      for (auto& s : slns) {
        std::string err;
        auto cmds = compdb::fromSolution(s, vo, &err);
        if (!err.empty()) log("vcxproj: " + err);
        all.insert(all.end(), cmds.begin(), cmds.end());
      }
      if (!all.empty()) {
        compdbPath_ = ws_.cacheDir() / "compile_commands.json";
        CompileDatabase::save(all, compdbPath_);
        compdbSource_ = "vcxproj";
      }
    }
  }
  if (compdbPath_.empty()) {
    // The fast index knows every #include in the workspace; use them to infer
    // the include directories instead of guessing. It is normally done within
    // a second or two; cap the wait so a huge cold workspace still starts.
    waitForFastIndex(15000);
    auto cmds = compdb::heuristic(root, ws_.sourceFiles(), ws_.headerFiles(), ws_.settings().cppStandard, fast_.allIncludes());
    // The include directories are identical for every file. Repeating them
    // per entry makes the JSON enormous (hundreds of MB on big trees), so the
    // database keeps only compiler + std + file and the shared flags go into
    // a clangd config (CompileFlags.Add), which applies to headers too.
    for (auto& c : cmds) {
      std::vector<std::string> keep;
      for (auto& a : c.arguments) {
        if (a.rfind("-I", 0) == 0) {
          if (std::find(sharedFlags_.begin(), sharedFlags_.end(), a) == sharedFlags_.end()) sharedFlags_.push_back(a);
        } else {
          keep.push_back(a);
        }
      }
      c.arguments = std::move(keep);
    }
    compdbPath_ = ws_.cacheDir() / "compile_commands.json";
    CompileDatabase::save(cmds, compdbPath_);
    compdbSource_ = "heuristic";
  }
  // clangd user config lives in $XDG_CONFIG_HOME/clangd/config.yaml. We give
  // clangd a private config home under .tenfold so we can add flags without
  // touching the user's files; projects can still add a .clangd at their root.
  // The file is (re)written when we generated the database or when it is
  // missing; a cached heuristic database keeps the config written alongside it.
  fs::path cfgDir = ws_.cacheDir() / "clangd-config" / "clangd";
  if (compdbSource_ == "heuristic" || !fs::is_regular_file(cfgDir / "config.yaml", ec)) {
    fs::create_directories(cfgDir, ec);
    std::ofstream cfg(cfgDir / "config.yaml");
    cfg << "# Generated by tenfold. Edit <workspace>/.clangd for project-specific settings instead.\n";
    if (!sharedFlags_.empty()) {
      cfg << "CompileFlags:\n";
      cfg << "  Add:\n";
      for (auto& f : sharedFlags_) cfg << "    - " << Json(f).dump() << "\n";
    }
    cfg << "Index:\n  Background: Build\n";
  }
  compileCommands_ = CompileDatabase::load(compdbPath_);
  log("compile database: " + compdbSource_ + " (" + std::to_string(compileCommands_.size()) + " entries) at " +
      compdbPath_.string());
}

bool SemanticEngine::startClangd(std::string* error) {
  std::string exe = opt_.clangdPath.empty() ? ws_.settings().clangdPath : opt_.clangdPath;
  if (exe.empty()) exe = "clangd";
  std::string resolved = findExecutable(exe);
  if (resolved.empty()) {
    if (error) *error = "clangd not found: " + exe;
    return false;
  }
  LspClient::Options lo;
  lo.serverPath = resolved;
  lo.rootPath = ws_.root();
  lo.logFile = opt_.logDir / "clangd.log";
  lo.args = {"--compile-commands-dir=" + compdbPath_.parent_path().string(),
             "--background-index",
             "--pch-storage=memory",
             "--header-insertion=never",
             "--completion-style=detailed",
             "--all-scopes-completion",
             "--limit-results=500",
             "--limit-references=10000",
             "--log=error",
             "-j=" + std::to_string(std::max(2u, std::thread::hardware_concurrency()))};
  for (auto& a : ws_.settings().clangdExtraArgs) lo.args.push_back(a);
  for (auto& a : opt_.clangdArgs) lo.args.push_back(a);
  lo.fallbackFlags = {"-std=" + ws_.settings().cppStandard, "-I" + ws_.root().string()};
  lo.env = {{"XDG_CONFIG_HOME", (ws_.cacheDir() / "clangd-config").string()}};
  if (opt_.traceLsp) {
    lo.trace = [this](const std::string& dir, const Json& j) { log(dir + " " + j.dump().substr(0, 400)); };
  }

  clangd_ = std::make_unique<LspClient>();
  clangd_->onFileStatus([this](const std::string& uri, const std::string& state) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      fileStatus_[uri] = state;
    }
    cv_.notify_all();
  });
  clangd_->onIndexProgress([this](const IndexProgress&) { cv_.notify_all(); });
  clangd_->onDiagnostics([this](const std::string& uri, int, std::vector<LspDiagnostic> ds) {
    std::function<void(const fs::path&, std::vector<Diagnostic>)> h;
    {
      std::lock_guard<std::mutex> lk(mu_);
      h = diagHandler_;
    }
    if (!h) return;
    fs::path file = uriToPath(uri);
    std::vector<Diagnostic> out;
    for (auto& d : ds) {
      Diagnostic x;
      x.file = file;
      x.range = fromServer(LspLocation{uri, d.range}).pos == d.range.start ? d.range : Range{fromServer({uri, d.range}).pos, fromServer({uri, {d.range.end, d.range.end}}).pos};
      x.severity = d.severity;
      x.message = d.message;
      x.code = d.code;
      out.push_back(std::move(x));
    }
    h(file, std::move(out));
  });
  clangd_->onLogMessage([this](int, const std::string& m) { log("clangd: " + m); });
  clangd_->onDisconnect([this] { log("clangd exited"); cv_.notify_all(); });

  std::string err;
  if (!clangd_->start(lo, &err)) {
    if (error) *error = err;
    log("failed to start clangd: " + err);
    clangd_.reset();
    return false;
  }
  log("clangd started: " + clangd_->serverVersion() + ", offsetEncoding=" + clangd_->offsetEncoding());
  return true;
}

void SemanticEngine::saveFastIndex() {
  if (!opt_.persistFastIndex) return;
  uint64_t gen = fast_.generation();
  if (gen == savedGen_) return;
  std::string err;
  if (FastIndexStore::save(ws_.cacheDir() / "fastindex.bin", fast_.exportFiles(), &err))
    savedGen_ = gen;
  else
    log("fast index save failed: " + err);
}

void SemanticEngine::startFastIndex() {
  fastDone_ = false;
  {
    std::lock_guard<std::mutex> g(saveMu_);
    saveRun_ = true;
  }
  indexThread_ = std::thread([this] {
    FastIndexStore::Entries cached;
    if (opt_.persistFastIndex) {
      std::string err;
      if (auto loaded = FastIndexStore::load(ws_.cacheDir() / "fastindex.bin", &err))
        cached = std::move(*loaded);
      else
        log("fast index store not used: " + err);
    }
    fastStats_ = fast_.indexFilesCached(ws_.codeFiles(), std::move(cached), opt_.indexThreads);
    {
      std::lock_guard<std::mutex> g(saveMu_);
      // Nothing to write back if every entry came from the store untouched.
      if (fastStats_.relexed == 0 && fastStats_.dropped == 0 && fastStats_.reused > 0) savedGen_ = fast_.generation();
    }
    fastDone_ = true;
    cv_.notify_all();
    log("fast index: " + std::to_string(fastStats_.files) + " files, " + std::to_string(fastStats_.symbols) +
        " symbols, " + std::to_string(fastStats_.bytes / 1024) + " KiB lexed (" + std::to_string(fastStats_.relexed) +
        " relexed, " + std::to_string(fastStats_.reused) + " cached, " + std::to_string(fastStats_.dropped) +
        " dropped) in " + std::to_string(fastStats_.seconds) + "s");
    // Persist now (cold start) and then every fastIndexSaveSeconds while dirty; once more at shutdown.
    std::unique_lock<std::mutex> lk(saveMu_);
    while (saveRun_) {
      saveFastIndex();
      saveCv_.wait_for(lk, std::chrono::seconds(std::max(1, opt_.fastIndexSaveSeconds)), [this] { return !saveRun_; });
    }
    saveFastIndex();
  });
}

bool SemanticEngine::start(std::string* error) {
  timerRun_ = true;
  timer_ = std::thread([this] { timerLoop(); });
  startFastIndex();
  prepareCompileCommands();
  bool ok = true;
  if (opt_.useClangd) ok = startClangd(error);
  return ok;
}

void SemanticEngine::stop() {
  if (timerRun_.exchange(false)) {
    cv_.notify_all();
    if (timer_.joinable()) timer_.join();
  }
  {
    std::lock_guard<std::mutex> g(saveMu_);
    saveRun_ = false;
  }
  saveCv_.notify_all();
  if (indexThread_.joinable()) indexThread_.join();
  if (clangd_) {
    clangd_->stop();
    clangd_.reset();
  }
}

SemanticEngine::Status SemanticEngine::status() const {
  Status s;
  if (clangd_) {
    s.clangdRunning = clangd_->alive();
    s.clangdInitialized = clangd_->initialized();
    s.clangdVersion = clangd_->serverVersion();
    s.offsetEncoding = clangd_->offsetEncoding();
    s.clangdIndex = clangd_->indexProgress();
  }
  s.fastIndexDone = fastDone_;
  s.fastRelexed = fastStats_.relexed;
  s.fastReused = fastStats_.reused;
  s.fastFiles = fast_.fileCount();
  s.fastSymbols = fast_.symbolCount();
  s.fastSeconds = fastDone_ ? fastStats_.seconds : 0;
  s.compdbSource = compdbSource_;
  s.compdbPath = compdbPath_;
  s.compdbEntries = compileCommands_.size();
  return s;
}

bool SemanticEngine::waitForFastIndex(int timeoutMs) const {
  std::unique_lock<std::mutex> lk(mu_);
  return cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return fastDone_.load(); });
}

bool SemanticEngine::waitForClangdIndex(int timeoutMs) const {
  if (!clangd_) return false;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  std::unique_lock<std::mutex> lk(mu_);
  while (true) {
    auto p = clangd_->indexProgress();
    if (p.percentage == 100 && !p.active) return true;
    if (!clangd_->alive()) return false;
    if (cv_.wait_until(lk, deadline) == std::cv_status::timeout) {
      auto q = clangd_->indexProgress();
      return q.percentage == 100 && !q.active;
    }
  }
}

bool SemanticEngine::fileReady(const fs::path& file) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = fileStatus_.find(pathToUri(file));
  return it != fileStatus_.end() && it->second == "idle";
}

bool SemanticEngine::waitForFile(const fs::path& file, int timeoutMs) const {
  if (!clangd_) return false;
  std::string uri = pathToUri(file);
  std::unique_lock<std::mutex> lk(mu_);
  return cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
    auto it = fileStatus_.find(uri);
    return it != fileStatus_.end() && it->second == "idle";
  });
}

// ---------------------------------------------------------------------------
// Encoding conversion
// ---------------------------------------------------------------------------

Position SemanticEngine::toServer(const Document& doc, Position p) const {
  if (!clangd_ || clangd_->offsetEncoding() == "utf-8") return p;
  p = doc.buffer().clamp(p);
  return {p.line, TextBuffer::utf8ToUtf16Column(doc.buffer().line(p.line), p.column)};
}

Location SemanticEngine::fromServer(const LspLocation& l) const {
  Location out;
  out.file = uriToPath(l.uri);
  out.pos = l.range.start;
  if (clangd_ && clangd_->offsetEncoding() != "utf-8" && out.pos.column > 0) {
    std::string line;
    const Document* doc = nullptr;
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto it = openDocs_.find(l.uri);
      if (it != openDocs_.end()) doc = it->second;
    }
    if (doc && out.pos.line < doc->buffer().lineCount()) line = doc->buffer().line(out.pos.line);
    else line = readLineOfFile(out.file, out.pos.line);
    out.pos.column = TextBuffer::utf16ToUtf8Column(line, out.pos.column);
  }
  return out;
}

std::vector<SymbolLocation> SemanticEngine::fromServer(const std::vector<LspLocation>& ls, Confidence c) const {
  std::vector<SymbolLocation> out;
  for (auto& l : ls) {
    SymbolLocation s;
    s.loc = fromServer(l);
    s.confidence = c;
    out.push_back(std::move(s));
  }
  return out;
}

// ---------------------------------------------------------------------------
// Document lifecycle
// ---------------------------------------------------------------------------

void SemanticEngine::didOpen(Document& doc) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    openDocs_[doc.uri()] = &doc;
  }
  fast_.updateFile(doc.path(), doc.buffer().text());
  if (auto* c = clangd()) c->didOpen(doc.uri(), languageIdFor(doc.path()), doc.version(), doc.buffer().text());
}

void SemanticEngine::didChange(Document& doc, const Document::Change& change) {
  // Keep the fast index fresh (cheap: single file re-lex).
  fast_.updateFile(doc.path(), doc.buffer().text());
  if (auto* c = clangd()) {
    if (c->offsetEncoding() == "utf-8") {
      c->didChangeIncremental(doc.uri(), doc.version(), {{change.removed, change.newText}});
    } else {
      c->didChangeFull(doc.uri(), doc.version(), doc.buffer().text());
    }
  }
}

void SemanticEngine::didSave(Document& doc) {
  if (auto* c = clangd()) c->didSave(doc.uri());
}

void SemanticEngine::didClose(Document& doc) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    openDocs_.erase(doc.uri());
    fileStatus_.erase(doc.uri());
  }
  if (auto* c = clangd()) c->didClose(doc.uri());
}

// ---------------------------------------------------------------------------
// Approximate (FastIndex) answers
// ---------------------------------------------------------------------------

namespace {
SymbolLocation fromFast(const FastIndex::Match& m) {
  const Symbol& s = m.sym();
  SymbolLocation out;
  out.loc.file = m.file->file;
  out.loc.pos = {s.line, s.col};
  out.name = s.name;
  out.container = s.scope;
  out.kind = symbolKindName(s.kind);
  out.definition = s.definition;
  out.confidence = Confidence::Approximate;
  return out;
}
}  // namespace

std::vector<SymbolLocation> SemanticEngine::approximateDefinitions(const Document& doc, Position pos) const {
  auto word = doc.buffer().wordAt(pos);
  std::vector<SymbolLocation> out;
  if (word.text.empty()) return out;
  // Qualified context: "A::B::name" to the left of the word narrows the scope.
  std::string qualifier;
  {
    const std::string& line = doc.buffer().line(doc.buffer().clamp(pos).line);
    int i = word.range.start.column;
    while (i >= 2 && line[static_cast<size_t>(i) - 1] == ':' && line[static_cast<size_t>(i) - 2] == ':') {
      i -= 2;
      int e = i;
      while (i > 0 && (std::isalnum(static_cast<unsigned char>(line[static_cast<size_t>(i) - 1])) || line[static_cast<size_t>(i) - 1] == '_')) --i;
      qualifier = line.substr(static_cast<size_t>(i), static_cast<size_t>(e - i)) + (qualifier.empty() ? "" : "::" + qualifier);
    }
  }
  auto matches = fast_.lookup(word.text);
  for (auto& m : matches) {
    if (!qualifier.empty()) {
      const std::string& sc = m.sym().scope;
      bool ok = sc == qualifier || sc.ends_with("::" + qualifier);
      if (!ok) continue;
    }
    out.push_back(fromFast(m));
  }
  // Same-file first, definitions first.
  std::stable_sort(out.begin(), out.end(), [&](const SymbolLocation& a, const SymbolLocation& b) {
    bool af = a.loc.file == doc.path(), bf = b.loc.file == doc.path();
    if (af != bf) return af;
    return a.definition > b.definition;
  });
  return out;
}

std::vector<SymbolLocation> SemanticEngine::approximateSymbols(const std::string& query, size_t limit) const {
  std::vector<SymbolLocation> out;
  for (auto& m : fast_.search(query, limit)) out.push_back(fromFast(m));
  return out;
}

// ---------------------------------------------------------------------------
// Budgeted delivery
// ---------------------------------------------------------------------------

void SemanticEngine::timerLoop() {
  std::unique_lock<std::mutex> lk(mu_);
  while (timerRun_) {
    if (pending_.empty()) {
      cv_.wait(lk);
      continue;
    }
    auto now = std::chrono::steady_clock::now();
    if (pending_.front().deadline > now) {
      cv_.wait_until(lk, pending_.front().deadline);
      continue;
    }
    auto st = pending_.front().state;
    pending_.pop_front();
    lk.unlock();
    {
      std::lock_guard<std::mutex> g(st->m);
      if (!st->finalDelivered && !st->approxDelivered) {
        st->approxDelivered = true;
        if (st->cb) st->cb(Confidence::Approximate, st->approx, {});
      }
    }
    lk.lock();
  }
}

void SemanticEngine::deliverWithBudget(
    std::vector<SymbolLocation> approx, LocationsCallback cb,
    std::function<void(std::function<void(std::vector<SymbolLocation>, std::string)>)> issue) {
  auto st = std::make_shared<PendingState>();
  st->approx = std::move(approx);
  st->cb = std::move(cb);

  LspClient* c = clangd();
  if (!c) {
    // No authority available: approximate is all we have.
    st->approxDelivered = true;
    st->cb(Confidence::Approximate, st->approx, c ? "" : "clangd not running");
    return;
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    pending_.push_back({std::chrono::steady_clock::now() + std::chrono::milliseconds(opt_.clangdBudgetMs), st});
  }
  cv_.notify_all();
  issue([st](std::vector<SymbolLocation> result, std::string err) {
    std::lock_guard<std::mutex> g(st->m);
    if (st->finalDelivered) return;
    st->finalDelivered = true;
    if (!err.empty() && !st->approxDelivered) {
      // clangd failed: fall back to approximate so the user still gets something.
      st->approxDelivered = true;
      st->cb(Confidence::Approximate, st->approx, err);
      return;
    }
    if (!err.empty()) { st->cb(Confidence::Authoritative, {}, err); return; }
    st->cb(Confidence::Authoritative, std::move(result), {});
  });
}

void SemanticEngine::locationQuery(
    const char* what, const Document& doc, Position pos, LocationsCallback cb,
    std::function<int(LspClient&, const std::string&, Position, LspClient::Callback<std::vector<LspLocation>>)> issue) {
  (void)what;
  std::vector<SymbolLocation> approx = approximateDefinitions(doc, pos);
  std::string uri = doc.uri();
  Position sp = toServer(doc, pos);
  deliverWithBudget(std::move(approx), std::move(cb), [this, uri, sp, issue](auto done) {
    LspClient* c = clangd();
    if (!c) return done(std::vector<SymbolLocation>{}, std::string("clangd not running"));
    issue(*c, uri, sp, [this, done](std::vector<LspLocation> ls, const std::string& err) {
      done(fromServer(ls, Confidence::Authoritative), err);
    });
  });
}

void SemanticEngine::augmentWithFastDefinitions(const std::string& name, std::vector<SymbolLocation>& locs) const {
  if (name.empty() || !clangd_) return;
  IndexProgress p = clangd_->indexProgress();
  if (p.percentage == 100 && !p.active) return;  // index complete: clangd's answer is final
  // Is any authoritative result already a definition (per the fast index)?
  for (auto& l : locs) {
    auto fsyms = fast_.fileSymbols(l.loc.file);
    if (!fsyms) return;  // file unknown to us: cannot judge, stay conservative
    for (auto& s : fsyms->symbols)
      if (s.line == l.loc.pos.line && s.name == name && s.definition) return;
  }
  for (auto& m : fast_.lookup(name)) {
    const Symbol& s = m.sym();
    if (!s.definition) continue;
    if (s.kind != SymbolKind::Function && s.kind != SymbolKind::Method && s.kind != SymbolKind::Constructor &&
        s.kind != SymbolKind::Variable && s.kind != SymbolKind::Field)
      continue;
    bool dup = false;
    for (auto& l : locs) dup = dup || (l.loc.file == m.file->file && l.loc.pos.line == s.line);
    if (dup) continue;
    SymbolLocation x = fromFast(m);
    locs.push_back(std::move(x));
  }
}

void SemanticEngine::gotoDefinition(const Document& doc, Position pos, LocationsCallback cb) {
  std::string word = doc.buffer().wordAt(pos).text;
  LocationsCallback wrapped = [this, word, cb = std::move(cb)](Confidence c, std::vector<SymbolLocation> locs, const std::string& err) {
    if (c == Confidence::Authoritative && err.empty()) augmentWithFastDefinitions(word, locs);
    cb(c, std::move(locs), err);
  };
  locationQuery("definition", doc, pos, std::move(wrapped), [](LspClient& c, const std::string& u, Position p, auto k) {
    return c.definition(u, p, std::move(k));
  });
}
void SemanticEngine::gotoDeclaration(const Document& doc, Position pos, LocationsCallback cb) {
  locationQuery("declaration", doc, pos, std::move(cb), [](LspClient& c, const std::string& u, Position p, auto k) {
    return c.declaration(u, p, std::move(k));
  });
}
void SemanticEngine::gotoTypeDefinition(const Document& doc, Position pos, LocationsCallback cb) {
  locationQuery("typeDefinition", doc, pos, std::move(cb), [](LspClient& c, const std::string& u, Position p, auto k) {
    return c.typeDefinition(u, p, std::move(k));
  });
}
void SemanticEngine::findImplementations(const Document& doc, Position pos, LocationsCallback cb) {
  locationQuery("implementation", doc, pos, std::move(cb), [](LspClient& c, const std::string& u, Position p, auto k) {
    return c.implementation(u, p, std::move(k));
  });
}

void SemanticEngine::findReferences(const Document& doc, Position pos, LocationsCallback cb) {
  // No approximate tier for references (a text search would be misleading); go straight to clangd.
  LspClient* c = clangd();
  if (!c) return cb(Confidence::Approximate, {}, "clangd not running");
  c->references(doc.uri(), toServer(doc, pos), true, [this, cb](std::vector<LspLocation> ls, const std::string& err) {
    cb(Confidence::Authoritative, fromServer(ls, Confidence::Authoritative), err);
  });
}

void SemanticEngine::workspaceSymbols(const std::string& query, LocationsCallback cb) {
  std::vector<SymbolLocation> approx = approximateSymbols(query);
  deliverWithBudget(std::move(approx), std::move(cb), [this, query](auto done) {
    LspClient* c = clangd();
    if (!c) return done(std::vector<SymbolLocation>{}, std::string("clangd not running"));
    c->workspaceSymbols(query, [this, done](std::vector<LspWorkspaceSymbol> syms, const std::string& err) {
      std::vector<SymbolLocation> out;
      for (auto& s : syms) {
        SymbolLocation x;
        x.loc = fromServer(s.location);
        x.name = s.name;
        x.container = s.containerName;
        x.kind = lspSymbolKindName(s.kind);
        x.confidence = Confidence::Authoritative;
        out.push_back(std::move(x));
      }
      // clangd also indexes system headers it has seen; 10x-style symbol
      // search wants the workspace's own symbols first.
      const std::string rootPrefix = ws_.root().generic_string() + "/";
      std::stable_partition(out.begin(), out.end(), [&](const SymbolLocation& l) {
        return l.loc.file.generic_string().rfind(rootPrefix, 0) == 0;
      });
      done(std::move(out), err);
    });
  });
}

void SemanticEngine::documentSymbols(const Document& doc, LocationsCallback cb) {
  std::vector<SymbolLocation> approx;
  if (auto fsyms = fast_.fileSymbols(doc.path())) {
    for (size_t i = 0; i < fsyms->symbols.size(); ++i) approx.push_back(fromFast({fsyms, i, 0}));
  }
  std::string uri = doc.uri();
  deliverWithBudget(std::move(approx), std::move(cb), [this, uri](auto done) {
    LspClient* c = clangd();
    if (!c) return done(std::vector<SymbolLocation>{}, std::string("clangd not running"));
    c->documentSymbols(uri, [this, uri, done](std::vector<LspDocumentSymbol> syms, const std::string& err) {
      std::vector<SymbolLocation> out;
      std::function<void(const LspDocumentSymbol&, const std::string&)> walk = [&](const LspDocumentSymbol& s, const std::string& container) {
        SymbolLocation x;
        x.loc = fromServer(LspLocation{uri, s.selectionRange});
        x.name = s.name;
        x.container = container;
        x.kind = lspSymbolKindName(s.kind);
        x.confidence = Confidence::Authoritative;
        out.push_back(std::move(x));
        std::string inner = container.empty() ? s.name : container + "::" + s.name;
        for (auto& ch : s.children) walk(ch, inner);
      };
      for (auto& s : syms) walk(s, "");
      done(std::move(out), err);
    });
  });
}

void SemanticEngine::switchSourceHeader(const Document& doc, Callback<fs::path> cb) {
  if (auto* c = clangd()) {
    c->switchSourceHeader(doc.uri(), [cb](std::string uri, const std::string& err) {
      cb(uri.empty() ? fs::path() : uriToPath(uri), err);
    });
    return;
  }
  // Fallback: same stem, sibling extension, anywhere in the workspace.
  std::string stem = doc.path().stem().string();
  bool isHeader = ws_.isHeader(doc.path());
  for (auto& f : isHeader ? ws_.sourceFiles() : ws_.headerFiles()) {
    if (f.stem().string() == stem) return cb(f, {});
  }
  cb({}, "no counterpart found");
}

// ---------------------------------------------------------------------------
// Information / editing
// ---------------------------------------------------------------------------

void SemanticEngine::hover(const Document& doc, Position pos, Callback<std::string> cb) {
  if (auto* c = clangd()) { c->hover(doc.uri(), toServer(doc, pos), std::move(cb)); return; }
  // Approximate hover: show what the fast index knows about the word.
  auto defs = approximateDefinitions(doc, pos);
  if (defs.empty()) return cb("", "clangd not running");
  std::string s;
  for (auto& d : defs) s += d.kind + " " + (d.container.empty() ? d.name : d.container + "::" + d.name) + "  (" + toString(d.loc) + ")\n";
  cb(s, {});
}

void SemanticEngine::completion(const Document& doc, Position pos, Callback<std::vector<LspCompletionItem>> cb) {
  if (auto* c = clangd()) { c->completion(doc.uri(), toServer(doc, pos), std::move(cb)); return; }
  // Approximate completion: fuzzy-match the prefix against the fast index.
  auto word = doc.buffer().wordAt({pos.line, std::max(0, pos.column - 1)});
  std::vector<LspCompletionItem> out;
  for (auto& m : fast_.search(word.text, 100)) {
    LspCompletionItem it;
    it.label = m.sym().name;
    it.detail = std::string(symbolKindName(m.sym().kind)) + " " + m.sym().qualified();
    it.insertText = it.label;
    it.filterText = it.label;
    it.sortText = it.label;
    out.push_back(std::move(it));
  }
  cb(std::move(out), {});
}

void SemanticEngine::signatureHelp(const Document& doc, Position pos, Callback<LspSignatureHelp> cb) {
  if (auto* c = clangd()) { c->signatureHelp(doc.uri(), toServer(doc, pos), std::move(cb)); return; }
  cb({}, "clangd not running");
}

void SemanticEngine::rename(const Document& doc, Position pos, const std::string& newName,
                            Callback<std::map<fs::path, std::vector<std::pair<Range, std::string>>>> cb) {
  LspClient* c = clangd();
  if (!c) return cb({}, "clangd not running");
  c->rename(doc.uri(), toServer(doc, pos), newName, [this, cb](Json edit, const std::string& err) {
    std::map<fs::path, std::vector<std::pair<Range, std::string>>> out;
    if (!err.empty()) return cb(out, err);
    auto addEdits = [&](const std::string& uri, const Json& edits) {
      fs::path file = uriToPath(uri);
      for (auto& e : edits) {
        Range r = LspClient::rangeFromJson(e.value("range", Json::object()));
        Location s = fromServer({uri, r});
        Location en = fromServer({uri, {r.end, r.end}});
        out[file].push_back({{s.pos, en.pos}, e.value("newText", "")});
      }
    };
    if (edit.contains("changes")) {
      for (auto& [uri, edits] : edit["changes"].items()) addEdits(uri, edits);
    }
    if (edit.contains("documentChanges")) {
      for (auto& dc : edit["documentChanges"]) {
        if (dc.contains("textDocument")) addEdits(dc["textDocument"].value("uri", ""), dc.value("edits", Json::array()));
      }
    }
    cb(out, {});
  });
}

void SemanticEngine::onDiagnostics(std::function<void(const fs::path&, std::vector<Diagnostic>)> h) {
  std::lock_guard<std::mutex> lk(mu_);
  diagHandler_ = std::move(h);
}

}  // namespace tf
