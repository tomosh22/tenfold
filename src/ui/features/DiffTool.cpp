#include "ui/features/DiffTool.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <system_error>

namespace tf::ui {

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
constexpr char kPathSep = ';';
#else
constexpr char kPathSep = ':';
#endif

bool executableFile(const fs::path& p) {
  std::error_code ec;
  if (!fs::is_regular_file(p, ec)) return false;
#ifdef _WIN32
  return true;
#else
  const auto perms = fs::status(p, ec).permissions();
  using P = fs::perms;
  return (perms & (P::owner_exec | P::group_exec | P::others_exec)) != P::none;
#endif
}

// `name` in the directories of `pathVar`; on Windows also with the usual executable suffixes.
std::string findOnPath(const std::string& name, const std::string& pathVar) {
  std::vector<std::string> names{name};
#ifdef _WIN32
  if (fs::path(name).extension().empty())
    for (const char* ext : {".exe", ".cmd", ".bat"}) names.push_back(name + ext);
#endif
  size_t from = 0;
  while (from <= pathVar.size()) {
    size_t to = pathVar.find(kPathSep, from);
    if (to == std::string::npos) to = pathVar.size();
    std::string dir = pathVar.substr(from, to - from);
    from = to + 1;
    if (dir.empty()) continue;
    if (dir.size() >= 2 && dir.front() == '"' && dir.back() == '"') dir = dir.substr(1, dir.size() - 2);
    for (const std::string& n : names) {
      const fs::path p = fs::path(dir) / n;
      if (executableFile(p)) return p.string();
    }
  }
  return {};
}

bool hasDirectory(const std::string& s) {
  return s.find('/') != std::string::npos || s.find('\\') != std::string::npos;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
  for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size()) s.replace(at, from.size(), to);
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// A settings value that is wrapped in double quotes loses the outer pair, so `"{base}" "{current}"` arrives as
// `{base}" "{current}`: quotes that touch text on the outside of the string are the sign. Give the pair back.
std::string repairLostQuotes(const std::string& s) {
  if (s.empty() || s.front() == '"' || s.back() == '"') return s;
  const size_t first = s.find('"'), last = s.rfind('"');
  if (first == std::string::npos || first == 0 || std::isspace(static_cast<unsigned char>(s[first - 1]))) return s;
  if (last + 1 >= s.size() || std::isspace(static_cast<unsigned char>(s[last + 1]))) return s;
  return "\"" + s + "\"";
}

fs::path uniqueDir(const fs::path& root) {
  static std::mutex mu;
  static int counter = 0;
  std::lock_guard<std::mutex> lock(mu);
  const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
  return root / (std::to_string(stamp) + "-" + std::to_string(counter++));
}

bool writeText(const fs::path& p, const std::string& text, std::string* error) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << text;
  out.flush();
  if (!out) {
    if (error) *error = "cannot write " + p.string();
    return false;
  }
  return true;
}

// Programs that may still run when the editor quits are parked here for good: destroying a Process kills its child.
std::vector<std::unique_ptr<Process>>& graveyard() {
  static auto* g = new std::vector<std::unique_ptr<Process>>();
  return *g;
}

}  // namespace

DiffTool::DiffTool() {
  std::error_code ec;
  root_ = fs::temp_directory_path(ec) / "tenfold-diff";
}

DiffTool::~DiffTool() {
  for (Running& r : running_) {
    if (r.proc->running()) {
      graveyard().push_back(std::move(r.proc));  // keeps running; its temp files stay for the next sweep
    } else {
      std::error_code ec;
      fs::remove_all(r.dir, ec);
    }
  }
}

bool DiffTool::isP4Merge(const std::string& exe) {
  const size_t slash = exe.find_last_of("/\\");
  std::string n = lower(slash == std::string::npos ? exe : exe.substr(slash + 1));
  if (n.size() > 4 && n.compare(n.size() - 4, 4, ".exe") == 0) n.resize(n.size() - 4);
  return n == "p4merge";
}

std::vector<std::string> DiffTool::splitArgs(const std::string& text) {
  const std::string s = repairLostQuotes(text);
  std::vector<std::string> out;
  std::string cur;
  bool quoted = false, any = false;
  for (char c : s) {
    if (c == '"') {
      quoted = !quoted;
      any = true;
    } else if (!quoted && std::isspace(static_cast<unsigned char>(c))) {
      if (any) out.push_back(cur);
      cur.clear();
      any = false;
    } else {
      cur += c;
      any = true;
    }
  }
  if (any) out.push_back(cur);
  return out;
}

