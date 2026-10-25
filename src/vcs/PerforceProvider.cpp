#include "vcs/PerforceProvider.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <set>

namespace tf {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

// Paths compare case-insensitively on Windows only.
std::string keyOf(const fs::path& p) {
  std::string s = pathToUtf8(p.lexically_normal());
  while (s.size() > 1 && s.back() == '/') s.pop_back();
#ifdef _WIN32
  return lower(s);
#else
  return s;
#endif
}

// The first line that says something: p4 prefixes its errors with a header line such as "Perforce client error:".
std::string firstNonEmptyLine(const std::string& s) {
  std::string header;
  size_t pos = 0;
  while (pos < s.size()) {
    size_t e = s.find_first_of("\r\n", pos);
    if (e == std::string::npos) e = s.size();
    std::string line = s.substr(pos, e - pos);
    pos = e + 1;
    const size_t b = line.find_first_not_of(" \t");
    if (b == std::string::npos) continue;
    line = line.substr(b);
    if (line.back() == ':' && header.empty()) {
      header = line;
      continue;
    }
    return line;
  }
  return header;
}

bool contains(const std::string& s, const char* what) { return lower(s).find(what) != std::string::npos; }

std::string trimEol(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

// Messages of calls that "failed" only because there was nothing to report.
bool isBenign(const std::string& err) {
  return contains(err, "no such file") || contains(err, "not opened") || contains(err, "not in client view") ||
         contains(err, "not under client's root") || contains(err, "no file(s)") || contains(err, "file(s) not");
}

constexpr const char* kFields =
    "depotFile,clientFile,headRev,haveRev,action,movedFile,unresolved,headAction,headType,type";

struct NegativeCache {
  std::mutex mu;
  std::map<std::string, std::pair<std::chrono::steady_clock::time_point, std::string>> entries;
};
NegativeCache& negCache() {
  static NegativeCache c;
  return c;
}
constexpr auto kNegativeTtl = std::chrono::seconds(15);

bool isWritable(const fs::path& p) {
  std::error_code ec;
  auto st = fs::status(p, ec);
  if (ec) return false;
  return (st.permissions() & fs::perms::owner_write) != fs::perms::none;
}

bool isGone(const std::string& headAction) {
  return headAction == "delete" || headAction == "move/delete" || headAction == "purge" || headAction == "archive";
}

}  // namespace

void PerforceProvider::clearDetectCache() {
  std::lock_guard<std::mutex> lk(negCache().mu);
  negCache().entries.clear();
}

std::vector<PerforceProvider::Record> PerforceProvider::parseTagged(const std::string& out) {
  std::vector<Record> res;
  Record cur;
  size_t pos = 0;
  while (pos <= out.size()) {
    size_t e = out.find('\n', pos);
    if (e == std::string::npos) e = out.size();
    std::string line = out.substr(pos, e - pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    pos = e + 1;
    if (line.empty()) {
      if (!cur.empty()) res.push_back(std::move(cur));
      cur.clear();
      continue;
    }
    if (line.compare(0, 4, "... ") != 0) continue;
    size_t sp = line.find(' ', 4);
    std::string key = line.substr(4, sp == std::string::npos ? std::string::npos : sp - 4);
    cur[key] = sp == std::string::npos ? std::string() : line.substr(sp + 1);
  }
  if (!cur.empty()) res.push_back(std::move(cur));
  return res;
}

bool PerforceProvider::isBinaryType(const std::string& type) {
  std::string base = lower(type.substr(0, type.find('+')));
  return base == "binary" || base == "ubinary" || base == "utf16" || base == "apple" || base == "resource" ||
         base == "symlink";
}

std::string PerforceProvider::escapePath(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '%': o += "%25"; break;
      case '@': o += "%40"; break;
      case '#': o += "%23"; break;
      case '*': o += "%2A"; break;
      default: o += c;
    }
  }
  return o;
}

VcsStatus PerforceProvider::classify(const Record& r, bool exists, bool writable) {
  auto get = [&](const char* k) -> std::string {
    auto it = r.find(k);
    return it == r.end() ? std::string() : it->second;
  };
  const std::string action = get("action");
  if (r.count("unresolved")) return VcsStatus::Conflicted;
  if (!action.empty()) {
    if (action == "add" || action == "branch") return VcsStatus::Added;
    if (action == "delete" || action == "move/delete") return VcsStatus::Deleted;
    if (action == "move/add") return VcsStatus::Renamed;
    return VcsStatus::CheckedOut;  // edit, integrate, import, purge, archive
  }
  if (!r.count("depotFile")) return exists ? VcsStatus::Untracked : VcsStatus::Unknown;
  if (isGone(get("headAction"))) return exists ? VcsStatus::Untracked : VcsStatus::Unknown;  // a new local file
  if (get("haveRev").empty() || get("haveRev") == "none") return VcsStatus::Unknown;         // not synced
  if (!exists) return VcsStatus::Unknown;
  return writable ? VcsStatus::Unmodified : VcsStatus::ReadOnlyNotCheckedOut;
}

