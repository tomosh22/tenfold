// W8f: PasteHistory, RecordKeySequence / PlaybackKeySequence, PerforceHistory and PerforceShowInP4v through the UI.
// Every command is run by pressing its 10x default key (the real embedded key bindings), against a null renderer and
// the mock font; Perforce is the fake p4 of tests/fake_p4_main.cpp, the clipboard a fake backend, p4v a captured
// launch.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "../test_w8f_p4util.h"
#include "../text/text_test_util.h"
#include "ui/commands/CommandNames.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/platform/Clipboard.h"
#include "ui/settings/KeyBindings.h"
#include "ui/settings/Settings.h"
#include "ui/shell/EditorApp.h"
#include "ui/widgets/TextView.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
#define REQUIRE_EQ(a, b) REQUIRE((a) == (b))
namespace fs = std::filesystem;

namespace {

struct NullRenderer : gfx::Renderer {
  void beginFrame(int, int, gfx::Color) override {}
  void drawRect(const gfx::RectF&, gfx::Color) override {}
  void drawLine(float, float, float, float, float, gfx::Color) override {}
  void drawGlyphs(gfx::TextureId, const gfx::GlyphQuad*, int, gfx::Color) override {}
  void pushScissor(const gfx::RectF&) override {}
  void popScissor() override {}
  gfx::TextureId createTexture(int, int, gfx::TextureFormat) override { return 1; }
  void updateTexture(gfx::TextureId, int, int, int, int, const void*, int) override {}
  void destroyTexture(gfx::TextureId) override {}
  void endFrame() override {}
  bool readPixels(int, int, uint8_t*) override { return false; }
};

struct FakeClipboard {
  std::string value;
  FakeClipboard() {
    setClipboardBackend({[this] { return value; },
                         [this](const std::string& s) {
                           value = s;
                           return true;
                         }});
  }
  ~FakeClipboard() { setClipboardBackend({}); }
};

InputEvent keyEv(Key k, uint8_t mods = 0) {
  InputEvent e;
  e.type = InputType::KeyDown;
  e.key = k;
  e.mods = mods;
  return e;
}

constexpr uint8_t kCS = ModCtrl | ModShift;

struct Env {
  fs::path dir, tmp;
  std::unique_ptr<w8f::P4Ws> p4;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer nr;
  std::unique_ptr<Workbench> bench;
  std::unique_ptr<EditorApp> app;
  int64_t now = 1000;

