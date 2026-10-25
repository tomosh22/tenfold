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
    case 1: return "file";
    case 2: return "module";
    case 3: return "namespace";
    case 4: return "package";
    case 5: return "class";
    case 6: return "method";
    case 7: return "property";
    case 8: return "field";
    case 9: return "constructor";
    case 10: return "enum";
    case 11: return "interface";
    case 12: return "function";
    case 13: return "variable";
    case 14: return "constant";
    case 15: return "string";
    case 16: return "number";
    case 17: return "boolean";
    case 18: return "array";
    case 19: return "object";
    case 20: return "key";
    case 21: return "null";
    case 22: return "enumerator";
    case 23: return "struct";
    case 24: return "event";
    case 25: return "operator";
    case 26: return "type parameter";
    default: return "symbol";
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

bool SemanticEngine::prepareCompileCommands(const std::vector<fs::path>& sources, const std::vector<fs::path>& headers,
                                            bool reconfigure) {
  // Results are collected in locals and published under mu_ at the end, because
  // a reconfigure runs on the worker thread while status() may be called.
  fs::path compdbPath;
  std::string compdbSource = "none";
  std::vector<std::string> sharedFlags;
  std::error_code ec;
  fs::create_directories(ws_.cacheDir(), ec);
  const fs::path root = ws_.root();

  // Our generated Visual Studio database records the solution configuration and source it was made for
  // (.tenfold/compile_commands.stamp), so a changed vsConfiguration setting is not served from a stale cache.
  const auto slns = ws_.solutionFiles();
  const std::string vsName = slns.empty() ? std::string() : ws_.activeVsConfiguration().name();
  const fs::path stampPath = ws_.cacheDir() / "compile_commands.stamp";
  std::string stampSource, stampConfig;
  {
    std::ifstream sf(stampPath);
    std::getline(sf, stampSource);
    std::getline(sf, stampConfig);
  }

  auto existing = CompileDatabase::find(root);
  const bool generated = existing && existing->parent_path() == ws_.cacheDir();
  if (existing && reconfigure && generated) existing.reset();  // regenerate our own database from scratch
  if (existing && generated && stampSource == "vcxproj" && stampConfig != vsName) existing.reset();
  // A cached heuristic database is only good while nothing better is available: a solution or a CMake project that
  // appeared since (or a shared-flags config that went missing, where the flags live in it) means "regenerate".
  const fs::path configYaml = ws_.cacheDir() / "clangd-config" / "clangd" / "config.yaml";
  if (existing && generated && stampSource == "heuristic") {
#if defined(__APPLE__) || defined(_WIN32)
    const bool configNeeded = false;
#else
    const bool configNeeded = true;
#endif
    if (!slns.empty() || (ws_.hasCMake() && ws_.settings().runCMake) ||
        (configNeeded && !fs::is_regular_file(configYaml, ec)))
      existing.reset();
  }
  bool cachedHeuristic = false;
  if (existing) {
    // Our own generated database lives in .tenfold; prefer a "real" one elsewhere. The stamp says what made it.
    compdbPath = *existing;
    compdbSource = generated ? (stampSource == "vcxproj"     ? "vcxproj"
                                : stampSource == "heuristic" ? "heuristic"
                                                             : "generated(cached)")
                             : "existing";
    cachedHeuristic = generated && stampSource != "vcxproj";  // incl. caches from before stamps existed
  } else if (ws_.hasCMake() && ws_.settings().runCMake) {
    std::string cmakeLog;
    if (auto p = compdb::fromCMake(root, ws_.cacheDir() / "cmake-build", &cmakeLog)) {
      compdbPath = *p;
      compdbSource = "cmake";
    } else {
      log("cmake configure failed:\n" + cmakeLog);
    }
  }
  if (compdbPath.empty()) {
    if (!slns.empty()) {
      // The solution's own configuration list decides what Debug|x64 means; each project gets its mapped
      // configuration (compdb::fromSolution). An unknown setting falls back to Debug|x64, then the first entry.
      compdb::VcxprojOptions vo;
      const auto active = ws_.activeVsConfiguration();
      vo.configuration = active.configuration;
      vo.platform = active.platform;
      if (active.configuration != ws_.settings().vsConfiguration || active.platform != ws_.settings().vsPlatform)
        log("vs configuration " + ws_.settings().vsConfiguration + "|" + ws_.settings().vsPlatform +
            " is not in the solution; using " + active.name());
      std::vector<CompileCommand> all;
      for (auto& s : slns) {
        std::string err;
        auto cmds = compdb::fromSolution(s, vo, &err);
        if (!err.empty()) log("vcxproj: " + err);
        all.insert(all.end(), cmds.begin(), cmds.end());
      }
      if (!all.empty()) {
        compdbPath = ws_.cacheDir() / "compile_commands.json";
        CompileDatabase::save(all, compdbPath);
        compdbSource = "vcxproj";
        std::ofstream sf(stampPath);
        sf << "vcxproj\n" << vsName << "\n";
      }
    }
  }
  if (compdbPath.empty()) {
    // The fast index knows every #include in the workspace; use them to infer
    // the include directories instead of guessing. It is normally done within
    // a second or two; cap the wait so a huge cold workspace still starts.
    waitForFastIndex(15000);
#ifdef _WIN32
    constexpr bool kClDriver = true;  // clangd finds the MSVC STL / Windows SDK through the clang-cl driver
#else
    constexpr bool kClDriver = false;
#endif
    auto cmds = compdb::heuristic(root, sources, headers, ws_.settings().cppStandard, fast_.allIncludes(), kClDriver);
    const std::vector<std::string> sysFlags = compdb::platformSystemFlags();
    // The include directories are identical for every file. Repeating them
    // per entry makes the JSON enormous (hundreds of MB on big trees), so the
    // database keeps only compiler + std + file and the shared flags go into
    // a clangd config (CompileFlags.Add), which applies to headers too.
    // That only works where clangd honours XDG_CONFIG_HOME (Linux); on macOS (~/Library/Preferences)
    // and Windows (%LOCALAPPDATA%) the private config may not be read, so the flags stay in the commands.
#if defined(__APPLE__) || defined(_WIN32)
    constexpr bool kFlagsViaConfig = false;
#else
    constexpr bool kFlagsViaConfig = true;
#endif
    for (auto& c : cmds) {
      std::vector<std::string> keep;
      for (auto& a : c.arguments) {
        if (kFlagsViaConfig && a.rfind("-I", 0) == 0) {
          if (std::find(sharedFlags.begin(), sharedFlags.end(), a) == sharedFlags.end()) sharedFlags.push_back(a);
        } else {
          keep.push_back(a);
        }
      }
      // A standalone clangd does not know the system headers (macOS SDK); they must
      // stay in the command, not the -I config, so they come right after the driver.
      keep.insert(keep.begin() + 1, sysFlags.begin(), sysFlags.end());
      c.arguments = std::move(keep);
    }
    compdbPath = ws_.cacheDir() / "compile_commands.json";
    CompileDatabase::save(cmds, compdbPath);
    compdbSource = "heuristic";
    std::ofstream sf(stampPath);
    sf << "heuristic\n\n";
  }
  // clangd user config lives in $XDG_CONFIG_HOME/clangd/config.yaml. We give
  // clangd a private config home under .tenfold so we can add flags without
  // touching the user's files; projects can still add a .clangd at their root.
  // The file is (re)written when we generated the database or when it is
  // missing; a cached heuristic database keeps the config written alongside it.
  // The config is rewritten whenever its content would differ, so heuristic -I flags can never apply to a database
  // from another source. A cached heuristic database keeps the shared flags written alongside it (it is only kept
  // while that file exists, see above).
  fs::path cfgDir = ws_.cacheDir() / "clangd-config" / "clangd";
  const bool keepCachedConfig = cachedHeuristic && fs::is_regular_file(configYaml, ec);
  if (!keepCachedConfig) {
    std::ostringstream cfg;
    cfg << "# Generated by tenfold. Edit <workspace>/.clangd for project-specific settings instead.\n";
    if (!sharedFlags.empty()) {
      cfg << "CompileFlags:\n";
      cfg << "  Add:\n";
      for (auto& f : sharedFlags) cfg << "    - " << Json(f).dump() << "\n";
    }
    cfg << "Index:\n  Background: Build\n";
    std::string current;
    {
      std::ifstream in(configYaml, std::ios::binary);
      current.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (current != cfg.str()) {
      fs::create_directories(cfgDir, ec);
      std::ofstream out(configYaml, std::ios::binary | std::ios::trunc);
      out << cfg.str();
    }
  }
  auto cmds = CompileDatabase::load(compdbPath);
  log("compile database: " + compdbSource + " (" + std::to_string(cmds.size()) + " entries) at " + compdbPath.string());
  bool locationChanged;
  {
    std::lock_guard<std::mutex> lk(mu_);
    locationChanged = compdbPath_.parent_path() != compdbPath.parent_path();
    compdbPath_ = std::move(compdbPath);
    compdbSource_ = std::move(compdbSource);
    sharedFlags_ = std::move(sharedFlags);
    compileCommands_ = std::make_shared<const std::vector<CompileCommand>>(std::move(cmds));
  }
  return locationChanged;
}

std::shared_ptr<LspClient> SemanticEngine::client() const {
  std::lock_guard<std::mutex> lk(clientMu_);
  return clangd_;
}

std::shared_ptr<LspClient> SemanticEngine::clangd() const {
  auto c = client();
  return c && c->initialized() ? c : nullptr;
}

std::shared_ptr<LspClient> SemanticEngine::takeClient() {
  std::lock_guard<std::mutex> lk(clientMu_);
  return std::move(clangd_);
}

std::shared_ptr<const std::vector<CompileCommand>> SemanticEngine::compileCommands() const {
  std::lock_guard<std::mutex> lk(mu_);
  return compileCommands_ ? compileCommands_ : std::make_shared<const std::vector<CompileCommand>>();
}

bool SemanticEngine::startClangd(std::string* error) {
  auto c = launchClangd(error);
  if (!c) return false;
  {
    std::lock_guard<std::mutex> lk(clientMu_);
    clangd_ = c;
  }
  log("clangd started: " + c->serverVersion() + ", offsetEncoding=" + c->offsetEncoding());
  return true;
}

std::shared_ptr<LspClient> SemanticEngine::launchClangd(std::string* error) {
  fs::path compdbPath;
  std::string compdbSource;
  {
    std::lock_guard<std::mutex> lk(mu_);
    compdbPath = compdbPath_;
    compdbSource = compdbSource_;
  }
  const std::string setting = opt_.clangdPath.empty() ? ws_.settings().clangdPath : opt_.clangdPath;
  // Precedence: clangdPath setting / --clangd > clangd bundled next to the editor > PATH.
  const ClangdChoice choice = findClangd(setting);
  if (choice.path.empty()) {
    if (error) {
      *error = "clangd not found: " + (setting.empty() ? std::string("clangd") : setting);
      if (setting.empty()) {
        *error += " (executable dir: " + executableDir().string() + "; looked next to it, in its bin/, and on PATH)";
      }
    }
    return nullptr;
  }
  const std::string resolved = choice.path;
  {
    std::lock_guard<std::mutex> lk(mu_);
    clangdExe_ = choice.path;
    clangdSource_ = choice.source;
  }
  log("clangd: using " + choice.path + " (" + choice.source + ")");
  LspClient::Options lo;
  lo.serverPath = resolved;
  lo.rootPath = ws_.root();
  lo.logFile = opt_.logDir / "clangd.log";
  log("clangd: starting " + resolved + " (compile commands: " + compdbSource + ", stderr: " + lo.logFile.string() +
      ")");
  lo.args = {"--compile-commands-dir=" + compdbPath.parent_path().string(),
             "--background-index",
             "--pch-storage=memory",
             "--header-insertion=never",
             "--completion-style=detailed",
             "--all-scopes-completion",
             "--limit-results=500",
             "--limit-references=10000",
             "--log=error",
             "-j=" + std::to_string(std::max(2u, std::thread::hardware_concurrency()))};
#ifdef _WIN32
  const bool wantMsvcArgs = compdbSource == "vcxproj" || compdbSource == "heuristic";
#else
  const bool wantMsvcArgs = compdbSource == "vcxproj";
#endif
  if (wantMsvcArgs) {
    // MSVC system headers: let clangd ask cl.exe. The toolchain is user-overridable and persisted.
    auto cl = compdb::discoverCl(ws_.settings().msvcToolchain, compdb::systemMsvcProbe());
    if (cl) {
      if (ws_.settings().msvcToolchain.empty()) {
        ws_.settings().msvcToolchain = cl->generic_string();
        ws_.saveSettings();
      }
      for (auto& a : compdb::msvcClangdArgs(*cl)) lo.args.push_back(a);
    } else {
      log("cl.exe not found; set \"msvcToolchain\" in .tenfold/workspace.json for MSVC system headers");
    }
  }
  for (auto& a : ws_.settings().clangdExtraArgs) lo.args.push_back(a);
  for (auto& a : opt_.clangdArgs) lo.args.push_back(a);
  lo.fallbackFlags = {"-std=" + ws_.settings().cppStandard, "-I" + ws_.root().string()};
  lo.env = {{"XDG_CONFIG_HOME", (ws_.cacheDir() / "clangd-config").string()}};
#ifdef _WIN32
  // clangd on Windows reads its user config from %LOCALAPPDATA%\clangd, ignoring XDG_CONFIG_HOME;
  // without this the shared -I flags in our private config.yaml would never be applied.
  lo.env.emplace_back("LOCALAPPDATA", (ws_.cacheDir() / "clangd-config").string());
#endif
  if (opt_.traceLsp) {
    lo.trace = [this](const std::string& dir, const Json& j) { log(dir + " " + j.dump().substr(0, 400)); };
  }

  auto c = std::make_shared<LspClient>();
  c->onFileStatus([this](const std::string& uri, const std::string& state) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      fileStatus_[uri] = state;
    }
    cv_.notify_all();
  });
  c->onIndexProgress([this](const IndexProgress&) { cv_.notify_all(); });
  c->onDiagnostics([this](const std::string& uri, int, std::vector<LspDiagnostic> ds) {
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
      x.range = fromServer(LspLocation{uri, d.range}).pos == d.range.start
                    ? d.range
                    : Range{fromServer({uri, d.range}).pos, fromServer({uri, {d.range.end, d.range.end}}).pos};
      x.severity = d.severity;
      x.message = d.message;
      x.code = d.code;
      out.push_back(std::move(x));
    }
    h(file, std::move(out));
  });
  c->onApplyEdit([this](const std::string& label, const Json& edit) {
    std::function<void(const std::string&, FileEdits)> h;
    {
      std::lock_guard<std::mutex> lk(mu_);
      h = applyEditHandler_;
    }
    if (h) h(label, editsFromServer(edit));
  });
  c->onLogMessage([this](int, const std::string& m) { log("clangd: " + m); });
  c->onDisconnect([this, wc = std::weak_ptr<LspClient>(c)] {
    log("clangd exited");
    cv_.notify_all();
    if (auto self = wc.lock()) noteCrash(std::move(self));
  });

  std::string err;
  if (!c->start(lo, &err)) {
    if (error) *error = err;
    log("failed to start clangd: " + err);
    return nullptr;
  }
  return c;
}

