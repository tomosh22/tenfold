#include "semantic/SemanticEngine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <tuple>

#include "core/JsonUtil.h"
#include "core/PathUtil.h"
#include "core/TextEncoding.h"
#include "semantic/DefChoice.h"
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

// Length of the line break at all[k] that clang knows ("\r\n", "\n", lone '\r'), else 0.
size_t plainBreakLen(const std::string& all, size_t k) {
  if (all[k] == '\n') return 1;
  if (all[k] == '\r') return (k + 1 < all.size() && all[k + 1] == '\n') ? 2 : 1;
  return 0;
}

// The raw bytes of line `line` of a file; lines end at "\r\n", "\n" or a lone '\r' (like clang's), and with
// `separators` (a UTF-8 file) at NEL, LS and PS too, like the editor's (core/LineBreaks.h).
std::string readLineOfFile(const fs::path& p, int line, bool separators = false) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string all = ss.str();
  size_t i = 0;
  auto breakLen = [&](size_t k) { return separators ? linebreak::breakLenAt(all, k) : plainBreakLen(all, k); };
  for (int l = 0; l < line && i < all.size(); ++l) {
    while (i < all.size() && !breakLen(i)) ++i;
    if (i < all.size()) i += breakLen(i);
  }
  size_t e = i;
  while (e < all.size() && !breakLen(e)) ++e;
  return all.substr(i, e - i);
}

std::string languageIdFor(const fs::path& p) {
  std::string e = pathUtf8(p.extension());
  for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (e == ".c") return "c";
  if (e == ".m") return "objective-c";
  if (e == ".mm") return "objective-cpp";
  if (e == ".cu" || e == ".cuh") return "cuda-cpp";
  return "cpp";
}

// Directory name of a solution configuration under .tenfold/vs: readable, filesystem-safe, and unique even when two
// configuration names differ only in characters that are replaced ("Debug|x64" vs "Debug_x64").
std::string vsConfigDirName(const std::string& name) {
  std::string readable, lowered;
  for (char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    lowered.push_back(static_cast<char>(std::tolower(u)));
    readable.push_back((std::isalnum(u) || c == '.' || c == '-' || c == '_') ? c : '_');
  }
  if (readable.size() > 40) readable.resize(40);
  uint32_t h = 2166136261u;  // FNV-1a over the case-folded name: configurations compare case-insensitively
  for (char c : lowered) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
  char hex[16];
  std::snprintf(hex, sizeof hex, "%08x", static_cast<unsigned>(h));
  return readable + "-" + hex;
}

std::string readLine(std::istream& in) {
  std::string l;
  std::getline(in, l);
  if (!l.empty() && l.back() == '\r') l.pop_back();
  return l;
}

}  // namespace

SemanticEngine::SemanticEngine(Workspace& ws, Options opt) : ws_(ws), opt_(std::move(opt)) {
  if (opt_.logDir.empty()) opt_.logDir = ws_.cacheDir();
  fastOnly_.store(opt_.fastOnlyQueries, std::memory_order_relaxed);
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

  // The stamp next to a generated database records what made it (source, solution configuration, toolchain mode), so a
  // changed vsConfiguration setting is not served from a stale cache. Solution databases are per configuration
  // (.tenfold/vs/<configuration>/), heuristic ones live in .tenfold/.
  const auto slns = ws_.solutionFiles();
  const std::string vsName = slns.empty() ? std::string() : ws_.activeVsConfiguration().name();
  const fs::path stampPath = ws_.cacheDir() / "compile_commands.stamp";
  std::string stampSource;
  {
    std::ifstream sf(stampPath);
    std::getline(sf, stampSource);
  }

  auto existing = CompileDatabase::find(root);
  const bool generated = existing && existing->parent_path() == ws_.cacheDir();
  if (existing && reconfigure && generated) existing.reset();  // regenerate our own database from scratch
  // A solution database generated by an earlier version lived directly in .tenfold (one clangd index for every
  // configuration); solution databases now live in .tenfold/vs/<configuration>/, so the old file is never reused.
  if (existing && generated && stampSource == "vcxproj") existing.reset();
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
        // Without an MSVC toolchain (anywhere but Windows, unless the user points msvcToolchain at one) the clang-cl
        // commands would find no standard library: emit them for the host's own headers instead. Windows never does.
#ifdef _WIN32
      const bool hostFallback = false;
#else
      const bool hostFallback = !compdb::hostTargetTriple().empty() &&
                                (opt_.forceHostStdlibFallback ||
                                 !compdb::discoverCl(ws_.settings().msvcToolchain, compdb::systemMsvcProbe()));
#endif
      std::string mode = "msvc";
      if (hostFallback) {
        vo.hostTarget = compdb::hostTargetTriple();
        vo.hostExtraFlags = compdb::platformSystemFlags();
        mode = "host:" + vo.hostTarget;
        for (auto& f : vo.hostExtraFlags) mode += " " + f;
        if (!hostFallbackLogged_.exchange(true))
          log("no MSVC toolchain (cl.exe) found: Visual Studio projects are parsed against this machine's standard "
              "library (--target=" +
              vo.hostTarget +
              "); defines, include directories and forced includes are kept. Set \"msvcToolchain\" in "
              ".tenfold/workspace.json to use MSVC's headers");
      }
      const fs::path vsRoot = ws_.cacheDir() / "vs";
      const fs::path vsDir = vsRoot / vsConfigDirName(vsName);
      const fs::path vsDb = vsDir / "compile_commands.json";
      const fs::path vsStamp = vsDir / "compile_commands.stamp";
      std::string cachedSource, cachedConfig, cachedMode;
      {
        std::ifstream sf(vsStamp);
        cachedSource = readLine(sf);
        cachedConfig = readLine(sf);
        cachedMode = readLine(sf);
      }
      if (!reconfigure && fs::is_regular_file(vsDb, ec) && cachedSource == "vcxproj" && cachedConfig == vsName &&
          cachedMode == mode) {
        compdbPath = vsDb;  // this configuration's database and index are still valid: reuse them
        compdbSource = "vcxproj";
      } else {
        std::vector<CompileCommand> all;
        for (auto& s : slns) {
          std::string err;
          auto cmds = compdb::fromSolution(s, vo, &err);
          if (!err.empty()) log("vcxproj: " + err);
          all.insert(all.end(), cmds.begin(), cmds.end());
        }
        if (!all.empty()) {
          fs::create_directories(vsDir, ec);
          compdbPath = vsDb;
          CompileDatabase::save(all, compdbPath);
          compdbSource = "vcxproj";
          std::ofstream sf(vsStamp);
          sf << "vcxproj\n" << vsName << "\n" << mode << "\n";
        }
      }
      if (!compdbPath.empty()) {
        fs::last_write_time(vsStamp, fs::file_time_type::clock::now(), ec);  // most recently used
        pruneVsConfigDirs(vsRoot, vsDir);
        // Remove what earlier versions generated: a solution database and the shared index of every configuration.
        fs::remove(ws_.cacheDir() / "compile_commands.json", ec);
        fs::remove(stampPath, ec);
        fs::remove_all(ws_.cacheDir() / ".cache" / "clangd" / "index", ec);
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
      for (auto& f : sharedFlags) cfg << "    - " << dumpSafe(Json(f)) << "\n";
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
  log("compile database: " + compdbSource + " (" + std::to_string(cmds.size()) + " entries) at " +
      pathUtf8(compdbPath));
  bool locationChanged;
  {
    std::lock_guard<std::mutex> lk(mu_);
    locationChanged = compdbPath_.parent_path() != compdbPath.parent_path();
    if (!vsName.empty() && !activeVsName_.empty() && compileCommands_ && vsName != activeVsName_)
      switchFrom_ = compileCommands_;  // the restart that follows decides which open documents leave clangd
    activeVsName_ = vsName;
    compdbPath_ = std::move(compdbPath);
    compdbSource_ = std::move(compdbSource);
    sharedFlags_ = std::move(sharedFlags);
    compileCommands_ = std::make_shared<const std::vector<CompileCommand>>(std::move(cmds));
  }
  return locationChanged;
}

// Keeps the most recently used configuration directories (by the mtime of their stamp), `keep` always among them.
void SemanticEngine::pruneVsConfigDirs(const fs::path& vsRoot, const fs::path& keep) {
  std::error_code ec;
  std::vector<std::pair<fs::file_time_type, fs::path>> dirs;
  for (auto& e : fs::directory_iterator(vsRoot, ec)) {
    if (!e.is_directory(ec) || e.path() == keep) continue;
    auto t = fs::last_write_time(e.path() / "compile_commands.stamp", ec);
    dirs.emplace_back(ec ? fs::file_time_type::min() : t, e.path());
  }
  const size_t room = static_cast<size_t>(std::max(1, opt_.maxVsConfigIndexes)) - 1;
  if (dirs.size() <= room) return;
  std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  for (size_t i = room; i < dirs.size(); ++i) {
    log("vs configuration cache dropped (limit " + std::to_string(opt_.maxVsConfigIndexes) +
        "): " + pathUtf8(dirs[i].second.filename()));
    fs::remove_all(dirs[i].second, ec);
  }
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
        *error += " (executable dir: " + pathUtf8(executableDir()) + "; looked next to it, in its bin/, and on PATH)";
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
  log("clangd: starting " + resolved + " (compile commands: " + compdbSource + ", stderr: " + pathUtf8(lo.logFile) +
      ")");
  lo.args = {"--compile-commands-dir=" + pathUtf8(compdbPath.parent_path()),
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
#ifdef _WIN32
      log("cl.exe not found; set \"msvcToolchain\" in .tenfold/workspace.json for MSVC system headers");
#endif
      // elsewhere the vcxproj commands were generated for the host's standard library (prepareCompileCommands)
    }
  }
  for (auto& a : ws_.settings().clangdExtraArgs) lo.args.push_back(a);
  for (auto& a : opt_.clangdArgs) lo.args.push_back(a);
  lo.fallbackFlags = {"-std=" + ws_.settings().cppStandard, "-I" + pathUtf8(ws_.root())};
  lo.env = {{"XDG_CONFIG_HOME", pathUtf8(ws_.cacheDir() / "clangd-config")}};
#ifdef _WIN32
  // clangd on Windows reads its user config from %LOCALAPPDATA%\clangd, ignoring XDG_CONFIG_HOME;
  // without this the shared -I flags in our private config.yaml would never be applied.
  lo.env.emplace_back("LOCALAPPDATA", pathUtf8(ws_.cacheDir() / "clangd-config"));
#endif
  if (opt_.utf16Positions) lo.positionEncodings = {"utf-16"};
  if (opt_.traceLsp) {
    lo.trace = [this](const std::string& dir, const Json& j) { log(dir + " " + dumpSafe(j).substr(0, 400)); };
  }

  auto c = std::make_shared<LspClient>();
  c->onFileStatus([this](const std::string& uri, const std::string& state) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      fileStatus_[uri] = state;
    }
    cv_.notify_all();
  });
  {
    std::lock_guard<std::mutex> lk(mu_);
    indexEnds_ = 0;
    indexIdleSince_ = {};
  }
  c->onIndexProgress([this](const IndexProgress& p) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      if (p.active) {
        indexIdleSince_ = {};
      } else if (p.percentage == 100) {
        ++indexEnds_;
        indexIdleSince_ = std::chrono::steady_clock::now();
      }
    }
    cv_.notify_all();
  });
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
      x.range = rangeFromServer(uri, d.range);
      x.severity = d.severity;
      x.message = d.message;
      x.code = d.code;
      out.push_back(std::move(x));
    }
    if (!showMisc_ && isMiscFile(file)) {  // VS: no squiggles in a file that belongs to no translation unit
      {
        std::lock_guard<std::mutex> lk(mu_);
        miscHeld_[pathKeyUtf8(file)] = {file, std::move(out)};
      }
      h(file, {});
      return;
    }
    {
      std::lock_guard<std::mutex> lk(mu_);
      miscHeld_.erase(pathKeyUtf8(file));
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
    try {
      fastStats_ = fast_.indexFilesCached(ws_.codeFiles(), std::move(cached), opt_.indexThreads);
    } catch (const std::exception& e) {  // never leave fastDone_ unset or take the process down
      log(std::string("fast index failed: ") + e.what());
    }
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

bool SemanticEngine::waitForClangdIndex(int timeoutMs, int quietMs) const {
  auto c = client();
  if (!c) return false;
  using Clock = std::chrono::steady_clock;
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
  const auto quiet = std::chrono::milliseconds(quietMs);
  std::unique_lock<std::mutex> lk(mu_);
  while (true) {
    const IndexProgress p = c->indexProgress();
    auto wakeAt = deadline;
    if (p.percentage == 100 && !p.active && indexEnds_ > 0 && indexIdleSince_ != Clock::time_point{}) {
      const auto settled = indexIdleSince_ + quiet;
      if (Clock::now() >= settled) return true;
      wakeAt = std::min(deadline, settled);
    }
    if (!c->alive()) return false;
    if (cv_.wait_until(lk, wakeAt) == std::cv_status::timeout && Clock::now() >= deadline) {
      const IndexProgress q = c->indexProgress();
      return q.percentage == 100 && !q.active && indexEnds_ > 0;
    }
  }
}

bool SemanticEngine::fileReady(const fs::path& file) const {
  std::lock_guard<std::mutex> lk(mu_);
  if (unsent_.count(pathToUri(file))) return true;
  auto it = fileStatus_.find(pathToUri(file));
  return it != fileStatus_.end() && it->second == "idle";
}

bool SemanticEngine::waitForFile(const fs::path& file, int timeoutMs) const {
  if (!client()) return false;
  std::string uri = pathToUri(file);
  std::unique_lock<std::mutex> lk(mu_);
  return cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
    if (unsent_.count(uri)) return true;
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
  std::string text;
  if (!readRegularFile(file, text)) {
    fast_.removeFile(file);  // vanished (or turned into a directory / device) between the event and now
    return;
  }
  fast_.updateFile(file, isValidUtf8(text) ? text : decodeFileBytes(text).text);
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
  std::shared_ptr<const std::vector<CompileCommand>> from, to;
  std::set<std::string> parked;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [uri, d] : openDocs_) docs.push_back(d);
    from = std::move(switchFrom_);
    to = compileCommands_;
    parked = parkedDocs_;
  }
  {
    // A new server knows none of the translation-unit contexts given to the old one.
    std::lock_guard<std::mutex> lk(tuMu_);
    for (auto& [k, o] : tuOverrides_) c.setCompileCommand(pathUtf8(o.file), pathUtf8(o.directory), o.info.arguments);
  }
  if (from && to) {
    // After a configuration switch: a document that the previous configuration compiled or included and the new one
    // does not (and one that is parked already) stays out of clangd until the user touches it again.
    std::set<std::string> keys;
    for (auto* d : docs) keys.insert(pathKey(d->path()));
    auto includesOf = [this](const fs::path& f) {
      auto fs = fast_.fileSymbols(f);
      return fs ? fs->includes : std::vector<std::string>();
    };
    const std::set<std::string> inNew = compdb::filesReachableFrom(*to, includesOf, keys);
    const std::set<std::string> inOld = compdb::filesReachableFrom(*from, includesOf, keys);
    std::set<std::string> next;
    for (auto* d : docs) {
      const std::string k = pathKey(d->path());
      if (inNew.count(k)) continue;
      if (inOld.count(k) || parked.count(d->uri())) next.insert(d->uri());
    }
    parked = std::move(next);
    for (auto* d : docs)
      if (parked.count(d->uri()))
        log("not part of the active configuration, left out of clangd until used: " + pathUtf8(d->path()));
    std::lock_guard<std::mutex> lk(mu_);
    parkedDocs_ = parked;
  }
  std::vector<const Document*> sent;
  for (auto* d : docs)
    if (!parked.count(d->uri()) && !unsent(d->uri())) sent.push_back(d);
  for (auto* d : sent) c.didOpen(d->uri(), languageIdFor(d->path()), d->version(), d->buffer().textForLsp());
  for (auto* d : sent) scheduleTokens(*d);
}

