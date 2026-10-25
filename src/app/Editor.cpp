#include "app/Editor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace tf {

namespace fs = std::filesystem;

namespace {
// Where position `p` (at or after `endPre`) ends up when the text up to `endPre` was replaced such that `endPre`
// became `endPost` (used to carry carets through LSP-supplied edits in applyEdits).
Position remapAfter(Position p, Position endPre, Position endPost) {
  if (p.line == endPre.line) return {endPost.line, endPost.column + (p.column - endPre.column)};
  return {p.line + (endPost.line - endPre.line), p.column};
}

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
// The pairs the editor closes by itself: parentheses, brackets, braces, double and single quotes.
bool isPair(char open, char close) {
  return (open == '(' && close == ')') || (open == '[' && close == ']') || (open == '{' && close == '}') ||
         (open == '"' && close == '"') || (open == '\'' && close == '\'');
}

// `p` moved forward over the first `bytes` bytes of `text`.
Position advanceBy(Position p, const std::string& text, size_t bytes) {
  for (size_t i = 0; i < bytes && i < text.size(); ++i) {
    if (text[i] == '\n') p = {p.line + 1, 0};
    else ++p.column;
  }
  return p;
}
}  // namespace

Editor::Editor(fs::path root) : Editor(std::move(root), Options()) {}

namespace {
// workspace.json {"vcs": {"provider": "auto|perforce|git|none", "p4Path": "..."}} (or the flat keys "vcs.provider" /
// "vcs.p4Path"). Only fills what the caller left at its default.
void readVcsSettings(const fs::path& file, VcsManager::Options& o) {
  std::ifstream in(file);
  if (!in) return;
  nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
  if (!j.is_object()) return;
  auto str = [&](const char* nested, const char* flat) -> std::string {
    if (j.contains("vcs") && j["vcs"].is_object() && j["vcs"].contains(nested) && j["vcs"][nested].is_string())
      return j["vcs"][nested].get<std::string>();
    if (j.contains(flat) && j[flat].is_string()) return j[flat].get<std::string>();
    return {};
  };
  const std::string prov = str("provider", "vcs.provider");
  if (o.provider == "auto" && (prov == "auto" || prov == "perforce" || prov == "git" || prov == "none"))
    o.provider = prov;
  const std::string p4 = str("p4Path", "vcs.p4Path");
  if (o.p4Path.empty()) o.p4Path = p4;
}
}  // namespace

Editor::Editor(fs::path root, Options opt) : ws_(std::move(root)), opt_(std::move(opt)), nav_(opt_.navigation) {
  // D3: every subsystem's messages also go to the local log file; the injected callbacks keep working.
  const auto userLog = opt_.log;
  opt_.log = Log::tee("editor", userLog);
  opt_.semantic.log = Log::tee("clangd", opt_.semantic.log ? opt_.semantic.log : userLog);
  opt_.vcs.log = Log::tee("vcs", opt_.vcs.log ? opt_.vcs.log : userLog);
  Log::setWorkspace(ws_.root());
  readVcsSettings(ws_.cacheDir() / "workspace.json", opt_.vcs);
  vcs_ = std::make_unique<VcsManager>(*this, opt_.vcs);
  ws_.loadSettings();
  debug_ = std::make_unique<DebugSession>(*this);
}

Editor::~Editor() { shutdown(); }

