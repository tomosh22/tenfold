#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "build/AnsiStrip.h"
#include "build/BuildRunner.h"
#include "build/ErrorParser.h"
#include "core/PathUtil.h"
#include "core/Platform.h"
#include "lsp/Process.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

// ---- parser helpers ----

std::vector<BuildMessage> parse(const std::vector<std::string>& lines, const std::string& cwd = "/work") {
  ErrorParser p(cwd);
  return p.parseAll(lines);
}

BuildMessage parseOne(const std::string& line, const std::string& cwd = "/work") {
  auto v = parse({line}, cwd);
  if (v.size() != 1) {
    tftest::fail(__FILE__, __LINE__, "expected exactly one message for: " + line);
    return {};
  }
  return v[0];
}

void expectMsg(const BuildMessage& m, const std::string& file, int line, int col, BuildSeverity sev,
               const std::string& code, const std::string& text) {
  CHECK_EQ(m.file, file);
  CHECK_EQ(m.line, line);
  CHECK_EQ(m.col, col);
  CHECK(m.severity == sev);
  CHECK_EQ(m.code, code);
  CHECK_EQ(m.text, text);
}

// ---- runner helpers ----

struct TempDir {
  fs::path path;
  TempDir() {
    static int n = 0;
    path = fs::temp_directory_path() /
           ("tenfold_build_" + std::to_string(tf::currentProcessId()) + "_" + std::to_string(n++));
    fs::remove_all(path);
    fs::create_directories(path);
    std::error_code ec;
    path = fs::canonical(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

// A main-thread stand-in: posted callbacks are queued and run by pumpUntil.
struct Pump {
  std::mutex mu;
  std::condition_variable cv;
  std::vector<std::function<void()>> q;
  BuildRunner::Post post() {
    return [this](std::function<void()> fn) {
      {
        std::lock_guard<std::mutex> lk(mu);
        q.push_back(std::move(fn));
      }
      cv.notify_all();
    };
  }
  bool until(const std::function<bool()>& pred, int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
      std::vector<std::function<void()>> batch;
      {
        std::unique_lock<std::mutex> lk(mu);
        if (q.empty())
          cv.wait_until(lk, std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(20)));
        batch.swap(q);
      }
      for (auto& f : batch) f();
      if (pred()) return true;
      if (std::chrono::steady_clock::now() >= deadline) return false;
    }
  }
};

BuildCommand shellCmd(const std::string& posix, const std::string& win, const fs::path& cwd = {}) {
  BuildCommand c;
#ifdef _WIN32
  c.shell = win;
#else
  c.shell = posix;
#endif
  (void)posix;
  (void)win;
  c.cwd = cwd;
  c.display = c.shell;
  return c;
}

// Prints "started", then keeps a long-running grandchild alive that holds the output pipe.
BuildCommand sleeperCmd(const std::string& marker) {
  return shellCmd("echo " + marker + "; sleep 120 & wait", "echo " + marker + "& ping -n 120 127.0.0.1 >nul");
}

}  // namespace

// ---------------------------------------------------------------------------
// ErrorParser
// ---------------------------------------------------------------------------

TEST(build_parse_msvc) {
  expectMsg(parseOne(R"(C:\path\file.cpp(12,5): error C2065: 'x': undeclared identifier)"), "C:/path/file.cpp", 12, 5,
            BuildSeverity::Error, "C2065", "'x': undeclared identifier");
  // Column-less, with a warning.
  expectMsg(parseOne(R"(C:\src\a.cpp(12): warning C4100: 'p': unreferenced formal parameter)"), "C:/src/a.cpp", 12, 0,
            BuildSeverity::Warning, "C4100", "'p': unreferenced formal parameter");
  // Old style with a space before the colon, relative path.
  expectMsg(parseOne("foo\\bar.cpp(7) : error C2143: syntax error: missing ';' before '}'"), "/work/foo/bar.cpp", 7, 0,
            BuildSeverity::Error, "C2143", "syntax error: missing ';' before '}'");
  // Range form (line,col,endLine,endCol).
  expectMsg(parseOne(R"(C:\a.cpp(3,4,5,6): error C1234: ranged)"), "C:/a.cpp", 3, 4, BuildSeverity::Error, "C1234",
            "ranged");
  // fatal error, note.
  expectMsg(parseOne(R"(C:\a.cpp(1): fatal error C1083: Cannot open include file: 'x.h': No such file or directory)"),
            "C:/a.cpp", 1, 0, BuildSeverity::Error, "C1083",
            "Cannot open include file: 'x.h': No such file or directory");
  expectMsg(parseOne(R"(C:\a.cpp(9,1): note: see declaration of 'f')"), "C:/a.cpp", 9, 1, BuildSeverity::Note, "",
            "see declaration of 'f'");
  // No code at all (custom MSBuild task).
  expectMsg(parseOne(R"(C:\a.targets(4,5): error : something broke)"), "C:/a.targets", 4, 5, BuildSeverity::Error, "",
            "something broke");
}