std::string DiffTool::resolveTool(const std::string& configured, const std::string& pathVar,
                                  const std::string& programFiles, std::string* error) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    return std::string();
  };
  if (!configured.empty()) {
    if (hasDirectory(configured)) {
      if (executableFile(configured)) return configured;
      return fail("diff tool '" + configured + "' not found (vcs.diffTool)");
    }
    const std::string found = findOnPath(configured, pathVar);
    return found.empty() ? fail("diff tool '" + configured + "' not found in PATH (vcs.diffTool)") : found;
  }
#ifdef _WIN32
  std::string found = findOnPath("p4merge.exe", pathVar);
  if (found.empty() && !programFiles.empty()) {
    const fs::path p = fs::path(programFiles) / "Perforce" / "p4merge.exe";
    if (executableFile(p)) found = p.string();
  }
#else
  (void)programFiles;
  const std::string found = findOnPath("p4merge", pathVar);
#endif
  return found.empty() ? fail("no diff tool found: install p4merge or set vcs.diffTool") : found;
}

std::string DiffTool::resolveTool(const std::string& configured, std::string* error) {
  const char* path = std::getenv("PATH");
  const char* pf = std::getenv("ProgramFiles");
  return resolveTool(configured, path ? path : "", pf ? pf : "", error);
}

void DiffTool::sweepOld() {
  swept_ = true;
  std::error_code ec;
  if (!fs::is_directory(root_, ec)) return;
  const auto now = fs::file_time_type::clock::now();
  for (const auto& e : fs::directory_iterator(root_, ec)) {
    const auto t = e.last_write_time(ec);
    if (!ec && now - t > std::chrono::hours(24)) fs::remove_all(e.path(), ec);
  }
}

DiffTool::Launched DiffTool::launch(const Request& r, const Config& c) {
  Launched out;
  std::string err;
  if (usePathOverride_) {
    const char* pf = std::getenv("ProgramFiles");
    out.exe = resolveTool(c.tool, pathOverride_, pf ? pf : "", &err);
  } else {
    out.exe = resolveTool(c.tool, &err);
  }
  if (out.exe.empty()) {
    out.error = err;
    return out;
  }
  if (!swept_) sweepOld();
  const fs::path dir = uniqueDir(root_);
  const std::string name = r.file.filename().string();
  out.baseFile = dir / "base" / name;
  out.currentFile = dir / "current" / name;
  if (!writeText(out.baseFile, r.baseText, &out.error) || !writeText(out.currentFile, r.currentText, &out.error)) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    return out;
  }
  const std::string baseTitle = r.title + r.baseLabel, currentTitle = r.title + r.currentLabel;
  std::string tmpl = c.args;
  const bool plain = tmpl.empty() || splitArgs(tmpl) == splitArgs(defaultArgs());
  if (tmpl.empty()) tmpl = defaultArgs();
  std::vector<std::string> args;
  if (plain && isP4Merge(out.exe)) args = {"-nl", "{baseTitle}", "-nr", "{currentTitle}"};
  for (std::string& t : splitArgs(tmpl)) args.push_back(std::move(t));
  for (std::string& a : args) {
    replaceAll(a, "{baseTitle}", baseTitle);
    replaceAll(a, "{currentTitle}", currentTitle);
    replaceAll(a, "{base}", out.baseFile.string());
    replaceAll(a, "{current}", out.currentFile.string());
    replaceAll(a, "{file}", r.file.string());
  }
  out.args = args;
  auto proc = std::make_unique<Process>();
  if (!proc->start(out.exe, args, r.file.parent_path(), {}, &err)) {
    out.error = "cannot start " + out.exe + ": " + err;
    std::error_code ec;
    fs::remove_all(dir, ec);
    return out;
  }
  proc->closeStdin();
  running_.push_back({std::move(proc), dir, std::chrono::steady_clock::now()});
  out.ok = true;
  return out;
}

void DiffTool::tick() {
  for (size_t i = running_.size(); i-- > 0;) {
    Running& r = running_[i];
    if (r.proc->running()) continue;
    const int code = r.proc->wait(0);
    const auto ran =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - r.started);
    if ((code == 127 || code == 126) && onError)
      onError("diff tool could not be started (exit code " + std::to_string(code) + ")");
    if (ran.count() >= keepMs_ || code == 127 || code == 126) {
      std::error_code ec;
      fs::remove_all(r.dir, ec);
    }
    running_.erase(running_.begin() + static_cast<long>(i));
  }
}

}  // namespace tf::ui
