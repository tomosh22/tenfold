#include "build/BuildRunner.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

#include "core/PathUtil.h"
#include "semantic/CompileCommands.h"
#include "semantic/XmlLite.h"

namespace tf {

namespace fs = std::filesystem;

namespace {

std::string quoteForDisplay(const std::string& a) {
  if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) return a;
  std::string q = "\"";
  for (char c : a) {
    if (c == '"') q += '\\';
    q += c;
  }
  return q + "\"";
}

BuildCommand direct(std::string exe, std::vector<std::string> args, fs::path cwd) {
  BuildCommand c;
  c.display = quoteForDisplay(exe);
  for (auto& a : args) c.display += " " + quoteForDisplay(a);
  c.exe = std::move(exe);
  c.args = std::move(args);
  c.cwd = std::move(cwd);
  return c;
}

fs::path firstExisting(const fs::path& dir, std::initializer_list<const char*> names) {
  std::error_code ec;
  for (auto n : names)
    if (fs::is_regular_file(dir / n, ec)) return dir / n;
  return {};
}

}  // namespace

namespace {

// The solution, else a lone project file in the root: what msbuild is pointed at.
fs::path msbuildTarget(const Workspace& ws) {
  auto slns = ws.solutionFiles();
  if (!slns.empty()) return slns.front();
  std::error_code ec;
  fs::path found;
  for (auto& e : fs::directory_iterator(ws.root(), ec))
    if (e.is_regular_file(ec) && e.path().extension() == ".vcxproj") found = e.path();
  return found;
}

fs::path cmakeBuildDir(const Workspace& ws, const BuildOverrides& o) {
  const auto& st = ws.settings();
  const fs::path root = ws.root();
  fs::path dir = o.buildDir;
  if (dir.empty() && !st.buildDir.empty()) dir = st.buildDir;
  if (dir.empty()) {
    // The same build tree clangd's compile database comes from, when it is a real CMake build directory.
    std::error_code ec;
    if (auto cc = CompileDatabase::find(root); cc && fs::exists(cc->parent_path() / "CMakeCache.txt", ec))
      dir = cc->parent_path();
    else dir = ws.cacheDir() / "cmake-build";
  }
  if (dir.is_relative()) dir = root / dir;
  return dir;
}

BuildCommand shellCommand(const std::string& line, const fs::path& cwd) {
  BuildCommand c;
  c.shell = line;
  c.display = line;
  c.cwd = cwd;
  return c;
}

std::string keyOf(const fs::path& p) {
  std::error_code ec;
  fs::path c = fs::weakly_canonical(p, ec);
  return pathKey((ec ? p : c).generic_string(), nativePathStyle());
}

// The .vcxproj among `projects` that lists `file` as a ClCompile item (wildcards are not expanded).
fs::path owningProject(const std::vector<fs::path>& projects, const fs::path& file) {
  const std::string want = keyOf(file);
  for (const fs::path& proj : projects) {
    auto doc = xml::parseFile(proj.string());
    if (!doc) continue;
    std::vector<const xml::Node*> stack{doc.get()};
    while (!stack.empty()) {
      const xml::Node* n = stack.back();
      stack.pop_back();
      if (n->name == "ClCompile") {
        std::string inc = n->attr("Include");
        std::replace(inc.begin(), inc.end(), '\\', '/');
        if (!inc.empty() && keyOf(proj.parent_path() / inc) == want) return proj;
      }
      for (const auto& c : n->children) stack.push_back(c.get());
    }
  }
  return {};
}

// "-o x", "-ox", "/Fox" of a compile command; empty when it names no output.
std::string outputOf(const std::vector<std::string>& args) {
  for (size_t i = 1; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "-o" && i + 1 < args.size()) return args[i + 1];
    if (a.size() > 2 && a.compare(0, 2, "-o") == 0) return a.substr(2);
    if (a.size() > 3 && (a.compare(0, 3, "/Fo") == 0 || a.compare(0, 3, "-Fo") == 0)) return a.substr(3);
  }
  return {};
}

