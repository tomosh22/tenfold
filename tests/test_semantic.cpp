// Integration tests: the two-tier engine against examples/sample_project with
// a real clangd. They are skipped (with a message) when clangd is unavailable.
#include <fstream>
#include <map>

#include "app/Editor.h"
#include "test_main.h"
#include "workspace/Workspace.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

const fs::path kRoot = fs::path(TENFOLD_SOURCE_DIR) / "examples/sample_project";

// Line (0-based) of "// @MARK:name" in a file.
int markLine(const fs::path& file, const std::string& name) {
  std::ifstream in(file);
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    if (line.find("@MARK:" + name) != std::string::npos) {
      // Make sure it's a whole-word marker match (@MARK:Vec2 vs @MARK:Vec2f).
      size_t p = line.find("@MARK:" + name) + 6 + name.size();
      if (p >= line.size() || !(std::isalnum(static_cast<unsigned char>(line[p])) || line[p] == '_')) return n;
    }
    ++n;
  }
  return -1;
}

// Column (0-based) of `token` on line `line` of `file`.
// `token` may contain a '|' marking where the caret goes: "w.|x" finds "w.x"
// and returns the column of the 'x'.
int columnOf(const fs::path& file, int line, const std::string& tokenSpec) {
  std::ifstream in(file);
  std::string s;
  for (int i = 0; i <= line && std::getline(in, s); ++i) {}
  std::string token = tokenSpec;
  size_t bar = token.find('|');
  size_t offset = 0;
  if (bar != std::string::npos) { token.erase(bar, 1); offset = bar; }
  size_t p = s.find(token);
  return p == std::string::npos ? -1 : static_cast<int>(p + offset);
}

bool clangdAvailable() { return !findExecutable("clangd").empty(); }

#define REQUIRE_RET(ret, ...) do { if (!(__VA_ARGS__)) { tftest::fail(__FILE__, __LINE__, "REQUIRE failed: " #__VA_ARGS__); return ret; } } while (0)

struct Fixture {
  Editor ed;
  bool ok = false;
  explicit Fixture(bool useClangd = true) : ed(kRoot, makeOptions(useClangd)) {
    std::string err;
    ok = ed.start(&err);
    if (!ok) std::cerr << "    engine start: " << err << "\n";
  }
  static Editor::Options makeOptions(bool useClangd) {
    Editor::Options o;
    o.semantic.useClangd = useClangd;
    o.semantic.clangdBudgetMs = 150;
    o.log = [](const std::string& m) { if (std::getenv("TENFOLD_TEST_VERBOSE")) std::cerr << "    [engine] " << m << "\n"; };
    return o;
  }

  // Run go-to-definition at (file, markerLine, token) and wait for the
  // authoritative answer. Returns all callbacks received.
  struct Outcome { std::vector<std::pair<Confidence, std::vector<SymbolLocation>>> calls; std::string error; };
  Outcome gotoDef(const fs::path& file, const std::string& useMark, const std::string& token) {
    Outcome out;
    int line = markLine(file, useMark);
    REQUIRE_RET(out, line >= 0);
    int col = columnOf(file, line, token);
    REQUIRE_RET(out, col >= 0);
    std::string e;
    REQUIRE_RET(out, ed.gotoLocation({file, {line, col}}, &e));
    bool done = false;
    ed.gotoDefinition([&](Confidence c, const std::vector<SymbolLocation>& locs, const std::string& err) {
      out.calls.emplace_back(c, locs);
      if (!err.empty()) out.error = err;
      if (c == Confidence::Authoritative || !ed.semantic().clangd()) done = true;
    });
    ed.waitFor([&] { return done; }, 60000);
    return out;
  }
};

}  // namespace

TEST(workspace_scan_sample_project) {
  Workspace ws(kRoot);
  ws.scan();
  CHECK_EQ(ws.sourceFiles().size(), 2u);
  CHECK_EQ(ws.headerFiles().size(), 1u);
  CHECK(ws.isHeader(kRoot / "include/geometry.h"));
  CHECK(ws.isIgnoredDir(kRoot / ".tenfold"));
  CHECK(ws.isIgnoredDir(kRoot / "build-x64"));
  CHECK(!ws.isIgnoredDir(kRoot / "src"));
}