TEST(build_parse_msvc_odd_paths) {
  // Spaces and parentheses in the path; MSBuild node prefix and project suffix.
  expectMsg(
      parseOne(
          R"(  1>C:\Program Files (x86)\My Proj\src (old)\main.cpp(7,3): error C2143: syntax error [C:\proj\app.vcxproj])"),
      "C:/Program Files (x86)/My Proj/src (old)/main.cpp", 7, 3, BuildSeverity::Error, "C2143", "syntax error");
  expectMsg(parseOne(R"(12>C:\p\a.cpp(1,1): warning C4996: 'f': was declared deprecated [C:\p\a b.vcxproj])"),
            "C:/p/a.cpp", 1, 1, BuildSeverity::Warning, "C4996", "'f': was declared deprecated");
  expectMsg(parseOne(R"(1:7>C:\p\a.cpp(2,1): error C2059: x [C:\p\a.vcxproj])"), "C:/p/a.cpp", 2, 1,
            BuildSeverity::Error, "C2059", "x");
  // UTF-8 path.
  expectMsg(parseOne("C:\\проект\\файл.cpp(3,1): error C1189: #error:  boom"), "C:/проект/файл.cpp", 3, 1,
            BuildSeverity::Error, "C1189", "#error:  boom");
  // Relative path with .. resolved against a Windows cwd.
  expectMsg(parseOne(R"(..\src\a.cpp(3): error C2065: 'q': undeclared identifier)", "C:/proj/build"),
            "C:/proj/src/a.cpp", 3, 0, BuildSeverity::Error, "C2065", "'q': undeclared identifier");
  // The path itself contains "(1)" before the real location.
  expectMsg(parseOne(R"(C:\dir(1)\a.cpp(5,2): error C2065: z)"), "C:/dir(1)/a.cpp", 5, 2, BuildSeverity::Error, "C2065",
            "z");
}

TEST(build_parse_msvc_tool_messages) {
  auto m = parseOne("LINK : fatal error LNK1104: cannot open file 'foo.lib'");
  expectMsg(m, "", 0, 0, BuildSeverity::Error, "LNK1104", "cannot open file 'foo.lib'");
  m = parseOne(
      R"x(main.obj : error LNK2019: unresolved external symbol "void __cdecl f(void)" (?f@@YAXXZ) referenced in function main)x");
  CHECK_EQ(m.file, "");
  CHECK_EQ(m.code, "LNK2019");
  CHECK(m.text.starts_with("main.obj: unresolved external symbol"));
  m = parseOne("  2>app.exe : fatal error LNK1120: 1 unresolved externals [C:\\p\\app.vcxproj]");
  CHECK_EQ(m.code, "LNK1120");
  CHECK_EQ(m.text, "app.exe: 1 unresolved externals");
  expectMsg(parseOne("cl : Command line warning D9002 : ignoring unknown option '/foo'"), "", 0, 0,
            BuildSeverity::Warning, "D9002", "ignoring unknown option '/foo'");
  expectMsg(parseOne("MSBUILD : error MSB1009: Project file does not exist."), "", 0, 0, BuildSeverity::Error,
            "MSB1009", "Project file does not exist.");
  expectMsg(parseOne(R"(C:\p\a.vcxproj(45,5): error MSB3073: The command "x" exited with code 1.)"), "C:/p/a.vcxproj",
            45, 5, BuildSeverity::Error, "MSB3073", "The command \"x\" exited with code 1.");
}

TEST(build_parse_nmake_and_make) {
  expectMsg(parseOne("NMAKE : fatal error U1077: 'cl.exe' : return code '0x2'"), "", 0, 0, BuildSeverity::Error,
            "U1077", "'cl.exe' : return code '0x2'");
  expectMsg(parseOne("make: *** [Makefile:5: all] Error 1"), "", 0, 0, BuildSeverity::Error, "make",
            "[Makefile:5: all] Error 1");
  expectMsg(parseOne("make[2]: *** [CMakeFiles/x.dir/build.make:70: x.o] Error 1"), "", 0, 0, BuildSeverity::Error,
            "make", "[CMakeFiles/x.dir/build.make:70: x.o] Error 1");
  expectMsg(parseOne("gmake: *** No rule to make target 'foo'.  Stop."), "", 0, 0, BuildSeverity::Error, "make",
            "No rule to make target 'foo'.  Stop.");
  expectMsg(parseOne("Makefile:12: *** missing separator.  Stop."), "/work/Makefile", 12, 0, BuildSeverity::Error,
            "make", "missing separator.  Stop.");
}

