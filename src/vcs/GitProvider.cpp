#include "vcs/GitProvider.h"

#include <algorithm>
#include <map>

#include "core/PathUtil.h"

namespace tf {

namespace fs = std::filesystem;

namespace {

std::string trimEol(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  return s;
}

std::string firstLine(const std::string& s) {
  size_t e = s.find_first_of("\r\n");
  return e == std::string::npos ? s : s.substr(0, e);
}

// The text after the n-th space of `s` (the fields before the path never contain spaces).
std::string afterSpaces(const std::string& s, int n) {
  size_t pos = 0;
  for (int i = 0; i < n; ++i) {
    pos = s.find(' ', pos);
    if (pos == std::string::npos) return {};
    ++pos;
  }
  return s.substr(pos);
}

bool contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

// `git show HEAD:path` failures that just mean "there is no committed version".
bool isMissingInHead(const std::string& err) {
  return contains(err, "does not exist in") || contains(err, "exists on disk, but not in") ||
         contains(err, "unknown revision") || contains(err, "invalid object name") || contains(err, "bad revision") ||
         contains(err, "ambiguous argument 'HEAD") || contains(err, "Needed a single revision");
}

}  // namespace

VcsStatus GitProvider::statusOfXY(char x, char y) {
  if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) return VcsStatus::Conflicted;
  if (x == 'D' || y == 'D') return VcsStatus::Deleted;
  if (x == 'A' || y == 'A') return VcsStatus::Added;
  if (x == 'R' || y == 'R') return VcsStatus::Renamed;
  if (x == 'C' || y == 'C') return VcsStatus::Added;
  if (x == '.' && y == '.') return VcsStatus::Unmodified;
  return VcsStatus::Modified;
}

std::vector<GitProvider::Entry> GitProvider::parseStatusV2(const std::string& out) {
  std::vector<Entry> res;
  size_t pos = 0;
  auto next = [&](std::string& tok) {
    if (pos >= out.size()) return false;
    size_t z = out.find('\0', pos);
    if (z == std::string::npos) z = out.size();
    tok.assign(out, pos, z - pos);
    pos = z + 1;
    return true;
  };
  std::string tok;
  while (next(tok)) {
    if (tok.size() < 2) continue;
    Entry e;
    switch (tok[0]) {
      case '1':  // 1 XY sub mH mI mW hH hI path
        if (tok.size() < 4) continue;
        e.status = statusOfXY(tok[2], tok[3]);
        e.path = afterSpaces(tok, 8);
        break;
      case '2': {  // 2 XY sub mH mI mW hH hI Xscore path <NUL> origPath
        if (tok.size() < 4) continue;
        e.status = statusOfXY(tok[2], tok[3]);
        e.path = afterSpaces(tok, 9);
        next(e.origPath);
        break;
      }
      case 'u':  // u XY sub m1 m2 m3 mW h1 h2 h3 path
        e.status = VcsStatus::Conflicted;
        e.path = afterSpaces(tok, 10);
        break;
      case '?':
        e.status = VcsStatus::Untracked;
        e.path = tok.substr(2);
        break;
      case '!':
        e.status = VcsStatus::Ignored;
        e.path = tok.substr(2);
        break;
      default:  // '#' headers
        continue;
    }
    if (!e.path.empty()) res.push_back(std::move(e));
  }
  return res;
}

ExecResult GitProvider::git(const std::vector<std::string>& args) const {
  ExecOptions o;
  o.exe = exe_;
  o.args = {"--no-optional-locks"};
  o.args.insert(o.args.end(), args.begin(), args.end());
  o.cwd = root_;
  o.env = {{"GIT_LITERAL_PATHSPECS", "1"}, {"GIT_TERMINAL_PROMPT", "0"}, {"LC_ALL", "C"}};
  return runCommand(o);
}

bool GitProvider::fail(std::string* error, const std::string& msg) {
  lastError_ = msg;
  if (error) *error = msg;
  return false;
}