bool SemanticEngine::unparkIfParked(Document& doc) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (parkedDocs_.empty() || !parkedDocs_.erase(doc.uri())) return false;
  }
  if (auto c = clangd()) {
    if (provideTuContext(*c, doc))  // an include-only file gets its translation-unit context, as in didOpen
      c->didOpen(doc.uri(), languageIdFor(doc.path()), doc.version(), doc.buffer().textForLsp());
  }
  scheduleTokens(doc);
  return true;
}

void SemanticEngine::activateDocument(Document& doc) { unparkIfParked(doc); }

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

void SemanticEngine::setHealth(ClangdHealth::Phase phase, int crashes, const std::string& err, int onlyIfConsecutive) {
  ClangdHealth snap;
  std::function<void(const ClangdHealth&)> h;
  // The supervisor thread (crash handling) and the document thread (adoptPending) both set the health. Taking the
  // snapshot under supMu_ but calling the handler outside any lock let a late "Running" overtake the "Stopped" of a
  // crash that was handled in between, leaving the UI without the stopped status.
  std::lock_guard<std::mutex> order(healthCbMu_);
  {
    std::lock_guard<std::mutex> lk(supMu_);
    if (onlyIfConsecutive >= 0 && consecutive_ != onlyIfConsecutive) return;
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
  setHealth(ClangdHealth::Phase::Running, crashes, {}, crashes);
  if (!nc->alive()) noteCrash(nc);  // died while the documents were being re-sent
}

// ---------------------------------------------------------------------------
// Encoding conversion
// ---------------------------------------------------------------------------

// NEL / LS / PS: clangd's lines are the editor's glued together at them (TextBuffer::textForLsp). Every position that
// goes to or comes from clangd goes through toServer / posFromServer (and serverPosition for a file by path), which
// consult the document's LspLineMap; a document without separators (nearly all) has none and costs one atomic load.
std::shared_ptr<const LspLineMap> SemanticEngine::lspMapFor(const std::string& uri) const {
  if (lspMapCount_.load(std::memory_order_relaxed) == 0) return nullptr;
  std::lock_guard<std::mutex> lk(mu_);
  auto it = lspMaps_.find(uri);
  return it == lspMaps_.end() ? nullptr : it->second;
}

void SemanticEngine::noteLspText(const Document& doc) {
  auto map = LspLineMap::build(doc.buffer());
  if (!map && lspMapCount_.load(std::memory_order_relaxed) == 0) return;
  const std::string uri = doc.uri();
  std::lock_guard<std::mutex> lk(mu_);
  if (map) lspMaps_[uri] = std::move(map);
  else lspMaps_.erase(uri);
  lspMapCount_.store(static_cast<int>(lspMaps_.size()), std::memory_order_relaxed);
}

std::shared_ptr<const SemanticEngine::DiskSeparators> SemanticEngine::diskSeparators(const fs::path& file) const {
  std::error_code ec;
  const auto size = fs::file_size(file, ec);
  if (ec || size > (64u << 20)) return nullptr;
  const int64_t mtime = fs::last_write_time(file, ec).time_since_epoch().count();
  const std::string key = pathKeyUtf8(file);
  {
    std::lock_guard<std::mutex> lk(encMu_);
    auto it = diskSep_.find(key);
    if (it != diskSep_.end() && it->second.mtime == mtime && it->second.size == size) return it->second.sep;
  }
  std::shared_ptr<const DiskSeparators> sep;
  std::string bytes;
  if (readRegularFile(file, bytes) && linebreak::hasSeparator(bytes)) {
    const DecodedText d = decodeFileBytes(bytes);
    if (d.encoding == Encoding::Utf8) {  // a Windows-1252 file has no NEL / LS / PS; UTF-16 is not what clangd reads
      auto ds = std::make_shared<DiskSeparators>();
      ds->text.setText(d.text);
      ds->map = LspLineMap::build(ds->text);
      if (ds->map) sep = std::move(ds);
    }
  }
  std::lock_guard<std::mutex> lk(encMu_);
  if (diskSep_.size() > 64) diskSep_.clear();
  diskSep_[key] = {mtime, size, sep};
  return sep;
}

Position SemanticEngine::toServer(const Document& doc, Position p) const {
  auto c = client();
  if (!c) return p;
  const bool utf16 = c->offsetEncoding() != "utf-8";
  if (auto m = lspMapFor(doc.uri())) return m->toServer(p, doc.buffer().lines(), utf16);
  if (!utf16) return p;
  p = doc.buffer().clamp(p);
  return {p.line, TextBuffer::utf8ToUtf16Column(doc.buffer().line(p.line), p.column)};
}

Encoding SemanticEngine::diskEncoding(const fs::path& file) const {
  std::error_code ec;
  const auto size = fs::file_size(file, ec);
  if (ec || size > (64u << 20)) return Encoding::Utf8;
  const int64_t mtime = fs::last_write_time(file, ec).time_since_epoch().count();
  const std::string key = pathKeyUtf8(file);
  {
    std::lock_guard<std::mutex> lk(encMu_);
    auto it = diskEnc_.find(key);
    if (it != diskEnc_.end() && it->second.mtime == mtime && it->second.size == size) return it->second.enc;
  }
  std::ifstream in(file, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  const Encoding enc = decodeFileBytes(ss.str()).encoding;
  std::lock_guard<std::mutex> lk(encMu_);
  diskEnc_[key] = {mtime, size, enc};
  return enc;
}

// Server columns -> columns in tenfold's text. Open documents were sent as their own UTF-8 text, so utf-8 columns
// already match and utf-16 ones are converted through the document's line. A file that is not open is read by
// clangd as raw bytes: for a single-byte file (Windows-1252) the byte column becomes the column in the decoded text.
Position SemanticEngine::posFromServer(const std::string& serverUri, Position p) const {
  const std::string uri = remapUri(serverUri);
  auto c = client();
  if (!c) return p;
  const bool utf8 = c->offsetEncoding() == "utf-8";
  const Document* doc = nullptr;
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = openDocs_.find(uri);
    if (it != openDocs_.end()) doc = it->second;
  }
  if (doc) {
    if (auto m = lspMapFor(uri)) return m->fromServer(p, doc->buffer().lines(), !utf8);
    if (!utf8 && p.column > 0 && p.line < doc->buffer().lineCount())
      p.column = TextBuffer::utf16ToUtf8Column(doc->buffer().line(p.line), p.column);
    return p;
  }
  const fs::path file = uriToPath(uri);
  if (auto ds = diskSeparators(file)) return ds->map->fromServer(p, ds->text.lines(), !utf8);
  if (p.column <= 0) return p;
  if (diskEncoding(file) == Encoding::Windows1252) {
    const std::string raw = readLineOfFile(file, p.line);
    const size_t n = std::min(raw.size(), static_cast<size_t>(p.column));
    p.column = static_cast<int>(decodeAs(std::string_view(raw).substr(0, n), Encoding::Windows1252).size());
  } else if (!utf8) {
    p.column = TextBuffer::utf16ToUtf8Column(readLineOfFile(file, p.line), p.column);
  }
  return p;
}

Location SemanticEngine::fromServer(const LspLocation& l) const {
  Location out;
  out.file = pathFromUri(l.uri);
  out.pos = posFromServer(l.uri, l.range.start);
  return out;
}

std::vector<SymbolLocation> SemanticEngine::fromServer(const std::vector<LspLocation>& ls, Confidence c) const {
  std::vector<SymbolLocation> out;
  const bool mapped = tuMapped_.load(std::memory_order_acquire);
  std::set<std::tuple<std::string, int, int>> seen;
  for (auto& l : ls) {
    SymbolLocation s;
    s.loc = fromServer(l);
    s.confidence = c;
    // A copy of an includer's text (tuContext) reports the same place as the file itself: one answer, not two.
    if (mapped && !seen.insert({pathKey(s.loc.file), s.loc.pos.line, s.loc.pos.column}).second) continue;
    out.push_back(std::move(s));
  }
  return out;
}

// ---------------------------------------------------------------------------
// Document lifecycle
// ---------------------------------------------------------------------------

// Only C/C++ sources and headers are indexed and synced to clangd. A .txt / .json / .md document used to be lexed as
// C++ on every keystroke (a 200 MB log file: a 200 MB copy plus a 200 MB lex per key) and sent to clangd as "cpp".
static bool isCodeDocument(const Workspace& ws, const Document& doc) {
  return ws.isSource(doc.path()) || ws.isHeader(doc.path());
}

namespace {
// Above this size a keystroke does not re-lex the file on the typing thread (about 75 MB/s: 5 MB = 65 ms, 40 MB =
// half a second per key); the fast index of the file is refreshed once typing pauses.
constexpr size_t kLazyReindexBytes = 1u << 20;
constexpr int kLazyReindexMs = 300;
}  // namespace

void SemanticEngine::didOpen(Document& doc) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    openDocs_[doc.uri()] = &doc;
  }
  if (!isCodeDocument(ws_, doc)) return;
  noteLspText(doc);
  fast_.updateFile(doc.path(), doc.buffer().textForIndex());
  // clangd gets the document as UTF-8 with the file's real line endings (a continued #define in a CRLF header
  // is not the same input to clang with LF; W9e finding 4). Line/column positions are the same either way.
  if (auto c = clangd()) {
    if (provideTuContext(*c, doc))
      c->didOpen(doc.uri(), languageIdFor(doc.path()), doc.version(), doc.buffer().textForLsp());
  }
  scheduleTokens(doc);
}