void SemanticEngine::saveFastIndex() {
  if (!opt_.persistFastIndex) return;
  uint64_t gen = fast_.generation();
  if (gen == savedGen_) return;
  std::string err;
  if (FastIndexStore::save(ws_.cacheDir() / "fastindex.bin", fast_.exportFiles(), &err)) savedGen_ = gen;
  else log("fast index save failed: " + err);
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
      if (auto loaded = FastIndexStore::load(ws_.cacheDir() / "fastindex.bin", &err)) cached = std::move(*loaded);
      else log("fast index store not used: " + err);
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
    // Warm the symbol-search structures (not part of the reported index time; a query that arrives first builds
    // them itself).
    fast_.prepareSearch();
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
  {
    std::lock_guard<std::mutex> lk(workMu_);
    workStop_ = false;
  }
  {
    std::lock_guard<std::mutex> lk(symMu_);
    symStop_ = false;
  }
  timerRun_ = true;
  timer_ = std::thread([this] { timerLoop(); });
  startFastIndex();
  prepareCompileCommands(ws_.sourceFiles(), ws_.headerFiles(), false);
  bool ok = true;
  {
    std::lock_guard<std::mutex> lk(supMu_);
    supStop_ = false;
    consecutive_ = 0;
  }
  if (opt_.useClangd) {
    setHealth(ClangdHealth::Phase::Starting, 0);
    std::string err;
    {
      std::lock_guard<std::mutex> life(lifeMu_);
      ok = startClangd(&err);
    }
    if (error && !ok) *error = err;
    {
      std::lock_guard<std::mutex> lk(supMu_);
      runStart_ = std::chrono::steady_clock::now();
    }
    setHealth(ok ? ClangdHealth::Phase::Running : ClangdHealth::Phase::Stopped, 0, err);
    supThread_ = std::thread([this] { supervisorLoop(); });
  }
  return ok;
}