bool detectFileBuild(const Workspace& ws, const BuildOverrides& o, BuildCommand* out, std::string* error) {
  auto fail = [&](std::string m) {
    if (error) *error = std::move(m);
    return false;
  };
  const fs::path root = ws.root();
  const auto& st = ws.settings();
  if (o.file.empty()) return fail("no file to build");
  const fs::path file = o.file.is_relative() ? root / o.file : o.file;
  const std::string name = file.filename().string();
  const std::string cfg = o.configuration.empty() ? st.vsConfiguration : o.configuration;
  const std::string plat = o.platform.empty() ? st.vsPlatform : o.platform;

  if (const fs::path msb = msbuildTarget(ws); !msb.empty()) {
    fs::path proj;
    std::string pcfg = cfg, pplat = plat;
    if (msb.extension() == ".vcxproj") {
      proj = msb;
    } else {
      const compdb::SolutionInfo info = compdb::readSolution(msb);
      std::vector<fs::path> projects;
      for (const auto& p : info.projects)
        if (p.isVcxproj()) projects.push_back(p.path);
      proj = owningProject(projects, file);
      if (!proj.empty())
        for (const auto& p : info.projects)  // the solution may map its configuration to another project one
          if (p.isVcxproj() && keyOf(p.path) == keyOf(proj))
            if (auto m = info.mapping(p, {cfg, plat}); m && !m->configuration.empty()) {
              pcfg = m->configuration;
              pplat = m->platform;
            }
    }
    if (proj.empty()) return fail("no project of " + msb.filename().string() + " compiles " + name);
    *out = direct("msbuild",
                  {proj.string(), "/t:ClCompile", "/p:SelectedFiles=" + file.string(), "/p:Configuration=" + pcfg,
                   "/p:Platform=" + pplat},
                  root);
    return true;
  }

  std::string err;
  const auto db = CompileDatabase::find(root);
  if (!db) return fail("no compile database: cannot build " + name + " on its own");
  const std::string want = keyOf(file);
  for (const CompileCommand& cc : CompileDatabase::load(*db, &err)) {
    fs::path f = cc.file;
    if (f.is_relative()) f = cc.directory / f;
    if (cc.arguments.empty() || keyOf(f) != want) continue;
    const std::string obj = outputOf(cc.arguments);
    std::error_code ec;
    if (!obj.empty() && fs::exists(cc.directory / "build.ninja", ec)) {
      *out = direct("ninja", {"-C", cc.directory.string(), obj}, root);
    } else {
      *out =
          direct(cc.arguments[0], std::vector<std::string>(cc.arguments.begin() + 1, cc.arguments.end()), cc.directory);
    }
    return true;
  }
  return fail("no compile command for " + name + " in " + db->filename().string());
}

}  // namespace

bool detectBuildCommand(const Workspace& ws, const BuildOverrides& o, BuildCommand* out, std::string* error) {
  if (o.kind == BuildOverrides::Kind::CurrentFile) return detectFileBuild(ws, o, out, error);
  const bool rebuild = o.kind == BuildOverrides::Kind::Rebuild;
  const fs::path root = ws.root();
  const auto& st = ws.settings();
  const std::string cfg = o.configuration.empty() ? st.vsConfiguration : o.configuration;
  const std::string plat = o.platform.empty() ? st.vsPlatform : o.platform;

  if (rebuild) {
    const std::string& rb = !o.rebuildCommand.empty() ? o.rebuildCommand : st.rebuildCommand;
    if (!rb.empty()) {
      *out = shellCommand(rb, root);
      return true;
    }
    if (!o.command.empty() || !st.buildCommand.empty()) {
      if (error) *error = "cannot rebuild a custom build: set \"rebuildCommand\" in .tenfold/workspace.json";
      return false;
    }
  } else {
    const std::string& custom = !o.command.empty() ? o.command : st.buildCommand;
    if (!custom.empty()) {
      *out = shellCommand(custom, root);
      return true;
    }
  }
  if (const fs::path msb = msbuildTarget(ws); !msb.empty()) {
    std::vector<std::string> args{msb.string()};
    if (rebuild) args.push_back("/t:Rebuild");
    args.insert(args.end(), {"/m", "/p:Configuration=" + cfg, "/p:Platform=" + plat});
    *out = direct("msbuild", std::move(args), root);
    return true;
  }
  if (ws.hasCMake()) {
    std::vector<std::string> args{"--build", cmakeBuildDir(ws, o).string(), "--config", cfg};
    if (rebuild) args.push_back("--clean-first");
    *out = direct("cmake", std::move(args), root);
    return true;
  }
  if (!firstExisting(root, {"Makefile", "makefile", "GNUmakefile"}).empty()) {
    unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const std::string jobs = "-j" + std::to_string(cores);
    if (rebuild) *out = shellCommand("make clean && make " + jobs, root);
    else *out = direct("make", {jobs}, root);
    return true;
  }
  if (error) *error = "no build command: set \"buildCommand\" in .tenfold/workspace.json";
  return false;
}

// ---------------------------------------------------------------------------

namespace {
struct Callbacks {
  BuildRunner::Lines onLines;
  BuildRunner::Done onDone;
};
}  // namespace