TEST(build_parse_gcc_clang) {
  expectMsg(parseOne("src/main.cpp:12:5: error: 'x' was not declared in this scope"), "/work/src/main.cpp", 12, 5,
            BuildSeverity::Error, "", "'x' was not declared in this scope");
  expectMsg(parseOne("/abs/main.cpp:1:10: fatal error: foo.h: No such file or directory"), "/abs/main.cpp", 1, 10,
            BuildSeverity::Error, "", "foo.h: No such file or directory");
  expectMsg(parseOne("/abs/a.cpp:4:9: warning: unused variable 'i' [-Wunused-variable]"), "/abs/a.cpp", 4, 9,
            BuildSeverity::Warning, "-Wunused-variable", "unused variable 'i'");
  expectMsg(parseOne("/abs/a.cpp:4:9: note: declared here"), "/abs/a.cpp", 4, 9, BuildSeverity::Note, "",
            "declared here");
  expectMsg(parseOne("a.cpp:7: error: no column here"), "/work/a.cpp", 7, 0, BuildSeverity::Error, "",
            "no column here");
  expectMsg(parseOne("../src/x.cpp:1:1: error: y", "/work/build"), "/work/src/x.cpp", 1, 1, BuildSeverity::Error, "",
            "y");
  // Spaces and parentheses in a GCC-style path, UTF-8.
  expectMsg(parseOne("/tmp/my proj/a (1).cpp:4:2: error: z"), "/tmp/my proj/a (1).cpp", 4, 2, BuildSeverity::Error, "",
            "z");
  expectMsg(parseOne("/home/ü/日本.cpp:3:1: error: x"), "/home/ü/日本.cpp", 3, 1, BuildSeverity::Error, "", "x");
  // Windows drive letters in GCC-style paths (mingw).
  expectMsg(parseOne("C:/x/y.cpp:3:1: error: expected ';' before '}' token"), "C:/x/y.cpp", 3, 1, BuildSeverity::Error,
            "", "expected ';' before '}' token");
  expectMsg(parseOne(R"(C:\x\y.cpp:3:1: warning: w [-Wall])"), "C:/x/y.cpp", 3, 1, BuildSeverity::Warning, "-Wall",
            "w");
  // Driver / linker messages without a location.
  expectMsg(parseOne("clang: error: linker command failed with exit code 1 (use -v to see invocation)"), "", 0, 0,
            BuildSeverity::Error, "", "linker command failed with exit code 1 (use -v to see invocation)");
  expectMsg(parseOne("collect2: error: ld returned 1 exit status"), "", 0, 0, BuildSeverity::Error, "",
            "ld returned 1 exit status");
  // ANSI colours (gcc -fdiagnostics-color=always).
  expectMsg(parseOne("\x1b[01m\x1b[Ksrc/a.cpp:3:5:\x1b[m\x1b[K \x1b[01;31m\x1b[Kerror: \x1b[m\x1b[Kfoo"),
            "/work/src/a.cpp", 3, 5, BuildSeverity::Error, "", "foo");
  // CRLF.
  expectMsg(parseOne("a.cpp:1:1: error: crlf\r"), "/work/a.cpp", 1, 1, BuildSeverity::Error, "", "crlf");
}

TEST(build_parse_include_chains) {
  auto v =
      parse({"In file included from /w/a.cpp:3:", "                 from /w/b.cpp:5:", "/w/h.h:10:2: error: boom"});
  REQUIRE(v.size() == 1);
  expectMsg(v[0], "/w/h.h", 10, 2, BuildSeverity::Error, "", "boom");
  REQUIRE(v[0].notes.size() == 2);
  expectMsg(v[0].notes[0], "/w/a.cpp", 3, 0, BuildSeverity::Note, "", "included from here");
  expectMsg(v[0].notes[1], "/w/b.cpp", 5, 0, BuildSeverity::Note, "", "included from here");

  // Comma-terminated form with columns and a Windows path; the chain belongs to the next message only.
  v = parse({"In file included from C:/w/a.h:3:1,", "                 from C:/w/b.cpp:5:", "C:/w/a.h:7:1: warning: w",
             "C:/w/c.cpp:1:1: error: later"});
  REQUIRE(v.size() == 2);
  REQUIRE(v[0].notes.size() == 2);
  CHECK_EQ(v[0].notes[0].file, "C:/w/a.h");
  CHECK_EQ(v[0].notes[0].line, 3);
  CHECK_EQ(v[0].notes[0].col, 1);
  CHECK(v[1].notes.empty());
  // The chain lines themselves are not messages.
  CHECK(parse({"In file included from /w/a.cpp:3:"}).empty());
}

