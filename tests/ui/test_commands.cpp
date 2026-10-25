// Command registry, key map (chord state machine) and key binding parser. No GL, no SDL window.
#include <filesystem>
#include <fstream>

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
  parseKeyBindings(text, "test.txt", p.map, p.diags);
  return p;
}

const KeyBinding* findB(const KeyMap& m, const char* chordText, const char* ctx = "global") {
  std::vector<KeyChord> seq;
  std::string_view s = chordText;
  while (!s.empty()) {
    size_t sp = s.find(' ');
    seq.push_back(chord(std::string(s.substr(0, sp)).c_str()));
    s = sp == std::string_view::npos ? std::string_view{} : s.substr(sp + 1);
  }
  for (auto& b : m.bindings())
    if (b.chords == seq && b.context == ctx) return &b;
  return nullptr;
}

}  // namespace

TEST(cmd_parse_chords_and_aliases) {
  CHECK(chord("Ctrl+Shift+-") == K(static_cast<Key>('-'), ModCtrl | ModShift));
  CHECK(chord("ctrl+minus") == K(static_cast<Key>('-'), ModCtrl));
  CHECK(chord("Ctrl+Plus") == K(static_cast<Key>('+'), ModCtrl));
  CHECK(chord("Ctrl++") == K(static_cast<Key>('+'), ModCtrl));
  CHECK(chord("+") == K(static_cast<Key>('+')));
  CHECK(chord("Ctrl+,") == chord("CTRL+Comma"));
  CHECK(chord("Alt+o") == Ch('O', ModAlt));
  CHECK(chord("pageup") == K(Key::PageUp));
  CHECK(chord("PgDn") == K(Key::PageDown));
  CHECK(chord("F1") == K(Key::F1));
  CHECK(chord("shift+f24") == K(Key::F24, ModShift));
  CHECK(chord("Num0") == Ch('0'));
  CHECK(chord("Win+Space") == K(Key::Space, ModSuper));
  CHECK(chord("Mouse4") == chordFromMouse(MouseButton::X1, ModNone));
  CHECK(chord("Ctrl+mouse5") == chordFromMouse(MouseButton::X2, ModCtrl));
  CHECK_EQ(formatChord(chord("Ctrl+Shift+-")), std::string("Ctrl+Shift+-"));
  CHECK_EQ(formatChord(chord("alt+left")), std::string("Alt+Left"));
  CHECK_EQ(formatChord(chord("Mouse5")), std::string("Mouse5"));
}

TEST(cmd_parse_good_lines) {
  Parsed p = parse(
      "# comment\n"
      "\n"
      "   # indented comment\n"
      "Ctrl+Shift+- = Nav.Forward\n"
      "[editor] Ctrl+K Ctrl+C = Editor.Comment\n"
      "Ctrl+1 = Workbench.FocusPanel 1\n"
      "Ctrl+= = Zoom.In\r\n"
      "= = Weird.Equals\n"
      "[picker]   F2 =   Picker.Rename   some  arg with spaces  \n");
  CHECK_EQ(p.diags.size(), 0u);
  CHECK_EQ(p.map.bindings().size(), 6u);
  auto* a = findB(p.map, "Ctrl+Shift+-");
  REQUIRE(a);
  CHECK_EQ(a->command, std::string("Nav.Forward"));
  CHECK_EQ(a->context, std::string("global"));
  auto* b = findB(p.map, "Ctrl+K Ctrl+C", "editor");
  REQUIRE(b);
  CHECK_EQ(b->chords.size(), 2u);
  auto* c = findB(p.map, "Ctrl+1");
  REQUIRE(c);
  CHECK_EQ(c->command, std::string("Workbench.FocusPanel"));
  CHECK_EQ(c->arg, std::string("1"));
  CHECK(findB(p.map, "Ctrl+="));
  CHECK(findB(p.map, "="));
  auto* d = findB(p.map, "F2", "picker");
  REQUIRE(d);
  CHECK_EQ(d->arg, std::string("some  arg with spaces"));
}