struct BuildRunner::Job {
  Process proc;
  std::thread reader;
  // After cancel() a second thread keeps re-sending the kill until the output pipe reaches EOF. A single
  // kill(-pgid) can miss a child the shell was in the middle of forking (fork is slow on macOS: the build
  // "started" line is seen, the cancel lands during `sleep 120 &`'s fork, the new child escapes the signal and
  // keeps the pipe open for its whole life). killMu orders the final kill against the leader being reaped, so
  // a recycled process-group id is never signalled.
  std::thread killer;
  std::mutex killMu;
  bool eof = false;  // guarded by killMu
  std::atomic<bool> cancelled{false};
  std::atomic<bool> finished{false};
};

BuildRunner::BuildRunner(Post post) : post_(std::move(post)) {}

BuildRunner::~BuildRunner() {
  cancel();
  reap(true);
}

void BuildRunner::reap(bool all) {
  std::vector<std::shared_ptr<Job>> take;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (all && current_) {
      retired_.push_back(std::move(current_));
      current_.reset();
    }
    for (auto it = retired_.begin(); it != retired_.end();) {
      if (all || (*it)->finished) {
        take.push_back(std::move(*it));
        it = retired_.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto& j : take) {
    if (j->reader.joinable()) j->reader.join();
    if (j->killer.joinable()) j->killer.join();
  }
}

void BuildRunner::cancel() {
  std::lock_guard<std::mutex> lk(mu_);
  if (!current_) return;
  current_->cancelled = true;
  current_->proc.kill();  // the whole tree: process group / job object
  if (!current_->killer.joinable()) {
    Job* j = current_.get();
    j->killer = std::thread([j] {
      while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::lock_guard<std::mutex> lk(j->killMu);
        if (j->eof) return;
        j->proc.kill();
      }
    });
  }
}

bool BuildRunner::running() const {
  std::lock_guard<std::mutex> lk(mu_);
  return current_ && !current_->finished;
}

bool BuildRunner::start(const BuildCommand& cmd, Lines onLines, Done onDone, std::string* error) {
  cancel();
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (current_) retired_.push_back(std::move(current_));
    current_.reset();
  }
  reap(false);

  auto job = std::make_shared<Job>();
  ProcessOptions po;
  po.mergeStderr = true;
  po.processTree = true;
  std::string exe = cmd.exe;
  std::vector<std::string> args = cmd.args;
  if (!cmd.shell.empty()) {
#ifdef _WIN32
    exe = "cmd.exe";
    po.rawCommandLine = "cmd.exe /s /c \"" + cmd.shell + "\"";  // /s: strip exactly the outer quotes
#else
    exe = "/bin/sh";
    args = {"-c", cmd.shell};
#endif
  }
  std::string err;
  if (!job->proc.start(exe, args, cmd.cwd, {}, &err, {}, po)) {
    if (error) *error = "cannot start '" + cmd.display + "': " + err;
    return false;
  }
  job->proc.closeStdin();
  {
    std::lock_guard<std::mutex> lk(mu_);
    current_ = job;
  }

  auto cb = std::make_shared<Callbacks>(Callbacks{std::move(onLines), std::move(onDone)});
  Post post = post_;
  const auto t0 = std::chrono::steady_clock::now();
  Job* j = job.get();
  job->reader = std::thread([j, cb, post, t0] {
    std::string pending;
    char buf[16384];
    auto flush = [&](std::vector<std::string> lines) {
      if (lines.empty() || j->cancelled) return;  // a cancelled build delivers no further output
      post([cb, lines = std::move(lines)]() mutable {
        if (cb->onLines) cb->onLines(std::move(lines));
      });
    };
    while (true) {
      long n = j->proc.read(buf, sizeof buf);
      if (n <= 0) break;
      pending.append(buf, static_cast<size_t>(n));
      std::vector<std::string> lines;
      size_t start = 0, nl;
      while ((nl = pending.find('\n', start)) != std::string::npos) {
        size_t end = nl;
        if (end > start && pending[end - 1] == '\r') --end;
        lines.emplace_back(pending, start, end - start);
        start = nl + 1;
      }
      pending.erase(0, start);
      flush(std::move(lines));
    }
    if (!pending.empty()) {
      if (pending.back() == '\r') pending.pop_back();
      flush({std::move(pending)});
    }
    {
      std::lock_guard<std::mutex> lk(j->killMu);
      j->eof = true;  // before the leader is reaped (and its process-group id becomes reusable)
    }
    BuildResult r;
    r.exitCode = j->proc.wait(-1);
    r.cancelled = j->cancelled;
    r.elapsedMs = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    post([cb, r] {
      if (cb->onDone) cb->onDone(r);
    });
    j->finished = true;
  });
  return true;
}

}  // namespace tf
