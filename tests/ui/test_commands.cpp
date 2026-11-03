// Command registry, command names (10x aliases), key map (chord state machine) and the 10x-format key binding parser
// and defaults. No GL, no SDL window.
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "ui/commands/CommandNames.h"
#include "ui/commands/CommandRegistry.h"
#include "ui/commands/EditorCommands.h"
#include "ui/commands/KeyMap.h"
#include "ui/settings/KeyBindings.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
namespace fs = std::filesystem;

namespace {

KeyChord K(Key k, uint8_t m = ModNone) { return chordFromKey(k, m); }
KeyChord Ch(char c, uint8_t m = ModNone) { return chordFromKey(static_cast<Key>(c), m); }

KeyChord chord(const char* s) {
  KeyChord c;
  std::string err;
  bool ok = parseKeyChord(s, c, err);
  if (!ok) tftest::fail(__FILE__, __LINE__, std::string("parseKeyChord(") + s + "): " + err);
  return c;
}

std::vector<KeyChord> chords(const char* s) {
  std::vector<KeyChord> v;
  std::string err;
  if (!parseKeyChords(s, v, err)) tftest::fail(__FILE__, __LINE__, std::string("parseKeyChords(") + s + "): " + err);
  return v;
}

struct Parsed {
  KeyMap map;
  std::vector<KeyBindingDiag> diags;
  int errors() const {
    int n = 0;
    for (auto& d : diags) n += !d.warning;
    return n;
  }
  int warnings() const { return static_cast<int>(diags.size()) - errors(); }
};
Parsed parse(std::string_view text) {
  Parsed p;
  parseKeyBindings(text, "test.10x_settings", p.map, p.diags);
  return p;
}

const KeyBinding* findB(const KeyMap& m, const char* chordText, const char* ctx = "global") {
  const std::vector<KeyChord> seq = chords(chordText);
  for (auto& b : m.bindings())
    if (b.chords == seq && b.context == ctx) return &b;
  return nullptr;
}

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// The expected 10x defaults, generated from the 10x documentation: "<keys>\t<command>" per line.
std::vector<std::pair<std::string, std::string>> expectedDefaults() {
  std::vector<std::pair<std::string, std::string>> out;
  std::istringstream in(slurp(fs::path(TENFOLD_SOURCE_DIR) / "tests" / "fixtures" / "KeyMappings.10x.expected.txt"));
  for (std::string line; std::getline(in, line);) {
    if (line.empty() || line[0] == '#') continue;
    const size_t tab = line.find('\t');
    if (tab == std::string::npos) tftest::fail(__FILE__, __LINE__, "bad fixture line: " + line);
    out.emplace_back(line.substr(0, tab), line.substr(tab + 1));
  }
  return out;
}

std::pair<std::string, std::string> splitDefaults() {
  const std::string all(defaultKeyBindingsText());
  const size_t at = all.find(kTenfoldAdditionsMarker);
  if (at == std::string::npos) tftest::fail(__FILE__, __LINE__, "defaults lack the tenfold additions marker");
  return {all.substr(0, at), all.substr(at)};
}

}  // namespace

// --- chords and key names ------------------------------------------------------------------------------------------

TEST(cmd_parse_every_10x_key_name) {
  // Every key name that appears in 10x's key map documentation, plus the modifier spellings.
  for (char c = 'A'; c <= 'Z'; ++c) {
    const std::string up(1, c), lo(1, static_cast<char>(c - 'A' + 'a'));
    CHECK(chord(up.c_str()) == Ch(c));
    CHECK(chord(lo.c_str()) == Ch(c));  // case-insensitive
    CHECK_EQ(formatChord(chord(up.c_str())), up);
  }
  for (char c = '0'; c <= '9'; ++c) {
    const std::string s(1, c);
    CHECK(chord(s.c_str()) == Ch(c));
    CHECK_EQ(formatChord(chord(s.c_str())), s);
  }
  for (int n = 1; n <= 24; ++n) {
    const std::string s = "F" + std::to_string(n);
    CHECK(chord(s.c_str()) == K(static_cast<Key>(static_cast<uint32_t>(Key::F1) + static_cast<uint32_t>(n - 1))));
    CHECK_EQ(formatChord(chord(s.c_str())), s);
  }
  const std::pair<const char*, Key> named[] = {{"Up", Key::Up},
                                               {"Down", Key::Down},
                                               {"Left", Key::Left},
                                               {"Right", Key::Right},
                                               {"Home", Key::Home},
                                               {"End", Key::End},
                                               {"PageUp", Key::PageUp},
                                               {"PageDown", Key::PageDown},
                                               {"Enter", Key::Enter},
                                               {"Tab", Key::Tab},
                                               {"Space", Key::Space},
                                               {"Backspace", Key::Backspace},
                                               {"Delete", Key::Delete},
                                               {"Insert", Key::Insert},
                                               {"Cancel", Key::Pause},
                                               {"AppsKey", Key::Menu},
                                               {"Escape", Key::Escape},
                                               {"KpEnter", Key::KpEnter},
                                               {"Comma", static_cast<Key>(',')}};
  for (auto& [name, key] : named) {
    CHECK(chord(name) == K(key));
    CHECK_EQ(formatChord(chord(name)), std::string(name));
    std::string lowered = name;
    for (auto& ch : lowered) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    CHECK(chord(lowered.c_str()) == K(key));
  }
  for (char c : std::string("'./=[]\\-;`")) {  // the punctuation keys
    const std::string s(1, c);
    CHECK(chord(s.c_str()) == Ch(c));
    CHECK_EQ(formatChord(chord(s.c_str())), s);
  }
  CHECK(chord("MouseMiddleButton") == chordFromMouse(MouseButton::Middle, ModNone));
  CHECK(chord("MouseBackButton") == chordFromMouse(MouseButton::X1, ModNone));
  CHECK(chord("MouseForwardButton") == chordFromMouse(MouseButton::X2, ModNone));
  CHECK_EQ(formatChord(chord("MouseMiddleButton")), std::string("MouseMiddleButton"));
  CHECK_EQ(formatChord(chord("Control MouseMiddleButton")), std::string("Control MouseMiddleButton"));
  CHECK_EQ(formatChord(chord("mousebackbutton")), std::string("MouseBackButton"));
  CHECK_EQ(formatChord(chord("MouseForwardButton")), std::string("MouseForwardButton"));
}