ExecResult PerforceProvider::p4(const std::vector<std::string>& args, int timeoutMs) const {
  ExecOptions o;
  o.exe = exe_;
  o.args = {"-ztag"};
  o.args.insert(o.args.end(), args.begin(), args.end());
  o.cwd = workDir_;
  o.timeoutMs = timeoutMs > 0 ? timeoutMs : timeoutMs_;
  return runCommand(o);
}

bool PerforceProvider::succeeded(const ExecResult& r) {
  if (!r.started || r.timedOut) return false;
  return r.exitCode == 0 || isBenign(r.err);
}

std::string PerforceProvider::messageOf(const ExecResult& r) {
  std::string m = trimEol(firstNonEmptyLine(r.err));
  if (m.empty()) m = trimEol(firstNonEmptyLine(r.out));
  if (m.empty() && r.timedOut) m = "p4 timed out";
  if (m.empty()) m = "p4 failed (exit code " + std::to_string(r.exitCode) + ")";
  return m;
}

bool PerforceProvider::fail(std::string* error, const std::string& msg) {
  lastError_ = msg;
  if (error) *error = msg;
  return false;
}

bool PerforceProvider::underRoot(const fs::path& file) const {
  if (rootKey_.empty()) return false;
  const std::string k = keyOf(file);
  return k.size() > rootKey_.size() && k.compare(0, rootKey_.size(), rootKey_) == 0 &&
         (rootKey_.back() == '/' || k[rootKey_.size()] == '/');
}

std::string PerforceProvider::arg(const fs::path& file) const { return escapePath(pathToUtf8(file)); }

bool PerforceProvider::detect(const fs::path& rootOrFile) {
  std::error_code ec;
  fs::path dir = rootOrFile;
  if (!fs::is_directory(dir, ec)) dir = dir.parent_path();
  if (dir.empty() || !fs::is_directory(dir, ec)) return fail(nullptr, "not a directory: " + rootOrFile.string());
  const std::string cacheKey = exe_ + "|" + keyOf(dir);
  {
    auto& c = negCache();
    std::lock_guard<std::mutex> lk(c.mu);
    auto it = c.entries.find(cacheKey);
    if (it != c.entries.end()) {
      if (std::chrono::steady_clock::now() - it->second.first < kNegativeTtl) return fail(nullptr, it->second.second);
      c.entries.erase(it);
    }
  }
  auto negative = [&](const std::string& msg) {
    auto& c = negCache();
    std::lock_guard<std::mutex> lk(c.mu);
    c.entries[cacheKey] = {std::chrono::steady_clock::now(), msg};
    return fail(nullptr, msg);
  };
  workDir_ = dir;
  // 1. a reachable server with a client: `p4 -ztag -F %clientRoot% info`
  ExecResult info = p4({"-F", "%clientRoot%", "info"}, detectTimeoutMs_);
  const std::string clientRoot = trimEol(firstNonEmptyLine(info.out));
  if (!info.ok() || clientRoot.empty() || clientRoot == "*unknown*" || clientRoot == "null" ||
      clientRoot.compare(0, 3, "...") == 0)
    return negative(info.ok() ? "no Perforce client for " + dir.string() : messageOf(info));
  // 2. the client maps the workspace directory: `p4 -ztag where <dir>/...`
  ExecResult where = p4({"where", escapePath(pathToUtf8(dir)) + "/..."}, detectTimeoutMs_);
  if (!where.started || where.timedOut) return negative(messageOf(where));
  bool mapped = false;
  for (const Record& r : parseTagged(where.out)) {
    auto d = r.find("depotFile");
    if (d != r.end() && !d->second.empty() && d->second[0] != '-') mapped = true;
  }
  if (!mapped) return negative("not under the Perforce client's root: " + dir.string());
  fs::path cr = pathFromUtf8(clientRoot);
  rootKey_ = keyOf(cr);
  root_ = cr;
  const std::string dk = keyOf(dir);
  if (!(dk == rootKey_ ||
        (dk.size() > rootKey_.size() && dk.compare(0, rootKey_.size(), rootKey_) == 0 && dk[rootKey_.size()] == '/'))) {
    root_ = dir;  // mapped through a view that lies outside the client root
    rootKey_ = dk;
  }
  lastError_.clear();
  return true;
}