bool Editor::start(std::string* error) {
  auto t0 = std::chrono::steady_clock::now();
  ws_.scan();
  loadBreakpoints();
  if (opt_.log) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    opt_.log("workspace scan: " + std::to_string(ws_.fileCount()) + " files in " + std::to_string(ms) + " ms");
  }
  semantic_ = std::make_unique<SemanticEngine>(ws_, opt_.semantic);
  semantic_->onDiagnostics([this](const fs::path& file, std::vector<Diagnostic> ds) {
    post([this, file, ds = std::move(ds)]() mutable { diagnostics_[file.generic_string()] = std::move(ds); });
  });
  semantic_->onApplyEdit([this](const std::string&, FileEdits edits) {
    post([this, edits = std::move(edits)] {
      std::string err;
      if (!applyEdits(edits, &err) && opt_.log) opt_.log("workspace/applyEdit failed: " + err);
    });
  });
  // Automatic clangd restarts re-send the open documents from the pump thread (they are owned by it).
  semantic_->setPoster([this](std::function<void()> fn) {
    post([this, fn = std::move(fn)] {
      if (semantic_) fn();
    });
  });
  semantic_->onClangdHealth([this](const SemanticEngine::ClangdHealth& h) {
    post([this, text = h.text()] {
      if (text == clangdStatus_) return;
      clangdStatus_ = text;
      if (opt_.onClangdStatus) opt_.onClangdStatus(text);
    });
  });
  hookTokens();
  if (ws_.settings().watchFiles) startWatching();
  bool ok = semantic_->start(error);
  // Debugger (C2b2): attach to a running Visual Studio that has the solution open; quiet when there is none, and
  // nothing at all on a platform without a debugger backend.
  if (ws_.settings().debugAttachAtStartup && debug_->hasBackend()) debug_->attachIfRunning();
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
  debug_->shutdown();          // first: it may wait (bounded) for a blocked backend call
  if (vcs_) vcs_->shutdown();  // joins its worker, which posts to this editor
  saveBookmarks();
  saveBreakpoints();
  buildRunner_.reset();  // kills a running build and joins its reader before the queue goes away
  buildRunning_ = false;
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
  if (!bookmarksLoaded_) {  // first document: bring the persisted bookmarks in (a missing file is fine)
    bookmarksLoaded_ = true;
    bookmarks_.load(ws_.cacheDir() / "bookmarks.json", ws_.root());
  }
  loadBreakpoints();
  auto doc = std::make_unique<Document>(path);
  if (!doc->loadFromDisk(error)) return nullptr;
  Document* raw = doc.get();
  raw->addListener([this](Document& d, const Document::Change& ch) {
    int newLines = 1 + static_cast<int>(std::count(ch.newText.begin(), ch.newText.end(), '\n'));
    nav_.onLinesReplaced(d.path(), ch.removed.start.line, ch.removed.end.line, newLines);
    bookmarks_.onLinesReplaced(d.path(), ch.removed.start.line, ch.removed.end.line, newLines);
    breakpoints_.onLinesReplaced(d.path(), ch.removed.start.line, ch.removed.end.line, newLines);
    if (lastEdit_.file == d.path()) {  // same shifting rule as NavigationHistory::onLinesReplaced
      int& ln = lastEdit_.pos.line;
      if (ln > ch.removed.end.line) {
        ln += newLines - (ch.removed.end.line - ch.removed.start.line + 1);
      } else if (ln > ch.removed.start.line) {
        ln = ch.removed.start.line;
        lastEdit_.pos.column = 0;
      }
    }
    if (semantic_) semantic_->didChange(d, ch);
    if (!reloading_) vcs_->documentChanged(d);
    if (current_ == &d && !inEdit_) clampCarets();
  });
  std::string key = docKey(path);
  docs_[key] = std::move(doc);
  diskHash_[key] = hashFile(path);
  if (semantic_) semantic_->didOpen(*raw);
  vcs_->documentOpened(*raw);
  return raw;
}

bool Editor::close(const fs::path& path) {
  Document* d = document(path);
  if (!d) return false;
  if (semantic_) semantic_->didClose(*d);
  vcs_->documentClosed(*d);
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
  if (editing_.trimTrailingWhitespace) {
    std::vector<Document::BatchEdit> trims;  // one undo step; the carets of the current document are carried along
    const TextBuffer& b = doc.buffer();
    for (int l = 0; l < b.lineCount(); ++l) {
      const std::string& s = b.line(l);
      size_t e = s.size();
      while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
      if (e < s.size()) trims.push_back({{{l, static_cast<int>(e)}, {l, static_cast<int>(s.size())}}, {}});
    }
    if (!trims.empty()) {
      if (&doc == current_) {
        runLineEdits(std::move(trims));
      } else {
        doc.beginGroup();
        doc.replaceMany(std::move(trims));
        doc.endGroup();
      }
    }
  }
  {
    // A read-only file (Perforce: not checked out, e.g. auto-checkout failed) must say so instead of "cannot write".
    std::error_code ec;
    if (fs::is_regular_file(doc.path(), ec) &&
        (fs::status(doc.path(), ec).permissions() & fs::perms::owner_write) == fs::perms::none) {
      if (error)
        *error = doc.path().filename().string() + " is read-only" +
                 (vcs_ && vcs_->active() && vcs_->providerName() == "perforce"
                      ? " (not checked out in Perforce; run p4 edit, or revert the edit)"
                      : " (clear the read-only attribute to save)");
      return false;
    }
  }
  if (!doc.saveToDisk(error)) return false;
  saveBookmarks();
  saveBreakpoints();
  diskHash_[docKey(doc.path())] = hashFile(doc.path());
  if (semantic_) semantic_->didSave(doc);
  vcs_->documentSaved(doc);
  return true;
}

bool Editor::reloadFromDisk(Document& doc, std::string* error) {
  struct ReloadScope {
    bool& flag;
    explicit ReloadScope(bool& f) : flag(f) { flag = true; }
    ~ReloadScope() { flag = false; }
  } reloadScope(reloading_);
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
  vcs_->documentReloaded(doc);
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

namespace {
// Files whose change invalidates the compile commands (a Visual Studio project edit included).
bool isBuildInputFile(const std::string& name) {
  if (name == "compile_commands.json" || name == "CMakeLists.txt" || name == "Directory.Build.props" ||
      name == "Directory.Build.targets")
    return true;
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos) return false;
  std::string ext = name.substr(dot);
  for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".sln" || ext == ".vcxproj" || ext == ".props" || ext == ".targets";
}
}  // namespace

