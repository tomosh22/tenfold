// D4: Editor::compileCommandInfo, the diagnostics behind "why is this symbol approximate?"
// (docs/USER_GUIDE.md, Help.ShowCompileCommand, tenfold-cli why), and the check that the guide's settings table
// lists exactly the keys of assets/settings.default.txt.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

#include "app/Editor.h"
#include "test_main.h"
#include "workspace/Workspace.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

const fs::path kSource = fs::path(TENFOLD_SOURCE_DIR);

fs::path freshDir(const std::string& name) {
  fs::path d = fs::weakly_canonical(fs::temp_directory_path()) / ("tenfold_ci_" + name);
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d);
  return d;
}

fs::path copyFixture(const std::string& fixture, const std::string& name) {
  fs::path d = freshDir(name);
  fs::copy(kSource / "tests/fixtures/vs" / fixture, d, fs::copy_options::recursive);
  return d;
}

bool has(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

Editor::Options noClangd() {
  Editor::Options o;
  o.semantic.useClangd = false;
  return o;
}

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

TEST(compile_info_vcxproj_lists_the_entry_and_the_active_configuration) {
  fs::path root = copyFixture("Multi", "vs_multi");
  {
    Editor ed(root, noClangd());
    std::string err;
    REQUIRE(ed.start(&err));
    const fs::path main = root / "App/src/main.cpp";
    auto info = ed.compileCommandInfo(main);
    CHECK(info.engineStarted);
    CHECK_EQ(info.source, "vcxproj");
    CHECK(info.listed);
    CHECK_EQ(info.solution.filename().string(), "Multi.sln");
    CHECK_EQ(info.vsConfiguration, "Debug|x64");
    CHECK(info.vsConfigurationUsed);
    CHECK(info.databaseEntries > 0);
    CHECK_EQ(info.databasePath.filename().string(), "compile_commands.json");
    CHECK(!info.directory.empty());
    REQUIRE(!info.argv.empty());
    CHECK(has(info.argv, "/DSHEET_DEFINE"));  // from DebugX64.props, which only the Debug|x64 configuration imports
    CHECK(has(info.argv, "/MDd"));
    // The entry is the one the engine holds for that file.
    const std::string key = fs::weakly_canonical(main).generic_string();
    bool same = false;
    for (const auto& c : *ed.semantic().compileCommands())
      if (fs::weakly_canonical(c.file).generic_string() == key) same = c.arguments == info.argv;
    CHECK(same);
    // No clangd in this run: approximate, and the file is not open.
    CHECK(!info.clangdRunning);
    CHECK(!info.open);
    CHECK_EQ(info.confidence, "approximate");
    CHECK(contains(info.confidenceText, "clangd is not running"));
    // The text carries all of it, and wrapping keeps every word.
    const std::string t = info.text();
    CHECK(contains(t, "Visual Studio solution Multi.sln"));
    CHECK(contains(t, "Debug|x64"));
    CHECK(contains(t, "/DSHEET_DEFINE"));
    CHECK(contains(t, "In database:    yes"));
    const std::string w = info.text(60);
    CHECK(contains(w, "/DSHEET_DEFINE"));
    CHECK(std::count(w.begin(), w.end(), '\n') > std::count(t.begin(), t.end(), '\n'));

    // A header is not compiled on its own: no entry, and the text says what clangd does then.
    auto hdr = ed.compileCommandInfo(root / "App/src/pch.h");
    CHECK(!hdr.listed);
    CHECK(hdr.argv.empty());
    CHECK(contains(hdr.text(), "In database:    no"));
    CHECK(contains(hdr.text(), "Fallback flags"));
    CHECK(has(hdr.fallbackFlags, "-std=c++20"));

    // A file the project excludes from the build in this configuration is not listed either.
    CHECK(!ed.compileCommandInfo(root / "App/src/legacy.cpp").listed);

    // Switching the configuration changes the answer, and a relative path is taken from the workspace root.
    bool applied = false;
    REQUIRE(ed.selectVsConfiguration("Release|x64", &err, [&] { applied = true; }));
    REQUIRE(ed.waitFor([&] { return applied; }, 30000));
    auto rel = ed.compileCommandInfo("App/src/main.cpp");
    CHECK_EQ(rel.vsConfiguration, "Release|x64");
    CHECK(rel.listed);
    CHECK(has(rel.argv, "/DNDEBUG"));
    CHECK(!has(rel.argv, "/DSHEET_DEFINE"));
    CHECK(!ed.compileCommandInfo(root / "Tests/tests.cpp").listed);  // Tests is not built in Release
  }
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(compile_info_existing_compile_commands_json_wins_over_the_solution) {
  fs::path root = copyFixture("Multi", "vs_existing");
  const fs::path main = root / "App/src/main.cpp";
  {
    std::ofstream(root / "compile_commands.json")
        << "[{\"directory\": \"" << root.generic_string() << "\", \"file\": \"" << main.generic_string()
        << "\", \"arguments\": [\"clang++\", \"-DFROM_JSON\", \"-c\", \"" << main.generic_string() << "\"]}]";
  }
  {
    Editor ed(root, noClangd());
    std::string err;
    REQUIRE(ed.start(&err));
    auto info = ed.compileCommandInfo(main);
    CHECK_EQ(info.source, "existing");
    CHECK(info.listed);
    CHECK(has(info.argv, "-DFROM_JSON"));
    CHECK_EQ(info.databaseEntries, size_t(1));
    CHECK(!info.vsConfigurationUsed);  // the solution is there, but its configuration decides nothing
    CHECK_EQ(info.vsConfiguration, "Debug|x64");
    CHECK(contains(info.text(), "not used"));
    CHECK(contains(info.sourceText, "takes precedence"));
  }
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(compile_info_heuristic_workspace_shows_the_guess) {
  fs::path root = freshDir("heuristic");
  fs::create_directories(root / "src");
  fs::create_directories(root / "inc/lib");
  std::ofstream(root / "inc/lib/thing.h") << "#pragma once\nstruct Thing { int x; };\n";
  std::ofstream(root / "src/a.cpp") << "#include \"lib/thing.h\"\nint main() { Thing t; return t.x; }\n";
  std::ofstream(root / "src/b.cpp") << "#include \"lib/thing.h\"\nint f() { return 1; }\n";
  {
    Editor ed(root, noClangd());
    std::string err;
    REQUIRE(ed.start(&err));
    auto info = ed.compileCommandInfo(root / "src/a.cpp");
    CHECK_EQ(info.source, "heuristic");
    CHECK(info.solution.empty());
    CHECK(info.vsConfiguration.empty());
    CHECK(info.listed);
    REQUIRE(!info.argv.empty());
    CHECK(contains(info.sourceText, "heuristic"));
    CHECK(contains(info.sourceText, "-std=c++20"));
    // The inferred include directory is either in the command or (where clangd honours a private config) among the
    // flags added to every file.
    auto inc = [&](const std::vector<std::string>& v) {
      return std::any_of(v.begin(), v.end(), [](const std::string& a) {
        return a.rfind("-I", 0) == 0 && a.find("inc") != std::string::npos;
      });
    };
    CHECK(inc(info.argv) || inc(info.sharedFlags));
    CHECK(info.argv.back().find("a.cpp") != std::string::npos);
    CHECK(!info.clangdRunning);
    CHECK_EQ(info.confidence, "approximate");
    const std::string t = info.text();
    CHECK(contains(t, "heuristic"));
    CHECK(contains(t, "Command (argv):"));
    CHECK(!contains(t, "Visual Studio:"));

    // Headers are not in the heuristic database: they get the shared flags, or the fallback.
    auto hdr = ed.compileCommandInfo(root / "inc/lib/thing.h");
    CHECK(!hdr.listed);
    CHECK(has(hdr.fallbackFlags, "-std=c++20"));
    CHECK(has(hdr.fallbackFlags, "-I" + root.string()));

    // An unrelated path is reported the same way and does not need to exist.
    CHECK(!ed.compileCommandInfo(root / "nowhere.cpp").listed);
  }
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(compile_info_before_start_says_so) {
  fs::path root = freshDir("unstarted");
  std::ofstream(root / "a.cpp") << "int x;\n";
  Editor ed(root, noClangd());
  auto info = ed.compileCommandInfo("a.cpp");
  CHECK(!info.engineStarted);
  CHECK(!info.listed);
  CHECK_EQ(info.confidence, "approximate");
  CHECK(contains(info.text(), "engine was not started"));
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(compile_info_reports_clangd_state_for_open_and_unopened_files) {
  if (findExecutable("clangd").empty()) {
    std::cerr << "    (skipped: clangd not found)\n";
    return;
  }
  fs::path root = freshDir("clangd");
  fs::create_directories(root / "src");
  std::ofstream(root / "src/a.cpp") << "int add(int a, int b) { return a + b; }\nint main() { return add(1, 2); }\n";
  std::ofstream(root / "src/b.cpp") << "int sub(int a, int b) { return a - b; }\n";
  {
    Editor::Options o;
    o.semantic.useClangd = true;
    Editor ed(root, o);
    std::string err;
    REQUIRE(ed.start(&err));
    const fs::path a = root / "src/a.cpp", b = root / "src/b.cpp";
    REQUIRE(ed.open(a, &err) != nullptr);
    CHECK(ed.semantic().waitForFile(a, 90000));
    auto open = ed.compileCommandInfo(a);
    CHECK(open.clangdRunning);
    CHECK(open.open);
    CHECK(open.ready);
    CHECK_EQ(open.confidence, "authoritative");
    CHECK(!open.clangdExe.empty());
    CHECK(!open.clangdSource.empty());
    CHECK(contains(open.text(), "Confidence:     authoritative"));
    auto closed = ed.compileCommandInfo(b);  // clangd has not been given this file
    CHECK(closed.clangdRunning);
    CHECK(!closed.open);
    CHECK_EQ(closed.confidence, "not open");
    ed.shutdown();
  }
  std::error_code ec;
  fs::remove_all(root, ec);
}

// ---------------------------------------------------------------------------
// docs/USER_GUIDE.md: the settings table is generated from assets/settings.default.txt; this keeps them in step.
// ---------------------------------------------------------------------------

namespace {

std::string trim(std::string s) {
  const char* ws = " \t\r";
  s.erase(0, s.find_first_not_of(ws) == std::string::npos ? s.size() : s.find_first_not_of(ws));
  s.erase(s.find_last_not_of(ws) == std::string::npos ? 0 : s.find_last_not_of(ws) + 1);
  return s;
}

// key -> value text exactly as the guide shows it: a value wrapped in quotes loses the outer pair, empty is "(empty)".
std::vector<std::pair<std::string, std::string>> defaultSettings() {
  std::vector<std::pair<std::string, std::string>> out;
  std::istringstream in(slurp(kSource / "assets/settings.default.txt"));
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string v = trim(line.substr(eq + 1));
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    out.emplace_back(trim(line.substr(0, eq)), v.empty() ? "(empty)" : v);
  }
  return out;
}

}  // namespace

TEST(user_guide_settings_table_matches_the_default_settings_file) {
  const std::string guide = slurp(kSource / "docs/USER_GUIDE.md");
  REQUIRE(!guide.empty());
  const size_t b = guide.find("<!-- settings:begin -->"), e = guide.find("<!-- settings:end -->");
  REQUIRE(b != std::string::npos && e != std::string::npos && b < e);
  // Rows look like: | `editor.wordWrap` | `true` | meaning |   (the default cell is `...` or (empty))
  std::vector<std::pair<std::string, std::string>> rows;
  std::istringstream in(guide.substr(b, e - b));
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("| `", 0) != 0) continue;
    const size_t k1 = line.find('`', 3);
    REQUIRE(k1 != std::string::npos);
    const std::string key = line.substr(3, k1 - 3);
    const size_t c2 = line.find('|', k1);
    const size_t c3 = line.find('|', c2 + 1);
    REQUIRE(c2 != std::string::npos && c3 != std::string::npos);
    std::string def = trim(line.substr(c2 + 1, c3 - c2 - 1));
    if (def.size() >= 2 && def.front() == '`' && def.back() == '`') def = def.substr(1, def.size() - 2);
    rows.emplace_back(key, def);
  }
  const auto want = defaultSettings();
  REQUIRE(!want.empty());
  CHECK_EQ(rows.size(), want.size());
  for (size_t i = 0; i < std::min(rows.size(), want.size()); ++i) {
    CHECK_EQ(rows[i].first, want[i].first);
    CHECK_EQ(rows[i].second, want[i].second);
  }
}