bool PerforceProvider::fstat(const std::vector<fs::path>& files, std::map<std::string, Record>& out, std::string& err) {
  constexpr size_t kChunk = 100;
  bool ok = true;
  for (size_t i = 0; i < files.size(); i += kChunk) {
    std::vector<std::string> args = {"fstat", "-T", kFields};
    for (size_t j = i; j < std::min(files.size(), i + kChunk); ++j) args.push_back(arg(files[j]));
    ExecResult r = p4(args);
    err += r.err;
    if (!succeeded(r)) {
      fail(nullptr, messageOf(r));
      ok = false;
      continue;
    }
    for (Record& rec : parseTagged(r.out)) {
      auto c = rec.find("clientFile");
      if (c != rec.end()) out[keyOf(pathFromUtf8(c->second))] = std::move(rec);
    }
  }
  return ok;
}

std::map<std::string, fs::path> PerforceProvider::localOfDepot(const std::vector<std::string>& depotPaths) {
  std::map<std::string, fs::path> res;
  if (depotPaths.empty()) return res;
  std::vector<std::string> args = {"where"};
  for (const std::string& d : depotPaths) args.push_back(escapePath(d));
  ExecResult r = p4(args);
  if (!succeeded(r)) return res;
  for (const Record& rec : parseTagged(r.out)) {
    auto d = rec.find("depotFile");
    auto p = rec.find("path");
    if (d != rec.end() && p != rec.end()) res[d->second] = pathFromUtf8(p->second);
  }
  return res;
}

VcsFileInfo PerforceProvider::infoOf(const fs::path& file, const Record* rec, const std::string& err,
                                     const std::map<std::string, fs::path>& moved) const {
  VcsFileInfo fi;
  fi.path = file;
  std::error_code ec;
  const bool exists = fs::exists(file, ec);
  if (rec) {
    fi.status = classify(*rec, exists, exists && isWritable(file));
    if (fi.status == VcsStatus::Renamed) {
      auto m = rec->find("movedFile");
      if (m != rec->end()) {
        auto it = moved.find(m->second);
        if (it != moved.end()) fi.renamedFrom = it->second;
      }
    }
    return fi;
  }
  // No record: look for p4's own explanation of this file.
  const std::string lp = lower(err);
  bool outside = false;
  size_t from = 0;
  const std::string name = lower(pathToUtf8(file));
  while (from < lp.size()) {
    size_t e = lp.find('\n', from);
    if (e == std::string::npos) e = lp.size();
    std::string line = lp.substr(from, e - from);
    from = e + 1;
    if (line.find(name) == std::string::npos) continue;
    if (line.find("not under client's root") != std::string::npos ||
        line.find("not in client view") != std::string::npos)
      outside = true;
  }
  fi.status = (!outside && exists) ? VcsStatus::Untracked : VcsStatus::Unknown;
  return fi;
}

std::vector<VcsFileInfo> PerforceProvider::status(const std::vector<fs::path>& files) {
  std::vector<VcsFileInfo> res;
  if (files.empty()) {
    // Workspace-wide: only the opened files (`fstat -Ro`, which, unlike `p4 opened`, reports local paths).
    ExecResult r = p4({"fstat", "-Ro", "-T", kFields, escapePath(pathToUtf8(root_)) + "/..."});
    if (!succeeded(r)) {
      fail(nullptr, messageOf(r));
      return res;
    }
    lastError_.clear();
    std::vector<Record> list = parseTagged(r.out);
    std::vector<std::string> movedDepot;
    for (const Record& rec : list)
      if (rec.count("movedFile")) movedDepot.push_back(rec.at("movedFile"));
    auto moved = localOfDepot(movedDepot);
    for (const Record& rec : list) {
      auto c = rec.find("clientFile");
      if (c == rec.end()) continue;
      VcsFileInfo fi = infoOf(pathFromUtf8(c->second), &rec, "", moved);
      if (fi.status != VcsStatus::Unmodified && fi.status != VcsStatus::Ignored) res.push_back(std::move(fi));
    }
    return res;
  }
  std::map<std::string, Record> recs;
  std::string err;
  std::vector<fs::path> query;
  for (const fs::path& f : files)
    if (underRoot(f)) query.push_back(f);
  const bool ok = query.empty() || fstat(query, recs, err);
  std::vector<std::string> movedDepot;
  for (auto& [k, rec] : recs)
    if (rec.count("movedFile")) movedDepot.push_back(rec.at("movedFile"));
  auto moved = localOfDepot(movedDepot);
  if (ok) lastError_.clear();
  for (const fs::path& f : files) {
    if (!ok || !underRoot(f)) {
      VcsFileInfo fi;
      fi.path = f;
      res.push_back(fi);  // Unknown
      continue;
    }
    auto it = recs.find(keyOf(f));
    res.push_back(infoOf(f, it == recs.end() ? nullptr : &it->second, err, moved));
  }
  return res;
}