TEST(cmd_parse_error_classes_have_line_numbers) {
  Parsed p = parse(
      "Ctrl+X = Good.One\n"           // 1 ok
      "Ctrl+Y Foo.Bar\n"              // 2 no '='
      "= Foo.Bar\n"                   // 3 no '=' set off by space before it -> not a separator
      " = Foo.Bar\n"                  // 4 (trimmed: same as 3)
      "Ctrl+Foo = Foo.Bar\n"          // 5 unknown key
      "Hyper+A = Foo.Bar\n"           // 6 unknown modifier
      "Ctrl+A =\n"                    // 7 missing command
      "[editor Ctrl+A = Foo.Bar\n"    // 8 missing ]
      "[Bad Ctx] Ctrl+A = Foo.Bar\n"  // 9 bad context
      "Ctrl+A = bad/id\n"             // 10 bad command id
      "Mouse1 = Foo.Bar\n"            // 11 unbindable mouse button
      "F25 = Foo.Bar\n"               // 12 F25 does not exist
      "Ctrl+A Ctrl+Nope = Foo.Bar\n"  // 13 error in second stroke
      "Ctrl+Z = -  extra\n"           // 14 unbind with argument
      "Ctrl+Q = Still.Loads\n");      // 15 loading continues
  CHECK_EQ(p.errors(), 13);
  CHECK_EQ(p.warnings(), 0);
  REQUIRE(p.diags.size() == 13u);
  for (size_t i = 0; i < p.diags.size(); ++i) {
    CHECK_EQ(p.diags[i].line, static_cast<int>(i) + 2);  // every bad line reported once, in order
    CHECK(!p.diags[i].message.empty());
    CHECK_EQ(p.diags[i].source, std::string("test.txt"));
  }
  CHECK(findB(p.map, "Ctrl+X"));
  CHECK(findB(p.map, "Ctrl+Q"));  // the rest of the file still loaded
  CHECK(formatDiag(p.diags[0]).find("test.txt:2: error:") == 0);
}

TEST(cmd_parse_duplicate_warns_later_wins) {
  Parsed p = parse(
      "Ctrl+X = First.One\n"
      "[editor] Ctrl+X = Scoped.One\n"  // different context: not a duplicate
      "ctrl+x = Second.One\n");
  CHECK_EQ(p.errors(), 0);
  CHECK_EQ(p.warnings(), 1);
  CHECK_EQ(p.diags[0].line, 3);
  CHECK(p.diags[0].warning);
  REQUIRE(findB(p.map, "Ctrl+X"));
  CHECK_EQ(findB(p.map, "Ctrl+X")->command, std::string("Second.One"));
  CHECK_EQ(p.map.bindings().size(), 2u);
}

TEST(cmd_overlay_order_and_unbind) {
  KeyMap m;
  std::vector<KeyBindingDiag> d;
  parseKeyBindings("F5 = Default.Run\nF6 = Default.Other\n", "defaults", m, d);
  parseKeyBindings("F5 = User.Run\nF6 = -\nF7 = User.New\n", "user", m, d);
  parseKeyBindings("F7 = Workspace.New\n", "workspace", m, d);
  CHECK_EQ(d.size(), 0u);  // overriding an earlier layer is not a duplicate
  CHECK_EQ(findB(m, "F5")->command, std::string("User.Run"));
  CHECK(!findB(m, "F6"));
  CHECK_EQ(findB(m, "F7")->command, std::string("Workspace.New"));
}