TEST(build_parse_ignores_noise_and_dedupes) {
  auto v = parse({"Compiling...", "  Generating code", "Build succeeded.", "    0 Error(s)", "    0 Warning(s)",
                  "1 error generated.", "ninja: build stopped: subcommand failed.", "FAILED: CMakeFiles/x.dir/a.o",
                  "[ 50%] Building CXX object a.o", "/usr/bin/ld: cannot find -lfoo",
                  "In function 'int main()':", "a.cpp: In function 'int main()':", ""});
  CHECK(v.empty());
  // MSBuild prints every diagnostic again in its summary.
  v = parse({R"(  1>C:\p\a.cpp(1,2): error C2065: 'x': undeclared identifier [C:\p\a.vcxproj])", "Build FAILED.",
             R"(C:\p\a.cpp(1,2): error C2065: 'x': undeclared identifier [C:\p\a.vcxproj])",
             R"(C:\p\a.cpp(9,2): error C2065: 'x': undeclared identifier [C:\p\a.vcxproj])"});
  CHECK_EQ(v.size(), size_t(2));
}

// ---------------------------------------------------------------------------
// BuildRunner
// ---------------------------------------------------------------------------

TEST(build_runner_streams_merged_output_and_exit_code) {
  Pump pump;
  BuildRunner runner(pump.post());
  std::vector<std::string> lines;
  BuildResult res;
  bool done = false;
  std::string err;
  auto cmd = shellCmd("echo one; echo two >&2; echo three; printf 'tail'; exit 2",
                      "echo one& echo two>&2& echo three& exit 2");
  REQUIRE(runner.start(
      cmd, [&](std::vector<std::string> l) { lines.insert(lines.end(), l.begin(), l.end()); },
      [&](const BuildResult& r) {
        res = r;
        done = true;
      },
      &err));
  REQUIRE(pump.until([&] { return done; }, 20000));
  CHECK_EQ(res.exitCode, 2);
  CHECK(!res.cancelled);
  CHECK(!res.ok());
  CHECK(res.elapsedMs >= 0 && res.elapsedMs < 20000);
  // stdout and stderr in write order; CR stripped.
  REQUIRE(lines.size() >= 3);
  CHECK_EQ(lines[0], "one");
  CHECK_EQ(lines[1], "two");
  CHECK_EQ(lines[2], "three");
#ifndef _WIN32
  REQUIRE(lines.size() == 4);
  CHECK_EQ(lines[3], "tail");  // an unterminated last line is delivered at EOF
#endif
  CHECK(!runner.running() || pump.until([&] { return !runner.running(); }, 5000));
}

TEST(build_runner_start_failure_reports) {
  Pump pump;
  BuildRunner runner(pump.post());
  BuildCommand c;
  c.exe = "tenfold-no-such-build-tool-xyz";
  c.cwd = fs::temp_directory_path();
  c.display = c.exe;
  bool done = false;
  BuildResult res;
  std::string err;
  bool started = runner.start(
      c, {},
      [&](const BuildResult& r) {
        res = r;
        done = true;
      },
      &err);
  // POSIX forks first and fails in the child (exit 127); Windows fails in CreateProcess.
  if (started) {
    REQUIRE(pump.until([&] { return done; }, 10000));
    CHECK(res.exitCode != 0);
  } else {
    CHECK(!err.empty());
  }
}

TEST(build_runner_cancel_kills_process_tree) {
  Pump pump;
  BuildRunner runner(pump.post());
  std::vector<std::string> lines;
  BuildResult res;
  bool done = false;
  std::string err;
  REQUIRE(runner.start(
      sleeperCmd("started"), [&](std::vector<std::string> l) { lines.insert(lines.end(), l.begin(), l.end()); },
      [&](const BuildResult& r) {
        res = r;
        done = true;
      },
      &err));
  REQUIRE(pump.until([&] { return !lines.empty(); }, 20000));
  CHECK(runner.running());
  // The grandchild keeps the output pipe open: the build cannot finish by itself.
  CHECK(!pump.until([&] { return done; }, 300));
  auto t0 = std::chrono::steady_clock::now();
  runner.cancel();
  // done only fires at EOF, which needs the grandchild to be gone too.
  REQUIRE(pump.until([&] { return done; }, 20000));
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(15));
  CHECK(res.cancelled);
  CHECK(!res.ok());
  CHECK(pump.until([&] { return !runner.running(); }, 5000));
}