void SemanticEngine::didChange(Document& doc, const Document::Change& change) {
  if (!isCodeDocument(ws_, doc)) return;
  noteLspText(doc);
  // Keep the fast index fresh: a single-file re-lex, immediately for ordinary files, once typing pauses for big ones.
  std::function<void(std::function<void()>)> poster;
  if (doc.buffer().byteSize() >= kLazyReindexBytes) {
    std::lock_guard<std::mutex> lk(supMu_);
    poster = poster_;
  }
  if (poster) {
    const std::string uri = doc.uri();
    uint64_t gen;
    {
      std::lock_guard<std::mutex> lk(mu_);
      gen = ++reindexGen_[uri];
    }
    runLater(kLazyReindexMs, [this, poster, uri, gen] {
      poster([this, uri, gen] {  // on the thread that owns the documents
        const Document* d = nullptr;
        {
          std::lock_guard<std::mutex> lk(mu_);
          auto g = reindexGen_.find(uri);
          if (g == reindexGen_.end() || g->second != gen) return;  // a newer edit re-armed it, or the document closed
          auto it = openDocs_.find(uri);
          if (it != openDocs_.end()) d = it->second;
        }
        if (d) fast_.updateFile(d->path(), d->buffer().textForIndex());
      });
    });
  } else {
    fast_.updateFile(doc.path(), doc.buffer().textForIndex());
  }
  if (unsent(doc.uri())) return;
  if (unparkIfParked(doc)) return;  // sent whole, with this change in it
  if (auto c = clangd()) {
    // A change that touches NEL / LS / PS, or a document that has them, is sent whole: the range would need the
    // server's lines of the text before the edit (LspLineMap describes the text after it).
    if (c->offsetEncoding() == "utf-8" && !change.separators && !doc.buffer().hasSeparators()) {
      c->didChangeIncremental(doc.uri(), doc.version(), {{change.removed, TextBuffer::lspText(change.newText)}});
    } else {
      c->didChangeFull(doc.uri(), doc.version(), doc.buffer().textForLsp());
    }
  }
  scheduleTokens(doc);
}

void SemanticEngine::didSave(Document& doc) {
  if (!isCodeDocument(ws_, doc) || unsent(doc.uri())) return;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (parkedDocs_.count(doc.uri())) return;
  }
  if (auto c = clangd()) c->didSave(doc.uri());
}

void SemanticEngine::didClose(Document& doc) {
  const std::string uri = doc.uri();
  bool busy = false;
  {
    std::lock_guard<std::mutex> lk(mu_);
    openDocs_.erase(uri);
    reindexGen_.erase(uri);
    if (lspMaps_.erase(uri)) lspMapCount_.store(static_cast<int>(lspMaps_.size()), std::memory_order_relaxed);
    const bool wasUnsent = unsent_.erase(uri) > 0;
    const bool wasParked = parkedDocs_.erase(uri) > 0;
    if (wasUnsent || wasParked) {  // never sent (TU fragment) or parked (configuration switch)
      fileStatus_.erase(uri);
      return;
    }
    auto st = fileStatus_.find(uri);
    busy = st == fileStatus_.end() || st->second != "idle";
    if (!busy) fileStatus_.erase(uri);
  }
  if (!isCodeDocument(ws_, doc)) return;
  {
    std::lock_guard<std::mutex> lk(tokMu_);
    tokens_.erase(uri);
  }
  if (busy && clangd()) {
    // Closing a document that clangd is still building (open, edit, save, close in a row is what a rename across
    // files does) leaves clangd's scheduler wedged under load: its workers wait for a permit that is never
    // released, and every later request stays "queued" (W9g G-2). Close it once it has gone idle.
    closeWhenIdle(uri, std::chrono::steady_clock::now() + std::chrono::seconds(30));
    return;
  }
  if (auto c = clangd()) c->didClose(uri);
}

void SemanticEngine::closeWhenIdle(const std::string& uri, std::chrono::steady_clock::time_point deadline) {
  bool reopened = false, idle = false;
  {
    std::lock_guard<std::mutex> lk(mu_);
    reopened = openDocs_.count(uri) != 0;
    auto st = fileStatus_.find(uri);
    idle = st != fileStatus_.end() && st->second == "idle";
    if (!reopened && (idle || std::chrono::steady_clock::now() >= deadline)) fileStatus_.erase(uri);
  }
  if (reopened) return;  // opened again meanwhile: the new didOpen is an update of the same document
  if (!idle && std::chrono::steady_clock::now() < deadline) {
    runLater(25, [this, uri, deadline] { closeWhenIdle(uri, deadline); });
    return;
  }
  if (auto c = clangd()) c->didClose(uri);
}

// ---------------------------------------------------------------------------
// Translation-unit context for included files (TuContext.h)
// ---------------------------------------------------------------------------

namespace {

std::string includeBaseName(const std::string& spelling) {
  const size_t i = spelling.find_last_of("/\\");
  return i == std::string::npos ? spelling : spelling.substr(i + 1);
}

// Stable (across runs and platforms) name for a prefix copy: FNV-1a over the key of the file and the line.
std::string prefixFileName(const fs::path& file, int line) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char ch : pathKey(file) + ":" + std::to_string(line)) {
    h ^= ch;
    h *= 1099511628211ull;
  }
  char buf[20];
  std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(h ^ (h >> 32)));
  return std::string(buf) + "-" + pathUtf8(file.filename()) + ".h";
}

bool isSourceLikeExtension(const fs::path& p) {
  std::string e = pathUtf8(p.extension());
  for (auto& ch : e) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return e == ".c" || e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".c++" || e == ".cp" || e == ".m" ||
         e == ".mm" || e == ".cu";
}

}  // namespace

bool SemanticEngine::unsent(const std::string& uri) const {
  std::lock_guard<std::mutex> lk(mu_);
  return unsent_.count(uri) != 0;
}

bool SemanticEngine::provideTuContext(LspClient& c, const Document& doc) {
  const fs::path file = doc.path();
  if (tu::isStandardHeaderExtension(file) || file.extension().empty()) return true;
  const auto snap = compileCommands();  // alive for the whole call: the plan points into it
  if (snap->empty()) return true;
  std::unordered_map<std::string, const CompileCommand*> byFile;
  byFile.reserve(snap->size());
  for (const CompileCommand& cc : *snap) {
    const fs::path f = cc.file.is_relative() ? cc.directory / cc.file : cc.file;
    byFile.emplace(pathKey(f.lexically_normal()), &cc);
  }
  auto exact = [&](const fs::path& f) -> const CompileCommand* {
    auto it = byFile.find(pathKey(f.lexically_normal()));
    return it == byFile.end() ? nullptr : it->second;
  };
  if (exact(file)) return true;  // a translation unit of its own

  tu::Env env;
  env.commandFor = exact;
  env.readText = [this](const fs::path& f, std::string& out) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto it = openDocs_.find(pathToUri(f));
      if (it != openDocs_.end()) {
        out = it->second->buffer().textForLsp();
        return true;
      }
    }
    std::error_code ec;
    if (fs::file_size(f, ec) > (16u << 20)) return false;
    std::string bytes;
    if (!readRegularFile(f, bytes)) return false;
    out = decodeFileBytes(bytes).text;
    return true;
  };
  env.includersOf = [this](const fs::path& target) {
    std::vector<fs::path> out;
    const std::string name = pathUtf8(target.filename());
    for (const auto& fsyms : fast_.exportFiles()) {
      for (const auto& inc : fsyms->includes) {
        if (includeBaseName(inc) == name) {
          out.push_back(fsyms->file);
          break;
        }
      }
    }
    return out;
  };
  std::optional<tu::Plan> plan = tu::planContext(file, env);
  if (!plan) return true;
  if (plan->fragment) {
    // Included inside a class body: parsed as a translation unit it ends in an unfinished class and its declarations
    // never reach clangd's index, while the open document would replace the entries the includer's translation unit
    // gave the background index. Not sending it keeps those; queries inside it get the fast tier.
    std::lock_guard<std::mutex> lk(mu_);
    unsent_.insert(doc.uri());
    log("tu context: " + pathUtf8(file) + " is included inside a class body; not sent to clangd");
    return false;
  }

  const fs::path dir = ws_.cacheDir() / "tu";
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::vector<fs::path> prefixFiles;
  std::map<std::string, fs::path> owners;
  for (const auto& link : plan->links) {
    const std::string name = prefixFileName(link.file, link.directive.line);
    const fs::path pf = dir / name;
    std::string current;
    const bool same = readRegularFile(pf, current) && current == link.prefix;
    if (!same) {
      std::ofstream out(pf, std::ios::binary | std::ios::trunc);
      out << link.prefix;
    }
    prefixFiles.push_back(pf);
    owners[name] = link.file;
  }
  tu::CommandOptions co;
  const std::string driver =
      plan->root.arguments.empty() ? std::string() : pathUtf8(fs::path(plan->root.arguments[0]).stem());
  std::string lowerDriver = driver;
  for (auto& ch : lowerDriver) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  co.clMode = lowerDriver == "cl" || lowerDriver == "clang-cl";
  std::string rootExt = pathUtf8(plan->root.file.extension());
  co.isC = rootExt == ".c";
  co.headerLike = !isSourceLikeExtension(file);
  TuOverride ov;
  ov.file = file;
  ov.directory = plan->root.directory;
  ov.info.root = plan->root.file;
  for (const auto& l : plan->links) ov.info.chain.push_back(l.file);
  ov.info.arguments = tu::buildArguments(*plan, file, prefixFiles, co);
  c.setCompileCommand(pathUtf8(file), pathUtf8(ov.directory), ov.info.arguments);
  log("tu context: " + pathUtf8(file) + " <- " + pathUtf8(plan->root.file) + " (" + std::to_string(plan->links.size()) +
      " include level" + (plan->links.size() == 1 ? "" : "s") + ")");
  {
    std::lock_guard<std::mutex> lk(tuMu_);
    for (auto& [n, f] : owners) tuPrefixOwner_[n] = f;
    tuOverrides_[pathKey(file)] = std::move(ov);
    tuMapped_.store(true, std::memory_order_release);
  }
  return true;
}