void SemanticEngine::stop() {
  {
    std::lock_guard<std::mutex> lk(supMu_);
    supStop_ = true;
  }
  supCv_.notify_all();  // interrupts a backoff wait
  if (supThread_.joinable()) supThread_.join();
  std::shared_ptr<LspClient> orphan, orphan2;
  {
    std::lock_guard<std::mutex> lk(supMu_);
    orphan = std::move(crashed_);
    orphan2 = std::move(adoptClient_);
    ++adoptGen_;
  }
  if (orphan2) orphan2->stop();
  {
    std::lock_guard<std::mutex> lk(workMu_);
    workStop_ = true;
    work_.clear();
  }
  workCv_.notify_all();
  if (worker_.joinable()) worker_.join();
  {
    std::lock_guard<std::mutex> lk(symMu_);
    symStop_ = true;
    symPending_.reset();
    ++symGen_;  // abort a scan in progress
  }
  symCv_.notify_all();
  if (symThread_.joinable()) symThread_.join();
  // Clear the flag under mu_: timerLoop checks it and then waits while holding mu_, so an unlocked store can
  // land between its check and its wait and the notification is lost (shutdown hangs forever).
  bool timerWasRunning;
  {
    std::lock_guard<std::mutex> g(mu_);
    timerWasRunning = timerRun_.exchange(false);
  }
  if (timerWasRunning) {
    cv_.notify_all();
    if (timer_.joinable()) timer_.join();
  }
  {
    std::lock_guard<std::mutex> g(saveMu_);
    saveRun_ = false;
  }
  saveCv_.notify_all();
  if (indexThread_.joinable()) indexThread_.join();
  if (auto old = takeClient()) old->stop();
}

SemanticEngine::Status SemanticEngine::status() const {
  Status s;
  if (auto c = client()) {
    s.clangdRunning = c->alive();
    s.clangdInitialized = c->initialized();
    s.clangdVersion = c->serverVersion();
    s.offsetEncoding = c->offsetEncoding();
    s.clangdIndex = c->indexProgress();
  }
  s.fastIndexDone = fastDone_;
  s.fastRelexed = fastStats_.relexed;
  s.fastReused = fastStats_.reused;
  s.fastFiles = fast_.fileCount();
  s.fastSymbols = fast_.symbolCount();
  s.fastSeconds = fastDone_ ? fastStats_.seconds : 0;
  {
    std::lock_guard<std::mutex> lk(mu_);
    s.compdbSource = compdbSource_;
    s.compdbPath = compdbPath_;
    s.compdbEntries = compileCommands_ ? compileCommands_->size() : 0;
    s.clangdExe = clangdExe_;
    s.clangdSource = clangdSource_;
  }
  return s;
}

bool SemanticEngine::waitForFastIndex(int timeoutMs) const {
  std::unique_lock<std::mutex> lk(mu_);
  return cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return fastDone_.load(); });
}