TEST(build_runner_restart_cancels_previous) {
  Pump pump;
  BuildRunner runner(pump.post());
  std::vector<std::string> aLines, bLines;
  BuildResult aRes, bRes;
  bool aDone = false, bDone = false;
  std::string err;
  REQUIRE(runner.start(
      sleeperCmd("A-started"), [&](std::vector<std::string> l) { aLines.insert(aLines.end(), l.begin(), l.end()); },
      [&](const BuildResult& r) {
        aRes = r;
        aDone = true;
      },
      &err));
  REQUIRE(pump.until([&] { return !aLines.empty(); }, 20000));
  REQUIRE(runner.start(
      shellCmd("echo B-out", "echo B-out"),
      [&](std::vector<std::string> l) { bLines.insert(bLines.end(), l.begin(), l.end()); },
      [&](const BuildResult& r) {
        bRes = r;
        bDone = true;
      },
      &err));
  REQUIRE(pump.until([&] { return aDone && bDone; }, 20000));
  CHECK(aRes.cancelled);
  CHECK(!bRes.cancelled);
  CHECK_EQ(bRes.exitCode, 0);
  CHECK(bRes.ok());
  REQUIRE(bLines.size() == 1);
  CHECK_EQ(bLines[0], "B-out");
  CHECK_EQ(aLines.size(), size_t(1));  // nothing from A after it was replaced
}

// ---------------------------------------------------------------------------
// detectBuildCommand
// ---------------------------------------------------------------------------

TEST(build_detect_command) {
  TempDir td;
  Workspace ws(td.path);
  BuildCommand c;
  std::string err;
  CHECK(!detectBuildCommand(ws, {}, &c, &err));
  CHECK(!err.empty());

  writeFile(td.path / "Makefile", "all:\n");
  REQUIRE(detectBuildCommand(ws, {}, &c, &err));
  CHECK_EQ(c.exe, "make");
  CHECK(c.args.size() == 1 && c.args[0].starts_with("-j"));

  writeFile(td.path / "CMakeLists.txt", "project(x)\n");
  ws.settings().vsConfiguration = "Release";
  ws.settings().buildDir = "out-dir";
  REQUIRE(detectBuildCommand(ws, {}, &c, &err));
  CHECK_EQ(c.exe, "cmake");
  REQUIRE(c.args.size() == 4);
  CHECK_EQ(c.args[0], "--build");
  CHECK_EQ(fs::path(c.args[1]), td.path / "out-dir");
  CHECK_EQ(c.args[2], "--config");
  CHECK_EQ(c.args[3], "Release");

  writeFile(td.path / "App.sln", "");
  ws.settings().vsPlatform = "ARM64";
  BuildOverrides o;
  o.configuration = "Debug";
  REQUIRE(detectBuildCommand(ws, o, &c, &err));
  CHECK_EQ(c.exe, "msbuild");
  REQUIRE(c.args.size() == 4);
  CHECK_EQ(fs::path(c.args[0]).filename(), "App.sln");
  CHECK_EQ(c.args[1], "/m");
  CHECK_EQ(c.args[2], "/p:Configuration=Debug");
  CHECK_EQ(c.args[3], "/p:Platform=ARM64");

  // A custom command wins over everything; the setting is used when there is no override.
  ws.settings().buildCommand = "ninja -C out";
  REQUIRE(detectBuildCommand(ws, {}, &c, &err));
  CHECK_EQ(c.shell, "ninja -C out");
  o.command = "custom.sh --x";
  REQUIRE(detectBuildCommand(ws, o, &c, &err));
  CHECK_EQ(c.shell, "custom.sh --x");
  CHECK_EQ(c.cwd, td.path);
}

TEST(build_settings_roundtrip) {
  TempDir td;
  {
    Workspace ws(td.path);
    ws.settings().buildCommand = "make all";
    ws.settings().buildDir = "bld";
    std::string err;
    REQUIRE(ws.saveSettings(&err));
  }
  Workspace ws(td.path);
  REQUIRE(ws.loadSettings());
  CHECK_EQ(ws.settings().buildCommand, "make all");
  CHECK_EQ(ws.settings().buildDir, "bld");
}

// ---------------------------------------------------------------------------
// Editor integration
// ---------------------------------------------------------------------------

namespace {
Editor::Options quietOptions() {
  Editor::Options o;
  o.semantic.useClangd = false;
  return o;
}
}  // namespace