TEST(cmd_parse_chord_forms) {
  CHECK(chord("Control Shift -") == K(static_cast<Key>('-'), ModCtrl | ModShift));
  CHECK(chord("Shift Alt =") == K(static_cast<Key>('='), ModShift | ModAlt));
  CHECK(chord("Alt Shift =") == chord("Shift Alt ="));  // modifier order is free
  CHECK(chord("control shift alt j") == Ch('J', ModCtrl | ModShift | ModAlt));
  CHECK(chord("Control Shift Alt /") == K(static_cast<Key>('/'), ModCtrl | ModShift | ModAlt));
  CHECK(chord("Control '") == K(static_cast<Key>('\''), ModCtrl));
  CHECK(chord("Control Shift \\") == K(static_cast<Key>('\\'), ModCtrl | ModShift));
  CHECK(chord("Control Shift [") == K(static_cast<Key>('['), ModCtrl | ModShift));
  CHECK(chord("Alt .") == K(static_cast<Key>('.'), ModAlt));
  CHECK(chord("Alt Comma") == K(static_cast<Key>(','), ModAlt));
  CHECK(chord("Control Cancel") == K(Key::Pause, ModCtrl));
  CHECK(chord("Shift F14") == K(static_cast<Key>(static_cast<uint32_t>(Key::F1) + 13), ModShift));
  CHECK(chord("Ctrl S") == chord("Control S"));
  CHECK_EQ(formatChord(chord("alt shift alt control a")), std::string("Control Shift Alt A"));  // canonical order

  const auto seq = chords("Control S, Control S");
  REQUIRE(seq.size() == 2u);
  CHECK(seq[0] == Ch('S', ModCtrl) && seq[1] == Ch('S', ModCtrl));
  CHECK_EQ(formatChords(seq), std::string("Control S, Control S"));
  CHECK_EQ(chords("Control K,Control C").size(), 2u);  // the space after the comma is optional
  CHECK_EQ(chords("Control K ,  Control C").size(), 2u);
  CHECK_EQ(chords("F5").size(), 1u);
  CHECK(chords("Control Comma")[0] == K(static_cast<Key>(','), ModCtrl));
  CHECK_EQ(formatChords(chords("Control Shift Comma")), std::string("Control Shift Comma"));

  std::string err;
  KeyChord c;
  CHECK(!parseKeyChord("", c, err));
  CHECK(!parseKeyChord("Control", c, err));  // a modifier is not a key
  CHECK(err.find("unknown key") != std::string::npos);
  CHECK(!parseKeyChord("Hyper A", c, err));
  CHECK(err.find("unknown modifier") != std::string::npos);
  CHECK(!parseKeyChord("Control+S", c, err));
  CHECK(err.find("spaces") != std::string::npos);
  CHECK(!parseKeyChord("F25", c, err));
  CHECK(!parseKeyChord("F0", c, err));
  CHECK(!parseKeyChord("MouseLeftButton", c, err));
  CHECK(err.find("not bindable") != std::string::npos);
  CHECK(!parseKeyChord("Mouse4", c, err));  // the old tenfold spelling is gone
  CHECK(!parseKeyChord(",", c, err));       // a comma key is written Comma
  std::vector<KeyChord> v;
  CHECK(!parseKeyChords("Control K,", v, err));
  CHECK(!parseKeyChords(", Control K", v, err));
  CHECK(!parseKeyChords("Control K, , Control C", v, err));
  CHECK(!parseKeyChords("Control K, Bogus", v, err));
}

// --- the file format -----------------------------------------------------------------------------------------------

TEST(cmd_parse_good_lines) {
  Parsed p = parse(
      "# comment\n"
      "\n"
      "   # indented comment\n"
      "Control Shift -: NextLocation\n"
      "Control K, Control C: Editor.Undo\n"
      "Alt 1: Workbench.FocusPanel 1\r\n"
      "Control =: PrevLocationCurrentFile\n"
      "Shift Alt =: SelectCurrentScope\n"
      "Control ':   NewTerminal  \n"
      "Control Shift Alt /: OpenFolderForActivefile\n"
      "F6:   Build.Build   make -j8  \n");
  CHECK_EQ(p.diags.size(), 0u);
  CHECK_EQ(p.map.bindings().size(), 8u);
  auto* a = findB(p.map, "Control Shift -");
  REQUIRE(a);
  CHECK_EQ(a->command, std::string("Nav.Forward"));  // NextLocation is tenfold's Nav.Forward
  CHECK_EQ(a->written, std::string("NextLocation"));
  CHECK_EQ(a->context, std::string("global"));
  auto* b = findB(p.map, "Control K, Control C", "editor");  // Editor.Undo belongs to the editor context
  REQUIRE(b);
  CHECK_EQ(b->chords.size(), 2u);
  auto* c = findB(p.map, "Alt 1");
  REQUIRE(c);
  CHECK_EQ(c->command, std::string("Workbench.FocusPanel"));
  CHECK_EQ(c->arg, std::string("1"));
  CHECK(findB(p.map, "Control ="));
  CHECK(findB(p.map, "Shift Alt =", "editor"));
  CHECK(findB(p.map, "Control '"));
  CHECK(findB(p.map, "Control Shift Alt /"));
  auto* d = findB(p.map, "F6");
  REQUIRE(d);
  CHECK_EQ(d->command, std::string("Build.Build"));
  CHECK_EQ(d->arg, std::string("make -j8"));
}

