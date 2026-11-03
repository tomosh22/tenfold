#include "corpus/Runner.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include "core/JsonUtil.h"
#include "core/TextBuffer.h"
#include "core/TextEncoding.h"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#include <unistd.h>
#endif
#if defined(_WIN32)
#include <process.h>
#endif

#include <nlohmann/json.hpp>

#include "app/Editor.h"
#include "core/Log.h"
#include "core/PathUtil.h"

namespace tf::corpus {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

namespace {

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

double peakRss() {
#if defined(__linux__)
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line))
    if (line.rfind("VmHWM:", 0) == 0) return std::stod(line.substr(6)) / 1024.0;  // kB
  return -1;
#elif defined(__unix__) || defined(__APPLE__)
  struct rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) != 0) return -1;
#if defined(__APPLE__)
  return static_cast<double>(ru.ru_maxrss) / (1024.0 * 1024.0);
#else
  return static_cast<double>(ru.ru_maxrss) / 1024.0;
#endif
#else
  return -1;
#endif
}

std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool writeFile(const fs::path& p, const std::string& text) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << text;
  return static_cast<bool>(out);
}

long processId() {
#if defined(_WIN32)
  return _getpid();
#else
  return static_cast<long>(getpid());
#endif
}

// Lines end at "\r\n", "\n" or a lone '\r', like TextBuffer's and clang's (the BOM stays in line 1).
std::vector<std::string> splitLines(const std::string& text) { return TextBuffer(text).lines(); }

// The identifier at an anchor (the '~' of a destructor name is not part of it; a unary '~' is the token itself).
std::string tokenAt(const std::vector<std::string>& lines, const Anchor& a) {
  if (a.line < 0 || static_cast<size_t>(a.line) >= lines.size()) return {};
  const std::string& l = lines[static_cast<size_t>(a.line)];
  size_t i = static_cast<size_t>(a.col + a.skip);
  size_t b = i;
  while (i < l.size() && (std::isalnum(static_cast<unsigned char>(l[i])) || l[i] == '_' || (l[i] & 0x80))) ++i;
  return i > b ? l.substr(b, i - b) : std::string(1, i < l.size() ? l[i] : ' ');
}

// "\n" and friends in a printed line.
std::string oneLine(std::string s) {
  for (char& c : s)
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  return s;
}

class Runner {
 public:
  explicit Runner(const RunOptions& o) : o_(o) {}

  Report run();

 private:
  struct Answer {
    bool got = false;
    Confidence conf = Confidence::Approximate;
    std::vector<SymbolLocation> locs;
    std::string text;  // hover
    std::string err;
    double ms = -1;
  };
  struct Pending {  // a fast probe whose final answer is still to be compared
    int line;
    Kind kind;
    std::string anchor;
    std::vector<Loc> fast;
  };

  void emit(const std::string& s) {
    if (o_.out) o_.out(s);
    else std::cout << s << "\n" << std::flush;
  }
  int remainingMs() const { return static_cast<int>(std::max<double>(0, deadlineMs_ - msSince(t0_))); }
  int queryTimeout() const { return std::min(30000, remainingMs()); }

  fs::path abs(const std::string& rel) const { return root_ / rel; }
  Loc toLoc(const SymbolLocation& s) const;
  std::vector<Loc> toLocs(const std::vector<SymbolLocation>& v) const {
    std::vector<Loc> out;
    for (auto& s : v) out.push_back(toLoc(s));
    return out;
  }

  bool setup();
  void copyTree(const fs::path& from, const fs::path& to);
  void finish();

  void record(ItemResult r);
  void runItem(const Item& it);
  void failed(const Item& it, const std::string& what);
  void runCheck(const Item& it);
  void runConfig(const Item& it);
  void runOpen(const Item& it);
  void runClose(const Item& it);
  void runEdit(const Item& it);
  void runRename(const Item& it, ItemResult& r);

  bool moveTo(const std::string& anchor, std::string& err);
  Answer askLocs(const std::function<void(Editor::NavResult)>& issue, bool wantFinal);
  Answer askHover(bool held);
  Answer queryLocs(Kind kind, const Item& it, bool held, const Anchor& a);
  void ensureReady(const std::string& relFile);
  // Before a query that needs clangd's view: false (with `err`) when the anchor's document is not synced to clangd at
  // all (it would never become ready), else waits for the TU of every document earlier lines opened or changed.
  bool awaitDocuments(const std::string& anchorRel, std::string& err);
  bool isSynced(const fs::path& file) const;
  void noteDoc(const fs::path& file) { dirtyDocs_.insert(file); }
  void sweepOpenDocs();
  void flushAgreement();
  void sample(Kind k, bool fast, double ms) {
    if (ms >= 0) rep_.samples[std::string(toString(k)) + (fast ? ".fast" : ".final")].push_back(ms);
  }
  std::string currentText(const fs::path& file);
  void rescanFile(const std::string& rel, const std::string& text);
  void noteOpened(const fs::path& file);