bool GitProvider::detect(const fs::path& rootOrFile) {
  std::error_code ec;
  fs::path dir = rootOrFile;
  if (!fs::is_directory(dir, ec)) dir = dir.parent_path();
  if (dir.empty() || !fs::is_directory(dir, ec)) return fail(nullptr, "not a directory: " + rootOrFile.string());
  ExecOptions o;
  o.exe = exe_;
  o.args = {"rev-parse", "--show-toplevel"};
  o.cwd = dir;
  o.env = {{"GIT_TERMINAL_PROMPT", "0"}, {"LC_ALL", "C"}};
  o.timeoutMs = 15000;
  ExecResult r = runCommand(o);
  const std::string top = trimEol(r.out);
  if (!r.ok() || top.empty())
    return fail(nullptr, firstLine(r.err).empty() ? "not a git repository" : firstLine(r.err));
  fs::path p = pathFromUtf8(top);
  root_ = fs::weakly_canonical(p, ec);
  if (ec || root_.empty()) root_ = p;
  lastError_.clear();
  return true;
}

std::optional<std::string> GitProvider::relative(const fs::path& file) const {
  if (root_.empty()) return std::nullopt;
  std::error_code ec;
  fs::path abs = file.is_absolute() ? file : root_ / file;
  fs::path canon = fs::weakly_canonical(abs, ec);
  if (ec) canon = abs.lexically_normal();
  fs::path rel = canon.lexically_relative(root_);
  if (rel.empty() || rel.is_absolute()) return std::nullopt;
  const std::string s = pathToUtf8(rel);
  if (s == "." || s == ".." || s.rfind("../", 0) == 0) return std::nullopt;
  return s;
}