TEST(cmd_parse_error_classes_have_line_numbers) {
  Parsed p = parse(
      "Control X: Good.One\n"               // 1 ok
      "Control Y Foo.Bar\n"                 // 2 no colon
      "Control Z:Foo.Bar\n"                 // 3 no space after the colon (10x requires it)
      ": Foo.Bar\n"                         // 4 no keys
      "Control Foo: Foo.Bar\n"              // 5 unknown key
      "Hyper A: Foo.Bar\n"                  // 6 unknown modifier
      "Control K,: Foo.Bar\n"               // 7 empty chord
      "Control Q = Foo.Bar\n"               // 8 the old tenfold format
      "Control Shift Alt: Foo.Bar\n"        // 9 modifiers only
      "MouseLeftButton: Foo.Bar\n"          // 10 unbindable mouse button
      "F25: Foo.Bar\n"                      // 11 F25 does not exist
      "Control A, Control Nope: Foo.Bar\n"  // 12 error in the second stroke
      "Control+Shift+F: Search\n"           // 13 VS style
      "Control W: Still.Loads\n");          // 14 loading continues
  CHECK_EQ(p.errors(), 12);
  CHECK_EQ(p.warnings(), 0);
  REQUIRE(p.diags.size() == 12u);
  for (size_t i = 0; i < p.diags.size(); ++i) {
    CHECK_EQ(p.diags[i].line, static_cast<int>(i) + 2);  // every bad line reported once, in order
    CHECK(!p.diags[i].message.empty());
    CHECK_EQ(p.diags[i].source, std::string("test.10x_settings"));
  }
  CHECK(findB(p.map, "Control X"));
  CHECK(findB(p.map, "Control W"));  // the rest of the file still loaded
  CHECK_EQ(p.map.bindings().size(), 2u);
  CHECK(formatDiag(p.diags[0]).find("test.10x_settings:2: error:") == 0);
  // The old format gets a message that points at the new one.
  CHECK(p.diags[6].message.find("no longer supported") != std::string::npos);
  CHECK(p.diags[6].message.find("Control Shift F: Search") != std::string::npos);
}

TEST(cmd_parse_python_statements_are_unsupported_not_errors) {
  Parsed p = parse(
      "Alt H: HelloWorld()\n"
      "Alt J: x = 1\n"
      "Alt K: Editor.Foo(\"a\")\n"
      "Alt L: ToggleBookmark\n");
  CHECK_EQ(p.errors(), 0);
  CHECK_EQ(p.warnings(), 3);
  for (auto& d : p.diags) CHECK(d.message.find("Python") != std::string::npos);
  for (const char* k : {"Alt H", "Alt J", "Alt K"}) {
    auto* b = findB(p.map, k);
    REQUIRE(b);
    CHECK_EQ(b->command, std::string(kPythonCommand));  // the key stays bound (it overrides the default) ...
  }
  CHECK_EQ(findB(p.map, "Alt H")->arg, std::string("HelloWorld()"));  // ... and keeps the statement for the message
  CHECK_EQ(findB(p.map, "Alt L")->command, std::string("Editor.ToggleBookmark"));
}

TEST(cmd_parse_duplicate_warns_later_wins) {
  Parsed p = parse(
      "Control X: First.One\n"
      "control x: Second.One\n");
  CHECK_EQ(p.errors(), 0);
  CHECK_EQ(p.warnings(), 1);
  CHECK_EQ(p.diags[0].line, 2);
  CHECK(p.diags[0].warning);
  REQUIRE(findB(p.map, "Control X"));
  CHECK_EQ(findB(p.map, "Control X")->command, std::string("Second.One"));
  CHECK_EQ(p.map.bindings().size(), 1u);
}

TEST(cmd_parse_same_keys_in_two_contexts_coexist_within_one_file) {
  // 10x lists Up twice (cursor, autocomplete list): tenfold files them by the commands' contexts.
  Parsed p = parse("Up: MoveCursorUp\nUp: AutocompleteUp\nEnter: SubmitRenameSymbol\nEnter: Editor.Enter\n");
  CHECK_EQ(p.diags.size(), 0u);
  CHECK_EQ(p.map.bindings().size(), 4u);
  CHECK_EQ(findB(p.map, "Up", "editor")->command, std::string("Editor.CaretUp"));
  CHECK_EQ(findB(p.map, "Up", "popup")->written, std::string("AutocompleteUp"));
  CHECK_EQ(findB(p.map, "Enter", "popup")->written, std::string("SubmitRenameSymbol"));
  CHECK_EQ(findB(p.map, "Enter", "editor")->command, std::string("Editor.Enter"));
  // The editor does not see the popup binding and the other way round.
  CHECK_EQ(p.map.feed(K(Key::Up), "editor", 0).binding->command, std::string("Editor.CaretUp"));
  CHECK(p.map.feed(K(Key::Up), "picker", 0).status == KeyMap::Status::NoMatch);  // pickers keep their own Up
  CHECK(p.map.feed(K(Key::Up), "global", 0).status == KeyMap::Status::NoMatch);
}