bool SemanticEngine::waitForClangdIndex(int timeoutMs) const {
  auto c = client();
  if (!c) return false;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  std::unique_lock<std::mutex> lk(mu_);
  while (true) {
    auto p = c->indexProgress();
    if (p.percentage == 100 && !p.active) return true;
    if (!c->alive()) return false;
    if (cv_.wait_until(lk, deadline) == std::cv_status::timeout) {
      auto q = c->indexProgress();
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
  if (!client()) return false;
  std::string uri = pathToUri(file);
  std::unique_lock<std::mutex> lk(mu_);
  return cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
    auto it = fileStatus_.find(uri);
    return it != fileStatus_.end() && it->second == "idle";
  });
}

// ---------------------------------------------------------------------------
// File-system changes
// ---------------------------------------------------------------------------

void SemanticEngine::runAsync(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lk(workMu_);
    if (workStop_) return;
    work_.push_back(std::move(task));
    if (!worker_.joinable()) worker_ = std::thread([this] { workerLoop(); });
  }
  workCv_.notify_one();
}

void SemanticEngine::workerLoop() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lk(workMu_);
      workCv_.wait(lk, [&] { return workStop_ || !work_.empty(); });
      if (workStop_) return;
      task = std::move(work_.front());
      work_.pop_front();
    }
    task();
  }
}

void SemanticEngine::fileChanged(const fs::path& file, bool includeOpen) {
  if (!ws_.isSource(file) && !ws_.isHeader(file)) return;
  if (!includeOpen) {
    std::lock_guard<std::mutex> lk(mu_);
    if (openDocs_.count(pathToUri(file))) return;
  }
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    fast_.removeFile(file);  // vanished between the event and now
    return;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  fast_.updateFile(file, ss.str());
}

void SemanticEngine::fileRemoved(const fs::path& file, bool includeOpen) {
  if (!includeOpen) {
    std::lock_guard<std::mutex> lk(mu_);
    if (openDocs_.count(pathToUri(file))) return;
  }
  fast_.removeFile(file);
}

void SemanticEngine::indexFilesAsync(std::vector<fs::path> files) {
  runAsync([this, files = std::move(files)] {
    for (auto& f : files) fileChanged(f);
  });
}

void SemanticEngine::refreshCompileCommands(std::vector<fs::path> sources, std::vector<fs::path> headers,
                                            std::function<void(bool)> done) {
  runAsync([this, sources = std::move(sources), headers = std::move(headers), done = std::move(done)] {
    bool moved = prepareCompileCommands(sources, headers, true);
    log(moved ? "compile database moved; clangd restart needed" : "compile database refreshed in place");
    if (done) done(moved);
  });
}

void SemanticEngine::resetClientState() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    fileStatus_.clear();
  }
  std::lock_guard<std::mutex> lk(tokMu_);  // result ids are meaningless to a new server
  for (auto& [uri, st] : tokens_) {
    st.resultId.clear();
    st.data.clear();
  }
}

// Re-send every open document with its current text and version and ask for fresh semantic tokens (diagnostics
// follow from the didOpen). Must run on the thread that owns the documents.
void SemanticEngine::reopenDocuments(LspClient& c) {
  std::vector<const Document*> docs;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [uri, d] : openDocs_) docs.push_back(d);
  }
  for (auto* d : docs) c.didOpen(d->uri(), languageIdFor(d->path()), d->version(), d->buffer().text());
  for (auto* d : docs) scheduleTokens(*d);
}

bool SemanticEngine::restartClangd(std::string* error) {
  if (!opt_.useClangd) return false;
  std::lock_guard<std::mutex> life(lifeMu_);
  std::shared_ptr<LspClient> orphan, orphan2;
  {
    std::lock_guard<std::mutex> lk(supMu_);
    ++manualGen_;  // abandon an automatic restart that is waiting or about to be adopted
    ++adoptGen_;
    consecutive_ = 0;
    orphan = std::move(adoptClient_);
    orphan2 = std::move(crashed_);
  }
  supCv_.notify_all();
  setHealth(ClangdHealth::Phase::Starting, 0);
  resetClientState();
  if (auto old = takeClient()) old->stop();
  if (orphan) orphan->stop();
  std::string err;
  if (!startClangd(&err)) {
    if (error) *error = err;
    setHealth(ClangdHealth::Phase::Stopped, 0, err);
    return false;
  }
  auto nc = client();
  if (!nc) return false;
  {
    std::lock_guard<std::mutex> lk(supMu_);
    runStart_ = std::chrono::steady_clock::now();
    ++health_.totalRestarts;
  }
  reopenDocuments(*nc);
  setHealth(ClangdHealth::Phase::Running, 0);
  if (!nc->alive()) noteCrash(nc);
  return true;
}

// ---------------------------------------------------------------------------
// Crash recovery (D2)
// ---------------------------------------------------------------------------

std::string SemanticEngine::ClangdHealth::text() const {
  switch (phase) {
    case Phase::Starting: return "clangd starting...";
    case Phase::Restarting: return "clangd restarting (" + std::to_string(crashes) + ")...";
    case Phase::Stopped:
      if (crashes > 0)
        return "clangd stopped: crashed " + std::to_string(crashes) + (crashes == 1 ? " time" : " times");
      return "clangd stopped" + (lastError.empty() ? std::string() : ": " + lastError);
    default: return {};
  }
}

int SemanticEngine::backoffDelayMs(const Options& opt, int crashes) {
  long long delay = std::max(0, opt.restartBackoffInitialMs);
  for (int i = 1; i < crashes && delay < opt.restartBackoffMaxMs; ++i) delay *= 2;
  return static_cast<int>(std::min<long long>(delay, opt.restartBackoffMaxMs));
}

SemanticEngine::ClangdHealth SemanticEngine::clangdHealth() const {
  std::lock_guard<std::mutex> lk(supMu_);
  return health_;
}

void SemanticEngine::onClangdHealth(std::function<void(const ClangdHealth&)> h) {
  std::lock_guard<std::mutex> lk(supMu_);
  healthHandler_ = std::move(h);
}

void SemanticEngine::setPoster(std::function<void(std::function<void()>)> poster) {
  std::lock_guard<std::mutex> lk(supMu_);
  poster_ = std::move(poster);
}

void SemanticEngine::setHealth(ClangdHealth::Phase phase, int crashes, const std::string& err) {
  ClangdHealth snap;
  std::function<void(const ClangdHealth&)> h;
  {
    std::lock_guard<std::mutex> lk(supMu_);
    health_.phase = phase;
    health_.crashes = crashes;
    health_.lastError = err;
    snap = health_;
    h = healthHandler_;
  }
  cv_.notify_all();
  if (h) h(snap);
}