TEST(build_editor_messages_navigation_and_diagnostics) {
  TempDir td;
  writeFile(td.path / "a.cpp", "int a;\nint bad_name;\nint c;\n");
  writeFile(td.path / "b.cpp", "int x;\nint y;\nint z;\nint w = foo;\n");
  Editor ed(td.path, quietOptions());
  REQUIRE(ed.start());

  BuildOverrides o;
  o.command = shellCmd(
                  "printf 'a.cpp:2:5: error: bad\\nnoise\\nb.cpp:4:9: warning: meh [-Wfoo]\\nclang: error: link "
                  "failed\\n'; exit 1",
                  "echo a.cpp:2:5: error: bad& echo noise& echo b.cpp:4:9: warning: meh [-Wfoo]& echo clang: error: "
                  "link failed& exit 1")
                  .shell;
  std::vector<std::string> streamed;
  BuildResult res;
  bool done = false;
  std::string err;
  REQUIRE(ed.build(
      o, [&](const std::vector<std::string>& l) { streamed.insert(streamed.end(), l.begin(), l.end()); },
      [&](const BuildResult& r) {
        res = r;
        done = true;
      },
      &err));
  CHECK(ed.buildRunning());
  REQUIRE(ed.waitFor([&] { return done; }, 20000));
  CHECK(!ed.buildRunning());
  CHECK_EQ(res.exitCode, 1);
  CHECK_EQ(res.errors, size_t(2));
  CHECK_EQ(res.warnings, size_t(1));
  CHECK_EQ(ed.lastBuildResult().errors, size_t(2));
  CHECK_EQ(streamed.size(), size_t(4));

  const auto& msgs = ed.buildMessages();
  REQUIRE(msgs.size() == 3);
  CHECK_EQ(pathKey(fs::path(msgs[0].file)), pathKey(fs::path((td.path / "a.cpp").generic_string())));
  CHECK_EQ(msgs[0].line, 2);
  CHECK_EQ(msgs[1].code, "-Wfoo");
  CHECK(msgs[2].file.empty());

  // Build diagnostics are separate from clangd ones and carry a range.
  const auto& da = ed.buildDiagnostics(td.path / "a.cpp");
  REQUIRE(da.size() == 1);
  CHECK_EQ(da[0].severity, 1);
  CHECK_EQ(da[0].message, "bad");
  CHECK_EQ(da[0].range.start.line, 1);
  CHECK_EQ(da[0].range.start.column, 4);
  CHECK_EQ(ed.buildDiagnostics(td.path / "b.cpp").size(), size_t(1));
  CHECK_EQ(ed.buildDiagnostics(td.path / "b.cpp")[0].severity, 2);
  CHECK(ed.diagnostics(td.path / "a.cpp").empty());
  CHECK(ed.buildDiagnostics(td.path / "c.cpp").empty());

  // Navigation goes through gotoLocation: history records the jumps and Back returns.
  const size_t historyBefore = ed.navigation().entries().size();
  auto m = ed.nextBuildMessage();
  REQUIRE(m.has_value());
  CHECK_EQ(m->line, 2);
  CHECK_EQ(ed.cursor().pos.line, 1);
  CHECK_EQ(ed.cursor().pos.column, 4);
  CHECK_EQ(pathKey(ed.cursor().file), pathKey(td.path / "a.cpp"));
  m = ed.nextBuildMessage();
  REQUIRE(m.has_value());
  CHECK_EQ(m->code, "-Wfoo");
  CHECK_EQ(pathKey(ed.cursor().file), pathKey(td.path / "b.cpp"));
  CHECK_EQ(ed.cursor().pos.line, 3);
  CHECK(ed.navigation().entries().size() > historyBefore);
  m = ed.nextBuildMessage();  // wraps; the location-less message is skipped
  REQUIRE(m.has_value());
  CHECK_EQ(m->line, 2);
  m = ed.prevBuildMessage();  // wraps back
  REQUIRE(m.has_value());
  CHECK_EQ(m->code, "-Wfoo");
  CHECK(ed.back());
  CHECK_EQ(pathKey(ed.cursor().file), pathKey(td.path / "a.cpp"));

  // A new build clears messages and diagnostics.
  done = false;
  BuildOverrides quiet;
  quiet.command = shellCmd("echo fine", "echo fine").shell;
  REQUIRE(ed.build(quiet, {}, [&](const BuildResult&) { done = true; }, &err));
  CHECK(ed.buildMessages().empty());
  CHECK(ed.buildDiagnostics(td.path / "a.cpp").empty());
  REQUIRE(ed.waitFor([&] { return done; }, 20000));
  CHECK(ed.lastBuildResult().ok());
  CHECK(!ed.nextBuildMessage().has_value());
}