TEST(semantic_fast_only_navigation_and_back) {
  Fixture f(false);
  REQUIRE(f.ok);
  CHECK(f.ed.semantic().waitForFastIndex(10000));
  fs::path main = kRoot / "src/main.cpp", hdr = kRoot / "include/geometry.h";
  auto o = f.gotoDef(main, "use_length_sq", "length_sq");
  REQUIRE(!o.calls.empty());
  CHECK(o.calls.back().first == Confidence::Approximate);
  // The fast index knows two length_sq symbols (decl + def) - approximate but useful.
  CHECK_EQ(o.calls.back().second.size(), 2u);
  // Ambiguous -> the editor does not auto-jump.
  CHECK(f.ed.cursor().file == main);
  // Unambiguous: Vec2f alias.
  auto o2 = f.gotoDef(main, "decl_v", "Vec2f");
  REQUIRE(!o2.calls.empty());
  REQUIRE(o2.calls.back().second.size() == 1u);
  CHECK(f.ed.cursor().file == hdr);
  CHECK_EQ(f.ed.cursor().pos.line, markLine(hdr, "Vec2f"));
  // ...and Back returns to main.cpp at the use site.
  CHECK(f.ed.back());
  CHECK(f.ed.cursor().file == main);
  CHECK_EQ(f.ed.cursor().pos.line, markLine(main, "decl_v"));
  CHECK(f.ed.forward());
  CHECK(f.ed.cursor().file == hdr);
  // Approximate completion / symbol search work without clangd.
  bool done = false;
  std::vector<SymbolLocation> syms;
  f.ed.findSymbols("Vec2", [&](Confidence, const std::vector<SymbolLocation>& l, const std::string&) { syms = l; done = true; });
  f.ed.waitFor([&] { return done; }, 5000);
  REQUIRE(!syms.empty());
  CHECK_EQ(syms[0].name, "Vec2");
}

TEST(semantic_clangd_resolves_what_the_fast_parser_cannot) {
  if (!clangdAvailable()) { std::cerr << "    (clangd not found: skipping)\n"; return; }
  Fixture f(true);
  REQUIRE(f.ok);
  fs::path main = kRoot / "src/main.cpp", hdr = kRoot / "include/geometry.h", geo = kRoot / "src/geometry.cpp";
  f.ed.open(main);
  // Wait for clangd to parse main.cpp (first parse builds the preamble) and
  // for its background index, which is what resolves definitions in other TUs.
  CHECK(f.ed.semantic().waitForFile(main, 90000));
  CHECK(f.ed.semantic().waitForClangdIndex(90000));

  struct Case { const char* useMark; const char* token; fs::path file; const char* defMark; };
  std::vector<Case> cases = {
      {"use_length_sq", "length_sq", hdr, "Vec2_length_sq_def"},            // via alias Vec2f -> Vec2<float>
      {"decl_v", "Vec2f", hdr, "Vec2f"},
      {"use_wx", "w.|x", hdr, "Vec2"},                                       // member via typedef; x is on the line after Vec2 (see below)
      {"use_area", "area", hdr, "Shape_area"},                               // through auto-deduced unique_ptr<Shape>
      {"use_traits_double", "value_type", hdr, "Traits_double_value_type"},  // explicit specialisation
      {"use_traits_int", "value_type", hdr, "Traits_value_type"},            // primary template
      {"use_get_w", "get_w", hdr, "Box_get_w"},                              // macro-generated member
      {"use_first_area", "first_area", hdr, "first_area"},                   // decltype trailing return
      {"use_box_name", "name", hdr, "Box_name"},                             // override resolution on a concrete type
      {"use_kind_box", "Box", hdr, "Kind"},                                  // enumerator (same line as enum)
      {"use_plus", "+", hdr, "Vec2_plus"},                                   // operator overload
      {"use_make_default_shape", "make_default_shape", geo, "make_default_shape_def"},
  };
  for (auto& c : cases) {
    auto o = f.gotoDef(main, c.useMark, c.token);
    REQUIRE(!o.calls.empty());
    CHECK_EQ(o.error, "");
    auto& [conf, locs] = o.calls.back();
    CHECK(conf == Confidence::Authoritative);
    if (locs.size() != 1u) {
      std::cerr << "    " << c.useMark << "/" << c.token << ": " << locs.size() << " locations\n";
      for (auto& l : locs) std::cerr << "      " << toString(l.loc) << "\n";
    }
    REQUIRE(locs.size() == 1u);
    int want = markLine(c.file, c.defMark);
    if (std::string(c.useMark) == "use_wx") want += 1;  // "T x, y;" is the line after the struct marker
    if (locs[0].loc.file != c.file || locs[0].loc.pos.line != want) {
      std::cerr << "    " << c.useMark << "/" << c.token << ": got " << toString(locs[0].loc) << " want " << c.file << ":" << want + 1 << "\n";
    }
    CHECK(locs[0].loc.file == c.file);
    CHECK_EQ(locs[0].loc.pos.line, want);
    // The editor jumped there and recorded history.
    CHECK(f.ed.cursor().file == c.file);
    CHECK_EQ(f.ed.cursor().pos.line, want);
  }
  // The fast index alone has no idea about get_w (macro-generated) - prove the
  // tiers differ where it matters.
  Document* mainDoc = f.ed.document(main);
  REQUIRE(mainDoc != nullptr);
  int line = markLine(main, "use_get_w");
  auto approx = f.ed.semantic().approximateDefinitions(*mainDoc, {line, columnOf(main, line, "get_w")});
  CHECK(approx.empty());

  // Back walks the whole chain of jumps in reverse, ending at the first use site.
  int backs = 0;
  while (f.ed.back()) ++backs;
  CHECK(backs >= static_cast<int>(cases.size()));
  CHECK(f.ed.cursor().file == main);
  CHECK_EQ(f.ed.cursor().pos.line, markLine(main, cases.front().useMark));
  // Forward all the way returns to the final destination.
  while (f.ed.forward()) {}
  CHECK(f.ed.cursor().file == cases.back().file);
}