// Reader thread of a client that died on its own. Never drops the last reference here (that would join the reader
// from itself): the supervisor / stop() release it.
void SemanticEngine::noteCrash(std::shared_ptr<LspClient> c) {
  {
    std::lock_guard<std::mutex> lk(supMu_);
    crashed_ = std::move(c);
  }
  supCv_.notify_all();
}

void SemanticEngine::supervisorLoop() {
  std::unique_lock<std::mutex> lk(supMu_);
  while (true) {
    supCv_.wait(lk, [&] { return supStop_ || crashed_; });
    if (supStop_) return;
    auto dead = std::move(crashed_);
    crashed_.reset();
    lk.unlock();
    handleCrash(dead);
    dead.reset();
    lk.lock();
  }
}

void SemanticEngine::handleCrash(const std::shared_ptr<LspClient>& dead) {
  using Clock = std::chrono::steady_clock;
  bool isCurrent = false;
  {
    std::lock_guard<std::mutex> lk(clientMu_);
    if (clangd_ == dead) {
      clangd_.reset();
      isCurrent = true;
    }
  }
  uint64_t gen;
  int attempt;
  {
    std::lock_guard<std::mutex> lk(supMu_);
    const bool wasPending = adoptClient_ == dead;
    if (wasPending) {
      adoptClient_.reset();
      ++adoptGen_;
    }
    if (!isCurrent && !wasPending) return;  // an older client; already replaced
    gen = manualGen_;
    if (isCurrent && Clock::now() - runStart_ >= std::chrono::milliseconds(opt_.restartHealthyMs)) consecutive_ = 0;
    attempt = ++consecutive_;
  }
  log("clangd crashed (" + std::to_string(attempt) + " in a row)");
  dead->stop();  // reaps the process; its in-flight requests were already failed by the reader

  std::string lastErr;
  while (true) {
    if (attempt >= opt_.restartMaxFastCrashes) {
      log("clangd: giving up after " + std::to_string(attempt) + " consecutive crashes");
      setHealth(ClangdHealth::Phase::Stopped, attempt, lastErr);
      return;
    }
    setHealth(ClangdHealth::Phase::Restarting, attempt, lastErr);
    const int delay = backoffDelayMs(opt_, attempt);
    {
      std::unique_lock<std::mutex> lk(supMu_);
      if (supCv_.wait_for(lk, std::chrono::milliseconds(delay), [&] { return supStop_ || manualGen_ != gen; })) return;
    }
    std::shared_ptr<LspClient> nc;
    std::string err;
    {
      std::lock_guard<std::mutex> life(lifeMu_);
      {
        std::lock_guard<std::mutex> lk(supMu_);
        if (supStop_ || manualGen_ != gen) return;
      }
      nc = launchClangd(&err);
    }
    if (!nc) {
      lastErr = err;
      ++attempt;
      std::lock_guard<std::mutex> lk(supMu_);
      consecutive_ = attempt;
      continue;
    }
    uint64_t id;
    std::function<void(std::function<void()>)> poster;
    bool abandoned;
    {
      std::lock_guard<std::mutex> lk(supMu_);
      abandoned = supStop_ || manualGen_ != gen;
      if (!abandoned) {
        adoptClient_ = nc;
        id = ++adoptGen_;
        poster = poster_;
      }
    }
    if (abandoned) {
      nc->stop();
      return;
    }
    if (poster) poster([this, id] { adoptPending(id); });
    else adoptPending(id);
    return;
  }
}

// Runs on the thread that owns the documents (see setPoster).
void SemanticEngine::adoptPending(uint64_t id) {
  std::shared_ptr<LspClient> nc;
  {
    std::lock_guard<std::mutex> lk(supMu_);
    if (supStop_ || id != adoptGen_ || !adoptClient_) return;  // superseded, or the new server died first
    nc = std::move(adoptClient_);
  }
  resetClientState();
  reopenDocuments(*nc);
  {
    std::lock_guard<std::mutex> lk(clientMu_);
    clangd_ = nc;
  }
  int crashes;
  {
    std::lock_guard<std::mutex> lk(supMu_);
    runStart_ = std::chrono::steady_clock::now();
    ++health_.totalRestarts;
    crashes = consecutive_;
  }
  log("clangd restarted: " + nc->serverVersion());
  setHealth(ClangdHealth::Phase::Running, crashes);
  if (!nc->alive()) noteCrash(nc);  // died while the documents were being re-sent
}

// ---------------------------------------------------------------------------
// Encoding conversion
// ---------------------------------------------------------------------------

Position SemanticEngine::toServer(const Document& doc, Position p) const {
  auto c = client();
  if (!c || c->offsetEncoding() == "utf-8") return p;
  p = doc.buffer().clamp(p);
  return {p.line, TextBuffer::utf8ToUtf16Column(doc.buffer().line(p.line), p.column)};
}

Position SemanticEngine::posFromServer(const std::string& uri, Position p) const {
  auto c = client();
  if (c && c->offsetEncoding() != "utf-8" && p.column > 0) {
    std::string line;
    const Document* doc = nullptr;
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto it = openDocs_.find(uri);
      if (it != openDocs_.end()) doc = it->second;
    }
    if (doc && p.line < doc->buffer().lineCount()) line = doc->buffer().line(p.line);
    else line = readLineOfFile(uriToPath(uri), p.line);
    p.column = TextBuffer::utf16ToUtf8Column(line, p.column);
  }
  return p;
}

Location SemanticEngine::fromServer(const LspLocation& l) const {
  Location out;
  out.file = uriToPath(l.uri);
  out.pos = posFromServer(l.uri, l.range.start);
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
  if (auto c = clangd()) c->didOpen(doc.uri(), languageIdFor(doc.path()), doc.version(), doc.buffer().text());
  scheduleTokens(doc);
}

void SemanticEngine::didChange(Document& doc, const Document::Change& change) {
  // Keep the fast index fresh (cheap: single file re-lex).
  fast_.updateFile(doc.path(), doc.buffer().text());
  if (auto c = clangd()) {
    if (c->offsetEncoding() == "utf-8") {
      c->didChangeIncremental(doc.uri(), doc.version(), {{change.removed, change.newText}});
    } else {
      c->didChangeFull(doc.uri(), doc.version(), doc.buffer().text());
    }
  }
  scheduleTokens(doc);
}

void SemanticEngine::didSave(Document& doc) {
  if (auto c = clangd()) c->didSave(doc.uri());
}