TEST(build_editor_cancel_and_restart) {
  TempDir td;
  writeFile(td.path / "a.cpp", "int a;\n");
  Editor ed(td.path, quietOptions());
  REQUIRE(ed.start());
  BuildOverrides slow;
  slow.command = sleeperCmd("a.cpp:1:1: error: slow").shell;
  // One slot per build: the done callbacks of a cancelled and its replacement build may arrive in either order
  // (killing the process tree takes as long as the machine is busy), so never index a shared results vector.
  std::optional<BuildResult> first, second, third;
  std::string err;
  REQUIRE(ed.build(slow, {}, [&](const BuildResult& r) { first = r; }, &err));
  REQUIRE(ed.waitFor([&] { return !ed.buildMessages().empty(); }, 60000));
  ed.cancelBuild();
  REQUIRE(ed.waitFor([&] { return first.has_value(); }, 60000));
  CHECK(first->cancelled);
  CHECK(!ed.buildRunning());

  // Restart while running: the second build ends cancelled, the third delivers its own messages only.
  REQUIRE(ed.build(slow, {}, [&](const BuildResult& r) { second = r; }, &err));
  REQUIRE(ed.waitFor([&] { return !ed.buildMessages().empty(); }, 60000));
  BuildOverrides quick;
  quick.command = shellCmd("echo a.cpp:1:3: warning: second", "echo a.cpp:1:3: warning: second").shell;
  REQUIRE(ed.build(quick, {}, [&](const BuildResult& r) { third = r; }, &err));
  REQUIRE(ed.waitFor([&] { return second.has_value() && third.has_value(); }, 60000));
  CHECK(second->cancelled);
  CHECK(third->ok());
  REQUIRE(ed.buildMessages().size() == 1);
  CHECK_EQ(ed.buildMessages()[0].text, "second");
  CHECK_EQ(third->warnings, size_t(1));
}

TEST(build_ansi_strip_csi_osc) {
  CHECK_EQ(stripAnsiEscapes("plain text"), "plain text");
  CHECK_EQ(stripAnsiEscapes("\x1b[01m\x1b[Ka.cpp:1:2:\x1b[m\x1b[K \x1b[01;31m\x1b[Kerror: \x1b[m\x1b[Kboom"),
           "a.cpp:1:2: error: boom");
  // OSC 8 hyperlink (gcc -fdiagnostics-urls), ST and BEL terminators.
  CHECK_EQ(stripAnsiEscapes("x \x1b]8;;https://gcc.gnu.org/w\x1b\\[-Wfoo]\x1b]8;;\x1b\\ y"), "x [-Wfoo] y");
  CHECK_EQ(stripAnsiEscapes("\x1b]0;title\a"
                            "ok"),
           "ok");
  CHECK_EQ(stripAnsiEscapes("tail\x1b[31"), "tail");  // truncated sequence is dropped
  CHECK_EQ(stripAnsiEscapes("tail\x1b"), "tail");
}

TEST(build_editor_ansi_coloured_gcc_output_yields_messages) {
#ifdef _WIN32
  return;  // the fake compiler output is produced with a POSIX printf
#else
  TempDir td;
  writeFile(td.path / "a.cpp", "int a;\nint bad_name;\nint c;\n");
  Editor ed(td.path, quietOptions());
  REQUIRE(ed.start());
  // What `gcc -fdiagnostics-color=always -fdiagnostics-urls=always` prints (printf escapes).
  const std::string out =
      "\\033[01m\\033[Ka.cpp:2:5:\\033[m\\033[K \\033[01;31m\\033[Kerror: \\033[m\\033[Kbad thing\\033[m\\033[K\\n"
      "\\033[01m\\033[Ka.cpp:3:1:\\033[m\\033[K \\033[01;35m\\033[Kwarning: \\033[m\\033[Kmeh "
      "\\033[01;35m\\033[K[\\033]8;;https://gcc.gnu.org/w\\033\\\\-Wunused\\033]8;;\\033\\\\]\\033[m\\033[K\\n";
  BuildOverrides o;
  o.command = "printf '" + out + "'";
  bool done = false;
  std::string err;
  REQUIRE(ed.build(o, {}, [&](const BuildResult&) { done = true; }, &err));
  REQUIRE(ed.waitFor([&] { return done; }, 60000));
  const auto& msgs = ed.buildMessages();
  REQUIRE(msgs.size() == 2);
  CHECK_EQ(msgs[0].line, 2);
  CHECK_EQ(msgs[0].col, 5);
  CHECK(msgs[0].severity == BuildSeverity::Error);
  CHECK_EQ(msgs[0].text, "bad thing");
  CHECK(msgs[1].severity == BuildSeverity::Warning);
  CHECK_EQ(msgs[1].code, "-Wunused");
  CHECK_EQ(ed.buildDiagnostics(td.path / "a.cpp").size(), size_t(2));
  auto m = ed.nextBuildMessage();  // F4
  REQUIRE(m.has_value());
  CHECK_EQ(m->line, 2);
#endif
}

