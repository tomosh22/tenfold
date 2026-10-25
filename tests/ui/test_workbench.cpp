// B3b: Workbench panel tree, tab movement, Editor sync and layout persistence. No GL except the golden: tests drive
// the workbench with direct calls and synthetic events against a null renderer and a mock monospace font.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../text/text_test_util.h"
#include "ui/gfx/PngWrite.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/Workbench.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;
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

std::unique_ptr<gfx::TextRenderer> mockText() {
  gfx::FontStack stack;
  stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz0123456789 ._-:,×●…ABCLMNTUFDK"));
  return gfx::TextRenderer::fromFonts(std::move(stack));
}

InputEvent mouse(InputType t, float x, float y) {
  InputEvent e;
  e.type = t;
  e.x = x;
  e.y = y;
  e.button = MouseButton::Left;
  e.clicks = 1;
  e.buttons = t == InputType::MouseUp ? 0 : 1;
  return e;
}

// A scratch workspace with a.cpp .. d.cpp (20 lines each) and its own Editor (not started: no clangd).
struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text = mockText();
  NullRenderer nr;
  Env() {
    static int n = 0;
    dir = uitest::tempRoot() / ("tf_wb_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                "_" + std::to_string(n++));
    fs::create_directories(dir);
    for (const char* f : {"a.cpp", "b.cpp", "c.cpp", "d.cpp"}) {
      std::ofstream out(dir / f, std::ios::binary);
      for (int i = 0; i < 20; ++i) out << "int " << f[0] << i << " = " << i << ";\n";
    }
    ed = std::make_unique<Editor>(dir);
  }
  ~Env() {
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  std::unique_ptr<Workbench> bench() {
    auto w = std::make_unique<Workbench>(*text, *ed);
    w->render(nr, 1000, 600);
    return w;
  }
};

std::vector<std::string> names(Workbench& w, Panel* p) {
  std::vector<std::string> out;
  for (const auto& t : p->tabs().tabs()) out.push_back(w.tabPath(t.id).filename().string());
  return out;
}
using V = std::vector<std::string>;

}  // namespace

TEST(workbench_open_focuses_existing_tab) {
  Env env;
  auto w = env.bench();
  const int a = w->openFile("a.cpp"), b = w->openFile("b.cpp");
  CHECK(a > 0 && b > 0 && a != b);
  CHECK(w->openFile("a.cpp") == a);  // focuses, no duplicate
  Panel* p = w->activePanel();
  CHECK((names(*w, p) == V{"a.cpp", "b.cpp"}));
  CHECK(p->activeId() == a);
  CHECK(env.ed->currentDocument() == env.ed->document(env.dir / "a.cpp"));
  CHECK(w->openFile("missing.cpp") == -1);
  w->sync();
  CHECK(w->status().text("pos") == "Ln 1, Col 1");
  CHECK(w->status().text("eol") == "LF");
}

TEST(workbench_split_duplicates_active_tab) {
  Env env;
  auto w = env.bench();
  w->openFile("a.cpp");
  w->openFile("b.cpp");
  Panel* left = w->activePanel();
  Panel* right = w->splitActive(Splitter::Orientation::Horizontal);
  CHECK(w->panels().size() == 2u);
  CHECK(w->panels()[0] == left && w->panels()[1] == right);
  CHECK(w->activePanel() == right);
  CHECK((names(*w, left) == V{"a.cpp", "b.cpp"}));  // the source keeps its tabs
  CHECK((names(*w, right) == V{"b.cpp"}));          // the active tab was duplicated
  CHECK(env.ed->documents().size() == 2u);          // one Document per file, two views of b
  CHECK(left->content(left->activeId()) != right->activeContent());
  // Closing one view keeps the document (the other tab still shows it).
  CHECK(w->closeTab(right->activeId()));
  CHECK(env.ed->document(env.dir / "b.cpp") != nullptr);
  CHECK(w->closeTab(left->tabs().tabs()[1].id));
  CHECK(env.ed->document(env.dir / "b.cpp") == nullptr);  // last view gone: closed in the Editor
  Panel* below = w->splitPanel(left, Splitter::Orientation::Vertical, true, -1);
  CHECK(w->panels().size() == 3u && w->panels()[1] == below);
  CHECK(static_cast<Splitter*>(left->parent())->orientation() == Splitter::Orientation::Vertical);
}