std::optional<SemanticEngine::TuContextInfo> SemanticEngine::tuContext(const fs::path& file) const {
  std::lock_guard<std::mutex> lk(tuMu_);
  auto it = tuOverrides_.find(pathKey(file));
  if (it == tuOverrides_.end()) return std::nullopt;
  return it->second.info;
}

std::string SemanticEngine::remapUri(const std::string& uri) const {
  if (!tuMapped_.load(std::memory_order_acquire)) return uri;
  const fs::path p = uriToPath(uri);
  if (p.parent_path().filename() != "tu") return uri;
  std::lock_guard<std::mutex> lk(tuMu_);
  auto it = tuPrefixOwner_.find(pathUtf8(p.filename()));
  return it == tuPrefixOwner_.end() ? uri : pathToUri(it->second);
}

// ---------------------------------------------------------------------------
// Rename safety
// ---------------------------------------------------------------------------

namespace {

bool isIdentChar(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

// Calls fn(line, column) for every identifier token equal to `name` in `lines`, skipping comments, string and
// character literals and the argument of #include.
template <class Lines, class Fn>
void forEachIdentifier(const Lines& lines, const std::string& name, Fn&& fn) {
  bool inBlock = false;
  for (size_t ln = 0; ln < lines.size(); ++ln) {
    const std::string_view line = lines[ln];
    size_t i = 0;
    {
      size_t k = 0;
      while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
      if (!inBlock && k < line.size() && line[k] == '#') {
        size_t w = k + 1;
        while (w < line.size() && (line[w] == ' ' || line[w] == '\t')) ++w;
        if (line.compare(w, 7, "include") == 0 || line.compare(w, 6, "import") == 0) continue;
      }
    }
    while (i < line.size()) {
      if (inBlock) {
        const size_t e = line.find("*/", i);
        if (e == std::string_view::npos) break;
        i = e + 2;
        inBlock = false;
        continue;
      }
      const char c = line[i];
      if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
      if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
        inBlock = true;
        i += 2;
        continue;
      }
      if (c == '"' || c == '\'') {
        ++i;
        while (i < line.size() && line[i] != c) i += line[i] == '\\' ? 2 : 1;
        ++i;
        continue;
      }
      if (isIdentChar(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c))) {
        size_t e = i;
        while (e < line.size() && isIdentChar(static_cast<unsigned char>(line[e]))) ++e;
        if (e - i == name.size() && line.compare(i, e - i, name) == 0) fn(static_cast<int>(ln), static_cast<int>(i));
        i = e;
        continue;
      }
      if (std::isdigit(static_cast<unsigned char>(c))) {
        while (i < line.size() && (isIdentChar(static_cast<unsigned char>(line[i])) || line[i] == '\'')) ++i;
        continue;
      }
      ++i;
    }
  }
}

std::vector<std::string> splitTextLines(const std::string& text) {
  // The editor's lines: "\r\n", '\n', a lone '\r', NEL, LS and PS end one (core/LineBreaks.h).
  std::vector<std::string> out;
  size_t b = 0, len = 0;
  linebreak::BreakFinder bf(text);
  for (size_t at = bf.next(0, len); at != std::string::npos; at = bf.next(b, len)) {
    out.push_back(text.substr(b, at - b));
    b = at + len;
  }
  out.push_back(text.substr(b));
  return out;
}

}  // namespace

SemanticEngine::RenameCoverage SemanticEngine::renameCoverage(const std::string& oldName, const FileEdits& edits,
                                                              const fs::path& origin, size_t maxReport,
                                                              size_t maxFiles) const {
  RenameCoverage res;
  if (oldName.empty()) return res;
  // The starts of the edits, per file.
  std::map<std::string, std::set<std::pair<int, int>>> covered;
  std::map<std::string, fs::path> candidates;  // pathKey -> path, in the order found
  std::vector<fs::path> frontier;
  auto add = [&](const fs::path& f) { return candidates.emplace(pathKey(f), f).second; };
  for (const auto& [f, list] : edits) {
    if (list.empty()) continue;
    for (const auto& e : list) covered[pathKey(f)].insert({e.first.start.line, e.first.start.column});
    if (add(f)) frontier.push_back(f);
  }
  if (!origin.empty() && add(origin)) frontier.push_back(origin);

  // Include graph from the fast index: file -> spellings, and the files by base name.
  const auto all = fast_.exportFiles();
  std::unordered_map<std::string, std::vector<const FileSymbols*>> byBase;
  std::unordered_map<std::string, const FileSymbols*> byKey;
  for (const auto& f : all) {
    byBase[pathUtf8(f->file.filename())].push_back(f.get());
    byKey[pathKey(f->file)] = f.get();
  }
  auto baseOf = [](const std::string& s) {
    const size_t i = s.find_last_of("/\\");
    return i == std::string::npos ? s : s.substr(i + 1);
  };
  // Up: files that include a candidate, three levels.
  std::vector<fs::path> level = frontier;
  for (int depth = 0; depth < 3 && !level.empty() && candidates.size() < maxFiles; ++depth) {
    std::vector<fs::path> next;
    for (const auto& target : level) {
      for (const auto& f : all) {
        bool hit = false;
        for (const auto& inc : f->includes)
          if (baseOf(inc) == pathUtf8(target.filename()) && tu::includeSpellingResolves(inc, f->file, target)) {
            hit = true;
            break;
          }
        if (hit && add(f->file)) next.push_back(f->file);
        if (candidates.size() >= maxFiles) break;
      }
    }
    level = std::move(next);
  }
  // Down: files included by an edited file, two levels (a header's class-body fragments live one level deeper).
  std::vector<fs::path> below = frontier;
  for (int depth = 0; depth < 2 && !below.empty(); ++depth) {
    std::vector<fs::path> nextBelow;
    for (const auto& top : below) {
      auto it = byKey.find(pathKey(top));
      if (it == byKey.end()) continue;
      for (const auto& inc : it->second->includes) {
        // Files the fast index does not scan (.def lists) are found next to the includer.
        std::error_code nec;
        const fs::path near = (top.parent_path() / inc).lexically_normal();
        if (candidates.size() < maxFiles && fs::is_regular_file(near, nec) && add(near)) nextBelow.push_back(near);
        auto b = byBase.find(baseOf(inc));
        if (b == byBase.end()) continue;
        for (const FileSymbols* c : b->second)
          if (tu::includeSpellingResolves(inc, top, c->file) && candidates.size() < maxFiles && add(c->file))
            nextBelow.push_back(c->file);
      }
    }
    below = std::move(nextBelow);
  }

  size_t bytes = 0;
  constexpr size_t kMaxBytes = 8u << 20;
  for (const auto& [key, file] : candidates) {
    std::vector<std::string> lines;
    const Document* doc = nullptr;
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto it = openDocs_.find(pathToUri(file));
      if (it != openDocs_.end()) doc = it->second;
    }
    if (doc) {
      lines.reserve(static_cast<size_t>(doc->buffer().lineCount()));
      for (int i = 0; i < doc->buffer().lineCount(); ++i) lines.push_back(doc->buffer().line(i));
    } else {
      std::error_code ec;
      const auto sz = fs::file_size(file, ec);
      if (ec || bytes + sz > kMaxBytes) {
        res.limited = true;
        continue;
      }
      std::string raw;
      if (!readRegularFile(file, raw)) continue;
      bytes += raw.size();
      lines = splitTextLines(decodeFileBytes(raw).text);
    }
    ++res.filesScanned;
    const auto cov = covered.find(key);
    forEachIdentifier(lines, oldName, [&](int line, int col) {
      if (cov != covered.end() && cov->second.count({line, col})) return;
      ++res.total;
      if (res.missed.size() < maxReport) res.missed.push_back({file, {line, col}});
    });
  }
  if (candidates.size() >= maxFiles) res.limited = true;
  return res;
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