  RunOptions o_;
  Report rep_;
  fs::path root_;
  AnchorMap anchors_;  // live: re-scanned after edits
  std::vector<Item> items_;
  std::unique_ptr<Editor> ed_;
  Clock::time_point t0_ = Clock::now();
  double deadlineMs_ = 0;
  bool haveClangd_ = false;
  bool indexWaitDue_ = true;
  bool indexComplete_ = true;     // clangd's background index finished at the last wait
  std::set<fs::path> dirtyDocs_;  // opened or changed since the last awaitDocuments
  std::set<fs::path> openSeen_;   // documents open at the last sweep
  std::vector<Pending> pending_;
  std::vector<std::string> engineLog_;
  std::mutex logMu_;

  std::thread tuThread_;
  std::atomic<bool> tuStop_{false};
  bool tuStarted_ = false;
  std::atomic<double> firstTuMs_{-1};
};

Loc Runner::toLoc(const SymbolLocation& s) const {
  Loc l;
  l.line = s.loc.pos.line;
  l.col = s.loc.pos.column;
  if (!s.extent.empty()) {
    l.endLine = s.extent.end.line;
    l.endCol = s.extent.end.column;
  }
  const std::string rootG = root_.generic_string() + "/";
  auto rel = [&](const fs::path& p) -> std::string {
    std::string g = p.lexically_normal().generic_string();
    if (g.rfind(rootG, 0) == 0) return g.substr(rootG.size());
    std::error_code ec;
    g = fs::weakly_canonical(p, ec).generic_string();
    if (!ec && g.rfind(rootG, 0) == 0) return g.substr(rootG.size());
    return p.generic_string();
  };
  l.file = rel(s.loc.file);
  return l;
}