TEST(workbench_close_panel_merges_tabs) {
  Env env;
  auto w = env.bench();
  w->openFile("a.cpp");
  Panel* left = w->activePanel();
  Panel* right = w->splitActive(Splitter::Orientation::Horizontal);  // right: a
  w->openFile("b.cpp");
  w->openFile("a.cpp");  // right: a, b -> a focused
  w->setPinned(right->tabs().tabs()[1].id, true);
  CHECK(w->closePanel(left));
  CHECK(w->panels().size() == 1u);
  CHECK(w->panels()[0] == right && w->activePanel() == right);
  CHECK((names(*w, right) == V{"b.cpp", "a.cpp"}));  // pinned b first; left's duplicate "a" folded into the existing
  CHECK(!w->closePanel(right));                      // the last panel stays

  // Different tabs really move.
  w->openFile("c.cpp");
  Panel* r2 = w->splitActive(Splitter::Orientation::Vertical);
  w->closeTab(r2->activeId());
  w->openFile("d.cpp");
  CHECK(w->closePanel(r2));
  CHECK((names(*w, w->panels()[0]) == V{"b.cpp", "a.cpp", "c.cpp", "d.cpp"}));
}

TEST(workbench_drag_tab_between_panels) {
  Env env;
  auto w = env.bench();
  w->openFile("a.cpp");
  const int b = w->openFile("b.cpp");
  Panel* left = w->activePanel();
  Panel* right = w->splitActive(Splitter::Orientation::Horizontal);  // right: b (duplicate)
  w->closeTab(right->activeId());
  w->render(env.nr, 1000, 600);
  const RectF rs = right->tabs().bounds();
  // Drag left's b tab onto right's (empty) strip.
  w->beginTabDrag(b, 100, 20);
  CHECK(w->dragging() && w->captured() == w.get());
  w->dispatch(mouse(InputType::MouseMove, rs.x + rs.w - 5, rs.y + 5));
  CHECK(w->dropTargetAt(rs.x + rs.w - 5, rs.y + 5).kind == Workbench::Drop::Strip);
  w->dispatch(mouse(InputType::MouseUp, rs.x + rs.w - 5, rs.y + 5));
  CHECK(!w->dragging());
  CHECK(w->panelOf(b) == right);
  CHECK((names(*w, left) == V{"a.cpp"}));
  CHECK((names(*w, right) == V{"b.cpp"}));
  CHECK(w->activePanel() == right);

  // Drop on the content's left edge of `left`: a new split holding the tab.
  w->render(env.nr, 1000, 600);
  const RectF c = left->contentRect();
  w->beginTabDrag(b, 700, 20);
  w->dispatch(mouse(InputType::MouseUp, c.x + 5, c.y + c.h / 2));
  CHECK(w->panels().size() == 2u);  // right was emptied and removed, a new panel took its place
  CHECK((names(*w, w->panels()[0]) == V{"b.cpp"}));
  CHECK((names(*w, w->panels()[1]) == V{"a.cpp"}));

  // Dropping outside any panel cancels.
  w->render(env.nr, 1000, 600);
  w->beginTabDrag(b, 10, 10);
  w->dispatch(mouse(InputType::MouseUp, 5000, 5000));
  CHECK(w->panelOf(b) == w->panels()[0] && w->panels().size() == 2u);

  // Dragging the only tab onto another panel's strip leaves a single panel.
  const RectF s1 = w->panels()[1]->tabs().bounds();
  w->beginTabDrag(b, 10, 10);
  w->dispatch(mouse(InputType::MouseUp, s1.x + 3, s1.y + 3));
  CHECK(w->panels().size() == 1u);
  CHECK((names(*w, w->panels()[0]) == V{"b.cpp", "a.cpp"}));
}

TEST(workbench_close_others_keeps_pinned) {
  Env env;
  auto w = env.bench();
  const int a = w->openFile("a.cpp"), b = w->openFile("b.cpp"), c = w->openFile("c.cpp");
  w->openFile("d.cpp");
  w->setPinned(b, true);
  w->closeOthers(c);
  Panel* p = w->activePanel();
  CHECK((names(*w, p) == V{"b.cpp", "c.cpp"}));
  CHECK(w->panelOf(a) == nullptr);
  w->openFile("d.cpp");
  w->closeAllButPinned(p);
  CHECK((names(*w, p) == V{"b.cpp"}));
  CHECK(env.ed->documents().size() == 1u);
}

TEST(workbench_mru_cycling) {
  Env env;
  auto w = env.bench();
  const int a = w->openFile("a.cpp"), b = w->openFile("b.cpp"), c = w->openFile("c.cpp");
  Panel* p = w->activePanel();  // MRU: c b a
  CHECK(w->cycleMru(p, true) == b);
  CHECK(w->cycleMru(p, true) == a);  // repeated presses walk deeper
  CHECK(w->cycleMru(p, false) == b);
  w->endMruCycle();
  CHECK(p->activeId() == b);
  CHECK(w->cycleMru(p, true) == a);  // a fresh cycle from MRU b a c: forward = the previous tab (a)
  w->endMruCycle();
  CHECK(w->cycleMru(p, false) == c);  // MRU a b c: backward wraps to the oldest
  CHECK(env.ed->currentDocument() == env.ed->document(env.dir / "c.cpp"));
}