void Editor::refreshBuildSystem(bool forceRestart, std::function<void()> done) {
  if (!semantic_) return;
  if (done) buildRefreshDone_.push_back(std::move(done));
  if (forceRestart) buildRefreshForce_ = true;
  if (buildRefreshRunning_) {
    buildRefreshAgain_ = true;  // the running refresh may have read the old settings: run once more afterwards
    return;
  }
  buildRefreshRunning_ = true;
  const bool force = std::exchange(buildRefreshForce_, false);
  auto dones = std::make_shared<std::vector<std::function<void()>>>(std::move(buildRefreshDone_));
  buildRefreshDone_.clear();
  semantic_->refreshCompileCommands(ws_.sourceFiles(), ws_.headerFiles(), [this, force, dones](bool moved) {
    post([this, force, dones, moved] {
      buildRefreshRunning_ = false;
      if (!semantic_) return;
      if (moved || force) {
        std::string err;
        if (!semantic_->restartClangd(&err) && opt_.log && !err.empty()) opt_.log("clangd restart failed: " + err);
      }
      for (auto& d : *dones) d();
      if (buildRefreshAgain_) {
        buildRefreshAgain_ = false;
        refreshBuildSystem();
      }
    });
  });
}

bool Editor::selectVsConfiguration(const std::string& name, std::string* error, std::function<void()> applied) {
  if (!ws_.setVsConfiguration(name, error)) return false;
  if (!semantic_) {
    if (applied) post(std::move(applied));
    return true;
  }
  refreshBuildSystem(true, std::move(applied));
  return true;
}

namespace {

std::string quoteArgForDisplay(const std::string& a) {
  if (!a.empty() && a.find_first_of(" \t\"'") == std::string::npos) return a;
  std::string o = "\"";
  for (char c : a) {
    if (c == '"') o += '\\';
    o += c;
  }
  return o + "\"";
}

// "label: value" where the value may continue on indented lines; `width` > 0 wraps at that many columns.
void putField(std::ostringstream& os, const std::string& label, const std::string& value, int width) {
  const std::string head = "  " + label + std::string(label.size() < 16 ? 16 - label.size() : 1, ' ');
  std::string line = head;
  size_t i = 0;
  bool first = true;
  while (i < value.size()) {
    size_t e = value.find(' ', i);
    if (e == std::string::npos) e = value.size();
    const std::string word = value.substr(i, e - i);
    if (!first && width > 0 && line.size() + 1 + word.size() > static_cast<size_t>(width)) {
      os << line << "\n";
      line = std::string(head.size(), ' ');
      first = true;
    }
    if (!first) line += ' ';
    line += word;
    first = false;
    i = e + 1;
  }
  os << line << "\n";
}

// A list of arguments (already quoted for display) under a heading, one wrapped paragraph indented by four.
void putArgs(std::ostringstream& os, const std::string& heading, const std::vector<std::string>& args, int width) {
  os << "  " << heading << "\n";
  std::string line = "    ";
  bool first = true;
  for (const auto& a : args) {
    const std::string q = quoteArgForDisplay(a);
    if (!first && width > 0 && line.size() + 1 + q.size() > static_cast<size_t>(width)) {
      os << line << "\n";
      line = "    ";
      first = true;
    }
    if (!first) line += ' ';
    line += q;
    first = false;
  }
  os << line << "\n";
}

// The "- <json string>" items of the generated clangd config (CompileFlags.Add).
std::vector<std::string> readClangdConfigFlags(const fs::path& config) {
  std::vector<std::string> out;
  std::ifstream in(config);
  std::string line;
  bool inAdd = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == "  Add:") {
      inAdd = true;
      continue;
    }
    if (!inAdd) continue;
    if (line.rfind("    - ", 0) != 0) break;
    auto j = Json::parse(line.substr(6), nullptr, false);
    if (j.is_string()) out.push_back(j.get<std::string>());
  }
  return out;
}

}  // namespace