void SemanticEngine::didClose(Document& doc) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    openDocs_.erase(doc.uri());
    fileStatus_.erase(doc.uri());
  }
  {
    std::lock_guard<std::mutex> lk(tokMu_);
    tokens_.erase(doc.uri());
  }
  if (auto c = clangd()) c->didClose(doc.uri());
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
      while (i > 0 && (std::isalnum(static_cast<unsigned char>(line[static_cast<size_t>(i) - 1])) ||
                       line[static_cast<size_t>(i) - 1] == '_'))
        --i;
      qualifier =
          line.substr(static_cast<size_t>(i), static_cast<size_t>(e - i)) + (qualifier.empty() ? "" : "::" + qualifier);
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
    if (pending_.empty() && delayed_.empty()) {
      cv_.wait(lk);
      continue;
    }
    auto now = std::chrono::steady_clock::now();
    if (!delayed_.empty() && delayed_.begin()->first <= now) {
      auto fn = std::move(delayed_.begin()->second);
      delayed_.erase(delayed_.begin());
      lk.unlock();
      fn();
      lk.lock();
      continue;
    }
    if (pending_.empty() || pending_.front().deadline > now) {
      auto next = pending_.empty() ? delayed_.begin()->first : pending_.front().deadline;
      if (!delayed_.empty()) next = std::min(next, delayed_.begin()->first);
      cv_.wait_until(lk, next);
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

  auto c = clangd();
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
    if (!err.empty()) {
      st->cb(Confidence::Authoritative, {}, err);
      return;
    }
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
    auto c = clangd();
    if (!c) return done(std::vector<SymbolLocation>{}, std::string("clangd not running"));
    issue(*c, uri, sp, [this, done](std::vector<LspLocation> ls, const std::string& err) {
      done(fromServer(ls, Confidence::Authoritative), err);
    });
  });
}