void Runner::copyTree(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::create_directories(to, ec);
  for (fs::recursive_directory_iterator it(from, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    // lexically: fs::relative() resolves symlinks, which would name a copied link after its target (W9e H1)
    const fs::path rel = it->path().lexically_relative(from);
    if (it->is_symlink(ec)) {
      fs::copy_symlink(it->path(), to / rel, ec);
    } else if (it->is_directory(ec)) {
      if (name == ".tenfold" || name == ".git") {
        it.disable_recursion_pending();
        continue;
      }
      fs::create_directories(to / rel, ec);
    } else if (it->is_regular_file(ec)) {
      fs::copy_file(it->path(), to / rel, fs::copy_options::overwrite_existing, ec);
    }
  }
}

void Runner::record(ItemResult r) {
  // A wrong answer from a partial clangd index is not evidence against tenfold: keep it apart from the FAILs.
  if (r.status == Status::Fail && r.isCheck && !r.fast && !indexComplete_ && !r.expected.empty() &&
      r.expected != "an answer") {
    r.status = Status::Partial;
    r.note += (r.note.empty() ? "" : " ") + std::string("judged on an incomplete clangd index");
  }
  switch (r.status) {
    case Status::Pass: ++rep_.pass; break;
    case Status::Fail: ++rep_.fail; break;
    case Status::XFail: ++rep_.xfail; break;
    case Status::XPass: ++rep_.xpass; break;
    case Status::Skip: ++rep_.skip; break;
    case Status::Partial: ++rep_.partial; break;
  }
  std::ostringstream os;
  os << toString(r.status);
  for (size_t n = std::string(toString(r.status)).size(); n < 6; ++n) os << ' ';
  os << rep_.name << ":" << r.line << "  " << r.text;
  if (r.isCheck && r.status != Status::Skip) {
    os << "  [" << (r.fast ? "fast" : "final");
    if (r.ms >= 0) {
      char b[32];
      std::snprintf(b, sizeof b, " %.1f ms", r.ms);
      os << b;
    }
    os << "]";
  }
  const bool bad =
      r.status == Status::Fail || r.status == Status::XFail || r.status == Status::XPass || r.status == Status::Partial;
  if (bad && !r.expected.empty()) os << "\n        expected: " << r.expected << "\n        actual:   " << r.actual;
  if (!r.note.empty() && (bad || r.status == Status::Skip)) os << "\n        " << r.note;
  emit(os.str());
  rep_.results.push_back(std::move(r));
}

std::string Runner::currentText(const fs::path& file) {
  if (Document* d = ed_->document(file)) return d->buffer().text();
  return decodeFileBytes(readFile(file)).text;  // columns are those of the UTF-8 text tenfold shows
}

void Runner::rescanFile(const std::string& rel, const std::string& text) {
  for (auto it = anchors_.begin(); it != anchors_.end();)
    it = it->second.file == rel ? anchors_.erase(it) : std::next(it);
  std::vector<std::string> ignored;
  scanAnchorsInText(rel, text, anchors_, ignored);
}

void Runner::noteOpened(const fs::path& file) {
  if (!haveClangd_ || tuStarted_) return;
  tuStarted_ = true;
  const auto start = Clock::now();
  tuThread_ = std::thread([this, file, start] {
    while (!tuStop_) {
      if (ed_->semantic().fileReady(file)) {
        firstTuMs_ = msSince(start);
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  });
}

bool Runner::moveTo(const std::string& anchor, std::string& err) {
  auto it = anchors_.find(anchor);
  if (it == anchors_.end()) {
    err = "anchor '" + anchor + "' no longer exists (an edit removed it?)";
    return false;
  }
  const Anchor& a = it->second;
  const int col = a.col + a.skip;  // query at the first identifier character: a destructor's '~' is not a word char
  std::string e;
  if (!ed_->gotoLocation({abs(a.file), {a.line, col}}, &e)) {
    err = "cannot open " + a.file + (e.empty() ? "" : ": " + e);
    return false;
  }
  noteOpened(abs(a.file));
  noteDoc(abs(a.file));
  return true;
}

bool Runner::isSynced(const fs::path& file) const {
  const Workspace& ws = ed_->workspace();
  return ws.isSource(file) || ws.isHeader(file);  // the engine syncs exactly these to clangd (isCodeDocument)
}

void Runner::sweepOpenDocs() {
  std::set<fs::path> now;
  for (Document* d : ed_->documents()) now.insert(d->path());
  for (auto& p : now)
    if (!openSeen_.count(p)) dirtyDocs_.insert(p);  // opened by a jump, a rename or a directive
  openSeen_ = std::move(now);
}

bool Runner::awaitDocuments(const std::string& anchorRel, std::string& err) {
  if (!haveClangd_) return true;
  if (!anchorRel.empty() && !isSynced(abs(anchorRel))) {
    err = "document not synced to clangd: " + anchorRel + " (not a C/C++ source or header; clangd never parses it)";
    return false;
  }
  sweepOpenDocs();
  if (!anchorRel.empty()) dirtyDocs_.insert(abs(anchorRel));
  std::set<fs::path> todo;
  todo.swap(dirtyDocs_);
  for (auto& p : todo)
    if (ed_->document(p) && isSynced(p)) ed_->semantic().waitForFile(p, std::min(60000, remainingMs()));
  return true;
}

void Runner::ensureReady(const std::string& relFile) {
  if (!haveClangd_) return;
  if (indexWaitDue_) {
    indexWaitDue_ = false;
    const auto t = Clock::now();
    const bool ok = ed_->semantic().waitForClangdIndex(std::min(o_.indexWaitSec * 1000, remainingMs()));
    if (rep_.clangdIndexMs < 0 && ok) rep_.clangdIndexMs = msSince(t);
    indexComplete_ = ok;
    rep_.indexComplete = rep_.indexComplete && ok;
    if (!ok) {
      if (auto c = ed_->semantic().clangd()) rep_.indexPercent = c->indexProgress().percentage;
      emit("INDEX  " + rep_.name + "  clangd's background index is incomplete after " +
           std::to_string(o_.indexWaitSec) + " s (" + std::to_string(rep_.indexPercent) +
           "%); failures from here are reported as PARTIAL (raise --index-wait)");
    }
  }
  if (!relFile.empty()) ed_->semantic().waitForFile(abs(relFile), std::min(60000, remainingMs()));
}

Runner::Answer Runner::askLocs(const std::function<void(Editor::NavResult)>& issue, bool wantFinal) {
  struct State {
    Answer a;
    bool done = false;
  };
  auto st = std::make_shared<State>();
  const auto t = Clock::now();
  issue([st, t, wantFinal](Confidence c, const std::vector<SymbolLocation>& locs, const std::string& err) {
    if (st->done) return;
    // The fast tier is the first callback; the final answer is the authoritative one, or any failure of clangd.
    if (wantFinal && c != Confidence::Authoritative && err.empty()) return;
    st->a.got = true;
    st->a.conf = c;
    st->a.locs = locs;
    st->a.err = err;
    st->a.ms = msSince(t);
    st->done = true;
  });
  ed_->waitFor([&] { return st->done; }, queryTimeout());
  return st->a;
}

Runner::Answer Runner::askHover(bool held) {
  struct State {
    Answer a;
    bool done = false;
  };
  auto st = std::make_shared<State>();
  const auto t = Clock::now();
  ed_->semantic().setFastOnlyQueries(held && haveClangd_);
  ed_->hover([st, t](const std::string& text, const std::string& err) {
    if (st->done) return;
    st->a.got = true;
    st->a.text = text;
    st->a.err = err;
    st->a.ms = msSince(t);
    st->done = true;
  });
  ed_->waitFor([&] { return st->done; }, queryTimeout());
  ed_->semantic().setFastOnlyQueries(haveClangd_);
  return st->a;
}

// One navigation-style query at the caret (already placed), through the Editor calls the UI makes.
Runner::Answer Runner::queryLocs(Kind kind, const Item& it, bool held, const Anchor&) {
  ed_->semantic().setFastOnlyQueries(held && haveClangd_);
  Answer a;
  switch (kind) {
    case Kind::Def: a = askLocs([&](Editor::NavResult cb) { ed_->gotoDefinition(std::move(cb)); }, !held); break;
    case Kind::Decl: a = askLocs([&](Editor::NavResult cb) { ed_->gotoDeclaration(std::move(cb)); }, !held); break;
    case Kind::Refs: a = askLocs([&](Editor::NavResult cb) { ed_->findReferences(std::move(cb)); }, true); break;
    case Kind::Sym: a = askLocs([&](Editor::NavResult cb) { ed_->findSymbols(it.query, std::move(cb)); }, !held); break;
    case Kind::Typedef:
      a = askLocs([&](Editor::NavResult cb) { ed_->gotoTypeDefinition(std::move(cb)); }, !held);
      break;
    case Kind::Impl: a = askLocs([&](Editor::NavResult cb) { ed_->findImplementations(std::move(cb)); }, !held); break;
    default: break;
  }
  ed_->semantic().setFastOnlyQueries(haveClangd_);
  return a;
}

void Runner::flushAgreement() {
  if (!haveClangd_) pending_.clear();  // fast against fast says nothing
  if (pending_.empty()) return;
  auto todo = std::move(pending_);
  pending_.clear();
  for (auto& p : todo) {
    if (remainingMs() <= 0) break;
    std::string err;
    try {
      if (!moveTo(p.anchor, err)) continue;
      if (!awaitDocuments(anchors_[p.anchor].file, err)) continue;
      ensureReady(anchors_[p.anchor].file);
    } catch (const std::exception& e) {  // W9e H2: an engine exception costs this comparison, not the run
      ed_->semantic().setFastOnlyQueries(haveClangd_);
      continue;
    }
    Item dummy;
    Answer fin;
    try {
      fin = queryLocs(p.kind, dummy, false, anchors_[p.anchor]);
    } catch (const std::exception&) {
      ed_->semantic().setFastOnlyQueries(haveClangd_);
      continue;
    }
    if (!fin.got) continue;
    sample(p.kind, false, fin.ms);
    ++rep_.agreeTotal;
    std::vector<Loc> finalLocs = toLocs(fin.locs);
    if (sameAnswer(p.fast, finalLocs)) ++rep_.agreeSame;
    else
      rep_.disagreements.push_back({p.line, std::string(toString(p.kind)) + " " + p.anchor,
                                    describeLocs(p.fast, anchors_), describeLocs(finalLocs, anchors_)});
  }
}

void Runner::runCheck(const Item& it) {
  ItemResult r;
  r.line = it.line;
  r.text = it.text;
  r.kind = it.kind;
  r.fast = it.fast;
  const bool needsClangd = !it.fast;
  if (needsClangd && !haveClangd_) {
    r.status = Status::Skip;
    r.note = "needs clangd (run without --no-clangd)";
    return record(std::move(r));
  }
  if (remainingMs() <= 0) {
    r.status = Status::Fail;
    r.note = "project time limit reached before this expectation ran";
    return record(std::move(r));
  }
  if (it.kind == Kind::Rename) {
    runRename(it, r);
    return;
  }
  std::string err;
  if (!it.anchor.empty() && !moveTo(it.anchor, err)) {
    r.status = Status::Fail;
    r.note = err;
    return record(std::move(r));
  }
  const Anchor anchor = it.anchor.empty() ? Anchor{} : anchors_[it.anchor];
  const bool agreementKind = it.kind == Kind::Def || it.kind == Kind::Decl;

  // The fast tier: clangd held back, so the answer is the fast index's regardless of what clangd has parsed.
  Answer fastAns;
  if (it.fast || agreementKind) {
    const bool tuReady = haveClangd_ && !anchor.file.empty() && ed_->semantic().fileReady(abs(anchor.file));
    if (it.kind == Kind::Hover) fastAns = askHover(true);
    else fastAns = queryLocs(it.kind, it, true, anchor);
    if (it.fast && haveClangd_) ++(tuReady ? rep_.fastAfterTu : rep_.fastBeforeTu);
    if (fastAns.got) sample(it.kind, true, fastAns.ms);
  }

  Answer ans = fastAns;
  if (!it.fast) {
    flushAgreement();  // earlier fast probes: compare them with clangd's answer before the document changes
    if (!it.anchor.empty() && !moveTo(it.anchor, err)) {
      r.status = Status::Fail;
      r.note = err;
      return record(std::move(r));
    }
    if (!awaitDocuments(anchor.file, err)) {
      r.status = Status::Fail;
      r.note = err;
      return record(std::move(r));
    }
    ensureReady(anchor.file);
    if (it.kind == Kind::Hover) ans = askHover(false);
    else ans = queryLocs(it.kind, it, false, anchor);
    if (ans.got) sample(it.kind, false, ans.ms);
  } else if (agreementKind && fastAns.got) {
    pending_.push_back({it.line, it.kind, it.anchor, toLocs(fastAns.locs)});
  }

  r.ms = ans.ms;
  if (!ans.got) {
    r.status = classify(it.xfail, false);
    r.expected = "an answer";
    r.actual = "no answer within the time limit";
    return record(std::move(r));
  }
  Verdict v;
  if (it.kind == Kind::Hover) {
    v = checkHover(it, ans.text);
    if (!ans.err.empty() && ans.text.empty()) v.actual += " (" + ans.err + ")";
  } else {
    v = checkLocations(it, toLocs(ans.locs), anchors_);
    if (!ans.err.empty() && ans.locs.empty()) v.actual += " (" + ans.err + ")";
  }
  if (it.xfail) r.note = "known wrong answer: " + it.reason;
  r.status = classify(it.xfail, v.ok);
  r.expected = v.expected;
  r.actual = oneLine(v.actual);
  if (!it.fast && agreementKind && fastAns.got && haveClangd_) {
    // The fast probe belongs to this line: compare it with the final answer right away.
    ++rep_.agreeTotal;
    if (sameAnswer(toLocs(fastAns.locs), toLocs(ans.locs))) ++rep_.agreeSame;
    else
      rep_.disagreements.push_back({it.line, std::string(toString(it.kind)) + " " + it.anchor,
                                    describeLocs(toLocs(fastAns.locs), anchors_),
                                    describeLocs(toLocs(ans.locs), anchors_)});
  }
  record(std::move(r));
}

void Runner::runRename(const Item& it, ItemResult& r) {
  std::string err;
  r.fast = false;
  // The pending fast probes are compared first: they move the caret, and the rename acts on the caret.
  flushAgreement();
  if (!moveTo(it.anchor, err) || !awaitDocuments(anchors_[it.anchor].file, err)) {
    r.status = Status::Fail;
    r.note = err;
    return record(std::move(r));
  }
  const Anchor a = anchors_[it.anchor];
  ensureReady(a.file);

  // Snapshot of every file with anchors: the text now (open documents: their buffer), and which were open.
  std::map<std::string, std::string> before;
  std::set<std::string> wasOpen;
  for (auto& [n, an] : anchors_) {
    if (before.count(an.file)) continue;
    before[an.file] = currentText(abs(an.file));
    if (ed_->document(abs(an.file))) wasOpen.insert(an.file);
  }
  std::map<std::string, std::string> rawBefore;  // disk bytes of files that were closed (exact restore)
  for (auto& [f, t] : before)
    if (!wasOpen.count(f)) rawBefore[f] = readFile(abs(f));
  std::map<std::string, std::string> tokensBefore;
  for (auto& [n, an] : anchors_) tokensBefore[n] = tokenAt(splitLines(before[an.file]), an);
  const AnchorMap anchorsBefore = anchors_;

  struct State {
    Editor::RenameResult res;
    std::string err;
    bool done = false;
  };
  auto st = std::make_shared<State>();
  const auto t = Clock::now();
  ed_->semantic().setFastOnlyQueries(false);
  ed_->renameSymbol(ed_->cursor().pos, it.query, [st](const Editor::RenameResult& res, const std::string& e) {
    st->res = res;
    st->err = e;
    st->done = true;
  });
  ed_->waitFor([&] { return st->done; }, queryTimeout());
  ed_->semantic().setFastOnlyQueries(haveClangd_);
  r.ms = msSince(t);
  sample(Kind::Rename, false, r.ms);

  Verdict v;
  if (!st->done && !(it.none || it.refused)) {
    v.expected = "a rename";
    v.actual = "no answer within the time limit";
  } else if (!st->err.empty() && st->res.edits == 0 && !(it.none || it.refused)) {
    v.expected = "{" + std::to_string(it.renamed.size()) + " anchors} renamed";
    v.actual = "rename failed: " + st->err;
  } else {
    AnchorMap after;
    std::vector<std::string> ignored;
    std::map<std::string, std::vector<std::string>> afterLines;
    for (auto& [f, txt] : before) {
      std::string now = currentText(abs(f));
      scanAnchorsInText(f, now, after, ignored);
      afterLines[f] = splitLines(now);
    }
    std::map<std::string, std::string> tokensAfter;
    for (auto& [n, an] : after) tokensAfter[n] = tokenAt(afterLines[an.file], an);
    if (it.none || it.refused) {
      v = checkRenameNoChange(it, st->done, st->err, st->res.edits, tokensBefore, tokensAfter);
    } else {
      v = checkRename(it, tokensBefore, tokensAfter);
      if (!st->err.empty()) v.actual += " (" + st->err + ")";
    }
  }

  // Restore the corpus: open documents get their old text back; files the rename wrote and closed are rewritten
  // byte for byte and clangd is told. The anchors are then re-scanned and must be where they were.
  for (auto& [f, txt] : before) {
    const fs::path p = abs(f);
    if (wasOpen.count(f)) {
      Document* d = ed_->document(p);
      if (d && d->buffer().text() != txt) {
        FileEdits fe;
        fe[p].push_back({Range{{0, 0}, d->buffer().endPosition()}, txt});
        ed_->applyEdits(fe);
        noteDoc(p);
      }
    } else {
      if (ed_->document(p)) ed_->close(p);
      if (readFile(p) != rawBefore[f]) {
        writeFile(p, rawBefore[f]);
        ed_->semantic().fileChanged(p);
        ed_->semantic().watchedFileChanged(p, 2);
      }
    }
  }
  {
    AnchorMap restored;
    std::vector<std::string> ignored;
    for (auto& [f, txt] : before) scanAnchorsInText(f, currentText(abs(f)), restored, ignored);
    bool same = restored.size() == anchorsBefore.size();
    for (auto& [n, an] : anchorsBefore) {
      auto x = restored.find(n);
      same = same && x != restored.end() && x->second.file == an.file && x->second.line == an.line &&
             x->second.col == an.col;
    }
    if (!same) r.note += "WARNING: the corpus could not be restored after the rename; later results are unreliable. ";
    anchors_ = restored;
  }

  if (it.xfail) r.note += "known wrong answer: " + it.reason;
  r.status = classify(it.xfail, v.ok);
  r.expected = v.expected;
  r.actual = oneLine(v.actual);
  record(std::move(r));
}

void Runner::runConfig(const Item& it) {
  ItemResult r;
  r.line = it.line;
  r.text = it.text;
  r.isCheck = false;
  flushAgreement();
  std::string err;
  bool applied = false;
  if (ed_->vsConfigurations().empty()) {
    r.status = Status::Fail;
    r.note = "config: the corpus has no Visual Studio solution (.sln) at its root";
    return record(std::move(r));
  }
  if (!ed_->selectVsConfiguration(it.arg, &err, [&] { applied = true; })) {
    r.status = Status::Fail;
    r.note = "config: " + err;
    return record(std::move(r));
  }
  if (!ed_->waitFor([&] { return applied; }, std::min(120000, remainingMs()))) {
    r.status = Status::Fail;
    r.note = "config: clangd was not restarted within the time limit";
    return record(std::move(r));
  }
  indexWaitDue_ = true;
  emit("CONFIG " + rep_.name + ":" + std::to_string(it.line) + "  " + it.arg +
       "  (active: " + ed_->activeVsConfiguration().name() + ")");
}

void Runner::runOpen(const Item& it) {
  std::string err;
  Document* d = ed_->open(abs(it.arg), &err);
  if (!d) {
    ItemResult r;
    r.line = it.line;
    r.text = it.text;
    r.isCheck = false;
    r.status = Status::Fail;
    r.note = "open: cannot open " + it.arg + (err.empty() ? "" : ": " + err);
    return record(std::move(r));
  }
  noteOpened(abs(it.arg));
  noteDoc(abs(it.arg));
}

void Runner::runClose(const Item& it) {
  flushAgreement();  // earlier fast probes are compared while the document is as they saw it
  const fs::path p = abs(it.arg);
  if (!ed_->document(p)) {
    ItemResult r;
    r.line = it.line;
    r.text = it.text;
    r.isCheck = false;
    r.status = Status::Fail;
    r.note = "close: " + it.arg + " is not open";
    return record(std::move(r));
  }
  ed_->close(p);  // unsaved edits are dropped: the file is what is on disk again
  dirtyDocs_.erase(p);
  openSeen_.erase(p);
  rescanFile(it.arg, currentText(p));  // anchors an `edit` introduced are gone, the others are where the disk has them
}

void Runner::runEdit(const Item& it) {
  ItemResult r;
  r.line = it.line;
  r.text = it.text;
  r.isCheck = false;
  flushAgreement();
  std::string err;
  const fs::path p = abs(it.arg);
  Document* d = ed_->open(p, &err);
  if (!d) {
    r.status = Status::Fail;
    r.note = "edit: cannot open " + it.arg + (err.empty() ? "" : ": " + err);
    return record(std::move(r));
  }
  const int line = it.editLine - 1, col = it.editCol - 1;
  if (line >= d->buffer().lineCount() || col > static_cast<int>(d->buffer().line(line).size())) {
    r.status = Status::Fail;
    r.note = "edit: " + it.arg + ":" + std::to_string(it.editLine) + ":" + std::to_string(it.editCol) +
             " is outside the document (" + std::to_string(d->buffer().lineCount()) + " lines)";
    return record(std::move(r));
  }
  if (!ed_->gotoLocation({p, {line, col}}, &err)) {
    r.status = Status::Fail;
    r.note = "edit: " + err;
    return record(std::move(r));
  }
  noteOpened(p);
  ed_->replaceRange({{line, col}, {line, col}}, it.editText);
  noteDoc(p);
  rescanFile(it.arg, d->buffer().text());
}

void Runner::runItem(const Item& it) {
  // W9e H2: whatever the engine throws while answering one line is that line's FAIL (with the exception text); the
  // run goes on with the next line and the report is still written.
  try {
    switch (it.type) {
      case Item::Type::Check: return runCheck(it);
      case Item::Type::Config: return runConfig(it);
      case Item::Type::Open: return runOpen(it);
      case Item::Type::Close: return runClose(it);
      case Item::Type::Edit: return runEdit(it);
    }
  } catch (const std::exception& e) { failed(it, std::string("exception: ") + e.what()); } catch (...) {
    failed(it, "exception: unknown");
  }
}

void Runner::failed(const Item& it, const std::string& what) {
  ed_->semantic().setFastOnlyQueries(haveClangd_);  // the throw may have skipped the restore in queryLocs
  ItemResult r;
  r.line = it.line;
  r.text = it.text;
  r.kind = it.kind;
  r.fast = it.fast;
  r.isCheck = it.type == Item::Type::Check;
  r.status = Status::Fail;
  r.note = what;
  record(std::move(r));
}

bool Runner::setup() {
  std::error_code ec;
  const fs::path src = fs::absolute(o_.dir, ec);
  rep_.dir = src.generic_string();
  rep_.name = src.filename().empty() ? src.parent_path().filename().string() : src.filename().string();
  if (!fs::is_regular_file(src / kExpectFileName, ec)) {
    rep_.fatal = src.generic_string() + ": no " + kExpectFileName;
    return false;
  }
  std::vector<std::string> errors;
  AnchorMap scanned;
  scanAnchors(src, scanned, errors);
  const std::string expectText = readFile(src / kExpectFileName);
  parseExpectations(expectText, rep_.name + "/" + kExpectFileName, scanned, items_, errors);
  if (!errors.empty()) {
    for (auto& e : errors) rep_.fatal += (rep_.fatal.empty() ? "" : "\n") + e;
    return false;
  }
  anchors_ = scanned;

  const fs::path parent = fs::weakly_canonical(fs::temp_directory_path(), ec);
  static std::atomic<int> counter{0};
  root_ =
      parent / ("tenfold-corpus-" + rep_.name + "-" + std::to_string(processId()) + "-" + std::to_string(counter++));
  fs::remove_all(root_, ec);
  copyTree(src, root_);
  rep_.scratch = root_.generic_string();
  // compile_commands.json may name the scratch copy through @CORPUS_ROOT@.
  for (fs::recursive_directory_iterator it(root_, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->path().filename() != "compile_commands.json" || !it->is_regular_file(ec)) continue;
    std::string text = readFile(it->path());
    const std::string tag = "@CORPUS_ROOT@";
    bool changed = false;
    for (size_t p = text.find(tag); p != std::string::npos; p = text.find(tag, p)) {
      text.replace(p, tag.size(), root_.generic_string());
      changed = true;
    }
    if (changed) writeFile(it->path(), text);
  }
  return true;
}

void Runner::finish() {
  tuStop_ = true;
  if (tuThread_.joinable()) tuThread_.join();
  rep_.firstTuMs = firstTuMs_;
  rep_.peakRssMiB = peakRss();
  rep_.totalMs = msSince(t0_);
  if (ed_) ed_->shutdown();
  ed_.reset();
  // The process-wide log buffers records for <scratch>/.tenfold/log.txt and writes them later; settle it first or the
  // file (and its directory) reappears after the scratch copy was removed.
  Log::flush();
  Log::setWorkspace({});
  std::error_code ec;
  if (!o_.keepScratch && !root_.empty()) fs::remove_all(root_, ec);
}

Report Runner::run() {
  t0_ = Clock::now();
  deadlineMs_ = o_.timeoutSec * 1000.0;
  if (!setup()) return rep_;

  Editor::Options eo;
  eo.semantic.useClangd = o_.useClangd;
  eo.semantic.clangdPath = o_.clangdPath;
  eo.semantic.fastOnlyQueries = o_.useClangd;  // see runCheck: every fast query holds clangd back
  eo.log = [this](const std::string& m) {
    std::lock_guard<std::mutex> lk(logMu_);
    engineLog_.push_back(m.substr(0, 400));
    if (engineLog_.size() > 60) engineLog_.erase(engineLog_.begin());
    if (o_.verbose) std::cerr << "    [engine] " << m << "\n";
  };
  ed_ = std::make_unique<Editor>(root_, eo);
  std::string err;
  const auto tStart = Clock::now();
  const bool started = ed_->start(&err);
  rep_.startMs = msSince(tStart);
  haveClangd_ = started && ed_->semantic().clangd() != nullptr;
  if (!started) {
    rep_.fatal = "engine did not start: " + err;
    finish();
    return rep_;
  }
  if (o_.useClangd && !haveClangd_) {
    rep_.fatal = "clangd could not be started" + (err.empty() ? std::string() : ": " + err) +
                 " (install clangd, pass --clangd <path>, or use --no-clangd for fast-tier expectations only)";
    finish();
    return rep_;
  }
  rep_.clangd = haveClangd_;
  auto st = ed_->semantic().status();
  rep_.clangdVersion = st.clangdVersion;
  if (!ed_->semantic().waitForFastIndex(std::min(300000, remainingMs()))) {
    rep_.fatal = "the fast index did not finish within the time limit";
    finish();
    return rep_;
  }
  st = ed_->semantic().status();
  rep_.fastIndexMs = st.fastSeconds * 1000.0;
  rep_.compdbSource = st.compdbSource;

  emit("== corpus " + rep_.name + ": " + std::to_string(items_.size()) + " lines, " + std::to_string(anchors_.size()) +
       " anchors, " + (haveClangd_ ? st.clangdVersion : std::string("clangd off")) +
       ", compile commands: " + st.compdbSource + " ==");
  for (auto& it : items_) runItem(it);
  flushAgreement();
  rep_.compdbSource = ed_->semantic().status().compdbSource;
  finish();
  emit(rep_.summary());
  return rep_;
}

}  // namespace

Report runCorpus(const RunOptions& opt) {
  Runner r(opt);
  Report rep = r.run();
  if (!rep.fatal.empty()) {
    if (opt.out) opt.out("ERROR  " + rep.fatal);
    else std::cerr << "ERROR  " << rep.fatal << "\n";
  }
  return rep;
}

std::vector<fs::path> listCorpora(const fs::path& root) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_directory(ec) && fs::is_regular_file(it->path() / kExpectFileName, ec)) out.push_back(it->path());
  std::sort(out.begin(), out.end());
  return out;
}