Editor::CompileCommandInfo Editor::compileCommandInfo(const fs::path& fileIn) {
  CompileCommandInfo info;
  info.file = fileIn.is_absolute() ? fileIn : ws_.root() / fileIn;
  info.fallbackFlags = {"-std=" + ws_.settings().cppStandard, "-I" + ws_.root().string()};
  const auto slns = ws_.solutionFiles();
  if (!slns.empty()) {
    info.solution = slns.front();
    info.vsConfiguration = ws_.activeVsConfiguration().name();
  }
  info.engineStarted = semantic_ != nullptr;
  if (!semantic_) {
    info.source = "none";
    info.sourceText = "the engine was not started, so no compile database was prepared";
    info.confidence = "approximate";
    info.confidenceText = "clangd is not running (engine not started)";
    return info;
  }
  const auto st = semantic_->status();
  info.source = st.compdbSource.empty() ? "none" : st.compdbSource;
  info.databasePath = st.compdbPath;
  info.databaseEntries = st.compdbEntries;
  info.clangdExe = st.clangdExe;
  info.clangdSource = st.clangdSource;
  info.clangdVersion = st.clangdVersion;
  info.clangdRunning = st.clangdInitialized;
  info.clangdStatus = semantic_->clangdStatusText();
  info.vsConfigurationUsed = info.source == "vcxproj";
  const std::string slnName = info.solution.filename().string();
  std::error_code relEc;
  const std::string db =
      info.databasePath.empty() ? std::string() : fs::relative(info.databasePath, ws_.root(), relEc).generic_string();
  if (info.source == "existing")
    info.sourceText = "an existing compile_commands.json found in the workspace (" + db +
                      "); tenfold does not change it, and it takes precedence over the solution and CMake";
  else if (info.source == "cmake") info.sourceText = "generated by configuring CMake into .tenfold/cmake-build";
  else if (info.source == "vcxproj")
    info.sourceText = "generated by tenfold from the Visual Studio solution " + slnName + " (clang-cl commands, " +
                      "configuration " + info.vsConfiguration + ")";
  else if (info.source == "heuristic")
    info.sourceText = "a heuristic guess (-std=" + ws_.settings().cppStandard +
                      " and include directories inferred from the #include lines); no project information was found";
  else if (info.source == "generated(cached)")
    info.sourceText = "a database tenfold generated in an earlier session and cached (" + db +
                      "): a heuristic guess, since no solution or build-system database applied then; delete the file "
                      "to regenerate it";
  else info.sourceText = "no compile database; clangd only has the fallback flags";

  const std::string key = pathKey(fs::weakly_canonical(info.file));
  auto cmds = semantic_->compileCommands();
  for (const auto& c : *cmds) {
    if (pathKey(fs::weakly_canonical(c.file)) != key) continue;
    info.listed = true;
    info.directory = c.directory;
    info.argv = c.arguments;
    break;
  }
  info.sharedFlags = readClangdConfigFlags(ws_.cacheDir() / "clangd-config" / "clangd" / "config.yaml");
#ifdef _WIN32
  const bool msvc = info.source == "vcxproj" || info.source == "heuristic";
#else
  const bool msvc = info.source == "vcxproj";
#endif
  if (msvc) info.msvcToolchain = ws_.settings().msvcToolchain;

  info.open = document(info.file) != nullptr;
  info.ready = info.open && semantic_->fileReady(info.file);
  if (!info.clangdRunning) {
    info.confidence = "approximate";
    info.confidenceText = "clangd is not running" +
                          (info.clangdStatus.empty() ? std::string() : " (" + info.clangdStatus + ")") +
                          ", so only the fast index answers";
  } else if (!info.open) {
    info.confidence = "not open";
    info.confidenceText = "the file is not open in the editor, so clangd has not been given it; open it to get " +
                          std::string("authoritative answers for it");
  } else if (!info.ready) {
    info.confidence = "approximate";
    info.confidenceText = "clangd is still parsing this file; answers are approximate until it finishes";
  } else {
    info.confidence = "authoritative";
    info.confidenceText = "clangd has parsed this file; answers are authoritative" +
                          std::string(info.listed ? "" : " (but with guessed flags, see above)");
  }
  return info;
}

std::string Editor::CompileCommandInfo::text(int width) const {
  std::ostringstream os;
  putField(os, "File:", file.generic_string(), 0);
  putField(os, "Compile flags:", sourceText, width);
  if (!databasePath.empty())
    putField(os, "Database:", databasePath.generic_string() + " (" + std::to_string(databaseEntries) + " entries)",
             width);
  if (!solution.empty())
    putField(os, "Visual Studio:",
             solution.filename().string() + ", active configuration " + vsConfiguration +
                 (vsConfigurationUsed ? " (the database was generated for it)"
                                      : " (not used: the compile flags do not come from the solution)"),
             width);
  if (!engineStarted) {
    putField(os, "In database:", "unknown", 0);
  } else if (listed) {
    putField(os, "In database:", "yes", 0);
    putField(os, "Directory:", directory.generic_string(), width);
    putArgs(os, "Command (argv):", argv, width);
  } else {
    putField(os, "In database:",
             "no. clangd then borrows the command of a similar entry, which it chooses itself and tenfold cannot "
             "show, or falls back to the flags below",
             width);
  }
  if (!sharedFlags.empty())
    putArgs(os, "Added to every file by tenfold's clangd config (.tenfold/clangd-config):", sharedFlags, width);
  putArgs(os, "Fallback flags (files the database does not cover):", fallbackFlags, width);
  if (!msvcToolchain.empty())
    putField(os, "MSVC toolchain:", msvcToolchain + " (cl.exe for clangd --query-driver)", width);
  else if (source == "vcxproj")
    putField(os, "MSVC toolchain:", "cl.exe not found; set msvcToolchain in .tenfold/workspace.json", width);
  if (!clangdExe.empty()) putField(os, "clangd:", clangdExe + " (" + clangdSource + ")", width);
  if (!clangdVersion.empty()) putField(os, "clangd version:", clangdVersion, width);
  putField(os, "clangd state:", clangdRunning ? "running" : (clangdStatus.empty() ? "not running" : clangdStatus),
           width);
  putField(os, "This file:",
           !open            ? "not open in the editor"
           : !clangdRunning ? "open (clangd is not running)"
           : ready          ? "open, parsed by clangd"
                            : "open, clangd is still parsing",
           width);
  putField(os, "Confidence:", confidence + ": " + confidenceText, width);
  return os.str();
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
  vcs_->fileEvent(ev.path);
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
      semantic_->watchedFileChanged(ev.path, ev.kind == E::Created ? 1 : 2);  // skipped for open documents
      break;
    case E::Deleted: {
      // A file, or (Windows reports no directory flag) a directory: drop everything at or below it.
      std::vector<fs::path> gone = ws_.filesUnder(ev.path);
      if (gone.empty() && !ev.isDirectory) semantic_->watchedFileChanged(ev.path, 3);
      for (auto& f : gone) {
        ws_.noteFileRemoved(f);
        semantic_->fileRemoved(f);
        semantic_->watchedFileChanged(f, 3);
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
          bookmarks_.onFileRenamed(f, to);
          breakpoints_.onFileRenamed(f, to);
          semantic_->fileRemoved(f);
          semantic_->watchedFileChanged(f, 3);
          semantic_->watchedFileChanged(to, 1);
          moved.push_back(to);
        }
        semantic_->indexFilesAsync(std::move(moved));
      } else {
        ws_.noteFileRenamed(ev.oldPath, ev.path);
        nav_.onFileRenamed(ev.oldPath, ev.path);
        bookmarks_.onFileRenamed(ev.oldPath, ev.path);
        breakpoints_.onFileRenamed(ev.oldPath, ev.path);
        semantic_->watchedFileChanged(ev.oldPath, 3);
        semantic_->watchedFileChanged(ev.path, 1);
        touched(ev.path);  // editors save by renaming a temp file over the target
        if (isBuildInputFile(ev.oldPath.filename().string())) refreshBuildSystem();
      }
      break;
  }
  const std::string name = buildFile.filename().string();
  if (!ev.isDirectory && isBuildInputFile(name)) refreshBuildSystem();
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