TEST(workbench_dirty_marker_and_close_dirty) {
  Env env;
  auto w = env.bench();
  const int a = w->openFile("a.cpp");
  env.ed->document(env.dir / "a.cpp")->insert({0, 0}, "x");
  w->sync();
  CHECK(w->activePanel()->tabs().tabs()[0].dirty);
  CHECK(w->closeTab(a));
  CHECK(env.ed->document(env.dir / "a.cpp") != nullptr);  // dirty documents stay open in the Editor
  CHECK(w->activePanel()->tabs().tabs().empty());
}

TEST(workbench_layout_round_trip) {
  Env env;
  std::string json;
  {
    auto w = env.bench();
    const int b = w->openFile("b.cpp");
    w->openFile("a.cpp");
    w->setPinned(b, true);
    Panel* p0 = w->activePanel();
    Panel* p1 = w->splitActive(Splitter::Orientation::Horizontal);
    w->openFile("c.cpp");
    Panel* p2 = w->splitActive(Splitter::Orientation::Vertical);
    static_cast<Splitter*>(p0->parent())->setRatio(0.3f);
    static_cast<Splitter*>(p2->parent())->setRatio(0.65f);
    w->setActivePanel(p1);
    p0->setActive(b);
    json = w->layoutJson();
    CHECK(w->saveLayout());
    CHECK(fs::exists(env.dir / ".tenfold" / "layout.json"));
  }
  Editor ed2(env.dir);
  Workbench w2(*env.text, ed2);
  CHECK(w2.restoreLayout());
  CHECK(w2.layoutJson() == json);
  CHECK(w2.panels().size() == 3u);
  CHECK(w2.activePanel() == w2.panels()[1]);
  CHECK(ed2.currentDocument() == ed2.document(env.dir / "c.cpp"));
  CHECK((names(w2, w2.panels()[0]) == V{"b.cpp", "a.cpp"}));
  CHECK(w2.panels()[0]->tabs().tabs()[0].pinned);
}

TEST(workbench_layout_missing_and_corrupt) {
  Env env;
  auto w = env.bench();
  CHECK(!w->restoreLayout());  // no file yet
  CHECK(w->panels().size() == 1u);
  auto write = [&](const std::string& s) {
    fs::create_directories(env.dir / ".tenfold");
    std::ofstream(env.dir / ".tenfold" / "layout.json", std::ios::binary | std::ios::trunc) << s;
  };
  for (const char* bad :
       {"{ not json", "", "[]", "{\"root\":5}", "{\"root\":{\"type\":\"split\"}}",
        "{\"root\":{\"type\":\"panel\",\"tabs\":[{\"path\":3}]}}", "{\"root\":{\"type\":\"nope\"}}"}) {
    write(bad);
    CHECK(!w->restoreLayout());
    CHECK(w->panels().size() == 1u);
  }
  // Wrong value types inside an otherwise valid file are tolerated, not fatal.
  write(R"({"activePanel":"x","root":{"type":"split","orientation":7,"ratio":"wide",
    "first":{"type":"panel","active":"a","tabs":[{"path":"a.cpp","pinned":"yes"},{"path":"gone.cpp"}]},
    "second":{"type":"panel","tabs":[]}}})");
  CHECK(w->restoreLayout());
  CHECK(w->panels().size() == 2u);
  CHECK((names(*w, w->panels()[0]) == V{"a.cpp"}));  // missing file dropped
  CHECK(w->panels()[1]->tabs().tabs().empty());
  // A pathological nesting depth is rejected rather than recursed into.
  std::string deep;
  for (int i = 0; i < 200; ++i) deep += R"({"type":"split","first":)";
  deep += R"({"type":"panel","tabs":[]})";
  for (int i = 0; i < 200; ++i) deep += R"(,"second":{"type":"panel","tabs":[]}})";
  write("{\"root\":" + deep + "}");
  CHECK(!w->restoreLayout());
}

