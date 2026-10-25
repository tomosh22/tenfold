// Settings and theme parser, layering, change notification, token fallback, shipped files, hot reload. No GL.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

#include "ui/settings/Settings.h"
#include "ui/settings/Theme.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;
  TempDir() {
    static int n = 0;
    auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    path = uitest::tempRoot() / ("tenfold_settings_" + std::to_string(t) + "_" + std::to_string(n++));
    fs::create_directories(path);
    std::error_code ec;
    path = fs::weakly_canonical(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};
void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}
std::string readAsset(const char* rel) {
  std::ifstream f(std::string(TENFOLD_SOURCE_DIR) + "/assets/" + rel, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

using Map = std::map<std::string, SettingValue>;
std::vector<SettingsDiag> parse(const char* text, Map& m) {
  std::vector<SettingsDiag> d;
  parseSettings(text, "t.txt", m, d);
  return d;
}
bool near(gfx::Color a, gfx::Color b) {
  auto n = [](float x, float y) { return std::abs(x - y) < 0.5f / 255 + 1e-4f; };
  return n(a.r, b.r) && n(a.g, b.g) && n(a.b, b.b) && n(a.a, b.a);
}

}  // namespace

TEST(settings_parse_all_types) {
  Map m;
  auto d = parse(
      "# comment\n\n   # indented comment\neditor.smoothScroll = false\neditor.tabWidth=8\nfont.size = 12.5\n"
      "ui.theme = \"light\"\nfont.fallbacks = A, B\nfont.editor = JetBrains Mono, 11\n",
      m);
  CHECK(d.empty());
  CHECK(!m["editor.smoothScroll"].b);
  CHECK_EQ(m["editor.tabWidth"].i, 8);
  CHECK_EQ(m["font.size"].f, 12.5);
  CHECK_EQ(m["ui.theme"].s, std::string("light"));
  CHECK_EQ(m["font.fallbacks"].s, std::string("A, B"));
  CHECK_EQ(m["font.editor"].font.family, std::string("JetBrains Mono"));
  CHECK_EQ(m["font.editor"].font.sizePt, 11.f);
  Map f;
  CHECK(parse("font.editor = /usr/share/fonts/x.ttf\n", f).empty());
  CHECK_EQ(f["font.editor"].font.family, std::string("/usr/share/fonts/x.ttf"));
  CHECK_EQ(f["font.editor"].font.sizePt, 0.f);
}

TEST(settings_colors) {
  gfx::Color c;
  std::string e;
  CHECK(parseColor("#ff8000", c, e));
  CHECK(near(c, {1, 128 / 255.f, 0, 1}));
  CHECK(parseColor("#00000080", c, e));
  CHECK(near(c, {0, 0, 0, 128 / 255.f}));
  CHECK(!parseColor("ff8000", c, e));
  CHECK(!parseColor("#ff80", c, e));
  CHECK(!parseColor("#gg0000", c, e));
}

TEST(settings_error_classes_with_line_numbers) {
  Map m;
  auto d = parse(
      "editor.tabWidth = 4\n"       // 1 ok
      "[editor]\n"                  // 2 section
      "no equals here\n"            // 3
      "= 5\n"                       // 4 missing key
      "bad key! = 1\n"              // 5 invalid key
      "editor.nope = 1\n"           // 6 unknown
      "editor.tabWidth = four\n"    // 7 int
      "editor.smoothScroll = 1\n"   // 8 bool
      "font.size = big\n"           // 9 float
      "font.editor = X, huge\n"     // 10 font size
      "editor.wordWrap = true\n"    // 11
      "editor.wordWrap = false\n",  // 12 duplicate (warning)
      m);
  std::map<int, bool> got;  // line -> warning
  for (auto& x : d) got[x.line] = x.warning;
  CHECK_EQ(d.size(), size_t(10));
  for (int l : {2, 3, 4, 5, 6, 7, 8, 9, 10}) CHECK(got.count(l) && !got[l]);
  CHECK(got.count(12) && got[12]);
  CHECK(!got.count(1));
  CHECK_EQ(m["editor.tabWidth"].i, 4);  // bad line did not clobber
  CHECK(!m["editor.wordWrap"].b);       // later duplicate wins
  CHECK_EQ(formatDiag(d[0]).substr(0, 8), std::string("t.txt:2:"));
}

TEST(settings_layering_and_changed_keys) {
  Settings s;
  CHECK_EQ(s.getInt("editor.tabWidth"), 4);
  CHECK(s.getBool("editor.smoothScroll"));
  std::vector<std::set<std::string>> calls;
  s.onChanged([&](const std::set<std::string>& k) { calls.push_back(k); });
  s.loadTexts("editor.tabWidth = 2\neditor.wordWrap = true\n", "editor.tabWidth = 8\n");
  CHECK_EQ(s.getInt("editor.tabWidth"), 8);  // workspace beats user
  CHECK(s.getBool("editor.wordWrap"));       // user beats default
  REQUIRE(calls.size() == 1);
  CHECK((calls[0] == std::set<std::string>{"editor.tabWidth", "editor.wordWrap"}));
  s.loadTexts("editor.tabWidth = 2\neditor.wordWrap = true\n", "editor.tabWidth = 8\n");
  CHECK_EQ(calls.size(), size_t(1));  // nothing changed: no notification
  s.loadTexts("editor.wordWrap = true\n", "");
  REQUIRE(calls.size() == 2);
  CHECK((calls[1] == std::set<std::string>{"editor.tabWidth"}));  // back to the default 4
  CHECK_EQ(s.getInt("editor.tabWidth"), 4);
  s.loadTexts("editor.bogus = 1\n", "");
  CHECK_EQ(s.diagnostics().size(), size_t(1));
  CHECK(editorSettings(s).smoothScroll);
}

TEST(settings_defaults_complete_and_clean) {
  Map m;
  std::vector<SettingsDiag> d;
  parseSettings(defaultSettingsText(), "default", m, d);
  CHECK(d.empty());
  for (auto& info : settingsSchema()) CHECK(m.count(info.key));
  CHECK_EQ(m.size(), settingsSchema().size());
  CHECK_EQ(defaultSettingsText(), readAsset("settings.default.txt"));
  CHECK(!m["font.editor"].font.family.size());  // unset -> discovery
}

TEST(theme_token_fallback_chain) {
  Theme t;
  const gfx::Color text = t.text, red{1, 0, 0, 1}, green{0, 1, 0, 1}, blue{0, 0, 1, 1};
  auto eq = [](gfx::Color a, gfx::Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; };
  CHECK(eq(t.tokenColor("method"), text));  // nothing defined -> foreground
  t.tokens["identifier"] = blue;
  CHECK(eq(t.tokenColor("method"), blue));  // method -> function -> identifier
  t.tokens["function"] = green;
  CHECK(eq(t.tokenColor("method"), green));
  t.tokens["method"] = red;
  CHECK(eq(t.tokenColor("method"), red));
  t.tokens["method.static"] = blue;
  CHECK(eq(t.tokenColor("method", {"static"}), blue));
  CHECK(eq(t.tokenColor("method", {"readonly"}), red));  // unknown modifier ignored
  CHECK(eq(t.tokenColor("class"), blue));                // class -> type -> identifier
  CHECK(eq(t.tokenColor("nonsense"), text));
}

TEST(theme_parse_and_errors) {
  std::vector<SettingsDiag> d;
  Theme t = parseTheme(
      "color.background = #102030\ntoken.keyword = #ff0000\ntoken.method.static = #00ff00\n"
      "color.nope = #000000\ncolor.text = red\ntoken.bogus = #000000\ntoken.method.weird = #000000\nfoo = 1\n",
      "th.txt", d);
  CHECK(near(t.background, {16 / 255.f, 32 / 255.f, 48 / 255.f, 1}));
  CHECK(t.tokens.count("keyword") && t.tokens.count("method.static"));
  CHECK_EQ(d.size(), size_t(5));
  CHECK_EQ(d[0].line, 4);
  CHECK_EQ(d[4].line, 8);
  CHECK(near(t.text, Theme::darkDefault().text));  // bad value left the base alone
}

TEST(theme_shipped_files_complete) {
  for (const char* name : {"dark", "light", "vs-dark"}) {
    std::string text(builtinThemeText(name));
    CHECK(!text.empty());
    CHECK_EQ(text, readAsset((std::string("themes/") + name + ".txt").c_str()));
    std::vector<SettingsDiag> d;
    parseTheme(text, name, d);
    CHECK(d.empty());
    std::set<std::string> keys;
    parseKeyValueLines(text, name, d, [&](int, std::string_view k, std::string_view) { keys.insert(std::string(k)); });
    for (auto& k : requiredThemeKeys()) CHECK(keys.count(k));
  }
  CHECK(readAsset("themes/dark.txt").find("TODO") != std::string::npos);
  // The application default is the Visual Studio dark theme (OPEN_QUESTIONS #7).
  CHECK(std::string(defaultSettingsText()).find("\nui.theme = vs-dark\n") != std::string::npos);
  // The dark theme reproduces today's hard-coded colours.
  std::vector<SettingsDiag> d;
  Theme dark = loadTheme("dark", {}, {}, d);
  Theme def = Theme::darkDefault();
  CHECK(d.empty());
  CHECK(near(dark.background, def.background) && near(dark.statusBackground, def.statusBackground) &&
        near(dark.tabAccent, def.tabAccent) && near(dark.gutterText, def.gutterText) &&
        near(dark.currentLine, def.currentLine) && near(dark.selection, def.selection) &&
        near(dark.panelBorder, def.panelBorder) && near(dark.closeHover, def.closeHover));
  Theme light = loadTheme("light", {}, {}, d);
  CHECK(!near(light.background, def.background));
  loadTheme("nonexistent", {}, {}, d);
  CHECK_EQ(d.size(), size_t(1));
}

// vs-dark must define every key the theme system knows (colours, optional colours, lexical and common semantic
// tokens, static member variants) so nothing silently falls back to the placeholder palette.
TEST(theme_vs_dark_resolves_every_key) {
  const std::string text(builtinThemeText("vs-dark"));
  CHECK(!text.empty());
  CHECK_EQ(text, readAsset("themes/vs-dark.txt"));
  std::vector<SettingsDiag> d;
  Theme t = loadTheme("vs-dark", {}, {}, d);
  CHECK(d.empty());  // no unknown keys, bad colours or duplicates
  std::set<std::string> keys;
  parseKeyValueLines(text, "vs-dark", d,
                     [&](int, std::string_view k, std::string_view) { keys.insert(std::string(k)); });
  for (auto& n : themeColorNames()) CHECK(keys.count("color." + n));  // optional ones too
  for (auto& k : requiredThemeKeys()) CHECK(keys.count(k));
  for (const char* k :
       {"token.struct", "token.interface", "token.method.static", "token.property", "token.variable.static"})
    CHECK(keys.count(k));
  for (auto& n : themeOptionalColorNames()) CHECK(keys.count("color." + n));
  // Every key resolves to its own colour, not the base fallback.
  CHECK(near(t.statusBackground, {0, 122 / 255.f, 204 / 255.f, 1}));
  CHECK(near(t.editorBackground, {30 / 255.f, 30 / 255.f, 30 / 255.f, 1}));
  CHECK(near(t.tokenColor("keyword"), {86 / 255.f, 156 / 255.f, 214 / 255.f, 1}));
  CHECK(near(t.tokenColor("class"), {78 / 255.f, 201 / 255.f, 176 / 255.f, 1}));
  CHECK(near(t.tokenColor("method", {"static"}), {220 / 255.f, 220 / 255.f, 170 / 255.f, 1}));
  CHECK(near(t.tokenColor("macro"), {189 / 255.f, 99 / 255.f, 197 / 255.f, 1}));
  CHECK(near(t.tokenColor("comment"), {87 / 255.f, 166 / 255.f, 74 / 255.f, 1}));
  CHECK(t.editorBackground.a > 0 && t.currentLineBorder.a > 0 && t.selectionInactive.a > 0 && t.popupBorder.a > 0 &&
        t.tabActiveText.a > 0 && t.findHighlightCurrent.a > 0);
  // dark / light leave the optional colours unset, so they paint as before.
  Theme dark = loadTheme("dark", {}, {}, d);
  CHECK(dark.editorBackground.a == 0 && dark.currentLineFill.a == 0 && dark.selectionActive.a == 0);
}

TEST(settings_hot_reload) {
  TempDir ws, user;
  writeFile(ws.path / ".tenfold" / "settings.txt", "editor.tabWidth = 2\n");
  SettingsService::Options o;
  o.workspaceRoot = ws.path;
  o.userDir = user.path;
  o.debounceMs = 50;
  o.pollMs = 100;
  o.forcePolling = true;
  SettingsService svc(o);
  CHECK_EQ(svc.settings().getInt("editor.tabWidth"), 2);
  std::vector<std::set<std::string>> calls;
  svc.settings().onChanged([&](const std::set<std::string>& k) { calls.push_back(k); });
  int themeCalls = 0;
  svc.onThemeChanged([&](const Theme&) { ++themeCalls; });
  svc.startWatching();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));  // let the first poll scan record the baseline

  auto waitFor = [&](size_t n) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (std::chrono::steady_clock::now() < deadline) {
      svc.poll();
      if (calls.size() >= n) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  };
  writeFile(ws.path / ".tenfold" / "settings.txt", "editor.tabWidth = 8\neditor.wordWrap = true\n");
  REQUIRE(waitFor(1));
  CHECK((calls[0] == std::set<std::string>{"editor.tabWidth", "editor.wordWrap"}));
  CHECK_EQ(svc.settings().getInt("editor.tabWidth"), 8);

  // Theme switch without restart, via the user settings file.
  writeFile(user.path / "settings.txt", "ui.theme = light\n");
  REQUIRE(waitFor(2));
  CHECK((calls[1] == std::set<std::string>{"ui.theme"}));
  CHECK_EQ(themeCalls, 1);
  CHECK(!near(svc.theme().background, Theme::darkDefault().background));

  // A user theme file edited in place is picked up too.
  writeFile(user.path / "settings.txt", "ui.theme = mine\n");
  writeFile(user.path / "themes" / "mine.txt", "color.background = #010203\n");
  REQUIRE(waitFor(3));
  CHECK(near(svc.theme().background, {1 / 255.f, 2 / 255.f, 3 / 255.f, 1}));
  svc.stopWatching();
}