TEST(cmd_overlay_order_and_unbind) {
  KeyMap m;
  std::vector<KeyBindingDiag> d;
  parseKeyBindings("F5: Default.Run\nF6: Default.Other\nUp: MoveCursorUp\nUp: AutocompleteUp\n", "defaults", m, d);
  parseKeyBindings("F5: User.Run\nF6:\nF7: User.New\nUp: User.Up\n", "user", m, d);
  parseKeyBindings("F7: Workspace.New\n", "workspace", m, d);
  CHECK_EQ(d.size(), 0u);  // overriding an earlier layer is not a duplicate
  CHECK_EQ(findB(m, "F5")->command, std::string("User.Run"));
  CHECK(!findB(m, "F6"));  // an empty value removes the binding
  CHECK_EQ(findB(m, "F7")->command, std::string("Workspace.New"));
  // A later layer replaces the keys in every context: neither the editor nor the popup Up survives.
  CHECK(!findB(m, "Up", "editor"));
  CHECK(!findB(m, "Up", "popup"));
  CHECK_EQ(findB(m, "Up")->command, std::string("User.Up"));
  CHECK_EQ(m.bindings().size(), 3u);
}

TEST(cmd_parse_bom_and_crlf) {
  Parsed p = parse(
      "\xEF\xBB\xBF"
      "Control X: A.One\r\nControl Y: A.Two\r\n");
  CHECK_EQ(p.diags.size(), 0u);
  CHECK_EQ(p.map.bindings().size(), 2u);
}