std::optional<std::string> PerforceProvider::baseText(const fs::path& file, std::string* error) {
  if (error) error->clear();
  if (!underRoot(file)) return std::nullopt;
  std::map<std::string, Record> recs;
  std::string err;
  if (!fstat({file}, recs, err)) {
    if (error) *error = lastError_;
    return std::nullopt;
  }
  auto it = recs.find(keyOf(file));
  if (it == recs.end()) return std::nullopt;  // not in the depot
  const Record& r = it->second;
  auto get = [&](const char* k) {
    auto i = r.find(k);
    return i == r.end() ? std::string() : i->second;
  };
  const std::string act = get("action");
  const std::string have = get("haveRev");
  if (!r.count("depotFile") || have.empty() || have == "none" || act == "add" || act == "branch" || act == "move/add")
    return std::nullopt;  // new file: no committed version
  std::string type = get("type");
  if (type.empty()) type = get("headType");
  if (isBinaryType(type)) return std::nullopt;  // no textual base
  ExecResult p = p4({"print", "-q", arg(file) + "#have"});
  if (!p.started || p.timedOut || (p.exitCode != 0 && !isBenign(p.err))) {
    fail(error, messageOf(p));
    return std::nullopt;
  }
  if (p.exitCode != 0) return std::nullopt;
  lastError_.clear();
  return std::move(p.out);
}

bool PerforceProvider::revert(const fs::path& file, std::string* error) {
  if (error) error->clear();
  if (!underRoot(file)) return fail(error, "not under the Perforce client: " + file.string());
  std::map<std::string, Record> recs;
  std::string err;
  if (!fstat({file}, recs, err)) {
    if (error) *error = lastError_;
    return false;
  }
  auto it = recs.find(keyOf(file));
  if (it == recs.end() || !it->second.count("action"))
    return fail(error, file.filename().string() + " is not opened in Perforce: nothing to revert");
  ExecResult r = p4({"revert", arg(file)});
  if (!r.started || r.timedOut || r.exitCode != 0) return fail(error, messageOf(r));
  lastError_.clear();
  return true;
}

bool PerforceProvider::beforeFirstEdit(const fs::path& file, std::string* error) {
  if (error) error->clear();
  if (!underRoot(file)) return true;
  std::map<std::string, Record> recs;
  std::string err;
  if (!fstat({file}, recs, err)) {
    if (error) *error = lastError_;
    return false;
  }
  auto it = recs.find(keyOf(file));
  if (it == recs.end() || !it->second.count("depotFile")) return true;  // not in the depot: nothing to check out
  const Record& r = it->second;
  if (r.count("action")) {
    if (isWritable(file)) return true;  // already opened
  } else {
    auto h = r.find("headAction");
    if (h != r.end() && isGone(h->second)) return true;  // deleted at the head: a new local file
    auto hv = r.find("haveRev");
    if (hv == r.end() || hv->second.empty() || hv->second == "none") return true;  // not synced
  }
  ExecResult e = p4({"edit", arg(file)});
  // Do not trust the exit code alone (a refused edit can be a warning): the file must be opened and writable now.
  recs.clear();
  std::string err2;
  const bool opened = fstat({file}, recs, err2) && recs.count(keyOf(file)) && recs[keyOf(file)].count("action");
  if (opened && isWritable(file)) {
    lastError_.clear();
    return true;
  }
  std::string msg = messageOf(e);
  if (e.started && !e.timedOut && e.exitCode == 0 && e.err.empty())
    msg = opened ? file.filename().string() + " was opened by p4 but is still read-only on disk"
                 : "p4 edit did not open " + file.filename().string();
  return fail(error, msg);
}

bool PerforceProvider::add(const fs::path& file, std::string* error) {
  if (error) error->clear();
  if (!underRoot(file)) return fail(error, "not under the Perforce client: " + file.string());
  ExecResult r = p4({"add", arg(file)});
  if (!r.started || r.timedOut || r.exitCode != 0) return fail(error, messageOf(r));
  lastError_.clear();
  return true;
}

}  // namespace tf