bool Editor::goToHistoryEntry(size_t index) {
  const size_t n = nav_.entries().size();
  if (index >= n) return false;
  const auto done = [&] {
    return nav_.cursor() == index || (index + 1 == nav_.entries().size() && nav_.cursor() >= nav_.entries().size());
  };
  if (nav_.cursor() >= n && index + 1 == n) {  // at the live position, asking for the newest entry
    const Location& e = nav_.entries().back().loc;
    if (e.file == cursor_.file && std::abs(e.pos.line - cursor_.pos.line) < opt_.navigation.coalesceLines) return true;
  }
  for (size_t guard = 0; guard < 2 * n + 4 && !done(); ++guard) {
    const bool live = nav_.cursor() >= nav_.entries().size();
    if (live || nav_.cursor() > index ? !back() : !forward()) return false;
  }
  return done();
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

void Editor::hoverAt(Document& doc, Position pos, std::function<void(const std::string&, const std::string&)> done) {
  semantic_->hover(doc, pos, [this, done](std::string text, const std::string& err) {
    post([text = std::move(text), err, done] {
      if (done) done(text, err);
    });
  });
}

void Editor::signatureHelp(std::function<void(const LspSignatureHelp&, const std::string&)> done) {
  if (!current_) {
    if (done) done({}, "no document");
    return;
  }
  semantic_->signatureHelp(*current_, cursor_.pos, [this, done](LspSignatureHelp h, const std::string& err) {
    post([h = std::move(h), err, done] {
      if (done) done(h, err);
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
  std::vector<Document::BatchEdit> batch(n);
  std::vector<std::string> placed(n);  // texts of edits that position their caret inside the inserted text
  for (size_t k = 0; k < n; ++k) {
    CaretEdit& ce = edits[order[k]];
    if (ce.headOffset != std::string::npos || ce.anchorOffset != std::string::npos) placed[k] = ce.text;
    batch[k] = {ce.range, std::move(ce.text)};
  }
  inEdit_ = true;
  const std::vector<Range> after = doc.replaceMany(std::move(batch));
  inEdit_ = false;
  for (size_t k = 0; k < n; ++k) {
    const CaretEdit& ce = edits[order[k]];
    if (ce.headOffset == std::string::npos && ce.anchorOffset == std::string::npos) {
      carets_[order[k]] = caretAt(after[k].end);
      continue;
    }
    const Position head =
        ce.headOffset == std::string::npos ? after[k].end : advanceBy(after[k].start, placed[k], ce.headOffset);
    const Position anchor =
        ce.anchorOffset == std::string::npos ? head : advanceBy(after[k].start, placed[k], ce.anchorOffset);
    carets_[order[k]] = {anchor, head};
  }
  wants_.assign(n, -1);
  normalizeCarets();
  doc.endGroup(carets_);
  noteEdit();
}

void Editor::insertText(const std::string& text) {
  if (!current_ || carets_.empty()) return;
  const bool anySelection = std::any_of(carets_.begin(), carets_.end(), [](const Selection& s) { return !s.empty(); });
  if (text.empty() && !anySelection) return;
  std::vector<CaretEdit> edits;
  edits.reserve(carets_.size());
  for (const Selection& s : carets_) edits.push_back({s.range(), text});
  typeEdits(std::move(edits), text);
}

// One edit per caret; `typed` is the keystroke they stem from. A single typed code point is coalesced with the
// preceding typing into one undo step according to Workspace::Settings::undoGrouping / undoGroupPauseMs.
void Editor::typeEdits(std::vector<CaretEdit> edits, const std::string& typed) {
  const bool anySelection = std::any_of(carets_.begin(), carets_.end(), [](const Selection& s) { return !s.empty(); });
  const Workspace::Settings& st = ws_.settings();
  const bool coalesce = isSingleCodepoint(typed) && st.undoGrouping != "char";
  const int64_t now = nowMs();
  const bool join = coalesce && !anySelection && typingOpen_ && typingDoc_ == current_ &&
                    typingVersion_ == current_->version() && typingCarets_ == carets_ &&
                    now - typingTime_ <= st.undoGroupPauseMs && current_->canUndo();
  applyCaretEdits(std::move(edits), join);
  if (coalesce) {
    typingOpen_ = !(typed.size() == 1 && isBlank(typed[0]));  // whitespace joins the step, then closes it
    typingDoc_ = current_;
    typingVersion_ = current_->version();
    typingTime_ = now;
    typingCarets_ = carets_;
  } else {
    breakTyping();
  }
}

void Editor::insertTextPerCaret(const std::vector<std::string>& texts) {
  if (!current_ || carets_.empty() || texts.size() != carets_.size()) return;
  breakTyping();
  std::vector<size_t> order(carets_.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return carets_[a].range() < carets_[b].range(); });
  std::vector<CaretEdit> edits(carets_.size());
  for (size_t r = 0; r < order.size(); ++r) edits[order[r]] = {carets_[order[r]].range(), texts[r]};
  applyCaretEdits(std::move(edits), false);
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
      // Between an empty auto-closed pair ("(|)", "\"|\""): delete both characters.
      const std::string& l = b.line(p.line);
      if (editing_.autoClosePairs && p.column > 0 && p.column < static_cast<int>(l.size()) &&
          isPair(l[static_cast<size_t>(p.column) - 1], l[static_cast<size_t>(p.column)]))
        r.end = {p.line, p.column + 1};
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
  noteEdit();
}

void Editor::deleteRange(Range r) { replaceRange(r, {}); }

bool Editor::undo() {
  if (!current_) return false;
  breakTyping();
  inEdit_ = true;
  const bool ok = current_->undo();
  inEdit_ = false;
  if (ok) {
    restoreCaretsAfterHistory();
    noteEdit();
  }
  return ok;
}

bool Editor::redo() {
  if (!current_) return false;
  breakTyping();
  inEdit_ = true;
  const bool ok = current_->redo();
  inEdit_ = false;
  if (ok) {
    restoreCaretsAfterHistory();
    noteEdit();
  }
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

bool Editor::restartClangd(std::string* error) {
  if (!semantic_) {
    if (error) *error = "not started";
    return false;
  }
  return semantic_->restartClangd(error);
}

bool Editor::toggleBreakpoint() {
  if (!current_) return false;
  const bool now = breakpoints_.toggle(current_->path(), carets_[0].head.line);
  saveBreakpoints();
  return now;
}

bool Editor::openInVisualStudio() {
  if (!current_) return false;
  return debug_->openInEditor(current_->path(), cursor_.pos.line, cursor_.pos.column);
}

void Editor::loadBreakpoints() {
  if (breakpointsLoaded_) return;
  breakpointsLoaded_ = true;
  breakpoints_.load(ws_.cacheDir() / "breakpoints.json", ws_.root());
}

void Editor::saveBreakpoints() {
  if (breakpoints_.dirty()) breakpoints_.save(ws_.cacheDir() / "breakpoints.json", ws_.root());
}

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
// Build
// ---------------------------------------------------------------------------

struct Editor::BuildSession {
  explicit BuildSession(std::string cwd) : parser(std::move(cwd)) {}
  ErrorParser parser;
};

bool Editor::build(const BuildOverrides& overrides, BuildLines onLines, BuildDone onDone, std::string* error) {
  BuildCommand cmd;
  if (!detectBuildCommand(ws_, overrides, &cmd, error)) return false;
  if (!buildRunner_)
    buildRunner_ = std::make_unique<BuildRunner>([this](std::function<void()> fn) { post(std::move(fn)); });
  auto session = std::make_shared<BuildSession>(cmd.cwd.generic_string());
  auto lines = [this, session, onLines = std::move(onLines)](std::vector<std::string> ls) {
    if (session != buildSession_) return;  // output of a build that was replaced
    BuildMessage m;
    for (auto& l : ls)  // -fdiagnostics-color / -fdiagnostics-urls output carries CSI / OSC sequences
      if (session->parser.parseLine(stripAnsiEscapes(l), &m)) addBuildMessage(std::move(m));
    if (onLines) onLines(ls);
  };
  auto done = [this, session, onDone = std::move(onDone)](const BuildResult& r) {
    BuildResult res = r;
    if (session == buildSession_) {
      for (auto& m : buildMessages_) {
        if (m.severity == BuildSeverity::Error) ++res.errors;
        else if (m.severity == BuildSeverity::Warning) ++res.warnings;
      }
      buildRunning_ = false;
      lastBuild_ = res;
    }
    if (opt_.log)
      opt_.log("build: finished, exit code " + std::to_string(res.exitCode) + ", " + std::to_string(res.errors) +
               " error(s)");
    if (onDone) onDone(res);
  };
  // The previous build's queued output is dropped by the session check; its done callback still runs.
  buildSession_ = session;
  buildMessages_.clear();
  buildDiags_.clear();
  buildCursor_ = -1;
  buildRunning_ = true;
  if (!buildRunner_->start(cmd, std::move(lines), std::move(done), error)) {
    buildRunning_ = false;
    return false;
  }
  if (opt_.log) opt_.log("build: " + cmd.display);
  return true;
}

void Editor::cancelBuild() {
  if (buildRunner_) buildRunner_->cancel();
}

void Editor::addBuildMessage(BuildMessage m) {
  if (m.hasLocation()) {
    Diagnostic d;
    d.file = fs::path(m.file);
    d.severity = m.severity == BuildSeverity::Error ? 1 : m.severity == BuildSeverity::Warning ? 2 : 3;
    d.message = m.text;
    d.code = m.code;
    const int line = m.line - 1;
    if (m.col > 0) {
      int c = m.col - 1, e = c + 1;
      if (Document* doc = document(d.file); doc && line < doc->buffer().lineCount()) {
        const std::string& text = doc->buffer().line(line);
        auto ident = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; };
        e = c;
        while (e < static_cast<int>(text.size()) && ident(text[static_cast<size_t>(e)])) ++e;
        if (e == c) e = c + 1;
      }
      d.range = {{line, c}, {line, e}};
    } else {
      d.range = {{line, 0}, {line + 1, 0}};
    }
    buildDiags_[docKey(d.file)].push_back(std::move(d));
  }
  buildMessages_.push_back(std::move(m));
}

const std::vector<Diagnostic>& Editor::buildDiagnostics(const fs::path& file) const {
  static const std::vector<Diagnostic> empty;
  auto it = buildDiags_.find(docKey(file));
  return it == buildDiags_.end() ? empty : it->second;
}

void Editor::setBuildCursor(size_t messageIndex) {
  if (messageIndex < buildMessages_.size()) buildCursor_ = static_cast<int>(messageIndex);
}

std::optional<BuildMessage> Editor::stepBuildMessage(int dir, std::string* error) {
  const int n = static_cast<int>(buildMessages_.size());
  int i = buildCursor_;
  for (int k = 0; k < n; ++k) {
    i = i < 0 ? (dir > 0 ? 0 : n - 1) : ((i + dir) % n + n) % n;
    const BuildMessage& m = buildMessages_[static_cast<size_t>(i)];
    if (!m.hasLocation() || m.severity == BuildSeverity::Note) continue;
    buildCursor_ = i;
    const std::string label = (m.severity == BuildSeverity::Error ? "error: " : "warning: ") + m.text;
    if (gotoLocation({fs::path(m.file), {m.line - 1, std::max(m.col - 1, 0)}}, error, label)) return m;
  }
  return std::nullopt;
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

// ---------------------------------------------------------------------------
// clangd features
// ---------------------------------------------------------------------------

void Editor::setDiagnostics(const fs::path& file, std::vector<Diagnostic> ds) {
  diagnostics_[file.generic_string()] = std::move(ds);
}

void Editor::hookTokens() {
  if (!semantic_ || !tokenHandler_) return;
  semantic_->onSemanticTokens([this](const fs::path& file, int version, std::vector<TokenSpan> spans) {
    post([this, file, version, spans = std::move(spans)] {
      Document* d = document(file);
      if (d && d->version() == version && tokenHandler_) tokenHandler_(*d, spans);
    });
  });
}

void Editor::onSemanticTokens(TokenHandler h) {
  tokenHandler_ = std::move(h);
  hookTokens();
}

namespace {
// Reports "no document" through the usual post() route.
template <class T, class F>
void noDocument(F&& post, std::function<void(const T&, const std::string&)> done) {
  post([done = std::move(done)] {
    if (done) done(T{}, "no document");
  });
}
}  // namespace

#define TF_NEED_DOC(T, done)                                                      \
  if (!current_ || !semantic_) {                                                  \
    noDocument<T>([this](std::function<void()> f) { post(std::move(f)); }, done); \
    return;                                                                       \
  }

void Editor::semanticTokens(Done<std::vector<TokenSpan>> done) {
  TF_NEED_DOC(std::vector<TokenSpan>, done)
  semantic_->semanticTokens(*current_, posted(std::move(done)));
}

void Editor::inlayHints(Range range, Done<std::vector<LspInlayHint>> done) {
  TF_NEED_DOC(std::vector<LspInlayHint>, done)
  semantic_->inlayHints(*current_, range, posted(std::move(done)));
}

void Editor::foldingRanges(Done<std::vector<LspFoldingRange>> done) {
  TF_NEED_DOC(std::vector<LspFoldingRange>, done)
  semantic_->foldingRanges(*current_, posted(std::move(done)));
}

void Editor::documentHighlights(Done<std::vector<LspHighlight>> done) {
  TF_NEED_DOC(std::vector<LspHighlight>, done)
  semantic_->documentHighlights(*current_, cursor_.pos, posted(std::move(done)));
}

void Editor::callHierarchy(Done<std::vector<HierarchyItem>> done) {
  TF_NEED_DOC(std::vector<HierarchyItem>, done)
  semantic_->prepareCallHierarchy(*current_, cursor_.pos, posted(std::move(done)));
}

void Editor::typeHierarchy(Done<std::vector<HierarchyItem>> done) {
  TF_NEED_DOC(std::vector<HierarchyItem>, done)
  semantic_->prepareTypeHierarchy(*current_, cursor_.pos, posted(std::move(done)));
}

void Editor::incomingCalls(const HierarchyItem& item, Done<std::vector<CallEdge>> done) {
  semantic_->incomingCalls(item, posted(std::move(done)));
}
void Editor::outgoingCalls(const HierarchyItem& item, Done<std::vector<CallEdge>> done) {
  semantic_->outgoingCalls(item, posted(std::move(done)));
}
void Editor::supertypes(const HierarchyItem& item, Done<std::vector<HierarchyItem>> done) {
  semantic_->supertypes(item, posted(std::move(done)));
}
void Editor::subtypes(const HierarchyItem& item, Done<std::vector<HierarchyItem>> done) {
  semantic_->subtypes(item, posted(std::move(done)));
}

bool Editor::gotoHierarchyItem(const HierarchyItem& item, std::string* error) {
  return gotoLocation(item.loc, error, item.name);
}

void Editor::codeActions(Range range, Done<std::vector<CodeAction>> done) {
  TF_NEED_DOC(std::vector<CodeAction>, done)
  semantic_->codeActions(*current_, range, posted(std::move(done)));
}

void Editor::applyCodeAction(const CodeAction& action, std::function<void(bool, const std::string&)> done) {
  if (action.hasEdit) {
    std::string err;
    if (!applyEdits(action.edit, &err)) {
      if (done) post([done, err] { done(false, err); });
      return;
    }
  }
  if (!action.hasCommand || !semantic_) {
    if (done) post([done] { done(true, {}); });
    return;
  }
  // clangd answers the command after the applyEdit requests it sent, which were posted before this reply.
  semantic_->executeCommand(action.command, action.arguments, [this, done](Json, const std::string& err) {
    post([done, err] {
      if (done) done(err.empty(), err);
    });
  });
}

bool Editor::applyEdits(const FileEdits& edits, std::string* error) {
  std::vector<std::pair<Document*, const TextEditList*>> work;
  for (auto& [file, list] : edits) {
    if (list.empty()) continue;
    Document* d = open(file, error);
    if (!d) return false;
    work.push_back({d, &list});
  }
  breakTyping();
  for (auto& [d, list] : work) {
    std::vector<size_t> order(list->size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return (*list)[a].first.start < (*list)[b].first.start; });
    const bool cur = d == current_;
    d->beginGroup(cur ? carets_ : std::vector<Selection>{});
    inEdit_ = true;
    for (size_t k = order.size(); k-- > 0;) {  // bottom-to-top: earlier ranges stay valid
      const auto& [range, text] = (*list)[order[k]];
      const TextBuffer::EditResult res = d->replace(range, text);
      if (!cur) continue;
      auto remap = [&](Position p) {
        if (p < res.removed.start) return p;
        if (p < res.removed.end) return res.inserted.end;
        return remapAfter(p, res.removed.end, res.inserted.end);
      };
      for (Selection& s : carets_) {
        s.anchor = remap(s.anchor);
        s.head = remap(s.head);
      }
    }
    inEdit_ = false;
    if (cur) {
      clampCarets();
      noteEdit();
    }
    d->endGroup(cur ? carets_ : std::vector<Selection>{});
  }
  return true;
}

void Editor::applyFormatting(Done<size_t> done, const TextEditList& edits, const std::string& err, const fs::path& file,
                             int version) {
  if (!done) return;
  Document* d = document(file);
  if (!err.empty()) return done(0, err);
  if (!d || d->version() != version) return done(0, "document changed while formatting");
  FileEdits fe;
  fe[d->path()] = edits;
  std::string e;
  if (!applyEdits(fe, &e)) return done(0, e);
  done(edits.size(), {});
}

void Editor::formatDocument(Done<size_t> done) {
  TF_NEED_DOC(size_t, done)
  fs::path file = current_->path();
  int version = current_->version();
  semantic_->formatDocument(*current_, [this, file, version, done](TextEditList edits, const std::string& err) {
    post([this, file, version, done, edits = std::move(edits), err] {
      applyFormatting(done, edits, err, file, version);
    });
  });
}

void Editor::formatRange(Range range, Done<size_t> done) {
  TF_NEED_DOC(size_t, done)
  fs::path file = current_->path();
  int version = current_->version();
  semantic_->formatRange(*current_, range, [this, file, version, done](TextEditList edits, const std::string& err) {
    post([this, file, version, done, edits = std::move(edits), err] {
      applyFormatting(done, edits, err, file, version);
    });
  });
}

#undef TF_NEED_DOC

}  // namespace tf