std::vector<SymbolLocation> SemanticEngine::approximateDefinitions(const Document& doc, Position pos,
                                                                   FastIndex::Want want) const {
  auto word = doc.buffer().wordAt(pos);
  std::vector<SymbolLocation> out;
  if (word.text.empty()) return out;
  // Qualified context: "A::B::name" to the left of the word narrows the scope.
  std::string qualifier;
  {
    const std::string& line = doc.buffer().line(doc.buffer().clamp(pos).line);
    int i = word.range.start.column;
    if (i > 0 && line[static_cast<size_t>(i) - 1] == '~') --i;  // Name::~Name
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
  // What the word is: `~Name` is the destructor, `operator<op>` / `operator T` an operator function, a name after
  // struct/class/union/enum a tag. (An operator token such as the `+` in `a + b` is not a word; it has no fast answer.)
  const std::string& text = doc.buffer().line(doc.buffer().clamp(pos).line);
  const size_t wStart = static_cast<size_t>(word.range.start.column), wEnd = static_cast<size_t>(word.range.end.column);
  FastIndex::Shape shape;
  shape.want = want;
  size_t b = wStart;  // start of what precedes the word, blanks and /* comments */ skipped
  for (;;) {
    while (b > 0 && (text[b - 1] == ' ' || text[b - 1] == '\t')) --b;
    if (b < 2 || text[b - 1] != '/' || text[b - 2] != '*') break;
    const size_t open = text.rfind("/*", b - 2);
    if (open == std::string::npos) break;
    b = open;
  }
  std::string lookupName = word.text;
  std::vector<FastIndex::Match> matches;
  if (word.text == "operator") {
    std::string op = operatorFunctionName(std::string_view(text).substr(std::min(wEnd, text.size())));
    if (!op.empty()) lookupName = op;
    matches = fast_.lookup(lookupName);
  } else {
    if (b > 0 && text[b - 1] == '~') matches = fast_.lookup("~" + word.text);  // a destructor, not the class
    if (matches.empty()) {
      matches = fast_.lookup(word.text);
      size_t e = b;
      while (e > 0 && (std::isalnum(static_cast<unsigned char>(text[e - 1])) || text[e - 1] == '_')) --e;
      const std::string_view prev = std::string_view(text).substr(e, b - e);
      shape.afterTagKeyword = prev == "struct" || prev == "class" || prev == "union" || prev == "enum";
      // The caret on the name of a constructor (declaration or definition) means constructors, not the class.
      for (const FastIndex::Match& m : matches) {
        const Symbol& s = m.sym();
        if (s.kind == SymbolKind::Constructor && s.line == word.range.start.line && s.col == word.range.start.column &&
            m.file->file == doc.path()) {
          shape.constructorOnly = true;
          shape.constructorScope = s.scope;
          shape.constructorArity = s.arity;
          break;
        }
      }
    }
  }
  std::vector<FastIndex::Match> kept;
  for (auto& m : matches) {
    if (!qualifier.empty()) {
      const std::string& sc = m.sym().scope;
      bool ok = sc == qualifier || sc.ends_with("::" + qualifier);
      if (!ok) continue;
    }
    // A macro defined further down the same file cannot be what an earlier line means (a macro acts from its
    // #define on): `int x = late; #define late 1` names a variable or nothing, not the macro.
    if (m.sym().kind == SymbolKind::Macro && m.file->file == doc.path() && m.sym().line > word.range.start.line)
      continue;
    // A function-like macro is only a macro where a '(' follows its name.
    if (m.sym().kind == SymbolKind::Macro && m.sym().arity >= 0) {
      size_t a = wEnd;
      while (a < text.size() && (text[a] == ' ' || text[a] == '\t')) ++a;
      if (a >= text.size() || text[a] != '(') continue;
    }
    kept.push_back(std::move(m));
  }
  for (auto& m : FastIndex::shapeAnswer(std::move(kept), shape)) out.push_back(fromFast(m));
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

  auto c = queryClient();
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

struct SemanticEngine::ReqGuard {
  std::atomic<bool> done{false};
  std::atomic<int> id{-1};
};

void SemanticEngine::armRequestTimeout(std::shared_ptr<ReqGuard> g, std::shared_ptr<LspClient> c, std::string what,
                                       std::function<void(const std::string&)> fail) {
  const int ms = opt_.requestTimeoutMs;
  if (ms <= 0) return;
  runLater(ms, [g, c, what = std::move(what), ms, fail = std::move(fail)] {
    if (g->done.exchange(true)) return;
    c->cancel(g->id.load());
    fail("clangd did not answer " + what + " within " +
         std::to_string(ms / 1000.0).substr(0, std::to_string(ms / 1000.0).find('.') + 2) +
         " s; the request was cancelled");
  });
}

void SemanticEngine::preferDefinitions(std::vector<SymbolLocation>& locs) const {
  std::vector<SymbolLocation> out;
  for (auto& l : locs) {
    bool replaced = false;
    if (auto fsyms = fast_.fileSymbols(l.loc.file)) {
      for (const Symbol& s : fsyms->symbols) {
        if (s.line != l.loc.pos.line || s.definition) continue;
        if (s.kind != SymbolKind::Function && s.kind != SymbolKind::Method) continue;
        for (auto& m : fast_.lookup(s.name)) {
          const Symbol& d = m.sym();
          if (!d.definition || d.scope != s.scope) continue;
          if (d.kind != SymbolKind::Function && d.kind != SymbolKind::Method) continue;
          out.push_back(fromFast(m));
          out.back().confidence = l.confidence;
          replaced = true;
        }
        break;
      }
    }
    if (!replaced) out.push_back(std::move(l));
  }
  std::vector<SymbolLocation> uniq;
  for (auto& l : out) {
    bool dup = false;
    for (auto& u : uniq) dup = dup || (u.loc.file == l.loc.file && u.loc.pos == l.loc.pos);
    if (!dup) uniq.push_back(std::move(l));
  }
  locs = std::move(uniq);
}

void SemanticEngine::locationQuery(
    const char* what, const Document& doc, Position pos, LocationsCallback cb,
    std::function<int(LspClient&, const std::string&, Position, LspClient::Callback<std::vector<LspLocation>>)> issue) {
  const std::string_view w = what;
  std::vector<SymbolLocation> approx = approximateDefinitions(doc, pos,
                                                              w == "definition"    ? FastIndex::Want::Definition
                                                              : w == "declaration" ? FastIndex::Want::Declaration
                                                                                   : FastIndex::Want::Any);
  locationQueryWith(std::move(approx), what, doc, pos, std::move(cb), std::move(issue));
}

void SemanticEngine::locationQueryWith(
    std::vector<SymbolLocation> approx, const char* what, const Document& doc, Position pos, LocationsCallback cb,
    std::function<int(LspClient&, const std::string&, Position, LspClient::Callback<std::vector<LspLocation>>)> issue) {
  std::string uri = doc.uri();
  Position sp = toServer(doc, pos);
  const std::string what_ = what;
  if (unsent(uri)) {  // the document is not known to clangd: only the fast tier can answer
    return deliverWithBudget(std::move(approx), std::move(cb), [](auto done) {
      done(std::vector<SymbolLocation>{}, std::string("clangd has no translation-unit context for this file"));
    });
  }
  deliverWithBudget(std::move(approx), std::move(cb), [this, uri, sp, issue, what_](auto done) {
    auto c = clangd();
    if (!c) return done(std::vector<SymbolLocation>{}, std::string("clangd not running"));
    auto g = std::make_shared<ReqGuard>();
    g->id = issue(*c, uri, sp, [this, done, g](std::vector<LspLocation> ls, const std::string& err) {
      if (g->done.exchange(true)) return;
      done(fromServer(ls, Confidence::Authoritative), err);
    });
    armRequestTimeout(g, c, what_, [done](const std::string& m) { done(std::vector<SymbolLocation>{}, m); });
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

// ---------------------------------------------------------------------------
// Go to definition with several targets (W10-CHOOSE)
// ---------------------------------------------------------------------------
//
// clangd answers the one entity the caret names. Through a using-declaration or an alias, and on a constructor call,
// the user wants a choice, so the authoritative answer is assembled from a few more questions to clangd (all asked
// about the caret's own document, which clangd has open):
//   * using-declaration: clangd already answers the declaration and the entity; they are labelled and ordered.
//   * alias (typedef / using X = / alias template): typeDefinition at the caret names what the alias stands for; the
//     chain is followed hop by hop (clangd when the alias' document is open, else the lexical index) up to the real
//     entity, so the intermediate aliases are listed too.
//   * constructor call (`Widget(2)`, `Widget{2}`, `Widget w(2)`, `new Widget(2)`, `: Base(0)`): definition at the
//     '(' or '{' of the call is the constructor clangd resolved overload resolution to. An inherited constructor
//     comes back as the using-declaration; the real one is found by the call's signature (textDocument/ast).

struct SemanticEngine::DefQuery {
  std::shared_ptr<LspClient> c;
  std::shared_ptr<ReqGuard> g;
  std::function<void(std::vector<SymbolLocation>, std::string)> done;
  Location caret;
  std::string uri, word;
  Position sp;                                 // caret, server coordinates
  Position wordStartSp;                        // start of the word, server coordinates
  int constructCol = -1;                       // '(' / '{' that makes the word a construction (-1: none)
  Position constructSp;                        // the same, server coordinates
  std::set<std::pair<std::string, int>> seen;  // alias chain cycle guard
};

namespace {
bool sameSpotLoc(const Location& a, const Location& b) { return a.file == b.file && a.pos.line == b.pos.line; }

const char* kindWord(SymbolKind k) {
  switch (k) {
    case SymbolKind::Namespace: return "namespace";
    case SymbolKind::Class: return "class";
    case SymbolKind::Struct: return "struct";
    case SymbolKind::Union: return "union";
    case SymbolKind::Enum: return "enum";
    case SymbolKind::Enumerator: return "enumerator";
    case SymbolKind::Function: return "function";
    case SymbolKind::Method: return "method";
    case SymbolKind::Constructor: return "constructor";
    case SymbolKind::Variable: return "variable";
    case SymbolKind::Field: return "field";
    case SymbolKind::Typedef: return "typedef";
    case SymbolKind::Macro: return "macro";
    case SymbolKind::Concept: return "concept";
    default: return "symbol";
  }
}

std::string wordAtCol(const std::string& line, int col) {
  if (col < 0 || static_cast<size_t>(col) >= line.size()) return {};
  size_t b = static_cast<size_t>(col), e = b;
  auto isId = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '~'; };
  while (e < line.size() && isId(line[e])) ++e;
  return line.substr(b, e - b);
}

std::string clip(std::string s, size_t n = 48) {
  if (s.size() > n) s = s.substr(0, n - 3) + "...";
  return s;
}

std::string trimmed(std::string s) {
  const auto b = s.find_first_not_of(" \t\r");
  if (b == std::string::npos) return {};
  const auto e = s.find_last_not_of(" \t\r");
  return s.substr(b, e - b + 1);
}

// First node of a textDocument/ast tree (pre-order) that is a constructor call, with its printed arcana.
std::string constructArcana(const Json& n) {
  if (!n.is_object()) return {};
  const std::string kind = n.value("kind", "");
  if (kind.rfind("CXXConstruct", 0) == 0 || kind.rfind("CXXTemporaryObject", 0) == 0) return n.value("arcana", "");
  if (n.contains("children") && n["children"].is_array())
    for (const Json& ch : n["children"]) {
      std::string a = constructArcana(ch);
      if (!a.empty()) return a;
    }
  return {};
}
}  // namespace

namespace {
// Is the name at `col` of `text` declared by a using-declaration (`using ns::f;`, also after other declarations on the
// line, as in `struct D : B { using B::B; };`)?
bool usingDeclarationAt(const std::string& text, int col) {
  const size_t at = static_cast<size_t>(std::max(col, 0));
  size_t u = text.rfind("using", at);
  while (u != std::string::npos) {
    const bool wordStart = u == 0 || !(std::isalnum(static_cast<unsigned char>(text[u - 1])) || text[u - 1] == '_');
    if (wordStart && defchoice::aliasForm(text.substr(u)) == defchoice::AliasForm::UsingDecl) {
      const size_t semi = text.find(';', u);
      return semi == std::string::npos || semi >= at;
    }
    if (u == 0) break;
    u = text.rfind("using", u - 1);
  }
  return false;
}
}  // namespace

std::string SemanticEngine::lineText(const fs::path& file, int line) const {
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = openDocs_.find(pathToUri(file));
    if (it != openDocs_.end()) {
      const TextBuffer& b = it->second->buffer();
      return line >= 0 && line < b.lineCount() ? defchoice::blankComments(b.line(line)) : std::string();
    }
  }
  return defchoice::blankComments(readLineOfFile(file, line, diskEncoding(file) == Encoding::Utf8));
}

std::optional<Symbol> SemanticEngine::fastSymbolAt(const Location& loc) const {
  auto fsyms = fast_.fileSymbols(loc.file);
  if (!fsyms) return std::nullopt;
  const Symbol* best = nullptr;
  for (const Symbol& s : fsyms->symbols) {
    if (s.line != loc.pos.line) continue;
    if (s.col == loc.pos.column) return s;
    if (!best) best = &s;
  }
  if (best) return *best;
  return std::nullopt;
}

bool SemanticEngine::openInClangd(const fs::path& file) const {
  const std::string uri = pathToUri(file);
  std::lock_guard<std::mutex> lk(mu_);
  return openDocs_.count(uri) != 0 && unsent_.count(uri) == 0;
}

Position SemanticEngine::serverPosition(const fs::path& file, Position p) const {
  auto c = client();
  if (!c) return p;
  const bool utf16 = c->offsetEncoding() != "utf-8";
  const Document* doc = nullptr;
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = openDocs_.find(pathToUri(file));
    if (it != openDocs_.end()) doc = it->second;
  }
  if (doc) return toServer(*doc, p);
  if (auto ds = diskSeparators(file)) return ds->map->toServer(p, ds->text.lines(), utf16);
  if (!utf16) return p;
  return {p.line, TextBuffer::utf8ToUtf16Column(lineText(file, p.line), p.column)};
}

// "class Widget", "method Widget::size()", "function ns::f(int)": kind and qualified name of what is declared at `loc`.
std::string SemanticEngine::describeTarget(const Location& loc) const {
  const std::string text = lineText(loc.file, loc.pos.line);
  std::optional<Symbol> s = fastSymbolAt(loc);
  std::string kind, scope, name = wordAtCol(text, loc.pos.column);
  if (s) {
    kind = kindWord(s->kind);
    scope = s->scope;
    if (s->kind == SymbolKind::Method) {  // `int ns::f(int)` defined out of line is a function of a namespace
      const size_t cut = scope.rfind("::");
      const std::string owner = cut == std::string::npos ? scope : scope.substr(cut + 2);
      bool isClass = scope.empty();
      for (auto& m : fast_.lookup(owner)) {
        const SymbolKind k = m.sym().kind;
        isClass = isClass || k == SymbolKind::Class || k == SymbolKind::Struct || k == SymbolKind::Union;
      }
      if (!isClass) kind = "function";
    }
    if (s->kind == SymbolKind::Typedef)
      kind = defchoice::aliasForm(text) == defchoice::AliasForm::Typedef ? "typedef" : "alias";
    if (name.empty() || s->col != loc.pos.column) name = s->name;
  } else {
    // Not in the lexical index (declared by a macro, ...): judge by what precedes the name.
    const std::string before = loc.pos.column > 0 ? text.substr(0, static_cast<size_t>(loc.pos.column)) : "";
    auto endsWith = [&](const char* w) {
      const std::string t = trimmed(before);
      const size_t n = std::strlen(w);
      return t.size() >= n && t.compare(t.size() - n, n, w) == 0 &&
             (t.size() == n ||
              !(std::isalnum(static_cast<unsigned char>(t[t.size() - n - 1])) || t[t.size() - n - 1] == '_'));
    };
    if (endsWith("class")) kind = "class";
    else if (endsWith("struct")) kind = "struct";
    else if (endsWith("union")) kind = "union";
    else if (endsWith("enum")) kind = "enum";
    else if (defchoice::aliasForm(text) == defchoice::AliasForm::UsingDecl) kind = "using-declaration";
    else {
      const size_t after = static_cast<size_t>(loc.pos.column) + name.size();
      kind = after < text.size() && text[after] == '(' ? "function" : "symbol";
    }
  }
  const bool fn = kind == "function" || kind == "method" || kind == "constructor";
  std::string shown = fn ? defchoice::signatureText(text, loc.pos.column) : name;
  if (shown.empty()) shown = name;
  if (!scope.empty() && kind != "constructor") shown = scope + "::" + shown;
  return kind + " " + shown;
}

// A constructor declaration whose out-of-line definition the lexical index knows: the definition (same class, same
// parameter types). Go to definition goes to the definition, whatever clangd's index state.
void SemanticEngine::preferConstructorDefinition(SymbolLocation& ctor) const {
  std::optional<Symbol> s = fastSymbolAt(ctor.loc);
  if (!s || s->kind != SymbolKind::Constructor || s->definition) return;
  const std::string declText = lineText(ctor.loc.file, ctor.loc.pos.line);
  bool okDecl = false;
  const std::vector<std::string> want =
      defchoice::paramTypes(declText, s->col + static_cast<int>(s->name.size()), &okDecl);
  if (!okDecl) return;
  std::vector<FastIndex::Match> hits;
  for (auto& m : fast_.lookup(s->name)) {
    const Symbol& d = m.sym();
    if (d.kind != SymbolKind::Constructor || !d.definition || d.scope != s->scope || d.arity != s->arity) continue;
    bool ok = false;
    const std::vector<std::string> got =
        defchoice::paramTypes(lineText(m.file->file, d.line), d.col + static_cast<int>(d.name.size()), &ok);
    if (ok && got == want) hits.push_back(m);
  }
  if (hits.size() != 1) return;
  SymbolLocation r = fromFast(hits[0]);
  r.confidence = ctor.confidence;
  r.label = ctor.label;
  r.preferred = ctor.preferred;
  ctor = std::move(r);
}

void SemanticEngine::gotoDefinition(const Document& doc, Position pos, LocationsCallback cb) {
  if (gotoInclude(doc, pos, cb)) return;
  const auto word = doc.buffer().wordAt(pos);
  LocationsCallback wrapped = [this, w = word.text, cb = std::move(cb)](Confidence c, std::vector<SymbolLocation> locs,
                                                                        const std::string& err) {
    if (c == Confidence::Authoritative && err.empty()) augmentWithFastDefinitions(w, locs);
    cb(c, std::move(locs), err);
  };
  std::vector<SymbolLocation> approx = approximateDefinitions(doc, pos, FastIndex::Want::Definition);
  auto q = std::make_shared<DefQuery>();
  q->caret = {doc.path(), pos};
  q->uri = doc.uri();
  q->word = word.text;
  q->sp = toServer(doc, pos);
  if (!word.text.empty()) {
    const std::string text = defchoice::blankComments(doc.buffer().line(doc.buffer().clamp(pos).line));
    q->constructCol = defchoice::constructPoint(text, word.range.end.column);
    q->wordStartSp = toServer(doc, word.range.start);
    if (q->constructCol >= 0) q->constructSp = toServer(doc, Position{word.range.end.line, q->constructCol});
  }
  // The fast tier cannot tell an alias or a constructor call from a plain type use: a single answer that might become
  // a choice is held back by the Editor until clangd has answered (or failed).
  if (approx.size() == 1) {
    std::optional<Symbol> s = fastSymbolAt(approx[0].loc);
    if (s && (s->kind == SymbolKind::Typedef ||
              (q->constructCol >= 0 &&
               (s->kind == SymbolKind::Class || s->kind == SymbolKind::Struct || s->kind == SymbolKind::Union))))
      approx[0].refinable = true;
  }
  if (unsent(q->uri)) {  // the document is not known to clangd: only the fast tier can answer
    return deliverWithBudget(std::move(approx), std::move(wrapped), [](auto done) {
      done(std::vector<SymbolLocation>{}, std::string("clangd has no translation-unit context for this file"));
    });
  }
  deliverWithBudget(std::move(approx), std::move(wrapped), [this, q](auto done) {
    auto c = clangd();
    if (!c) return done(std::vector<SymbolLocation>{}, std::string("clangd not running"));
    q->c = c;
    q->g = std::make_shared<ReqGuard>();
    q->done = done;
    q->g->id = c->definition(q->uri, q->sp, [this, q](std::vector<LspLocation> ls, const std::string& err) {
      if (q->g->done.load()) return;
      if (!err.empty()) {
        if (!q->g->done.exchange(true)) q->done({}, err);
        return;
      }
      refineDefinition(q, fromServer(ls, Confidence::Authoritative));
    });
    armRequestTimeout(q->g, c, "definition", [done](const std::string& m) { done(std::vector<SymbolLocation>{}, m); });
  });
}

void SemanticEngine::refineDefinition(std::shared_ptr<DefQuery> q, std::vector<SymbolLocation> locs) {
  auto finish = [q](std::vector<SymbolLocation> r) {
    if (!q->g->done.exchange(true)) q->done(std::move(r), {});
  };
  if (locs.empty()) return finish(std::move(locs));
  for (const SymbolLocation& l : locs)
    if (sameSpotLoc(l.loc, q->caret)) return finish(std::move(locs));  // the caret is on a declaration itself
  // Through a using-declaration clangd answers the declaration and the entity: label them, entity last and preferred.
  std::vector<SymbolLocation> usings, others;
  for (SymbolLocation& l : locs) {
    const std::string text = lineText(l.loc.file, l.loc.pos.line);
    (usingDeclarationAt(text, l.loc.pos.column) ? usings : others).push_back(std::move(l));
  }
  if (!usings.empty()) {
    std::vector<SymbolLocation> out;
    for (SymbolLocation& u : usings) {
      u.label = "using-declaration";
      out.push_back(std::move(u));
    }
    for (SymbolLocation& o : others) {
      o.label = describeTarget(o.loc);
      out.push_back(std::move(o));
    }
    if (!others.empty()) out[usings.size()].preferred = true;
    return finish(std::move(out));
  }
  locs = std::move(others);
  if (locs.size() == 1) {
    const std::string text = lineText(locs[0].loc.file, locs[0].loc.pos.line);
    const defchoice::AliasForm form = defchoice::aliasForm(text);
    if (form == defchoice::AliasForm::Typedef || form == defchoice::AliasForm::Using)
      return aliasChoices(q, std::move(locs[0]));
    if (q->constructCol >= 0 && classLike(locs[0])) return constructorChoices(q, std::move(locs[0]));
  }
  finish(std::move(locs));
}

bool SemanticEngine::classLike(const SymbolLocation& l) const {
  std::optional<Symbol> s = fastSymbolAt(l.loc);
  if (s) return s->kind == SymbolKind::Class || s->kind == SymbolKind::Struct || s->kind == SymbolKind::Union;
  const std::string d = describeTarget(l.loc);
  return d.rfind("class ", 0) == 0 || d.rfind("struct ", 0) == 0 || d.rfind("union ", 0) == 0;
}

// ---- aliases ----

void SemanticEngine::aliasChoices(std::shared_ptr<DefQuery> q, SymbolLocation alias) {
  struct Walk {
    std::vector<SymbolLocation> chain;  // aliases, nearest to the caret first
    std::optional<SymbolLocation> entity;
    int hops = 0;
  };
  auto w = std::make_shared<Walk>();
  q->seen.insert({pathKeyUtf8(alias.loc.file), alias.loc.pos.line});
  w->chain.push_back(std::move(alias));

  auto finish = [this, q, w] {
    std::vector<SymbolLocation> out = std::move(w->chain);
    std::vector<std::string> names;
    for (const SymbolLocation& a : out)
      names.push_back(wordAtCol(lineText(a.loc.file, a.loc.pos.line), a.loc.pos.column));
    std::string entityName;
    if (w->entity) {
      w->entity->label = describeTarget(w->entity->loc);
      const std::string t = lineText(w->entity->loc.file, w->entity->loc.pos.line);
      entityName = wordAtCol(t, w->entity->loc.pos.column);
    }
    for (size_t i = 0; i < out.size(); ++i) {
      const std::string text = lineText(out[i].loc.file, out[i].loc.pos.line);
      const bool isTypedef = defchoice::aliasForm(text) == defchoice::AliasForm::Typedef;
      std::string target = i + 1 < out.size() ? names[i + 1] : entityName;
      if (target.empty()) {  // names something with no declaration of its own (int, a function pointer, ...)
        const size_t eq = text.find('=');
        const size_t tp = text.find("typedef");
        if (!isTypedef && eq != std::string::npos) target = trimmed(text.substr(eq + 1, text.find(';', eq) - eq - 1));
        else if (isTypedef && tp != std::string::npos && static_cast<size_t>(out[i].loc.pos.column) > tp + 7)
          target = trimmed(text.substr(tp + 7, static_cast<size_t>(out[i].loc.pos.column) - tp - 7));
        target = clip(target, 40);
      }
      out[i].label = std::string(isTypedef ? "typedef " : "alias ") + names[i] + (target.empty() ? "" : " → " + target);
    }
    if (w->entity && q->constructCol >= 0 && classLike(*w->entity))  // `Bar b(1, 2);`: the constructor too
      return constructorChoices(q, std::move(*w->entity), std::move(out));
    if (w->entity) out.push_back(std::move(*w->entity));
    out.back().preferred = true;
    if (!q->g->done.exchange(true)) q->done(std::move(out), {});
  };

  auto isAlias = [this](const SymbolLocation& l) {
    const defchoice::AliasForm f = defchoice::aliasForm(lineText(l.loc.file, l.loc.pos.line));
    return f == defchoice::AliasForm::Typedef || f == defchoice::AliasForm::Using;
  };

  // The hop from the alias `cur` to what it names; `first` = the hop from the caret's own alias, answered by
  // typeDefinition at the caret. The callback gets nothing when the chain cannot be followed.
  auto step = std::make_shared<std::function<void(SymbolLocation, bool)>>();
  // The function refers to itself weakly (a strong self-capture is a cycle that leaks the query and everything its
  // callbacks hold); `next` holds it strongly while a hop is in flight.
  *step = [this, q, w, finish, isAlias, weakStep = std::weak_ptr(step)](SymbolLocation cur, bool first) {
    const auto step = weakStep.lock();
    if (!step || q->g->done.load()) return;
    auto next = [this, q, w, finish, isAlias, step, cur](std::optional<SymbolLocation> n) {
      if (q->g->done.load()) return;
      if (!n || sameSpotLoc(n->loc, cur.loc)) return finish();
      if (!q->seen.insert({pathKeyUtf8(n->loc.file), n->loc.pos.line}).second || ++w->hops > 8) return finish();
      if (isAlias(*n)) {
        w->chain.push_back(*n);
        return (*step)(*n, false);
      }
      w->entity = *n;
      finish();
    };
    auto lexical = [this, cur, next]() {
      const std::string text = lineText(cur.loc.file, cur.loc.pos.line);
      std::optional<defchoice::AliasRhs> rhs = defchoice::aliasRhs(text, cur.loc.pos.column);
      if (!rhs) return next(std::nullopt);
      std::vector<SymbolLocation> hits;
      for (auto& m : fast_.lookup(rhs->name)) {
        const Symbol& s = m.sym();
        if (s.kind == SymbolKind::Macro || s.kind == SymbolKind::Namespace || s.kind == SymbolKind::Variable ||
            s.kind == SymbolKind::Field || s.kind == SymbolKind::Function || s.kind == SymbolKind::Method ||
            s.kind == SymbolKind::Constructor || s.kind == SymbolKind::Enumerator)
          continue;
        if (!s.definition && s.kind != SymbolKind::Typedef) continue;
        const std::string q2 = s.qualified();
        if (!rhs->qualifier.empty() && q2 != rhs->qualifier + "::" + rhs->name &&
            !q2.ends_with("::" + rhs->qualifier + "::" + rhs->name))
          continue;
        hits.push_back(fromFast(m));
      }
      if (hits.size() != 1) return next(std::nullopt);  // unknown or ambiguous by name: do not guess
      next(std::move(hits[0]));
    };
    if (first) {
      q->g->id = q->c->typeDefinition(
          q->uri, q->sp, [this, q, cur, next, lexical](std::vector<LspLocation> ls, const std::string& err) {
            if (q->g->done.load()) return;
            std::vector<SymbolLocation> r =
                err.empty() ? fromServer(ls, Confidence::Authoritative) : std::vector<SymbolLocation>{};
            if (!r.empty() && !sameSpotLoc(r[0].loc, cur.loc)) return next(std::move(r[0]));
            lexical();
          });
      return;
    }
    // Later hops: ask clangd at the aliased name when the alias' document is open there, else the lexical index.
    const std::string text = lineText(cur.loc.file, cur.loc.pos.line);
    std::optional<defchoice::AliasRhs> rhs = defchoice::aliasRhs(text, cur.loc.pos.column);
    if (rhs && openInClangd(cur.loc.file)) {
      q->g->id = q->c->definition(pathToUri(cur.loc.file), serverPosition(cur.loc.file, {cur.loc.pos.line, rhs->col}),
                                  [this, q, next, lexical](std::vector<LspLocation> ls, const std::string& err) {
                                    if (q->g->done.load()) return;
                                    std::vector<SymbolLocation> r = err.empty()
                                                                        ? fromServer(ls, Confidence::Authoritative)
                                                                        : std::vector<SymbolLocation>{};
                                    if (!r.empty()) return next(std::move(r[0]));
                                    lexical();
                                  });
      return;
    }
    lexical();
  };
  (*step)(w->chain[0], true);
}

// ---- constructor calls ----

void SemanticEngine::constructorChoices(std::shared_ptr<DefQuery> q, SymbolLocation cls,
                                        std::vector<SymbolLocation> prefix) {
  q->g->id = q->c->definition(
      q->uri, q->constructSp,
      [this, q, cls, prefix = std::move(prefix)](std::vector<LspLocation> ls, const std::string& err) {
        if (q->g->done.load()) return;
        // The aliases that led to the class come first; with them the entity (the class, or the constructor) is
        // selected.
        auto finish = [q, &prefix](std::vector<SymbolLocation> r, bool selectLast) {
          std::vector<SymbolLocation> out = prefix;
          for (SymbolLocation& l : r) out.push_back(std::move(l));
          if (selectLast || !prefix.empty()) out.back().preferred = true;
          if (!q->g->done.exchange(true)) q->done(std::move(out), {});
        };
        std::vector<SymbolLocation> found =
            err.empty() ? fromServer(ls, Confidence::Authoritative) : std::vector<SymbolLocation>{};
        SymbolLocation klass = cls;
        if (!prefix.empty() || !found.empty()) klass.label = describeTarget(klass.loc);
        if (found.empty()) return finish({klass}, false);  // aggregate initialisation, or nothing to add
        SymbolLocation ctor = found[0];
        const std::string ctorText = lineText(ctor.loc.file, ctor.loc.pos.line);
        const std::string className = wordAtCol(lineText(klass.loc.file, klass.loc.pos.line), klass.loc.pos.column);
        if (usingDeclarationAt(ctorText, ctor.loc.pos.column))
          return inheritedConstructorChoices(q, klass, ctor, prefix);
        if (sameSpotLoc(ctor.loc, klass.loc) && ctor.loc.pos.column == klass.loc.pos.column) {
          // clangd places an implicit constructor at its class
          klass.label = describeTarget(klass.loc) + " (implicit constructor)";
          return finish({klass}, true);
        }
        if (sameSpotLoc(ctor.loc, q->caret))
          return finish({klass}, false);  // a constructor declaration being asked about
        std::optional<Symbol> s = fastSymbolAt(ctor.loc);
        const bool isCtor =
            s ? s->kind == SymbolKind::Constructor : wordAtCol(ctorText, ctor.loc.pos.column) == className;
        if (!isCtor) return finish({klass}, false);
        preferConstructorDefinition(ctor);
        ctor.label = describeTarget(ctor.loc);
        finish({klass, ctor}, true);
      });
}

void SemanticEngine::inheritedConstructorChoices(std::shared_ptr<DefQuery> q, SymbolLocation cls,
                                                 SymbolLocation usingDecl, std::vector<SymbolLocation> prefix) {
  auto finish = [this, q, cls, usingDecl, prefix = std::move(prefix)](std::optional<SymbolLocation> real) mutable {
    std::vector<SymbolLocation> out = std::move(prefix);
    cls.label = describeTarget(cls.loc);
    out.push_back(cls);
    if (real) {
      real->label = describeTarget(real->loc) + " (inherited)";
      real->preferred = true;
      out.push_back(*real);
    } else {
      usingDecl.label = "using-declaration (inherited constructors)";
      usingDecl.preferred = true;
      out.push_back(usingDecl);
    }
    if (!q->g->done.exchange(true)) q->done(std::move(out), {});
  };
  // The signature of the call: the constructor expression clangd's AST has for it.
  Json params = {{"textDocument", {{"uri", q->uri}}},
                 {"range",
                  {{"start", {{"line", q->wordStartSp.line}, {"character", q->wordStartSp.column}}},
                   {"end", {{"line", q->constructSp.line}, {"character", q->constructSp.column + 1}}}}}};
  q->g->id = q->c->rawRequest(
      "textDocument/ast", params, [this, q, usingDecl, finish](Json r, const std::string& err) mutable {
        if (q->g->done.load()) return;
        if (!err.empty()) return finish(std::nullopt);
        bool ok = false;
        const std::vector<std::string> want = defchoice::signatureParamTypes(constructArcana(r), &ok);
        const std::string base = defchoice::usingQualifier(lineText(usingDecl.loc.file, usingDecl.loc.pos.line));
        if (!ok || base.empty()) return finish(std::nullopt);
        const size_t cut = base.rfind("::");
        const std::string baseName = cut == std::string::npos ? base : base.substr(cut + 2);
        std::set<std::string> scopes;  // qualified names of the base class (it can be declared in several namespaces)
        for (auto& m : fast_.lookup(baseName)) {
          const Symbol& s = m.sym();
          if ((s.kind == SymbolKind::Class || s.kind == SymbolKind::Struct) && s.definition)
            scopes.insert(s.qualified());
        }
        if (scopes.size() != 1) return finish(std::nullopt);
        std::vector<FastIndex::Match> hits;
        for (auto& m : fast_.lookup(baseName)) {
          const Symbol& s = m.sym();
          if (s.kind != SymbolKind::Constructor || s.scope != *scopes.begin()) continue;
          bool pok = false;
          const std::vector<std::string> got =
              defchoice::paramTypes(lineText(m.file->file, s.line), s.col + static_cast<int>(s.name.size()), &pok);
          if (pok && got == want) hits.push_back(m);
        }
        if (hits.empty()) return finish(std::nullopt);
        std::stable_sort(hits.begin(), hits.end(), [](const FastIndex::Match& a, const FastIndex::Match& b) {
          return a.sym().definition > b.sym().definition;
        });
        SymbolLocation real = fromFast(hits[0]);
        preferConstructorDefinition(real);
        finish(std::move(real));
      });
}

void SemanticEngine::gotoDeclaration(const Document& doc, Position pos, LocationsCallback cb) {
  if (gotoInclude(doc, pos, cb)) return;
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
  LocationsCallback wrapped = [this, cb = std::move(cb)](Confidence c, std::vector<SymbolLocation> locs,
                                                         const std::string& err) {
    if (c == Confidence::Authoritative && err.empty()) preferDefinitions(locs);
    cb(c, std::move(locs), err);
  };
  locationQuery(
      "implementation", doc, pos, std::move(wrapped),
      [](LspClient& c, const std::string& u, Position p, auto k) { return c.implementation(u, p, std::move(k)); });
}

// ---------------------------------------------------------------------------
// Go To Document on an #include line (VS-NAV, VS_BEHAVIOUR N1)
// ---------------------------------------------------------------------------

incnav::SearchPath SemanticEngine::searchPathFor(const fs::path& file) const {
  const auto snap = compileCommands();
  if (!snap || snap->empty()) return {};
  auto fullPath = [](const CompileCommand& c) {
    return (c.file.is_relative() ? c.directory / c.file : c.file).lexically_normal();
  };
  const std::string key = pathKey(file.lexically_normal());
  const CompileCommand* use = nullptr;
  for (const CompileCommand& c : *snap)
    if (pathKey(fullPath(c)) == key) {
      use = &c;
      break;
    }
  if (!use) {  // an included fragment: the translation unit it was given
    if (auto ctx = tuContext(file))
      for (const CompileCommand& c : *snap)
        if (pathKey(fullPath(c)) == pathKey(ctx->root.lexically_normal())) {
          use = &c;
          break;
        }
  }
  if (use) return incnav::searchPathOf(*use);
  // A file in no translation unit: every directory the database searches (first command first), each once.
  incnav::SearchPath all;
  std::set<std::string> seen;
  for (const CompileCommand& c : *snap) {
    incnav::SearchPath sp = incnav::searchPathOf(c);
    for (auto* v : {&sp.angle}) {
      for (fs::path& d : *v)
        if (seen.insert(pathKey(d)).second) all.angle.push_back(std::move(d));
    }
    if (all.angle.size() > 256) break;
  }
  return all;
}

bool SemanticEngine::gotoInclude(const Document& doc, Position pos, LocationsCallback& cb) {
  const Position at = doc.buffer().clamp(pos);
  const auto ref = incnav::includeAt(doc.buffer().line(at.line), at.column);
  if (!ref) return false;
  auto whole = [](const fs::path& file, Confidence c) {
    SymbolLocation l;
    l.loc = {file, Position{0, 0}};
    l.name = pathUtf8(file.filename());
    l.kind = "file";
    l.confidence = c;
    // The top of the file: the extent is its first line, so "the target is the file" holds whatever token starts it.
    std::ifstream in(file, std::ios::binary);
    std::string first;
    std::getline(in, first);
    if (!first.empty() && first.back() == '\r') first.pop_back();
    l.extent = Range{Position{0, 0}, Position{0, static_cast<int>(first.size())}};
    return l;
  };
  if (auto f = incnav::resolveInclude(*ref, doc.path(), searchPathFor(doc.path()))) {
    cb(Confidence::Authoritative, {whole(*f, Confidence::Authoritative)}, "");
    return true;
  }
  // Not on the include path (a system header clangd knows, or a file the database does not mention): the fast index
  // offers the workspace file whose path ends with the spelling nearest to the includer; clangd answers for the rest.
  std::vector<SymbolLocation> approx;
  std::string spelling = ref->spelling;
  std::replace(spelling.begin(), spelling.end(), '\\', '/');
  const std::string suffix = "/" + spelling;
  const std::string base = pathKeyUtf8(doc.path().parent_path());
  size_t bestShared = 0;
  fs::path best;
  for (const auto& fsyms : fast_.exportFiles()) {
    const std::string g = pathKeyUtf8(fsyms->file);
    if (g.size() < suffix.size() || g.compare(g.size() - suffix.size(), suffix.size(), pathKey(suffix)) != 0) continue;
    size_t shared = 0;
    while (shared < base.size() && shared < g.size() && base[shared] == g[shared]) ++shared;
    if (best.empty() || shared > bestShared || (shared == bestShared && fsyms->file < best)) {
      best = fsyms->file;
      bestShared = shared;
    }
  }
  if (!best.empty()) approx.push_back(whole(best, Confidence::Approximate));
  locationQueryWith(
      std::move(approx), "definition", doc, pos, std::move(cb),
      [](LspClient& c, const std::string& u, Position p, auto k) { return c.definition(u, p, std::move(k)); });
  return true;
}

// ---------------------------------------------------------------------------
// Find All References: roles and group order (VS-NAV, VS_BEHAVIOUR N5)
// ---------------------------------------------------------------------------

void SemanticEngine::classifyReferences(std::vector<SymbolLocation>& locs) const {
  // Every file is read once (the open document's text, else the disk), whatever the number of references in it.
  std::map<std::string, std::vector<std::string>> cache;
  auto linesOf = [&](const fs::path& file) -> const std::vector<std::string>& {
    auto [it, fresh] = cache.try_emplace(pathKeyUtf8(file));
    if (!fresh) return it->second;
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto d = openDocs_.find(pathToUri(file));
      if (d != openDocs_.end()) {
        const TextBuffer& b = d->second->buffer();
        for (int i = 0; i < b.lineCount(); ++i) it->second.push_back(b.line(i));
        return it->second;
      }
    }
    std::error_code ec;
    std::string bytes;
    if (fs::file_size(file, ec) > (8u << 20) || !readRegularFile(file, bytes)) return it->second;
    if (diskEncoding(file) == Encoding::Utf8) {  // the editor's lines (NEL / LS / PS end one too)
      it->second = splitTextLines(bytes);
    } else {  // a single-byte file: lines end at "\r\n", "\n", '\r' only
      size_t i = 0;
      while (i <= bytes.size()) {
        size_t e = i;
        while (e < bytes.size() && bytes[e] != '\n' && bytes[e] != '\r') ++e;
        it->second.push_back(bytes.substr(i, e - i));
        if (e >= bytes.size()) break;
        i = e + ((bytes[e] == '\r' && e + 1 < bytes.size() && bytes[e + 1] == '\n') ? 2 : 1);
      }
    }
    return it->second;
  };
  for (SymbolLocation& l : locs) {
    const std::vector<std::string>& lines = linesOf(l.loc.file);
    const std::string text = l.loc.pos.line >= 0 && static_cast<size_t>(l.loc.pos.line) < lines.size()
                                 ? lines[static_cast<size_t>(l.loc.pos.line)]
                                 : std::string();
    const int col = l.loc.pos.column;
    size_t len = 0;
    while (static_cast<size_t>(col) + len < text.size() &&
           (std::isalnum(static_cast<unsigned char>(text[static_cast<size_t>(col) + len])) ||
            text[static_cast<size_t>(col) + len] == '_'))
      ++len;
    const std::string word = col >= 0 && static_cast<size_t>(col) <= text.size()
                                 ? text.substr(static_cast<size_t>(col), len)
                                 : std::string();
    bool decl = false;
    if (auto fsyms = fast_.fileSymbols(l.loc.file))
      for (const Symbol& s : fsyms->symbols)
        if (s.line == l.loc.pos.line && s.name == word) {
          decl = true;
          break;
        }
    l.role = decl ? "Definition" : incnav::roleName(incnav::classifyReference(text, col, static_cast<int>(len)));
  }
  std::stable_sort(locs.begin(), locs.end(), [](const SymbolLocation& a, const SymbolLocation& b) {
    const bool da = a.role == "Definition", db = b.role == "Definition";
    if (da != db) return da;
    const std::string fa = pathKeyUtf8(a.loc.file), fb = pathKeyUtf8(b.loc.file);
    if (fa != fb) return fa < fb;
    return a.loc.pos < b.loc.pos;
  });
}

void SemanticEngine::findReferences(const Document& doc, Position pos, LocationsCallback cb) {
  // No approximate tier for references (a text search would be misleading); go straight to clangd.
  auto c = queryClient();
  if (!c) return cb(Confidence::Approximate, {}, "clangd not running");
  if (unsent(doc.uri()))
    return cb(Confidence::Authoritative, {}, "clangd has no translation-unit context for this file");
  auto g = std::make_shared<ReqGuard>();
  g->id = c->references(doc.uri(), toServer(doc, pos), true,
                        [this, cb, g](std::vector<LspLocation> ls, const std::string& err) {
                          if (g->done.exchange(true)) return;
                          std::vector<SymbolLocation> out = fromServer(ls, Confidence::Authoritative);
                          if (err.empty()) classifyReferences(out);
                          cb(Confidence::Authoritative, std::move(out), err);
                        });
  armRequestTimeout(g, c, "find references", [cb](const std::string& m) { cb(Confidence::Authoritative, {}, m); });
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
      const std::string rootPrefix = pathKeyUtf8(ws_.root()) + "/";
      std::stable_partition(out.begin(), out.end(),
                            [&](const SymbolLocation& l) { return pathKeyUtf8(l.loc.file).rfind(rootPrefix, 0) == 0; });
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
        x.extent = Range{posFromServer(uri, s.range.start), posFromServer(uri, s.range.end)};
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
    c->switchSourceHeader(doc.uri(), [this, cb](std::string uri, const std::string& err) {
      cb(uri.empty() ? fs::path() : pathFromUri(uri), err);
    });
    return;
  }
  // Fallback: same stem, sibling extension, anywhere in the workspace.
  std::string stem = pathUtf8(doc.path().stem());
  bool isHeader = ws_.isHeader(doc.path());
  for (auto& f : isHeader ? ws_.sourceFiles() : ws_.headerFiles()) {
    if (pathUtf8(f.stem()) == stem) return cb(f, {});
  }
  cb({}, "no counterpart found");
}

