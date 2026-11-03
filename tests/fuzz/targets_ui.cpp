// Fuzz harnesses that need the UI library (built with -DTENFOLD_BUILD_UI=ON): the layout.json reader of the workbench
// and the explorer's project / solution readers. A real Editor and Workbench run against a null renderer.
#include <filesystem>
#include <fstream>
#include <memory>

#include "FuzzTarget.h"
#include "app/Editor.h"
#include "text/text_test_util.h"
#include "ui/features/WorkspaceExplorer.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/shell/Workbench.h"

namespace tf::fuzz {
namespace {

namespace fs = std::filesystem;
using namespace tf::ui;

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

// One workspace with a.cpp .. d.cpp, one editor (not started: no clangd) and one workbench for the whole process.
struct Env {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  std::unique_ptr<gfx::TextRenderer> text;
  NullRenderer nr;
  std::unique_ptr<Workbench> bench;
  Env() {
    dir = fs::path(scratchDir()) / "layout-ws";
    fs::create_directories(dir);
    for (const char* f : {"a.cpp", "b.cpp", "c.cpp", "d.cpp"}) {
      std::ofstream out(dir / f, std::ios::binary);
      for (int i = 0; i < 20; ++i) out << "int " << f[0] << i << " = " << i << ";\n";
    }
    ed = std::make_unique<Editor>(dir);
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyz0123456789 ._-:,;=ABCDEFGHIJ"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    bench = std::make_unique<Workbench>(*text, *ed);
    bench->render(nr, 1000, 600);
  }
};

Env& env() {
  static Env e;
  return e;
}

void runLayout(const uint8_t* d, size_t n) {
  Env& e = env();
  const std::string text(reinterpret_cast<const char*>(d), n);
  const bool ok = e.bench->restoreLayoutFromJson(text);
  e.bench->render(e.nr, 1000, 600);
  if (ok) {
    // What was accepted must save and restore again.
    const std::string saved = e.bench->layoutJson();
    FUZZ_REQUIRE(e.bench->restoreLayoutFromJson(saved));
    FUZZ_REQUIRE(!e.bench->panels().empty());
  }
}

// Parts: 0 = App/App.vcxproj, 1 = App/App.vcxproj.filters, 2 = the .sln.
void runExplorer(const uint8_t* d, size_t n) {
  const std::vector<std::string> parts = splitParts(d, n, 3);
  writeScratch("ex/App/App.vcxproj", parts[0]);
  writeScratch("ex/App/App.vcxproj.filters", parts.size() > 1 ? parts[1] : "");
  writeScratch("ex/App.sln", parts.size() > 2 ? parts[2] : "");
  const fs::path root = fs::path(scratchDir()) / "ex";
  WorkspaceExplorer::readProject(root / "App" / "App.vcxproj");
  WorkspaceExplorer::buildSolutionTree(root / "App.sln");
}

Registrar r1({"layout",
              runLayout,
              {},
              {"{\"version\":1,\"activePanel\":0,\"root\":{\"type\":\"split\",\"orientation\":\"horizontal\",\"ratio\":"
               "0.4,\"first\":{\"type\":\"panel\",\"tabs\":[{\"path\":\"a.cpp\",\"pinned\":true},{\"path\":\"b.cpp\"}],"
               "\"active\":1},"
               "\"second\":{\"type\":\"panel\",\"tabs\":[{\"path\":\"c.cpp\"}]}},\"bottom\":{\"visible\":true,"
               "\"panel\":\"Build\",\"height\":0.3},\"left\":{\"visible\":true,\"panel\":\"Explorer\",\"width\":0.2},"
               "\"recent\":[\"a.cpp\",\"d.cpp\"]}",
               "{\"root\":{\"type\":\"panel\",\"tabs\":[]}}",
               "{\"root\":{\"type\":\"split\",\"first\":{\"type\":\"panel\",\"tabs\":[]}}}", "{\"root\":5}", "[]"},
              {"{",
               "}",
               "[",
               "]",
               ":",
               ",",
               "\"",
               "\"type\":\"panel\"",
               "\"type\":\"split\"",
               "\"first\":",
               "\"second\":",
               "\"tabs\":",
               "\"path\":",
               "\"pinned\":",
               "\"active\":",
               "\"ratio\":",
               "\"orientation\":\"vertical\"",
               "\"orientation\":\"horizontal\"",
               "\"bottom\":",
               "\"left\":",
               "\"visible\":",
               "\"panel\":",
               "\"height\":",
               "\"width\":",
               "\"recent\":",
               "\"activePanel\":",
               "\"root\":",
               "\"version\":",
               "\"a.cpp\"",
               "\"b.cpp\"",
               "\"c.cpp\"",
               "\"d.cpp\"",
               "\"missing.cpp\"",
               "\"../x\"",
               "\"/abs/a.cpp\"",
               "null",
               "true",
               "false",
               "-1",
               "0",
               "0.5",
               "1e999",
               "-1e999",
               "99999999999999999999",
               "2147483648",
               "nan",
               "\\u0000",
               "\\"},
              1024});
Registrar r2(
    {"explorer",
     runExplorer,
     {"tests/fixtures/vs:.vcxproj,.filters,.sln"},
     {"<Project><ItemGroup><ClCompile Include=\"a.cpp;sub\\b.cpp\"><Filter>Src</Filter></ClCompile><ClInclude "
      "Include=\"x.h\"/><None Include=\"*.txt\"/></ItemGroup></Project>\n#--#\n"
      "<Project><ItemGroup><Filter Include=\"Src\"><UniqueIdentifier>{1}</UniqueIdentifier></Filter><ClCompile "
      "Include=\"a.cpp\"><Filter>Src</Filter></ClCompile></ItemGroup></Project>\n#--#\n"
      "Microsoft Visual Studio Solution File, Format Version "
      "12.00\nProject(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"Dir\", \"Dir\", "
      "\"{AAAA}\"\n\tProjectSection(SolutionItems) = preProject\n\t\tREADME.md = "
      "README.md\n\tEndProjectSection\nEndProject\n"
      "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"App\", \"App\\App.vcxproj\", "
      "\"{BBBB}\"\nEndProject\nGlobal\n\tGlobalSection(NestedProjects) = preSolution\n\t\t{BBBB} = {AAAA}\n\t\t{AAAA} "
      "= {BBBB}\n\tEndGlobalSection\nEndGlobal\n"},
     {"<ItemGroup>",
      "<ClCompile Include=\"",
      "<Filter Include=\"",
      "<Filter>",
      "*",
      "**",
      "?",
      "\\",
      "/",
      "..\\",
      "$(",
      "{",
      "}",
      "ProjectSection(SolutionItems)",
      "EndProjectSection",
      "GlobalSection(NestedProjects)",
      "EndGlobalSection",
      "Project(\"",
      "EndProject",
      "\n#--#\n",
      " = ",
      "\"",
      ","},
     2048});

}  // namespace
}  // namespace tf::fuzz