TEST(semantic_clangd_hover_references_symbols_and_edits) {
  if (!clangdAvailable()) { std::cerr << "    (clangd not found: skipping)\n"; return; }
  Fixture f(true);
  REQUIRE(f.ok);
  fs::path main = kRoot / "src/main.cpp", hdr = kRoot / "include/geometry.h";
  Document* doc = f.ed.open(main);
  REQUIRE(doc != nullptr);
  CHECK(f.ed.semantic().waitForFile(main, 90000));

  // Hover on the auto variable tells us the deduced type.
  int line = markLine(main, "use_area");
  f.ed.gotoLocation({main, {line, columnOf(main, line, "s->")}});
  std::string hoverText;
  bool done = false;
  f.ed.hover([&](const std::string& t, const std::string&) { hoverText = t; done = true; });
  f.ed.waitFor([&] { return done; }, 30000);
  CHECK(hoverText.find("unique_ptr") != std::string::npos);

  // References to Shape::area from its declaration: the pure virtual decl, s->area(), first_area's two uses.
  int areaLine = markLine(hdr, "Shape_area");
  f.ed.gotoLocation({hdr, {areaLine, columnOf(hdr, areaLine, "area")}});
  CHECK(f.ed.semantic().waitForFile(hdr, 90000));
  std::vector<SymbolLocation> refs;
  done = false;
  f.ed.findReferences([&](Confidence, const std::vector<SymbolLocation>& l, const std::string&) { refs = l; done = true; });
  f.ed.waitFor([&] { return done; }, 30000);
  CHECK(refs.size() >= 3u);
  bool sawUse = false;
  for (auto& r : refs) if (r.loc.file == main && r.loc.pos.line == markLine(main, "use_area")) sawUse = true;
  CHECK(sawUse);

  // Workspace symbol search: approximate results arrive first (instantly), authoritative later.
  std::vector<Confidence> order;
  std::vector<SymbolLocation> lastSyms;
  done = false;
  f.ed.semantic().waitForClangdIndex(60000);
  f.ed.findSymbols("Circle", [&](Confidence c, const std::vector<SymbolLocation>& l, const std::string&) {
    order.push_back(c);
    lastSyms = l;
    if (c == Confidence::Authoritative) done = true;
  });
  f.ed.waitFor([&] { return done; }, 30000);
  REQUIRE(!order.empty());
  CHECK(order.back() == Confidence::Authoritative);
  bool foundCircle = false;
  for (auto& s : lastSyms) if (s.name == "Circle" && s.loc.file == hdr) foundCircle = true;
  CHECK(foundCircle);

  // Editing keeps clangd and the navigation history in sync: insert two
  // lines at the top of main.cpp, then go-to-definition still works and
  // history entries below the edit have shifted.
  f.ed.gotoLocation({main, {0, 0}});
  size_t entriesBefore = f.ed.navigation().entries().size();
  int useLine = markLine(main, "use_length_sq");
  f.ed.insertText("// inserted\n// lines\n");
  CHECK_EQ(doc->buffer().line(0), "// inserted");
  CHECK_EQ(markLine(main, "use_length_sq"), useLine);  // on disk unchanged
  bool shifted = false;
  for (auto& e : f.ed.navigation().entries())
    if (e.loc.file == main && e.loc.pos.line == markLine(main, "use_area") + 2) shifted = true;
  CHECK(shifted);
  CHECK(f.ed.navigation().entries().size() >= entriesBefore);
  f.ed.gotoLocation({main, {useLine + 2, columnOf(main, useLine, "length_sq")}});
  done = false;
  std::vector<SymbolLocation> locs;
  f.ed.gotoDefinition([&](Confidence c, const std::vector<SymbolLocation>& l, const std::string&) {
    if (c == Confidence::Authoritative) { locs = l; done = true; }
  });
  f.ed.waitFor([&] { return done; }, 60000);
  REQUIRE(locs.size() == 1u);
  CHECK_EQ(locs[0].loc.pos.line, markLine(hdr, "Vec2_length_sq_def"));
  // Undo restores the buffer (and clangd gets the change too).
  CHECK(doc->undo());
  CHECK_EQ(doc->buffer().line(0), "#include <utility>");
}