// ---------------------------------------------------------------------------
// Information / editing
// ---------------------------------------------------------------------------

void SemanticEngine::hover(const Document& doc, Position pos, Callback<std::string> cb) {
  if (unsent(doc.uri())) return cb("", "clangd has no translation-unit context for this file");
  if (auto c = queryClient()) {
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
  auto c = queryClient();
  if (!c) return cb({}, "clangd not running");
  if (unsent(doc.uri())) return cb({}, "clangd has no translation-unit context for this file");
  auto g = std::make_shared<ReqGuard>();
  g->id = c->rename(doc.uri(), toServer(doc, pos), newName, [this, cb, g](Json edit, const std::string& err) {
    if (g->done.exchange(true)) return;
    if (!err.empty()) return cb({}, err);
    cb(editsFromServer(edit), {});
  });
  armRequestTimeout(g, c, "rename", [cb](const std::string& m) { cb({}, m); });
}

bool SemanticEngine::isMiscFile(const fs::path& file) const {
  const auto snap = compileCommands();
  if (!snap || snap->empty()) return false;  // no database: there is no translation unit to be outside of
  if (!fastDone_) return false;              // the include graph is still being read: do not guess
  const std::string key = pathKey(file.lexically_normal());
  const uint64_t gen = fast_.generation();
  {
    std::lock_guard<std::mutex> lk(miscMu_);
    if (miscSnap_ != snap.get() || miscGen_ != gen) {
      miscCache_.clear();
      miscSnap_ = snap.get();
      miscGen_ = gen;
    }
    auto it = miscCache_.find(key);
    if (it != miscCache_.end()) return it->second;
  }
  bool misc = true;
  for (const CompileCommand& c : *snap) {
    const fs::path f = (c.file.is_relative() ? c.directory / c.file : c.file).lexically_normal();
    if (pathKey(f) == key) {
      misc = false;
      break;
    }
  }
  if (misc && tuContext(file)) misc = false;  // a fragment that was given its includer's context
  if (misc) {
    auto includesOf = [this](const fs::path& f) {
      auto fsyms = fast_.fileSymbols(f);
      return fsyms ? fsyms->includes : std::vector<std::string>();
    };
    misc = compdb::filesReachableFrom(*snap, includesOf, {key}).empty();
  }
  std::lock_guard<std::mutex> lk(miscMu_);
  if (miscSnap_ == snap.get() && miscGen_ == gen) miscCache_[key] = misc;
  return misc;
}

void SemanticEngine::setMiscFileDiagnostics(bool show) {
  if (showMisc_.exchange(show) == show) return;
  std::function<void(const fs::path&, std::vector<Diagnostic>)> h;
  std::vector<std::pair<fs::path, std::vector<Diagnostic>>> held;
  {
    std::lock_guard<std::mutex> lk(mu_);
    h = diagHandler_;
    if (show) {  // what was held back appears; it is not held any more
      for (auto& [k, v] : miscHeld_) held.push_back(std::move(v));
      miscHeld_.clear();
    }
  }
  if (!h || !show) return;  // turning it off waits for the next publish (the file's diagnostics are replaced then)
  for (auto& [file, ds] : held) h(file, std::move(ds));
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
    TextEditList& list = out[pathFromUri(uri)];
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
  if (tuMapped_.load(std::memory_order_acquire)) {
    // The same edit arrives twice when a file and the prefix copy of it (tuContext) both hold the symbol.
    for (auto& [file, list] : out) {
      std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return std::tie(a.first.start.line, a.first.start.column, a.first.end.line, a.first.end.column, a.second) <
               std::tie(b.first.start.line, b.first.start.column, b.first.end.line, b.first.end.column, b.second);
      });
      list.erase(std::unique(list.begin(), list.end(),
                             [](const auto& a, const auto& b) {
                               return a.first.start == b.first.start && a.first.end == b.first.end &&
                                      a.second == b.second;
                             }),
                 list.end());
    }
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
  const auto map = lspMapFor(uri);  // NEL / LS / PS: server lines are editor lines glued together
  if (utf16 || map) {
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
    if (map && doc) {  // a token may span a separator: one span per editor line it covers
      const std::vector<std::string>& lines = doc->buffer().lines();
      const Position a = map->fromServer({line, col}, lines, utf16);
      const Position e = map->fromServer({line, col + t.len}, lines, utf16);
      for (int l = a.line; l <= e.line && l < static_cast<int>(lines.size()); ++l) {
        TokenSpan part = t;
        part.line = l;
        part.col = l == a.line ? a.column : 0;
        part.len = (l == e.line ? e.column : static_cast<int>(lines[static_cast<size_t>(l)].size())) - part.col;
        if (part.len > 0) out.push_back(part);
      }
      continue;
    }
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
    const bool lines = lspMapFor(uri) != nullptr;  // in a document with NEL / LS / PS the line changes too
    for (auto& r : rs) {
      if (!lines) {
        if (r.startColumn >= 0) r.startColumn = posFromServer(uri, {r.startLine, r.startColumn}).column;
        if (r.endColumn >= 0) r.endColumn = posFromServer(uri, {r.endLine, r.endColumn}).column;
        continue;
      }
      const Position s = posFromServer(uri, {r.startLine, std::max(r.startColumn, 0)});
      const Position e = posFromServer(uri, {r.endLine, std::max(r.endColumn, 0)});
      r.startLine = s.line;
      r.endLine = e.line;
      if (r.startColumn >= 0) r.startColumn = s.column;
      if (r.endColumn >= 0) r.endColumn = e.column;
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
  out.loc = {pathFromUri(it.uri), out.selection.start};
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