void SemanticEngine::augmentWithFastDefinitions(const std::string& name, std::vector<SymbolLocation>& locs) const {
  auto c = client();
  if (name.empty() || !c) return;
  IndexProgress p = c->indexProgress();
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
  LocationsCallback wrapped = [this, word, cb = std::move(cb)](Confidence c, std::vector<SymbolLocation> locs,
                                                               const std::string& err) {
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
  auto c = clangd();
  if (!c) return cb(Confidence::Approximate, {}, "clangd not running");
  c->references(doc.uri(), toServer(doc, pos), true, [this, cb](std::vector<LspLocation> ls, const std::string& err) {
    cb(Confidence::Authoritative, fromServer(ls, Confidence::Authoritative), err);
  });
}

void SemanticEngine::workspaceSymbols(const std::string& query, LocationsCallback cb) {
  auto job = std::make_unique<SymbolJob>();
  job->query = query;
  job->cb = std::move(cb);
  {
    std::lock_guard<std::mutex> lk(symMu_);
    if (!symStop_) {
      job->gen = ++symGen_;          // from here on every older query is superseded
      symPending_ = std::move(job);  // an older job that never started is dropped
      if (!symThread_.joinable()) symThread_ = std::thread([this] { symbolLoop(); });
      symCv_.notify_one();
      return;
    }
  }
  job->cb(Confidence::Approximate, {}, "semantic engine stopped");
}

void SemanticEngine::symbolLoop() {
  for (;;) {
    std::unique_ptr<SymbolJob> job;
    {
      std::unique_lock<std::mutex> lk(symMu_);
      symCv_.wait(lk, [&] { return symStop_ || symPending_; });
      if (symStop_) return;
      job = std::move(symPending_);
    }
    const uint64_t gen = job->gen;
    auto superseded = [this, gen] { return symGen_.load(std::memory_order_acquire) != gen; };
    std::vector<SymbolLocation> approx;
    for (auto& m : fast_.search(job->query, 200, superseded)) approx.push_back(fromFast(m));
    if (superseded()) continue;
    // Late results of a query that was superseded meanwhile (clangd answer, budget timer) are dropped too.
    LocationsCallback cb = [this, gen, inner = std::move(job->cb)](Confidence c, std::vector<SymbolLocation> locs,
                                                                   const std::string& err) {
      if (symGen_.load(std::memory_order_acquire) != gen) return;
      inner(c, std::move(locs), err);
    };
    workspaceSymbolsBudgeted(job->query, std::move(approx), std::move(cb));
  }
}

void SemanticEngine::workspaceSymbolsBudgeted(const std::string& query, std::vector<SymbolLocation> approx,
                                              LocationsCallback cb) {
  deliverWithBudget(std::move(approx), std::move(cb), [this, query](auto done) {
    auto c = clangd();
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
    auto c = clangd();
    if (!c) return done(std::vector<SymbolLocation>{}, std::string("clangd not running"));
    c->documentSymbols(uri, [this, uri, done](std::vector<LspDocumentSymbol> syms, const std::string& err) {
      std::vector<SymbolLocation> out;
      std::function<void(const LspDocumentSymbol&, const std::string&)> walk = [&](const LspDocumentSymbol& s,
                                                                                   const std::string& container) {
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
  if (auto c = clangd()) {
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
  if (auto c = clangd()) {
    c->hover(doc.uri(), toServer(doc, pos), std::move(cb));
    return;
  }
  // Approximate hover: show what the fast index knows about the word.
  auto defs = approximateDefinitions(doc, pos);
  if (defs.empty()) return cb("", "clangd not running");
  std::string s;
  for (auto& d : defs)
    s += d.kind + " " + (d.container.empty() ? d.name : d.container + "::" + d.name) + "  (" + toString(d.loc) + ")\n";
  cb(s, {});
}

void SemanticEngine::completion(const Document& doc, Position pos, Callback<std::vector<LspCompletionItem>> cb) {
  if (auto c = clangd()) {
    c->completion(doc.uri(), toServer(doc, pos), std::move(cb));
    return;
  }
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
  if (auto c = clangd()) {
    c->signatureHelp(doc.uri(), toServer(doc, pos), std::move(cb));
    return;
  }
  cb({}, "clangd not running");
}

void SemanticEngine::rename(const Document& doc, Position pos, const std::string& newName,
                            Callback<std::map<fs::path, std::vector<std::pair<Range, std::string>>>> cb) {
  auto c = clangd();
  if (!c) return cb({}, "clangd not running");
  c->rename(doc.uri(), toServer(doc, pos), newName, [this, cb](Json edit, const std::string& err) {
    if (!err.empty()) return cb({}, err);
    cb(editsFromServer(edit), {});
  });
}

void SemanticEngine::onDiagnostics(std::function<void(const fs::path&, std::vector<Diagnostic>)> h) {
  std::lock_guard<std::mutex> lk(mu_);
  diagHandler_ = std::move(h);
}

// ---------------------------------------------------------------------------
// clangd-only features (no approximate tier: an answer from clangd, or an error)
// ---------------------------------------------------------------------------

template <class T>
std::shared_ptr<LspClient> SemanticEngine::requireClangd(const Callback<T>& cb) {
  auto c = clangd();
  if (!c) cb(T{}, "clangd not running");
  return c;
}

FileEdits SemanticEngine::editsFromServer(const Json& edit) const {
  FileEdits out;
  auto addEdits = [&](const std::string& uri, const Json& edits) {
    TextEditList& list = out[uriToPath(uri)];
    for (auto& e : edits) {
      Range r = rangeFromServer(uri, LspClient::rangeFromJson(e.value("range", Json::object())));
      list.push_back({r, e.value("newText", "")});
    }
  };
  if (edit.contains("changes"))
    for (auto& [uri, edits] : edit["changes"].items()) addEdits(uri, edits);
  if (edit.contains("documentChanges")) {
    for (auto& dc : edit["documentChanges"])
      if (dc.contains("textDocument")) addEdits(dc["textDocument"].value("uri", ""), dc.value("edits", Json::array()));
  }
  return out;
}

void SemanticEngine::onApplyEdit(std::function<void(const std::string&, FileEdits)> h) {
  std::lock_guard<std::mutex> lk(mu_);
  applyEditHandler_ = std::move(h);
}

void SemanticEngine::runLater(int delayMs, std::function<void()> fn) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    delayed_.emplace(std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs), std::move(fn));
  }
  cv_.notify_all();
}

// ---- semantic tokens ----

LspSemanticTokensLegend SemanticEngine::tokenLegend() const {
  auto c = clangd();
  return c ? c->semanticTokensLegend() : LspSemanticTokensLegend{};
}

SemanticEngine::TokenStats SemanticEngine::tokenStats() const {
  std::lock_guard<std::mutex> lk(tokMu_);
  return tokenStats_;
}

std::vector<TokenSpan> SemanticEngine::decodeTokens(const std::string& uri, const std::vector<uint32_t>& d) const {
  auto c = client();
  const bool utf16 = c && c->offsetEncoding() != "utf-8";
  const Document* doc = nullptr;
  if (utf16) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = openDocs_.find(uri);
    if (it != openDocs_.end()) doc = it->second;
  }
  std::vector<TokenSpan> out;
  out.reserve(d.size() / 5);
  int line = 0, col = 0;
  for (size_t i = 0; i + 4 < d.size(); i += 5) {
    if (d[i] != 0) {
      line += static_cast<int>(d[i]);
      col = static_cast<int>(d[i + 1]);
    } else {
      col += static_cast<int>(d[i + 1]);
    }
    TokenSpan t;
    t.line = line;
    t.col = col;
    t.len = static_cast<int>(d[i + 2]);
    t.type = static_cast<uint16_t>(d[i + 3]);
    t.modifiers = d[i + 4];
    if (utf16 && doc && line < doc->buffer().lineCount()) {
      const std::string& text = doc->buffer().line(line);
      t.col = TextBuffer::utf16ToUtf8Column(text, col);
      t.len = TextBuffer::utf16ToUtf8Column(text, col + t.len) - t.col;
    }
    out.push_back(t);
  }
  return out;
}

void SemanticEngine::requestTokens(const std::string& uri, Callback<std::vector<TokenSpan>> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  if (!c->supportsSemanticTokens()) return cb({}, "clangd does not provide semantic tokens");
  // Folds a reply into the stored state and delivers the decoded spans. False: a delta that does not fit.
  auto accept = [this, uri, cb](LspSemanticTokens r, const std::string& prevId) {
    std::vector<TokenSpan> spans;
    {
      std::lock_guard<std::mutex> lk(tokMu_);
      TokenState& st = tokens_[uri];
      if (r.full) {
        st.data = std::move(r.data);
      } else {
        if (st.resultId != prevId) return false;
        std::sort(r.edits.begin(), r.edits.end(), [](auto& a, auto& b) { return a.start > b.start; });
        for (auto& e : r.edits) {
          if (static_cast<size_t>(e.start) + e.deleteCount > st.data.size()) return false;
          auto at = st.data.begin() + e.start;
          at = st.data.erase(at, at + e.deleteCount);
          st.data.insert(at, e.data.begin(), e.data.end());
        }
      }
      st.resultId = std::move(r.resultId);
      auto t0 = std::chrono::steady_clock::now();
      spans = decodeTokens(uri, st.data);
      tokenStats_.lastDecodeMs =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    cb(std::move(spans), {});
    return true;
  };
  auto full = [this, c, uri, cb, accept] {
    {
      std::lock_guard<std::mutex> lk(tokMu_);
      ++tokenStats_.fullRequests;
    }
    c->semanticTokensFull(uri, [cb, accept](LspSemanticTokens r, const std::string& err) {
      if (!err.empty()) return cb({}, err);
      r.full = true;
      accept(std::move(r), {});
    });
  };
  std::string prev;
  {
    std::lock_guard<std::mutex> lk(tokMu_);
    auto it = tokens_.find(uri);
    if (it != tokens_.end() && c->supportsTokenDelta()) prev = it->second.resultId;
    if (!prev.empty()) ++tokenStats_.deltaRequests;
  }
  if (prev.empty()) return full();
  c->semanticTokensDelta(uri, prev, [full, accept, prev](LspSemanticTokens r, const std::string& err) {
    if (!err.empty() || !accept(std::move(r), prev)) full();  // unknown result id etc.: start over
  });
}

void SemanticEngine::semanticTokens(const Document& doc, Callback<std::vector<TokenSpan>> cb) {
  requestTokens(doc.uri(), std::move(cb));
}

void SemanticEngine::onSemanticTokens(std::function<void(const fs::path&, int, std::vector<TokenSpan>)> h) {
  std::lock_guard<std::mutex> lk(tokMu_);
  tokenHandler_ = std::move(h);
}

void SemanticEngine::scheduleTokens(const Document& doc) {
  if (opt_.semanticTokensDebounceMs < 0) return;
  std::string uri = doc.uri();
  uint64_t gen;
  {
    std::lock_guard<std::mutex> lk(tokMu_);
    if (!tokenHandler_) return;
    TokenState& st = tokens_[uri];
    gen = ++st.generation;
    st.pendingVersion = doc.version();
  }
  runLater(opt_.semanticTokensDebounceMs, [this, uri, gen] {
    int version;
    std::function<void(const fs::path&, int, std::vector<TokenSpan>)> h;
    {
      std::lock_guard<std::mutex> lk(tokMu_);
      auto it = tokens_.find(uri);
      if (it == tokens_.end() || it->second.generation != gen || !tokenHandler_) return;  // superseded / closed
      version = it->second.pendingVersion;
      h = tokenHandler_;
    }
    if (!clangd()) return;
    requestTokens(uri, [h, uri, version](std::vector<TokenSpan> spans, const std::string& err) {
      if (err.empty()) h(uriToPath(uri), version, std::move(spans));
    });
  });
}

// ---- simple per-document queries ----

void SemanticEngine::inlayHints(const Document& doc, Range range, Callback<std::vector<LspInlayHint>> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  std::string uri = doc.uri();
  c->inlayHints(uri, toServer(doc, range), [this, uri, cb](std::vector<LspInlayHint> hs, const std::string& err) {
    for (auto& h : hs) h.pos = posFromServer(uri, h.pos);
    cb(std::move(hs), err);
  });
}

void SemanticEngine::foldingRanges(const Document& doc, Callback<std::vector<LspFoldingRange>> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  std::string uri = doc.uri();
  c->foldingRanges(uri, [this, uri, cb](std::vector<LspFoldingRange> rs, const std::string& err) {
    for (auto& r : rs) {
      if (r.startColumn >= 0) r.startColumn = posFromServer(uri, {r.startLine, r.startColumn}).column;
      if (r.endColumn >= 0) r.endColumn = posFromServer(uri, {r.endLine, r.endColumn}).column;
    }
    cb(std::move(rs), err);
  });
}

void SemanticEngine::documentHighlights(const Document& doc, Position pos, Callback<std::vector<LspHighlight>> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  std::string uri = doc.uri();
  c->documentHighlights(uri, toServer(doc, pos), [this, uri, cb](std::vector<LspHighlight> hs, const std::string& err) {
    for (auto& h : hs) h.range = rangeFromServer(uri, h.range);
    cb(std::move(hs), err);
  });
}

void SemanticEngine::formatDocument(const Document& doc, Callback<TextEditList> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  std::string uri = doc.uri();
  c->formatting(uri, [this, uri, cb](std::vector<LspTextEdit> es, const std::string& err) {
    TextEditList out;
    for (auto& e : es) out.push_back({rangeFromServer(uri, e.range), e.newText});
    cb(std::move(out), err);
  });
}

void SemanticEngine::formatRange(const Document& doc, Range range, Callback<TextEditList> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  std::string uri = doc.uri();
  c->rangeFormatting(uri, toServer(doc, range), [this, uri, cb](std::vector<LspTextEdit> es, const std::string& err) {
    TextEditList out;
    for (auto& e : es) out.push_back({rangeFromServer(uri, e.range), e.newText});
    cb(std::move(out), err);
  });
}

// ---- hierarchies ----

HierarchyItem SemanticEngine::hierarchyItemFromServer(const LspHierarchyItem& it) const {
  HierarchyItem out;
  out.name = it.name;
  out.detail = it.detail;
  out.kind = lspSymbolKindName(it.kind);
  out.range = rangeFromServer(it.uri, it.range);
  out.selection = rangeFromServer(it.uri, it.selectionRange);
  out.loc = {uriToPath(it.uri), out.selection.start};
  out.server = it;
  return out;
}

void SemanticEngine::prepareCallHierarchy(const Document& doc, Position pos, Callback<std::vector<HierarchyItem>> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  c->callHierarchyPrepare(doc.uri(), toServer(doc, pos),
                          [this, cb](std::vector<LspHierarchyItem> is, const std::string& e) {
                            std::vector<HierarchyItem> out;
                            for (auto& i : is) out.push_back(hierarchyItemFromServer(i));
                            cb(std::move(out), e);
                          });
}

void SemanticEngine::prepareTypeHierarchy(const Document& doc, Position pos, Callback<std::vector<HierarchyItem>> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  c->typeHierarchyPrepare(doc.uri(), toServer(doc, pos),
                          [this, cb](std::vector<LspHierarchyItem> is, const std::string& e) {
                            std::vector<HierarchyItem> out;
                            for (auto& i : is) out.push_back(hierarchyItemFromServer(i));
                            cb(std::move(out), e);
                          });
}

void SemanticEngine::incomingCalls(const HierarchyItem& item, Callback<std::vector<CallEdge>> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  c->callHierarchyCalls(item.server, true, [this, cb](std::vector<LspCall> cs, const std::string& e) {
    std::vector<CallEdge> out;
    for (auto& call : cs) {  // call sites live in the caller's file
      CallEdge x{hierarchyItemFromServer(call.item), {}};
      for (auto& r : call.ranges) x.ranges.push_back(rangeFromServer(call.item.uri, r));
      out.push_back(std::move(x));
    }
    cb(std::move(out), e);
  });
}

void SemanticEngine::outgoingCalls(const HierarchyItem& item, Callback<std::vector<CallEdge>> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  std::string uri = item.server.uri;
  c->callHierarchyCalls(item.server, false, [this, uri, cb](std::vector<LspCall> cs, const std::string& e) {
    std::vector<CallEdge> out;
    for (auto& call : cs) {  // call sites live in the queried (calling) item's file
      CallEdge x{hierarchyItemFromServer(call.item), {}};
      for (auto& r : call.ranges) x.ranges.push_back(rangeFromServer(uri, r));
      out.push_back(std::move(x));
    }
    cb(std::move(out), e);
  });
}

void SemanticEngine::supertypes(const HierarchyItem& item, Callback<std::vector<HierarchyItem>> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  c->typeHierarchyItems(item.server, true, [this, cb](std::vector<LspHierarchyItem> is, const std::string& e) {
    std::vector<HierarchyItem> out;
    for (auto& i : is) out.push_back(hierarchyItemFromServer(i));
    cb(std::move(out), e);
  });
}

void SemanticEngine::subtypes(const HierarchyItem& item, Callback<std::vector<HierarchyItem>> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  c->typeHierarchyItems(item.server, false, [this, cb](std::vector<LspHierarchyItem> is, const std::string& e) {
    std::vector<HierarchyItem> out;
    for (auto& i : is) out.push_back(hierarchyItemFromServer(i));
    cb(std::move(out), e);
  });
}

// ---- code actions, commands, watched files ----

void SemanticEngine::codeActions(const Document& doc, Range range, Callback<std::vector<CodeAction>> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  c->codeActions(doc.uri(), toServer(doc, range), [this, cb](std::vector<LspCodeAction> as, const std::string& err) {
    std::vector<CodeAction> out;
    for (auto& a : as) {
      CodeAction x;
      x.title = a.title;
      x.kind = a.kind;
      x.preferred = a.preferred;
      x.hasEdit = a.edit.is_object();
      if (x.hasEdit) x.edit = editsFromServer(a.edit);
      x.hasCommand = a.hasCommand;
      x.command = a.command;
      x.arguments = a.arguments;
      out.push_back(std::move(x));
    }
    cb(std::move(out), err);
  });
}

void SemanticEngine::executeCommand(const std::string& command, Json arguments, Callback<Json> cb) {
  auto c = requireClangd(cb);
  if (!c) return;
  c->executeCommand(command, std::move(arguments), std::move(cb));
}

void SemanticEngine::watchedFileChanged(const fs::path& file, int type) {
  auto c = clangd();
  if (!c) return;
  std::string uri = pathToUri(file);
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (openDocs_.count(uri)) return;
  }
  c->didChangeWatchedFiles({{uri, type}});
}

}  // namespace tf
