// Fuzz harnesses of the user-editable configuration parsers: 10x KeyMappings (.10x_settings), tenfold settings and
// theme files. They live in src/ui/settings but are compiled into the fuzz binaries without the UI library.
#include <map>
#include <string>
#include <string_view>

#include "FuzzTarget.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/settings/KeyBindings.h"
#include "ui/settings/Settings.h"
#include "ui/settings/Theme.h"

// Widget.cpp (linked for SettingsService::applyTo) draws text through TextRenderer, which needs FreeType; the parsers
// never draw. A weak definition, so that a build that does link the real TextRenderer (the UI) wins.
namespace tf::gfx {
__attribute__((weak)) bool TextRenderer::flushText(Renderer&) { return true; }
}  // namespace tf::gfx

namespace tf::fuzz {
namespace {

using namespace tf::ui;

std::string_view view(const uint8_t* d, size_t n) { return {reinterpret_cast<const char*>(d), n}; }

void runKeyBindings(const uint8_t* d, size_t n) {
  const std::string_view text = view(d, n);
  KeyMap map;
  std::vector<KeyBindingDiag> diags;
  parseKeyBindings(text, "KeyMappings.10x_settings", map, diags);
  for (const KeyBindingDiag& g : diags) {
    FUZZ_REQUIRE(g.line >= 0);
    formatDiag(g);
  }
  // A second layer on top (override / removal) must not break the first.
  parseKeyBindings(text, "second", map, diags);
  for (const KeyBinding& b : map.bindings()) {
    FUZZ_REQUIRE(!b.chords.empty());
    // A binding's chords must print and parse back to the same chords.
    std::vector<KeyChord> again;
    std::string err;
    FUZZ_REQUIRE(parseKeyChords(formatChords(b.chords), again, err));
    FUZZ_REQUIRE(again == b.chords);
  }
  // Press every bound chord: the state machine must stay sane.
  int64_t now = 0;
  for (const KeyBinding& b : std::vector<KeyBinding>(map.bindings())) {
    for (const KeyChord& c : b.chords) map.feed(c, b.context, now += 100);
    map.tick(now += 5000);
  }
  KeyChord c;
  std::string err;
  parseKeyChord(text, c, err);
  std::vector<KeyChord> seq;
  parseKeyChords(text, seq, err);
}

void runSettings(const uint8_t* d, size_t n) {
  const std::string_view text = view(d, n);
  std::map<std::string, SettingValue> into;
  std::vector<SettingsDiag> diags;
  parseSettings(text, "settings.txt", into, diags);
  // The same text as user and as workspace layer of a whole Settings object, and every value parser on its own.
  Settings s;
  s.loadTexts(text, text);
  const size_t half = n / 2;
  s.loadTexts(view(d, half), view(d + half, n - half));
  for (const SettingInfo& info : settingsSchema()) {
    SettingValue v;
    std::string err;
    parseSettingValue(info.type, text, v, err);
  }
  gfx::Color col;
  std::string err;
  parseColor(text, col, err);
  editorSettings(s);
  parseKeyValueLines(text, "x", diags, [](int line, std::string_view key, std::string_view value) {
    FUZZ_REQUIRE(line >= 1);
    FUZZ_REQUIRE(!key.empty());
    (void)value;
  });
}

void runTheme(const uint8_t* d, size_t n) {
  std::vector<SettingsDiag> diags;
  parseTheme(view(d, n), "theme.txt", diags);
  parseTheme(view(d, n), "theme.txt", diags, parseTheme(builtinThemeText("vs-dark"), "vs-dark", diags));
}

Registrar r1({"keybindings",
              runKeyBindings,
              {"assets/KeyMappings.default.10x_settings", "tests/fixtures/KeyMappings.10x.expected.txt"},
              {"Control S: SaveAll\nControl K, Control C: CommentSelection\nShift Alt O: FindFile\nControl Shift "
               "Comma: ZoomOut\nControl Q:\nF5: Workbench.FocusPanel 2\n"
               "# comment\nMouseBackButton: NavigateBackward\nControl X: import os; os.exit()\n",
               "Control S: A\nControl S: B\nControl S, Control S: C\n",
               ": \n:\n \n\t: x\nControl: x\nControl Control Control A: x\n"},
              {"Control ",
               "Shift ",
               "Alt ",
               "Control Shift Alt ",
               "Comma",
               "MouseMiddleButton",
               "MouseBackButton",
               "MouseForwardButton",
               "F1",
               "F24",
               "F25",
               "F0",
               "Up",
               "Down",
               "PageUp",
               "PageDown",
               "Home",
               "End",
               "Enter",
               "Tab",
               "Space",
               "Backspace",
               "Delete",
               "Insert",
               "Cancel",
               "AppsKey",
               "Escape",
               ": ",
               ":",
               ", ",
               ",",
               "\n",
               "\r\n",
               "# ",
               "#",
               "Control K, Control C: ",
               "Workbench.FocusPanel ",
               "import ",
               ";",
               "\"",
               "'",
               "`",
               "\\",
               "[",
               "]",
               "=",
               "-",
               ".",
               "/",
               "\xFF",
               "\xC0\x80"},
              1024});
Registrar r2({"settings",
              runSettings,
              {"assets/settings.default.txt"},
              {"editor.tabWidth = 4\neditor.wordWrap = true\ncolor.text = #aabbcc\nfont.editor = \"Cascadia Mono, "
               "11\"\neditor.indentUnit = tab\n",
               "[section]\nnokey\n= nothing\nkey =\n = x\neditor.tabWidth = 99999999999999999999\neditor.tabWidth = "
               "-1\neditor.tabWidth = 1e3\n"},
              {" = ",
               "=",
               "\n",
               "\r\n",
               "# ",
               "#",
               "true",
               "false",
               "tab",
               "0",
               "-1",
               "1e308",
               "1e-400",
               "nan",
               "inf",
               "99999999999999999999",
               "0x10",
               "#rrggbb",
               "#12345",
               "#1234567",
               "#123456789",
               "#ggg",
               "\"",
               "'",
               ", ",
               "editor.",
               "ui.",
               "font.",
               "keys.import10x",
               "editor.tabWidth",
               "editor.indentUnit",
               "editor.fontSize",
               "theme",
               ".",
               "..",
               "[",
               "]",
               "\xFF",
               "\xE4\xB8\xAD"},
              1024});
Registrar r3({"theme",
              runTheme,
              {"assets/themes:.txt"},
              {"color.background = #112233\ncolor.text = #ffffff80\ntoken.keyword = "
               "#ff0000\ntoken.function.declaration = #00ff00\ntoken.nonsense.mod = #000\n",
               "color.background = red\ncolor. = #fff\ntoken. = #fff\ntoken..x = #fff\n"},
              {"color.",
               "token.",
               "token.keyword",
               "token.function",
               "token.comment",
               "token.variable.readonly",
               "token.class.declaration",
               ".",
               " = ",
               "#",
               "#fff",
               "#ffffff",
               "#ffffffff",
               "\n",
               "\r\n",
               "background",
               "text",
               "statusBackground",
               "readonly",
               "static",
               "defaultLibrary",
               "=",
               "\xFF"},
              1024});

}  // namespace
}  // namespace tf::fuzz