std::string Report::summary() const {
  char b[512];
  std::snprintf(b, sizeof b,
                "== %s: %d pass, %d fail, %d xfail, %d xpass, %d skip; fast/final agreement %zu/%zu; total %.1f s ==",
                name.c_str(), pass, fail, xfail, xpass, skip, agreeSame, agreeTotal, totalMs / 1000.0);
  std::string s = b;
  if (!indexComplete || partial > 0) {
    char c[160];
    std::snprintf(c, sizeof c,
                  "\n== %s: clangd index INCOMPLETE (%d%%): %d expectation(s) PARTIAL, not counted as fail ==",
                  name.c_str(), indexPercent, partial);
    s += c;
  }
  return s;
}

std::string Report::toJson() const {
  Json j;
  j["corpus"] = name;
  j["dir"] = dir;
  if (!fatal.empty()) j["fatal"] = fatal;
  j["clangd"] = clangd ? Json(clangdVersion) : Json(nullptr);
  j["compileCommands"] = compdbSource;
  j["counts"] = {{"total", results.size()}, {"pass", pass}, {"fail", fail},      {"xfail", xfail},
                 {"xpass", xpass},          {"skip", skip}, {"partial", partial}};
  j["clangdIndexComplete"] = indexComplete;
  if (!indexComplete) j["clangdIndexPercent"] = indexPercent;
  Json agree;
  agree["queries"] = agreeTotal;
  agree["agree"] = agreeSame;
  if (agreeTotal) agree["rate"] = static_cast<double>(agreeSame) / static_cast<double>(agreeTotal);
  else agree["rate"] = nullptr;
  agree["disagreements"] = Json::array();
  for (auto& d : disagreements)
    agree["disagreements"].push_back({{"line", d.line}, {"query", d.query}, {"fast", d.fast}, {"final", d.final}});
  j["fastAgreement"] = agree;
  j["fastIsolation"] = {{"method", "clangd answers held back (SemanticEngine::Options::fastOnlyQueries)"},
                        {"fastChecksBeforeTu", fastBeforeTu},
                        {"fastChecksAfterTu", fastAfterTu}};
  Json lat = Json::object();
  for (auto& [k, v] : samples) {
    LatencyStats s = computeStats(v);
    lat[k] = {{"n", s.n}, {"median", s.median}, {"p95", s.p95}, {"max", s.max}};
  }
  j["latencyMs"] = lat;
  j["timingMs"] = {{"start", startMs},
                   {"fastIndex", fastIndexMs},
                   {"firstTu", firstTuMs},
                   {"clangdIndex", clangdIndexMs},
                   {"total", totalMs}};
  j["peakRssMiB"] = peakRssMiB;
  Json rs = Json::array();
  for (auto& r : results) {
    Json x = {{"line", r.line}, {"text", r.text}, {"status", toString(r.status)}};
    if (r.isCheck) {
      x["kind"] = toString(r.kind);
      x["tier"] = r.fast ? "fast" : "final";
      if (r.ms >= 0) x["ms"] = r.ms;
    }
    if (!r.expected.empty()) x["expected"] = r.expected;
    if (!r.actual.empty()) x["actual"] = r.actual;
    if (!r.note.empty()) x["note"] = r.note;
    rs.push_back(x);
  }
  j["results"] = rs;
  return dumpSafe(j, 2);
}

}  // namespace tf::corpus