TEST(cmd_load_files_overlay) {
  fs::path dir = fs::temp_directory_path() / ("tf_keys_" + std::to_string(reinterpret_cast<uintptr_t>(&dir)));
  fs::create_directories(dir / "user");
  fs::create_directories(dir / "ws" / ".tenfold");
  {
    std::ofstream(dir / "user" / "keybindings.txt") << "F12 = User.Goto\nF7 = User.Only\nbroken line\n";
    std::ofstream(dir / "ws" / ".tenfold" / "keybindings.txt") << "F7 = Ws.Wins\n";
  }
  std::vector<KeyBindingDiag> d;
  KeyMap m = loadKeyBindings(dir / "ws", dir / "user", d);
  CHECK_EQ(findB(m, "F12")->command, std::string("User.Goto"));      // user beats defaults
  CHECK_EQ(findB(m, "F7")->command, std::string("Ws.Wins"));         // workspace beats user
  CHECK_EQ(findB(m, "Alt+Left")->command, std::string("Nav.Back"));  // defaults still there
  REQUIRE(d.size() == 1u);                                           // only the broken line
  CHECK_EQ(d[0].line, 3);
  CHECK(d[0].source.find("keybindings.txt") != std::string::npos);
  // Missing files are not an error.
  std::vector<KeyBindingDiag> d2;
  KeyMap m2 = loadKeyBindings(dir / "nowhere", dir / "nada", d2);
  CHECK_EQ(d2.size(), 0u);
  CHECK(findB(m2, "F12"));
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(cmd_chord_single_and_mouse) {
  Parsed p = parse("F12 = A.One\nMouse4 = Nav.Back\nCtrl+Mouse5 = Nav.Forward\n");
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
  Parsed p = parse("Ctrl+K Ctrl+C = Edit.Comment\nCtrl+K Ctrl+U = Edit.Uncomment\nCtrl+K = Never.Alone\n");
  // The single-stroke Ctrl+K binding matches immediately (exact beats waiting for a longer chord).
  CHECK_EQ(p.errors(), 0);

  Parsed q = parse("Ctrl+K Ctrl+C = Edit.Comment\nCtrl+K Ctrl+U = Edit.Uncomment\nF1 = Help\n");
  auto r = q.map.feed(Ch('K', ModCtrl), "editor", 1000);
  CHECK(r.status == KeyMap::Status::Pending);
  CHECK_EQ(r.pendingText, std::string("Ctrl+K\xE2\x80\xA6"));
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

TEST(cmd_context_priority) {
  Parsed p = parse(
      "Enter = Global.Enter\n"
      "[editor] Enter = Editor.Enter\n"
      "[picker] Enter = Picker.Accept\n"
      "[picker] Ctrl+K Ctrl+X = Picker.Chord\n"
      "F9 = Only.Global\n");
  CHECK_EQ(p.map.feed(K(Key::Enter), "editor", 0).binding->command, std::string("Editor.Enter"));
  CHECK_EQ(p.map.feed(K(Key::Enter), "picker", 0).binding->command, std::string("Picker.Accept"));
  CHECK_EQ(p.map.feed(K(Key::Enter), "statusbar", 0).binding->command, std::string("Global.Enter"));
  CHECK_EQ(p.map.feed(K(Key::F9), "picker", 0).binding->command, std::string("Only.Global"));
  CHECK(p.map.feed(Ch('K', ModCtrl), "editor", 0).status == KeyMap::Status::NoMatch);  // picker-only chord
  CHECK(p.map.feed(Ch('K', ModCtrl), "picker", 0).status == KeyMap::Status::Pending);
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

TEST(cmd_defaults_parse_clean_and_resolve) {
  Parsed p = parse(defaultKeyBindingsText());
  for (auto& d : p.diags) std::cerr << "    " << formatDiag(d) << "\n";
  CHECK_EQ(p.diags.size(), 0u);

  CommandRegistry reg;
  registerEditorCommands(reg);
  CHECK(reg.size() > 40u);
  for (auto& b : p.map.bindings()) {
    if (!reg.find(b.command)) std::cerr << "    unregistered default: " << b.command << "\n";
    CHECK(reg.find(b.command) != nullptr);
  }
  // The plan's list.
  const std::pair<const char*, const char*> must[] = {{"Alt+Left", "Nav.Back"},
                                                      {"Alt+Right", "Nav.Forward"},
                                                      {"Ctrl+-", "Nav.Back"},
                                                      {"Ctrl+Shift+-", "Nav.Forward"},
                                                      {"Mouse4", "Nav.Back"},
                                                      {"Mouse5", "Nav.Forward"},
                                                      {"F12", "Editor.GotoDefinition"},
                                                      {"Ctrl+F12", "Editor.GotoDeclaration"},
                                                      {"Shift+F12", "Editor.FindReferences"},
                                                      {"Alt+O", "Editor.SwitchHeaderSource"},
                                                      {"Ctrl+,", "Search.SymbolSearch"},
                                                      {"Ctrl+P", "File.OpenFile"},
                                                      {"Ctrl+O", "File.OpenFile"},
                                                      {"Ctrl+Shift+F", "Search.FindInWorkspace"}};
  for (auto& [k, cmd] : must) {
    auto* b = findB(p.map, k);
    if (!b) std::cerr << "    missing default: " << k << "\n";
    REQUIRE(b);
    CHECK_EQ(b->command, std::string(cmd));
  }
  CHECK_EQ(findB(p.map, "Ctrl+Y", "editor")->command, std::string("Editor.Redo"));
  CHECK_EQ(findB(p.map, "Ctrl+Shift+Z", "editor")->command, std::string("Editor.Redo"));
  CHECK_EQ(findB(p.map, "Ctrl+Alt+Down", "editor")->command, std::string("Editor.AddCaretBelow"));
  // The 10x TODO marker must stay in the header until the user's key map arrives.
  CHECK(defaultKeyBindingsText().find("TODO(OPEN_QUESTIONS #1") != std::string_view::npos);
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

TEST(cmd_end_to_end_alt_left_goes_back) {
  fs::path dir = fs::temp_directory_path() / ("tf_cmd_e2e_" + std::to_string(reinterpret_cast<uintptr_t>(&dir)));
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
    press(K(Key::Left, ModAlt));
    CHECK(ed.cursor() == (Location{f, {100, 2}}));
    press(chordFromMouse(MouseButton::X1, 0));  // thumb button back
    CHECK(ed.cursor() == (Location{f, {0, 0}}));
    press(K(Key::Right, ModAlt));
    CHECK(ed.cursor() == (Location{f, {100, 2}}));
    press(Ch('-', ModCtrl | ModShift));
    CHECK(ed.cursor() == (Location{f, {200, 4}}));
    // Nothing further forward: the command is reported disabled, not silently ignored.
    auto r = p.map.feed(K(Key::Right, ModAlt), "editor", 0);
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