TEST(build_editor_set_build_cursor_moves_f4) {
  TempDir td;
  writeFile(td.path / "a.cpp", "int a;\nint b;\nint c;\nint d;\n");
  Editor ed(td.path, quietOptions());
  REQUIRE(ed.start());
  BuildOverrides o;
  o.command = shellCmd("printf 'a.cpp:1:1: error: one\\nnoise\\na.cpp:2:1: error: two\\na.cpp:3:1: error: three\\n'",
                       "echo a.cpp:1:1: error: one& echo a.cpp:2:1: error: two& echo a.cpp:3:1: error: three")
                  .shell;
  bool done = false;
  std::string err;
  REQUIRE(ed.build(o, {}, [&](const BuildResult&) { done = true; }, &err));
  REQUIRE(ed.waitFor([&] { return done; }, 60000));
  REQUIRE(ed.buildMessages().size() == 3);

  ed.setBuildCursor(1);            // as if the user clicked the second message in the build panel
  auto m = ed.nextBuildMessage();  // F4 continues after it
  REQUIRE(m.has_value());
  CHECK_EQ(m->text, "three");
  m = ed.prevBuildMessage();
  REQUIRE(m.has_value());
  CHECK_EQ(m->text, "two");
  ed.setBuildCursor(2);
  m = ed.prevBuildMessage();
  REQUIRE(m.has_value());
  CHECK_EQ(m->text, "two");
  ed.setBuildCursor(99);  // out of range: ignored
  m = ed.nextBuildMessage();
  REQUIRE(m.has_value());
  CHECK_EQ(m->text, "three");
}

TEST(build_editor_no_command_fails) {
  TempDir td;
  Editor ed(td.path, quietOptions());
  std::string err;
  CHECK(!ed.build({}, {}, {}, &err));
  CHECK(!err.empty());
  CHECK(!ed.buildRunning());
}

// ---------------------------------------------------------------------------
// End to end: a real CMake project with a deliberate compile error
// ---------------------------------------------------------------------------

TEST(build_e2e_cmake_compile_error) {
  if (findExecutable("cmake").empty()) return;  // skip: no cmake
  std::string cxx;
  for (const char* n : {"c++", "g++", "clang++"})
    if (cxx.empty() && !findExecutable(n).empty()) cxx = n;
  if (cxx.empty()) return;  // skip: no compiler

  TempDir td;
  writeFile(td.path / "CMakeLists.txt",
            "cmake_minimum_required(VERSION 3.10)\nproject(broken CXX)\nadd_executable(broken main.cpp)\n");
  writeFile(td.path / "main.cpp", "#include <cstdio>\n\nint main() {\n  return undeclared_name + 1;\n}\n");

  // Configure the build tree the way a user would; Editor::build finds it through the compile database.
  Process cfg;
  std::string err;
  ProcessOptions po;
  po.mergeStderr = true;
  REQUIRE(cfg.start("cmake",
                    {"-S", td.path.string(), "-B", (td.path / "build").string(), "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                     "-DCMAKE_CXX_COMPILER=" + cxx},
                    {}, {}, &err, {}, po));
  char buf[4096];
  std::string log;
  for (long n; (n = cfg.read(buf, sizeof buf)) > 0;) log.append(buf, static_cast<size_t>(n));
  if (cfg.wait(120000) != 0) return;  // skip: this host cannot configure a C++ project

  Editor ed(td.path, quietOptions());
  REQUIRE(ed.start());
  bool done = false;
  BuildResult res;
  REQUIRE(ed.build(
      {}, {},
      [&](const BuildResult& r) {
        res = r;
        done = true;
      },
      &err));
  REQUIRE(ed.waitFor([&] { return done; }, 180000));
  CHECK(res.exitCode != 0);
  CHECK(res.errors >= 1);
  const BuildMessage* first = nullptr;
  for (auto& m : ed.buildMessages())
    if (m.severity == BuildSeverity::Error && m.hasLocation()) {
      first = &m;
      break;
    }
  REQUIRE(first != nullptr);
  CHECK_EQ(first->line, 4);
  CHECK(first->col > 0);
  CHECK_EQ(fs::path(first->file).filename().string(), "main.cpp");
  std::error_code ec;
  CHECK(fs::equivalent(fs::path(first->file), td.path / "main.cpp", ec));
  auto jumped = ed.nextBuildMessage();
  REQUIRE(jumped.has_value());
  CHECK_EQ(ed.cursor().pos.line, 3);
  CHECK(!ed.buildDiagnostics(td.path / "main.cpp").empty());
}