TEST(cmd_load_files_overlay_import_and_legacy) {
  fs::path dir = uitest::tempRoot() / ("tf_keys_" + std::to_string(reinterpret_cast<uintptr_t>(&dir)));
  fs::create_directories(dir / "user");
  fs::create_directories(dir / "ws" / ".tenfold");
  fs::create_directories(dir / "10x");
  {
    std::ofstream(dir / "10x" / "KeyMappings.10x_settings", std::ios::binary)
        << "F12: ToggleBookmark\nF7: Imported.F7\nF2: Imported.F2\nF3: Imported.F3\nAlt H: HelloWorld()\nbroken line\n";
    std::ofstream(dir / "user" / "KeyMappings.10x_settings", std::ios::binary) << "F12: User.Goto\nF2:\n";
    std::ofstream(dir / "user" / "keybindings.txt", std::ios::binary) << "Ctrl+Q = App.Quit\n";
    std::ofstream(dir / "ws" / ".tenfold" / "KeyMappings.10x_settings", std::ios::binary) << "F7: Ws.Wins\n";
  }
  KeyLoadOptions o;
  o.workspaceRoot = dir / "ws";
  o.userDir = dir / "user";
  o.import10xFile = dir / "10x" / "KeyMappings.10x_settings";
  std::vector<KeyBindingDiag> d;
  KeyMap m = loadKeyBindings(o, d);
  CHECK_EQ(findB(m, "F12")->command, std::string("User.Goto"));              // user beats the import
  CHECK_EQ(findB(m, "F7")->command, std::string("Ws.Wins"));                 // workspace beats the import
  CHECK(!findB(m, "F2"));                                                    // the user unbinds an imported key
  CHECK_EQ(findB(m, "F3")->command, std::string("Imported.F3"));             // the import beats the defaults
  CHECK_EQ(findB(m, "F9")->command, std::string("Debug.ToggleBreakpoint"));  // the defaults are still there
  CHECK_EQ(findB(m, "Alt H")->command, std::string(kPythonCommand));
  int errors = 0, warnings = 0, legacy = 0;
  for (auto& x : d) {
    errors += !x.warning;
    warnings += x.warning;
    legacy += x.message.find("no longer read") != std::string::npos;
  }
  CHECK_EQ(errors, 1);    // "broken line" in the import
  CHECK_EQ(legacy, 1);    // the leftover keybindings.txt
  CHECK_EQ(warnings, 2);  // Python statement + legacy file

  // keys.import10x off: the 10x file is ignored.
  o.import10x = false;
  std::vector<KeyBindingDiag> d2;
  KeyMap m2 = loadKeyBindings(o, d2);
  CHECK_EQ(findB(m2, "F3")->command, std::string("Editor.FindNext"));
  CHECK(!findB(m2, "F2"));
  CHECK(!findB(m2, "Alt H"));
  // Missing files are not an error.
  std::vector<KeyBindingDiag> d3;
  KeyLoadOptions none;
  none.workspaceRoot = dir / "nowhere";
  none.userDir = dir / "nada";
  none.import10xFile = dir / "nothing";
  KeyMap m3 = loadKeyBindings(none, d3);
  CHECK_EQ(d3.size(), 0u);
  CHECK(findB(m3, "F12"));
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// --- the key map state machine -------------------------------------------------------------------------------------

TEST(cmd_chord_single_and_mouse) {
  Parsed p = parse("F12: A.One\nMouseBackButton: PrevLocation\nControl MouseForwardButton: NextLocation\n");
  auto r = p.map.feed(K(Key::F12), "editor", 0);
  CHECK(r.status == KeyMap::Status::Matched);
  CHECK_EQ(r.binding->command, std::string("A.One"));
  r = p.map.feed(chordFromMouse(MouseButton::X1, 0), "editor", 10);
  CHECK(r.status == KeyMap::Status::Matched);
  CHECK_EQ(r.binding->command, std::string("Nav.Back"));
  r = p.map.feed(chordFromMouse(MouseButton::X2, ModCtrl), "editor", 20);
  CHECK_EQ(r.binding->command, std::string("Nav.Forward"));
  r = p.map.feed(chordFromMouse(MouseButton::X2, 0), "editor", 30);
  CHECK(r.status == KeyMap::Status::NoMatch);
  r = p.map.feed(K(Key::F11), "editor", 40);
  CHECK(r.status == KeyMap::Status::NoMatch);
  CHECK(!r.abortedChord);
}

TEST(cmd_chord_two_stroke_timeout_and_mismatch) {
  Parsed q = parse("Control K, Control C: Edit.Comment\nControl K, Control U: Edit.Uncomment\nF1: Help\n");
  auto r = q.map.feed(Ch('K', ModCtrl), "editor", 1000);
  CHECK(r.status == KeyMap::Status::Pending);
  CHECK_EQ(r.pendingText, std::string("Control K\xE2\x80\xA6"));
  CHECK(q.map.pending());
  r = q.map.feed(Ch('C', ModCtrl), "editor", 1500);
  CHECK(r.status == KeyMap::Status::Matched);
  CHECK_EQ(r.binding->command, std::string("Edit.Comment"));
  CHECK(!q.map.pending());

  // Mismatch mid-chord: reset, the offending stroke is swallowed.
  r = q.map.feed(Ch('K', ModCtrl), "editor", 2000);
  CHECK(r.status == KeyMap::Status::Pending);
  r = q.map.feed(K(Key::F1), "editor", 2100);
  CHECK(r.status == KeyMap::Status::NoMatch);
  CHECK(r.abortedChord);
  CHECK(!q.map.pending());
  r = q.map.feed(K(Key::F1), "editor", 2200);  // and is a normal key again afterwards
  CHECK(r.status == KeyMap::Status::Matched);

  // Timeout: the second stroke arrives too late and is treated as a fresh first stroke.
  r = q.map.feed(Ch('K', ModCtrl), "editor", 3000);
  CHECK(r.status == KeyMap::Status::Pending);
  r = q.map.feed(Ch('C', ModCtrl), "editor", 3000 + q.map.timeoutMs + 1);
  CHECK(r.status == KeyMap::Status::NoMatch);
  CHECK(!r.abortedChord);

  // tick() expires a stale pending chord so the status bar can clear.
  r = q.map.feed(Ch('K', ModCtrl), "editor", 5000);
  CHECK(!q.map.tick(5100));
  CHECK(q.map.tick(5000 + q.map.timeoutMs + 1));
  CHECK(!q.map.pending());
}

TEST(cmd_chord_that_is_also_the_start_of_a_longer_chord) {
  // 10x binds both "Control S" (SaveFile) and "Control S, Control S" (SaveAll).
  Parsed p = parse("Control S: SaveFile\nControl S, Control S: SaveAll\nControl X: Cut\n");
  CHECK_EQ(p.diags.size(), 0u);
  auto r = p.map.feed(Ch('S', ModCtrl), "editor", 1000);
  CHECK(r.status == KeyMap::Status::Matched);  // saves at once
  CHECK_EQ(r.binding->command, std::string("File.Save"));
  CHECK(p.map.pending());  // ... and waits for a second stroke
  r = p.map.feed(Ch('S', ModCtrl), "editor", 1300);
  CHECK(r.status == KeyMap::Status::Matched);
  CHECK_EQ(r.binding->written, std::string("SaveAll"));
  CHECK(!p.map.pending());
  // A different stroke after the first one is an ordinary stroke: not swallowed.
  r = p.map.feed(Ch('S', ModCtrl), "editor", 2000);
  CHECK_EQ(r.binding->command, std::string("File.Save"));
  r = p.map.feed(Ch('X', ModCtrl), "editor", 2100);
  CHECK(r.status == KeyMap::Status::Matched);
  CHECK(!r.abortedChord);
  CHECK_EQ(r.binding->command, std::string("Editor.Cut"));
  CHECK(!p.map.pending());
  // After the timeout a second Control S is a fresh save again.
  r = p.map.feed(Ch('S', ModCtrl), "editor", 3000);
  CHECK(p.map.tick(3000 + p.map.timeoutMs + 1));
  r = p.map.feed(Ch('S', ModCtrl), "editor", 6000);
  CHECK_EQ(r.binding->command, std::string("File.Save"));
  // An unrelated stroke that matches nothing is not swallowed either.
  r = p.map.feed(K(Key::F9), "editor", 6100);
  CHECK(r.status == KeyMap::Status::NoMatch);
  CHECK(!r.abortedChord);
}

TEST(cmd_context_priority) {
  Parsed p = parse(
      "Enter: Global.Enter\n"
      "Control A: SelectAll\n"
      "F9: Only.Global\n");
  CHECK_EQ(p.map.feed(K(Key::Enter), "editor", 0).binding->command, std::string("Global.Enter"));
  CHECK_EQ(p.map.feed(Ch('A', ModCtrl), "editor", 0).binding->command, std::string("Editor.SelectAll"));
  CHECK(p.map.feed(Ch('A', ModCtrl), "picker", 0).status == KeyMap::Status::NoMatch);  // editor-only binding
  CHECK(p.map.feed(Ch('A', ModCtrl), "global", 0).status == KeyMap::Status::NoMatch);
  CHECK_EQ(p.map.feed(K(Key::F9), "picker", 0).binding->command, std::string("Only.Global"));
}

TEST(cmd_registry_run_unknown_disabled_unbound) {
  CommandRegistry reg;
  int ran = 0;
  std::string gotArg;
  reg.add({"A.Run",
           "Run",
           [&](const CommandContext&, std::string_view a) {
             ++ran;
             gotArg = std::string(a);
             return true;
           },
           {}});
  bool allow = false;
  reg.add({"A.Gated", "Gated", [&](const CommandContext&, std::string_view) { return ++ran > 0; },
           [&](const CommandContext&) { return allow; }});
  reg.add({"A.Fails", "Fails", [](const CommandContext&, std::string_view) { return false; }, {}});
  reg.add({"A.Slot", "Slot", {}, {}});
  CommandContext ctx;

  CHECK(reg.run("A.Run", "42", ctx).ok());
  CHECK_EQ(gotArg, std::string("42"));
  CHECK(reg.run("No.Such", "", ctx).status == RunStatus::UnknownCommand);
  CHECK(reg.run("No.Such", "", ctx).message.find("No.Such") != std::string::npos);
  CHECK(reg.run("A.Gated", "", ctx).status == RunStatus::Disabled);
  CHECK_EQ(ran, 1);
  allow = true;
  CHECK(reg.run("A.Gated", "", ctx).ok());
  CHECK(reg.run("A.Fails", "", ctx).status == RunStatus::Failed);
  CHECK(reg.run("A.Slot", "", ctx).status == RunStatus::Unbound);
  CHECK(reg.bind("A.Slot", [](const CommandContext&, std::string_view) { return true; }));
  CHECK(reg.run("A.Slot", "", ctx).ok());
  CHECK(!reg.bind("No.Such", {}));

  auto all = reg.all();
  REQUIRE(all.size() == 4u);
  CHECK_EQ(all[0]->id, std::string("A.Fails"));  // sorted by id
  CHECK_EQ(all[0]->name, std::string("Fails"));
}

// --- command names (10x aliases) -----------------------------------------------------------------------------------

TEST(cmd_alias_table_points_at_registered_commands) {
  CommandRegistry reg;
  registerEditorCommands(reg);
  std::set<std::string> seen;
  size_t mapped = 0;
  for (const CommandAlias& a : commandAliases()) {
    CHECK(seen.insert(a.name10x).second);  // every 10x command once
    const std::string ctx = a.context;
    CHECK(ctx == "global" || ctx == "editor" || ctx == "popup");
    if (!*a.tenfoldId) {
      CHECK(std::string(a.note).empty());
      continue;
    }
    ++mapped;
    if (!reg.find(a.tenfoldId))
      std::cerr << "    alias target not registered: " << a.name10x << " -> " << a.tenfoldId << "\n";
    CHECK(reg.find(a.tenfoldId) != nullptr);
    CHECK(!displayName10x(a.tenfoldId).empty());
  }
  CHECK(commandAliases().size() >= 300u);  // all of Commands.txt
  CHECK(mapped >= 80u);
  // Two 10x names for one tenfold command: the first row is the one the palette shows.
  CHECK_EQ(std::string(displayName10x("File.OpenFile")), std::string("FindFile"));
  CHECK_EQ(std::string(displayName10x("Vcs.RevertFile")), std::string("PerforceRevert"));
  CHECK_EQ(std::string(displayName10x("Editor.Enter")), std::string());  // tenfold-only commands have no 10x name
}

TEST(cmd_alias_resolution) {
  const std::pair<const char*, const char*> want[] = {{"PrevLocation", "Nav.Back"},
                                                      {"NextLocation", "Nav.Forward"},
                                                      {"PrevEditLocation", "Nav.LastEditLocation"},
                                                      {"GotoSymbolDefinition", "Editor.GotoDefinition"},
                                                      {"FindSymbolReferences", "Editor.FindReferences"},
                                                      {"CppParser.ToggleSourceHeader", "Editor.SwitchHeaderSource"},
                                                      {"ShowCommandPanel", "App.CommandPalette"},
                                                      {"OpenCurrentFileInDebugger", "Debug.OpenInVisualStudio"},
                                                      {"StartDebugging", "Debug.Start"},
                                                      {"RestartDebugging", "Debug.Restart"},
                                                      {"StopDebugging", "Debug.Stop"},
                                                      {"DebugStepOver", "Debug.StepOver"},
                                                      {"Run", "Debug.StartWithoutDebugging"},
                                                      {"ToggleBreakpoint", "Debug.ToggleBreakpoint"},
                                                      {"BuildActiveWorkspace", "Build.Build"},
                                                      {"CancelBuild", "Build.Cancel"},
                                                      {"GotoNextBuildWarningOrError", "Build.NextError"},
                                                      {"PerforceOpenForEdit", "Vcs.CheckOut"},
                                                      {"PerforceRevert", "Vcs.RevertFile"},
                                                      {"PerforceDiff", "Vcs.DiffFileExternal"},
                                                      {"SaveFile", "File.Save"},
                                                      {"CloseFile", "Workbench.CloseTab"},
                                                      {"MoveCursorUp", "Editor.CaretUp"},
                                                      {"SelectToLineEnd", "Editor.SelectLineEnd"},
                                                      {"MoveCursorPrevWordSelect", "Editor.SelectWordLeft"},
                                                      {"FindInFile", "Editor.Find"},
                                                      {"FindReplaceInFile", "Editor.Replace"},
                                                      {"GoToLine", "Editor.GotoLine"},
                                                      {"UnindentLine", "Editor.Outdent"},
                                                      {"SelectNextMultiCursor", "Editor.AddCaretAtNextOccurrence"},
                                                      {"Exit", "App.Quit"}};
  for (auto& [n10, id] : want) {
    const ResolvedCommand r = resolveCommandName(n10);
    CHECK(r.known10x);
    CHECK(r.implemented);
    CHECK_EQ(r.id, std::string(id));
    CHECK_EQ(r.name10x, std::string(n10));
  }
  // 10x matches names without regard to case.
  CHECK_EQ(resolveCommandName("openfile").id, std::string("File.OpenFile"));
  CHECK_EQ(resolveCommandName("OPENFILE").name10x, std::string("OpenFile"));
  // A tenfold id keeps working and reports the 10x name of the command.
  const ResolvedCommand t = resolveCommandName("Editor.GotoDefinition");
  CHECK(!t.known10x);
  CHECK(t.implemented);
  CHECK_EQ(t.id, std::string("Editor.GotoDefinition"));
  CHECK_EQ(t.name10x, std::string("GotoSymbolDefinition"));
  // 10x commands tenfold lacks keep their 10x name as id and say so.
  const ResolvedCommand m = resolveCommandName("SaveAll");
  CHECK(m.known10x);
  CHECK(!m.implemented);
  CHECK_EQ(m.id, std::string("SaveAll"));
  // Unknown names pass through for the registry to reject.
  const ResolvedCommand u = resolveCommandName("Totally.Unknown");
  CHECK(!u.known10x);
  CHECK_EQ(u.id, std::string("Totally.Unknown"));
  // Contexts: caret movement is an editor binding, the autocomplete list's keys are popup bindings.
  CHECK_EQ(resolveCommandName("MoveCursorUp").context, std::string("editor"));
  CHECK_EQ(resolveCommandName("AutocompleteUp").context, std::string("popup"));
  CHECK_EQ(resolveCommandName("SubmitRenameSymbol").context, std::string("popup"));
  CHECK_EQ(resolveCommandName("Search").context, std::string("global"));
  CHECK_EQ(resolveCommandName("Editor.Enter").context, std::string("editor"));
}

TEST(cmd_ui_only_commands_report_unbound_and_editor_commands_need_editor) {
  CommandRegistry reg;
  registerEditorCommands(reg);
  CommandContext ctx;
  CHECK(reg.run("File.OpenFile", "", ctx).status == RunStatus::Unbound);
  CHECK(reg.run("Editor.Paste", "", ctx).status == RunStatus::Unbound);
  CHECK(reg.run("Editor.Undo", "", ctx).status == RunStatus::Disabled);  // no editor / document
  CHECK(reg.run("Nav.Back", "", ctx).status == RunStatus::Disabled);
}

// --- the defaults --------------------------------------------------------------------------------------------------

TEST(cmd_defaults_are_10x_defaults_plus_marked_additions) {
  const auto [part10x, additions] = splitDefaults();
  Parsed p10;
  parseKeyBindings(part10x, "10x part", p10.map, p10.diags);
  Parsed pAdd;
  parseKeyBindings(additions, "additions", pAdd.map, pAdd.diags);
  for (auto& d : p10.diags) std::cerr << "    " << formatDiag(d) << "\n";
  for (auto& d : pAdd.diags) std::cerr << "    " << formatDiag(d) << "\n";
  CHECK_EQ(p10.diags.size(), 0u);
  CHECK_EQ(pAdd.diags.size(), 0u);
  Parsed all = parse(defaultKeyBindingsText());
  CHECK_EQ(all.diags.size(), 0u);
  CHECK_EQ(all.map.bindings().size(), p10.map.bindings().size() + pAdd.map.bindings().size());

  // The 10x part is exactly the documented key map: same keys, same command names, same order.
  const auto expected = expectedDefaults();
  REQUIRE(expected.size() == 183u);
  REQUIRE(p10.map.bindings().size() == expected.size());
  std::set<std::pair<std::string, std::string>> want(expected.begin(), expected.end()), got;
  for (size_t i = 0; i < expected.size(); ++i) {
    const KeyBinding& b = p10.map.bindings()[i];
    got.emplace(formatChords(b.chords), b.written);
    CHECK_EQ(formatChords(b.chords), expected[i].first);
    CHECK_EQ(b.written, expected[i].second);
  }
  CHECK(got == want);
  CHECK_EQ(got.size(), expected.size());

  // Every command name in it is a 10x command, and the ones tenfold implements resolve to registered commands.
  CommandRegistry reg;
  registerEditorCommands(reg);
  size_t implemented = 0;
  for (const KeyBinding& b : p10.map.bindings()) {
    const ResolvedCommand r = resolveCommandName(b.written);
    if (!r.known10x) std::cerr << "    not a 10x command: " << b.written << "\n";
    CHECK(r.known10x);
    if (r.implemented) {
      ++implemented;
      if (!reg.find(b.command)) std::cerr << "    unregistered: " << b.written << " -> " << b.command << "\n";
      CHECK(reg.find(b.command) != nullptr);
    } else {
      CHECK_EQ(b.command, r.name10x);  // an unimplemented command keeps its 10x name
    }
  }
  CHECK(implemented > 90u);

  // The additions use no key of 10x's map in the same context, and name tenfold commands (or 10x ones).
  for (const KeyBinding& b : pAdd.map.bindings()) {
    for (const KeyBinding& t : p10.map.bindings())
      if (t.chords == b.chords && t.context == b.context)
        std::cerr << "    addition clashes with 10x: " << formatChords(b.chords) << "\n";
    CHECK(findB(p10.map, formatChords(b.chords).c_str(), b.context.c_str()) == nullptr);
    CHECK(reg.find(b.command) != nullptr);
  }
  // No single key of the additions is the first stroke of a 10x multi-stroke chord ("Control K" would end it early),
  // and no 10x single stroke other than Control S shadows a 10x chord's start.
  for (const KeyBinding& b : all.map.bindings()) {
    if (b.chords.size() != 1) continue;
    for (const KeyBinding& t : all.map.bindings())
      if (t.chords.size() > 1 && t.chords[0] == b.chords[0] && formatChords(b.chords) != "Control S") {
        std::cerr << "    single stroke hides a chord: " << formatChords(b.chords) << "\n";
        CHECK(false);
      }
  }
}

TEST(cmd_defaults_key_contexts) {
  Parsed p = parse(defaultKeyBindingsText());
  CHECK_EQ(findB(p.map, "Up", "editor")->command, std::string("Editor.CaretUp"));
  CHECK_EQ(findB(p.map, "Up", "popup")->written, std::string("AutocompleteUp"));
  CHECK_EQ(findB(p.map, "PageUp", "editor")->command, std::string("Editor.CaretPageUp"));
  CHECK_EQ(findB(p.map, "PageUp", "popup")->written, std::string("AutocompletePageUp"));
  CHECK_EQ(findB(p.map, "Enter", "popup")->written, std::string("SubmitRenameSymbol"));
  CHECK_EQ(findB(p.map, "Enter", "editor")->command, std::string("Editor.Enter"));  // tenfold addition
  // Pickers keep their own arrow keys: nothing global or picker-scoped on them.
  for (Key k : {Key::Up, Key::Down, Key::PageUp, Key::PageDown, Key::Enter, Key::Left, Key::Right, Key::Home, Key::End})
    CHECK(p.map.feed(K(k), "picker", 0).status == KeyMap::Status::NoMatch);
  // The removed placeholders: keys that clash with 10x's map now mean what 10x says.
  CHECK_EQ(findB(p.map, "Control Shift P")->written, std::string("PlaybackKeySequence"));
  CHECK(!findB(p.map, "Control P"));  // would end the Control P, Control E ... chords early
  CHECK_EQ(findB(p.map, "Alt Left")->written, std::string("MovePanelFocusLeft"));
  CHECK_EQ(findB(p.map, "Control Alt Up")->written, std::string("MovePanelUp"));
  CHECK_EQ(findB(p.map, "F8")->command, std::string("Build.NextError"));
  CHECK_EQ(findB(p.map, "F4")->command, std::string("Search.NextResult"));
  CHECK_EQ(findB(p.map, "Control Shift M", "editor")->written, std::string("SelectCurrentBracketScope"));
  CHECK_EQ(findB(p.map, "Control Cancel")->command, std::string("Build.Cancel"));
  CHECK_EQ(findB(p.map, "Control Tab")->command, std::string("Workbench.CycleMruNext"));
  CHECK_EQ(findB(p.map, "Control Shift X")->command, std::string("App.CommandPalette"));
  CHECK_EQ(findB(p.map, "Control P, Control R")->command, std::string("Vcs.RevertFile"));
  CHECK_EQ(findB(p.map, "Control P, Control E")->command, std::string("Vcs.CheckOut"));
  CHECK_EQ(findB(p.map, "Control P, Control D")->command, std::string("Vcs.DiffFileExternal"));
  CHECK_EQ(findB(p.map, "MouseBackButton")->command, std::string("Nav.Back"));
  CHECK_EQ(findB(p.map, "MouseForwardButton")->command, std::string("Nav.Forward"));
  CHECK_EQ(findB(p.map, "Control Shift Backspace")->command, std::string("Nav.LastEditLocation"));
  CHECK_EQ(findB(p.map, "Alt O")->command, std::string("Editor.SwitchHeaderSource"));
  CHECK_EQ(findB(p.map, "Alt G")->command, std::string("Editor.GotoDefinition"));
}

TEST(cmd_end_to_end_control_minus_goes_back) {
  fs::path dir = uitest::tempRoot() / ("tf_cmd_e2e_" + std::to_string(reinterpret_cast<uintptr_t>(&dir)));
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "a.txt", std::ios::binary);
    for (int i = 0; i < 300; ++i) out << "line " << i << "\n";
  }
  {
    Editor ed(dir);
    Document* doc = ed.open("a.txt");
    REQUIRE(doc != nullptr);
    CommandRegistry reg;
    registerEditorCommands(reg);
    Parsed p = parse(defaultKeyBindingsText());
    CHECK_EQ(p.diags.size(), 0u);

    const fs::path f = doc->path();
    CHECK(ed.gotoLocation({f, {0, 0}}));
    CHECK(ed.gotoLocation({f, {100, 2}}));  // two "go to definition" style jumps
    CHECK(ed.gotoLocation({f, {200, 4}}));
    CommandContext ctx;
    ctx.editor = &ed;
    ctx.context = "editor";

    auto press = [&](const KeyChord& c) {
      auto r = p.map.feed(c, "editor", 0);
      REQUIRE(r.status == KeyMap::Status::Matched);
      auto rr = reg.run(r.binding->command, r.binding->arg, ctx);
      if (!rr.ok()) std::cerr << "    " << rr.message << "\n";
      CHECK(rr.ok());
    };
    press(K(static_cast<Key>('-'), ModCtrl));  // PrevLocation
    CHECK(ed.cursor() == (Location{f, {100, 2}}));
    press(chordFromMouse(MouseButton::X1, 0));  // thumb button back
    CHECK(ed.cursor() == (Location{f, {0, 0}}));
    press(K(static_cast<Key>('-'), ModCtrl | ModShift));  // NextLocation
    CHECK(ed.cursor() == (Location{f, {100, 2}}));
    press(chordFromMouse(MouseButton::X2, 0));
    CHECK(ed.cursor() == (Location{f, {200, 4}}));
    // Nothing further forward: the command is reported disabled, not silently ignored.
    auto r = p.map.feed(K(static_cast<Key>('-'), ModCtrl | ModShift), "editor", 0);
    CHECK(reg.run(r.binding->command, "", ctx).status == RunStatus::Disabled);

    // Editing keys go through the same path.
    ed.setSelections({caretAt({0, 0})});
    press(K(Key::Down));
    press(K(Key::End, ModShift));
    CHECK_EQ(ed.selectedText(), std::string("line 1"));
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
}