  explicit Env(const std::string& aText = "foo bar foo\nbaz foo\nlonger line\nx\n", bool perforce = false) {
    static int n = 0;
    tmp = uitest::tempRoot() /
          ("tf_w8f_tmp_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(n++));
    Editor::Options o;
    o.semantic.useClangd = false;
    if (perforce) {
      PerforceProvider::clearDetectCache();
      p4 = std::make_unique<w8f::P4Ws>();
      dir = p4->ws;
      p4->depotFile("a.cpp", aText);
      p4->save();
      o.vcs.provider = "perforce";
      o.vcs.p4Path = TENFOLD_FAKE_P4;
    } else {
      dir = tmp / "ws";
      fs::create_directories(dir);
      std::ofstream(dir / "a.cpp", std::ios::binary) << aText;
      o.vcs.provider = "none";
    }
    fs::create_directories(tmp);
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-:,;/()[]{}<>+*&#?=!\"'\\|"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    ed = std::make_unique<Editor>(dir, o);
    bench = std::make_unique<Workbench>(*text, *ed);
    app = std::make_unique<EditorApp>(*bench, *ed, bench.get());
    app->perforceUi().setTempRoot(tmp / "p4");
    bench->openFile("a.cpp");
    bench->sync();
    frame();
  }
  ~Env() {
    app.reset();
    bench.reset();
    ed.reset();
    std::error_code ec;
    fs::remove_all(tmp, ec);
    p4.reset();
  }
  void frame() {
    ed->pump();
    app->tick(now);
    bench->render(nr, 1000, 600);
  }
  bool waitUntil(const std::function<bool()>& pred, int ms = 15000) {
    for (int waited = 0; waited < ms; waited += 5) {
      frame();
      if (pred()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
  }
  bool key(Key k, uint8_t mods = 0) {
    now += 10;
    const bool r = app->handleEvent(keyEv(k, mods), now);
    frame();
    return r;
  }
  void type(const std::string& t) {
    InputEvent e;
    e.type = InputType::Text;
    e.text = t;
    send(e);
  }
  bool send(const InputEvent& e) {
    now += 10;
    const bool r = app->handleEvent(e, now);
    frame();
    return r;
  }
  Document* doc() { return ed->currentDocument(); }
  std::string name() { return doc()->path().filename().string(); }
  std::string txt() { return doc()->buffer().text(); }
  std::string line(int i) { return doc()->buffer().line(i); }
  void set(std::vector<Selection> s) { ed->setSelections(std::move(s)); }
  size_t tabCount() {
    size_t n = 0;
    for (Panel* p : bench->panels()) n += p->tabs().tabs().size();
    return n;
  }
};

Selection C(int l, int c) { return {{l, c}, {l, c}}; }
Selection S(int l1, int c1, int l2, int c2) { return {{l1, c1}, {l2, c2}}; }

}  // namespace

TEST(w8f_names_resolve_and_defaults_bind_to_them) {
  const char* names[] = {"PasteHistory", "RecordKeySequence", "PlaybackKeySequence", "PerforceHistory",
                         "PerforceShowInP4v"};
  Env env;
  for (const char* n : names) {
    const ResolvedCommand r = resolveCommandName(n);
    CHECK(r.known10x);
    CHECK(r.implemented);
    const Command* c = env.app->commands().find(r.id);
    if (!c) std::cerr << "    no command for " << n << " -> " << r.id << "\n";
    CHECK(c != nullptr);
    CHECK(c && static_cast<bool>(c->handler));
  }
  KeyMap map;
  std::vector<KeyBindingDiag> diags;
  parseKeyBindings(defaultKeyBindingsText(), "defaults", map, diags);
  CHECK_EQ(diags.size(), size_t(0));
}

TEST(w8f_paste_history_lists_newest_first_and_pastes_the_chosen_entry) {
  FakeClipboard cb;
  Env env;
  env.set({S(0, 0, 0, 3)});
  env.key(Key('C'), ModCtrl);  // "foo"
  env.set({S(0, 4, 0, 7)});
  env.key(Key('C'), ModCtrl);  // "bar"
  env.set({S(1, 0, 1, 3)});
  env.key(Key('X'), ModCtrl);  // cut "baz"
  REQUIRE_EQ(env.app->pasteHistory().entries().size(), size_t(3));
  CHECK_EQ(env.app->pasteHistory().entries()[0], std::string("baz"));
  CHECK_EQ(env.app->pasteHistory().entries()[2], std::string("foo"));

  env.set({C(3, 0)});
  env.key(Key('V'), kCS);  // PasteHistory
  REQUIRE(env.app->pasteHistory().isOpen());
  ListPopup* p = env.app->pasteHistory().popup();
  REQUIRE_EQ(p->rows().size(), size_t(3));
  CHECK_EQ(p->rows()[0].primary, std::string("baz"));
  CHECK_EQ(p->rows()[1].primary, std::string("bar"));
  CHECK_EQ(p->rows()[2].primary, std::string("foo"));
  env.key(Key::Down);
  env.key(Key::Down);
  env.key(Key::Enter);  // "foo"
  CHECK(!env.app->pasteHistory().isOpen());
  CHECK_EQ(env.line(3), std::string("foox"));
  CHECK_EQ(env.app->pasteHistory().entries()[0], std::string("foo"));  // moved to the top
  CHECK_EQ(env.app->pasteHistory().entries().size(), size_t(3));
  CHECK_EQ(cb.value, std::string("foo"));  // and made the clipboard text: Control V pastes it again
  env.key(Key('V'), ModCtrl);
  CHECK_EQ(env.line(3), std::string("foofoox"));
}

TEST(w8f_paste_history_filter_multi_caret_and_empty_history) {
  FakeClipboard cb;
  Env env;
  env.set({C(0, 0)});
  env.key(Key('V'), kCS);
  REQUIRE(env.app->pasteHistory().isOpen());
  CHECK_EQ(env.app->pasteHistory().popup()->rows().size(), size_t(0));  // "Nothing copied yet"
  env.key(Key::Escape);
  CHECK(!env.app->pasteHistory().isOpen());
  CHECK_EQ(env.txt(), std::string("foo bar foo\nbaz foo\nlonger line\nx\n"));

  env.app->pasteHistory().record("alpha\nbeta");
  env.app->pasteHistory().record("gamma");
  env.set({C(0, 0), C(1, 0)});
  env.key(Key('V'), kCS);
  env.type("alp");  // the input filters
  REQUIRE_EQ(env.app->pasteHistory().popup()->rows().size(), size_t(1));
  CHECK_EQ(env.app->pasteHistory().popup()->rows()[0].primary, std::string("alpha ..."));
  CHECK_EQ(env.app->pasteHistory().popup()->rows()[0].detail, std::string("2 lines"));
  env.key(Key::Enter);  // two carets, two lines: one line each
  CHECK_EQ(env.line(0), std::string("alphafoo bar foo"));
  CHECK_EQ(env.line(1), std::string("betabaz foo"));
  env.key(Key('Z'), ModCtrl);
  CHECK_EQ(env.txt(), std::string("foo bar foo\nbaz foo\nlonger line\nx\n"));
}

TEST(w8f_paste_history_size_duplicates_and_external_clipboard) {
  FakeClipboard cb;
  Env env;
  PasteHistory& h = env.app->pasteHistory();
  CHECK_EQ(h.limit(), size_t(20));
  Settings s;
  CHECK_EQ(s.getInt("editor.pasteHistorySize"), 20);
  s.loadTexts("editor.pasteHistorySize = 3\n", "");
  env.app->applySettings(s);
  CHECK_EQ(h.limit(), size_t(3));
  for (const char* t : {"a", "b", "c", "d"}) h.record(t);
  REQUIRE_EQ(h.entries().size(), size_t(3));
  CHECK_EQ(h.entries()[0], std::string("d"));
  CHECK_EQ(h.entries()[2], std::string("b"));
  h.record("c");  // a duplicate moves to the top
  CHECK_EQ(h.entries()[0], std::string("c"));
  CHECK_EQ(h.entries().size(), size_t(3));
  h.record("");
  CHECK_EQ(h.entries().size(), size_t(3));

  cb.value = "from outside";  // another program copied: noticed when the window regains focus
  InputEvent f;
  f.type = InputType::FocusGained;
  env.send(f);
  CHECK_EQ(h.entries()[0], std::string("from outside"));
  env.send(f);  // no change: no second entry
  CHECK_EQ(h.entries().size(), size_t(3));

  s.loadTexts("editor.pasteHistorySize = 0\n", "");
  env.app->applySettings(s);
  CHECK_EQ(h.entries().size(), size_t(0));
  h.record("x");
  CHECK_EQ(h.entries().size(), size_t(0));
}

TEST(w8f_record_and_playback_a_key_sequence_as_one_undo_step) {
  Env env;
  KeySequence& ks = env.app->keySequence();
  env.key(Key('P'), kCS);  // nothing recorded yet
  CHECK(env.app->message().find("No key sequence recorded") != std::string::npos);

  env.set({C(0, 0)});
  env.key(Key('R'), kCS);  // RecordKeySequence
  CHECK(ks.recording());
  CHECK(env.app->message().find("Recording") != std::string::npos);
  env.type("ab");
  env.key(Key::End);  // MoveToLineEnd
  env.type("!");
  env.key(Key('P'), kCS);  // playback while recording is refused
  CHECK(ks.recording());
  env.key(Key('R'), kCS);  // stop
  CHECK(!ks.recording());
  CHECK(env.app->message().find("3 steps") != std::string::npos);
  REQUIRE_EQ(ks.steps().size(), size_t(3));  // the record / playback keys are not part of it
  CHECK(ks.steps()[0].kind == KeySequence::Step::Kind::Text);
  CHECK_EQ(ks.steps()[0].text, std::string("ab"));
  CHECK(ks.steps()[1].kind == KeySequence::Step::Kind::Command);
  CHECK_EQ(ks.steps()[1].id, resolveCommandName("MoveToLineEnd").id);
  CHECK_EQ(ks.steps()[2].text, std::string("!"));
  CHECK_EQ(env.line(0), std::string("abfoo bar foo!"));

  env.set({C(1, 0)});
  env.key(Key('P'), kCS);  // PlaybackKeySequence
  CHECK_EQ(env.line(1), std::string("abbaz foo!"));
  CHECK(env.app->message().find("Played 3") != std::string::npos);
  env.set({C(2, 0)});
  env.key(Key('P'), kCS);
  CHECK_EQ(env.line(2), std::string("ablonger line!"));
  env.key(Key('Z'), ModCtrl);  // one undo step per playback
  CHECK_EQ(env.line(2), std::string("longer line"));
  CHECK_EQ(env.line(1), std::string("abbaz foo!"));
  env.key(Key('Z'), ModCtrl);
  CHECK_EQ(env.line(1), std::string("baz foo"));
  CHECK_EQ(env.line(0), std::string("abfoo bar foo!"));
  // typing right after a playback is not glued to the playback's undo step
  env.set({C(3, 0)});
  env.key(Key('P'), kCS);
  env.type("Q");
  env.key(Key('Z'), ModCtrl);
  CHECK_EQ(env.line(3), std::string("ab"
                                    "x"
                                    "!"));
}

TEST(w8f_recording_ignores_picker_input_and_a_second_recording_replaces_the_first) {
  Env env;
  KeySequence& ks = env.app->keySequence();
  env.set({C(0, 0)});
  env.key(Key('R'), kCS);
  env.key(Key('G'), ModCtrl);  // GoToLine: its input is not the editor's
  env.type("3");
  env.key(Key::Escape);
  env.key(Key('R'), kCS);
  for (const auto& s : ks.steps()) CHECK(s.kind != KeySequence::Step::Kind::Text);
  CHECK(!ks.steps().empty());

  env.key(Key('R'), kCS);
  env.key(Key('R'), kCS);  // empty recording: the previous one is kept
  CHECK(env.app->message().find("previous") != std::string::npos);
  CHECK(!ks.steps().empty());

  env.key(Key('R'), kCS);
  env.type("zz");
  env.key(Key('R'), kCS);
  REQUIRE_EQ(ks.steps().size(), size_t(1));
  CHECK_EQ(ks.steps()[0].text, std::string("zz"));
}

TEST(w8f_perforce_history_lists_revisions_and_opens_one_read_only) {
  Env env("third\n", true);
  REQUIRE(env.waitUntil([&] { return env.ed->vcs().active(); }));
  env.key(Key('P'), ModCtrl);
  env.key(Key('H'), ModCtrl);  // PerforceHistory
  REQUIRE(env.waitUntil([&] { return env.app->perforceUi().isOpen(); }));
  ListPopup* p = env.app->perforceUi().popup();
  REQUIRE_EQ(p->rows().size(), size_t(3));
  CHECK(p->rows()[0].primary.find("#3") != std::string::npos);
  CHECK(p->rows()[0].primary.find("change 300") != std::string::npos);
  CHECK(p->rows()[0].primary.find("2023-11-14 22:13") != std::string::npos);
  CHECK(p->rows()[0].primary.find("carol") != std::string::npos);
  CHECK_EQ(p->rows()[0].secondary, std::string("Third change"));
  CHECK_EQ(p->rows()[0].detail, std::string("edit"));
  CHECK_EQ(p->rows()[2].detail, std::string("add"));
  env.type("bob");  // filter by user
  REQUIRE_EQ(env.app->perforceUi().popup()->rows().size(), size_t(1));
  CHECK_EQ(env.app->perforceUi().popup()->rows()[0].secondary, std::string("Second change"));
  env.key(Key::Enter);  // VC6: Enter opens the action menu of the revision, "Open" is its first row
  REQUIRE(env.waitUntil([&] { return env.app->perforceUi().actionMenu() != nullptr; }));
  env.key(Key::Enter);
  REQUIRE(env.waitUntil([&] { return env.name() == "a#2.cpp"; }));
  CHECK_EQ(env.txt(), std::string("second\n"));
  CHECK_EQ(env.tabCount(), size_t(2));
  CHECK(env.app->message().find("read-only") != std::string::npos);
  std::error_code ec;
  CHECK((fs::status(env.doc()->path(), ec).permissions() & fs::perms::owner_write) == fs::perms::none);
  CHECK(env.doc()->path().string().find("p4") != std::string::npos);
  // the working file is untouched
  CHECK_EQ(tfvcs::readFile(env.dir / "a.cpp"), std::string("third\n"));
}

TEST(w8f_perforce_history_needs_perforce_and_a_depot_file) {
  Env env;  // no version control at all
  env.key(Key('P'), ModCtrl);
  env.key(Key('H'), ModCtrl);
  REQUIRE(env.waitUntil([&] { return env.app->message().find("Perforce history") != std::string::npos; }, 3000));
  CHECK(!env.app->perforceUi().isOpen());

  Env p("local\n", true);
  REQUIRE(p.waitUntil([&] { return p.ed->vcs().active(); }));
  p.p4->state["files"]["a.cpp"]["inDepot"] = false;  // a new local file
  p.p4->save();
  p.key(Key('P'), ModCtrl);
  p.key(Key('H'), ModCtrl);
  REQUIRE(p.waitUntil([&] { return p.app->message().find("not in the Perforce depot") != std::string::npos; }, 5000));
  CHECK(!p.app->perforceUi().isOpen());
}

TEST(w8f_perforce_show_in_p4v_launches_p4v_with_the_depot_path) {
  Env env("third\n", true);
  REQUIRE(env.waitUntil([&] { return env.ed->vcs().active(); }));
  std::string exe;
  std::vector<std::string> args;
  bool fail = false;
  OsLauncher l;
  l.startProgram = [&](const std::string& e, const std::vector<std::string>& a, std::string* err) {
    exe = e;
    args = a;
    if (fail && err) *err = "no such program";
    return !fail;
  };
  env.app->setLauncher(l);
  env.key(Key('P'), ModCtrl);
  env.key(Key('V'), ModCtrl);  // PerforceShowInP4v (the chord, not Paste)
  REQUIRE(env.waitUntil([&] { return !exe.empty(); }));
  CHECK_EQ(exe, std::string("p4v"));
  const std::vector<std::string> want = {"-p", "fake-server:1666", "-u", "tester",
                                         "-c", "fake-client",      "-s", "//depot/a.cpp"};
  CHECK(args == want);
  CHECK_EQ(env.txt(), std::string("third\n"));  // it was not a Paste

  // vcs.p4vPath names another program; a failed start is reported
  Settings s;
  s.loadTexts("vcs.p4vPath = /opt/p4v/bin/p4v\n", "");
  env.app->applySettings(s);
  exe.clear();
  fail = true;
  env.key(Key('P'), ModCtrl);
  env.key(Key('V'), ModCtrl);
  REQUIRE(env.waitUntil([&] { return !exe.empty(); }));
  CHECK_EQ(exe, std::string("/opt/p4v/bin/p4v"));
  CHECK(env.waitUntil([&] { return env.app->message().find("Cannot start") != std::string::npos; }, 3000));
  CHECK(env.app->message().find("no such program") != std::string::npos);

  CHECK(PerforceUi::p4vArgs({"", "u", "", "//d/f"}) == (std::vector<std::string>{"-u", "u", "-s", "//d/f"}));
  CHECK_EQ(PerforceUi::formatTime(0), std::string("1970-01-01 00:00"));
}