std::vector<VcsFileInfo> GitProvider::status(const std::vector<fs::path>& files) {
  std::vector<VcsFileInfo> out;
  if (root_.empty()) {
    for (auto& f : files) out.push_back({f, VcsStatus::Unknown, {}});
    return out;
  }
  auto abs = [&](const std::string& rel) { return root_ / pathFromUtf8(rel); };
  if (files.empty()) {
    ExecResult r = git({"status", "--porcelain=v2", "-z", "-uall"});
    if (!r.ok()) {
      fail(nullptr, firstLine(r.err));
      return out;
    }
    lastError_.clear();
    for (auto& e : parseStatusV2(r.out)) {
      VcsFileInfo fi;
      fi.path = abs(e.path);
      fi.status = e.status;
      if (!e.origPath.empty()) fi.renamedFrom = abs(e.origPath);
      out.push_back(std::move(fi));
    }
    return out;
  }

  // Query in chunks (command line length), then answer every input in order.
  std::map<std::string, Entry> found;  // key: pathKey(abs)
  std::vector<std::string> ignoredDirs;
  std::vector<std::optional<std::string>> rels;
  for (auto& f : files) rels.push_back(relative(f));
  std::vector<std::string> pending;
  auto flush = [&] {
    if (pending.empty()) return;
    std::vector<std::string> args = {"status", "--porcelain=v2", "-z", "-uall", "--ignored=matching", "--"};
    args.insert(args.end(), pending.begin(), pending.end());
    ExecResult r = git(args);
    if (r.ok()) {
      for (auto& e : parseStatusV2(r.out)) {
        if (e.status == VcsStatus::Ignored && !e.path.empty() && e.path.back() == '/') {
          ignoredDirs.push_back(e.path);
          continue;
        }
        // A path can show up twice (staged delete + untracked recreation): keep the tracked record.
        found.emplace(pathKey(abs(e.path)), e);
      }
      lastError_.clear();
    } else {
      lastError_ = firstLine(r.err);
    }
    pending.clear();
  };
  for (auto& r : rels) {
    if (!r) continue;
    pending.push_back(*r);
    if (pending.size() >= 100) flush();
  }
  flush();

  // A pathspec hides the other half of a rename, so a staged rename shows up as a plain Added file above. Ask the
  // index (no pathspec, cheap) for the renames and upgrade those.
  std::map<std::string, std::string> renames;  // key: pathKey(abs new path) -> old relative path
  const bool anyAdded =
      std::any_of(found.begin(), found.end(), [](auto& kv) { return kv.second.status == VcsStatus::Added; });
  if (anyAdded) {
    ExecResult d = git({"diff", "--cached", "-M", "--name-status", "-z"});
    if (d.ok()) {
      std::vector<std::string> tok;
      size_t pos = 0;
      while (pos < d.out.size()) {
        size_t z = d.out.find('\0', pos);
        if (z == std::string::npos) z = d.out.size();
        tok.emplace_back(d.out, pos, z - pos);
        pos = z + 1;
      }
      for (size_t k = 0; k < tok.size();) {  // "R100 <old> <new>" or "<X> <path>"
        const bool two = !tok[k].empty() && (tok[k][0] == 'R' || tok[k][0] == 'C');
        if (two && tok[k][0] == 'R' && k + 2 < tok.size()) renames[pathKey(abs(tok[k + 2]))] = tok[k + 1];
        k += two ? 3 : 2;
      }
    }
  }

  for (size_t i = 0; i < files.size(); ++i) {
    VcsFileInfo fi;
    fi.path = files[i];
    if (!rels[i]) {
      fi.status = VcsStatus::Unknown;
    } else {
      auto it = found.find(pathKey(abs(*rels[i])));
      if (it != found.end()) {
        fi.status = it->second.status;
        if (!it->second.origPath.empty()) fi.renamedFrom = abs(it->second.origPath);
        auto rn = renames.find(it->first);
        if (fi.status == VcsStatus::Added && rn != renames.end()) {
          fi.status = VcsStatus::Renamed;
          fi.renamedFrom = abs(rn->second);
        }
      } else {
        bool ignored = false;
        for (auto& d : ignoredDirs)
          if (rels[i]->rfind(d, 0) == 0) ignored = true;
        std::error_code ec;
        if (ignored) fi.status = VcsStatus::Ignored;
        else if (fs::exists(fi.path, ec)) fi.status = VcsStatus::Unmodified;
        else fi.status = VcsStatus::Unknown;
      }
    }
    out.push_back(std::move(fi));
  }
  return out;
}

std::optional<std::string> GitProvider::baseText(const fs::path& file, std::string* error) {
  if (error) error->clear();
  auto rel = relative(file);
  if (!rel) {
    fail(error, "not inside the git repository: " + file.string());
    return std::nullopt;
  }
  ExecResult r = git({"show", "HEAD:" + *rel});
  if (r.ok()) {
    lastError_.clear();
    return std::move(r.out);
  }
  if (r.started && !r.timedOut && isMissingInHead(r.err)) {
    lastError_.clear();
    return std::nullopt;  // new file / empty repository: no committed version, not an error
  }
  fail(error, firstLine(r.err));
  return std::nullopt;
}

bool GitProvider::revert(const fs::path& file, std::string* error) {
  auto rel = relative(file);
  if (!rel) return fail(error, "not inside the git repository: " + file.string());
  ExecResult r = git({"restore", "--source=HEAD", "--staged", "--worktree", "--", *rel});
  if (r.ok()) {
    lastError_.clear();
    return true;
  }
  const std::string first = firstLine(r.err);
  ExecResult c = git({"checkout", "HEAD", "--", *rel});  // git < 2.23 has no `restore`
  if (c.ok()) {
    lastError_.clear();
    return true;
  }
  // `restore` exists on every git that matters; its message is the useful one unless it was not a command at all.
  const bool noRestore = contains(r.err, "not a git command") || contains(r.err, "is not a git command");
  return fail(error, noRestore ? firstLine(c.err) : first);
}

}  // namespace tf