TEST(workbench_tab_switch_is_not_navigation) {
  Env env;
  auto w = env.bench();
  const int a = w->openFile("a.cpp");
  const int b = w->openFile("b.cpp");
  Panel* p = w->activePanel();
  CHECK(env.ed->navigation().entries().empty());  // opening/switching tabs records nothing
  p->setActive(a);
  p->onActivated(a);  // what a click on the tab does
  CHECK(env.ed->currentDocument() == env.ed->document(env.dir / "a.cpp"));
  p->setActive(b);
  p->onActivated(b);
  w->cycleMru(p, true);
  w->sync();
  CHECK(env.ed->navigation().entries().empty());

  // Real navigation into a file with no tab opens one; Back activates the previous tab again.
  p->setActive(a);
  p->onActivated(a);
  CHECK(env.ed->gotoLocation({env.dir / "c.cpp", {7, 0}}));
  const size_t entries = env.ed->navigation().entries().size();
  CHECK(entries >= 1u);
  w->sync();
  CHECK(p->tabs().tabs().size() == 3u);
  CHECK(w->tabPath(p->activeId()).filename() == "c.cpp");
  CHECK(env.ed->navigation().entries().size() == entries);  // the reveal did not add history
  CHECK(w->status().text("pos") == "Ln 8, Col 1");
  CHECK(env.ed->back());
  w->sync();
  CHECK(w->tabPath(p->activeId()).filename() == "a.cpp");
  CHECK(p->tabs().tabs().size() == 3u);
  CHECK(env.ed->forward());
  w->sync();
  CHECK(w->tabPath(p->activeId()).filename() == "c.cpp");

  // A jump into a file open only in the other panel activates that panel's tab (no new tab).
  Panel* q = w->splitActive(Splitter::Orientation::Horizontal);  // q: c (duplicate)
  w->openFile("d.cpp", q);
  q->setActive(q->tabs().tabs()[1].id);
  w->setActivePanel(p);
  w->closeTab(p->tabs().tabs()[0].id);  // a
  w->closeTab(p->tabs().tabs()[0].id);  // b
  w->closeTab(p->tabs().tabs()[0].id);  // c
  CHECK(p->tabs().tabs().empty());
  CHECK(env.ed->gotoLocation({env.dir / "d.cpp", {1, 0}}));
  w->sync();
  CHECK(w->activePanel() == q);
  CHECK(w->tabPath(q->activeId()).filename() == "d.cpp");
  CHECK(p->tabs().tabs().empty());
}

TEST(workbench_golden_b3_workbench) {
  SKIP_IF_NO_GL(fx);
  std::string font;
  for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"})
    if (std::ifstream(p).good()) {
      font = p;
      break;
    }
  if (font.empty()) {
    std::cerr << "    SKIP golden: DejaVuSansMono not installed; goldens are only comparable with the same font\n";
    return;
  }
  std::string err;
  auto tr = gfx::TextRenderer::create(font, 15.0f, 1.0f, &err);
  REQUIRE(tr != nullptr);
  Editor ed(TENFOLD_SOURCE_DIR "/examples/sample_project");
  Workbench wb(*tr, ed);
  const int geo = wb.openFile("src/geometry.cpp");
  wb.openFile("src/main.cpp");
  wb.openFile("include/geometry.h");
  wb.setPinned(geo, true);
  Panel* p0 = wb.activePanel();
  Panel* p1 = wb.splitActive(Splitter::Orientation::Horizontal);  // include/geometry.h duplicated on the right
  wb.openFile("src/main.cpp");
  Panel* p2 = wb.splitPanel(p1, Splitter::Orientation::Vertical, true, -1);  // bottom right: main.cpp duplicated
  wb.openFile("src/geometry.cpp");
  static_cast<Splitter*>(p0->parent())->setRatio(0.45f);
  wb.setActivePanel(p0);
  p0->setActive(p0->tabs().tabs()[1].id);
  wb.setActivePanel(p2);
  wb.sync();
  uitest::wrapOff(wb);
  gfx::GlRenderer& r = *fx->renderer;
  const int w = 1024, h = 640;
  r.setOffscreen(true);
  wb.render(r, w, h);
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  REQUIRE(r.readPixels(w, h, px.data()));
  tr->releaseTexture(r);

  const std::string path = TENFOLD_SOURCE_DIR "/tests/golden/b3_workbench.png";
  if (uitest::updateGoldens) {
    REQUIRE(gfx::writePngFile(path, px.data(), w, h));
    std::cerr << "    golden updated: " << path << "\n";
    return;
  }
  int gw = 0, gh = 0;
  std::vector<uint8_t> golden;
  if (!gfx::readPngFile(path, gw, gh, golden)) {
    CHECK(!"golden missing or unreadable; run tenfold-ui-tests --update-goldens");
    return;
  }
  REQUIRE(gw == w && gh == h);
  size_t bad = 0;
  for (size_t i = 0; i < golden.size(); i += 4) {
    int d = 0;
    for (int c = 0; c < 4; ++c) d = std::max(d, std::abs(int(golden[i + c]) - int(px[i + c])));
    if (d > 3) ++bad;
  }
  if (bad > golden.size() / 4 / 500) {
    gfx::writePngFile("b3_workbench.actual.png", px.data(), w, h);
    std::cerr << "    " << bad << " pixels differ; wrote b3_workbench.actual.png\n";
  }
  CHECK(bad <= golden.size() / 4 / 500);
}
