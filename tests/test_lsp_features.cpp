// Integration tests for the clangd feature set of LspClient / SemanticEngine / Editor (semantic tokens,
// hierarchies, highlights, folding, inlay hints, code actions, formatting, watched files). They need clangd and are
// skipped (with a message) without it; the no-clangd contract and edit application are checked either way.
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

#include "app/Editor.h"
#include "core/PathUtil.h"
#include "core/Platform.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

const fs::path kRoot = fs::path(TENFOLD_SOURCE_DIR) / "examples/sample_project";

bool clangdAvailable() { return !findExecutable("clangd").empty(); }

#define SKIP_WITHOUT_CLANGD()                               \
  if (!clangdAvailable()) {                                 \
    std::cerr << "    SKIPPED: clangd not found on PATH\n"; \
    return;                                                 \
  }

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void spit(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << text;
}

int markLine(const fs::path& file, const std::string& name) {
  std::ifstream in(file);
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    if (line.find("@MARK:" + name) != std::string::npos) return n;
    ++n;
  }
  return -1;
}

int columnOf(const fs::path& file, int line, const std::string& token) {
  std::ifstream in(file);
  std::string s;
  for (int i = 0; i <= line && std::getline(in, s); ++i) {}
  size_t p = s.find(token);
  return p == std::string::npos ? -1 : static_cast<int>(p);
}

struct TempDir {
  fs::path path;
  TempDir() {
    static std::atomic<int> n{0};
    path =
        fs::temp_directory_path() / ("tenfold_lsp_" + std::to_string(currentProcessId()) + "_" + std::to_string(n++));
    fs::remove_all(path);
    fs::create_directories(path);
    std::error_code ec;
    path = fs::weakly_canonical(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

// An Editor on a workspace (the sample project, or a temp copy of it), with a synchronous call helper.
struct Fx {
  fs::path root;
  Editor ed;
  bool ok = false;
  explicit Fx(const fs::path& r, bool useClangd = true, bool polling = false)
      : root(r), ed(r, options(useClangd, polling)) {
    std::string err;
    ok = ed.start(&err);
    if (!ok) std::cerr << "    engine start: " << err << "\n";
  }
  static Editor::Options options(bool useClangd, bool polling) {
    Editor::Options o;
    o.semantic.useClangd = useClangd;
    o.semantic.traceLsp = std::getenv("TENFOLD_TEST_TRACE") != nullptr;
    o.semantic.semanticTokensDebounceMs = 50;
    o.watchForcePolling = polling;
    o.watchPollMs = 100;
    o.log = [](const std::string& m) {
      if (std::getenv("TENFOLD_TEST_VERBOSE")) std::cerr << "    [engine] " << m << "\n";
    };
    return o;
  }
  // Open `file`, make it current and wait until clangd has its AST.
  Document* openReady(const fs::path& file, Position at = {0, 0}) {
    std::string e;
    if (!ed.gotoLocation({file, at}, &e)) return nullptr;
    if (ed.semantic().clangd()) ed.semantic().waitForFile(file, 60000);
    return ed.document(file);
  }
  template <class T, class F>
  bool call(F issue, T* out, std::string* err = nullptr, int ms = 60000) {
    bool done = false;
    std::string e;
    issue(Editor::Done<T>([&](const T& v, const std::string& er) {
      *out = v;
      e = er;
      done = true;
    }));
    bool finished = ed.waitFor([&] { return done; }, ms);
    if (err) *err = finished ? e : "timeout";
    return finished && e.empty();
  }
};

const fs::path kMain = kRoot / "src/main.cpp";
const fs::path kHeader = kRoot / "include/geometry.h";

bool hasType(const Fx& f, const std::vector<TokenSpan>& toks, int line, int col, const std::string& type) {
  auto legend = const_cast<Fx&>(f).ed.semantic().tokenLegend();
  for (auto& t : toks)
    if (t.line == line && t.col == col && t.type < legend.types.size() && legend.types[t.type] == type) return true;
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// No clangd: an error, never fabricated data
// ---------------------------------------------------------------------------

TEST(lsp_features_error_without_clangd) {
  Fx f(kRoot, /*useClangd=*/false);
  REQUIRE(f.ok);
  Document* d = f.openReady(kMain);
  REQUIRE(d != nullptr);
  std::string err;
  std::vector<TokenSpan> toks;
  CHECK(!f.call<std::vector<TokenSpan>>([&](auto done) { f.ed.semanticTokens(done); }, &toks, &err));
  CHECK_EQ(err, std::string("clangd not running"));
  CHECK(toks.empty());
  std::vector<HierarchyItem> items;
  CHECK(!f.call<std::vector<HierarchyItem>>([&](auto done) { f.ed.callHierarchy(done); }, &items, &err));
  CHECK(items.empty());
  std::vector<CodeAction> actions;
  CHECK(!f.call<std::vector<CodeAction>>([&](auto done) { f.ed.codeActions({{0, 0}, {0, 1}}, done); }, &actions, &err));
  size_t n = 0;
  CHECK(!f.call<size_t>([&](auto done) { f.ed.formatDocument(done); }, &n, &err));
  CHECK(!err.empty());
  CHECK(!d->dirty());
}

// ---------------------------------------------------------------------------
// Edit application (no clangd needed)
// ---------------------------------------------------------------------------

TEST(lsp_features_apply_edits_one_undo_group_and_unopened_files) {
  TempDir dir;
  spit(dir.path / "src/a.cpp", "int a;\nint b;\nint c;\n");
  spit(dir.path / "src/b.cpp", "int x;\n");
  Fx f(dir.path, false);
  REQUIRE(f.ok);
  Document* a = f.ed.open(dir.path / "src/a.cpp");
  REQUIRE(a != nullptr);
  f.ed.gotoLocation({a->path(), {2, 3}});
  const std::string before = a->buffer().text();
  FileEdits edits;
  // Out of order on purpose, and two insertions at the same point (they must keep their order).
  edits[a->path()] = {
      {{{2, 4}, {2, 5}}, "cc"}, {{{0, 0}, {0, 0}}, "// A\n"}, {{{0, 0}, {0, 0}}, "// B\n"}, {{{1, 4}, {1, 5}}, "bb"}};
  edits[dir.path / "src/b.cpp"] = {{{{0, 0}, {0, 0}}, "#pragma once\n"}};
  std::string err;
  REQUIRE(f.ed.applyEdits(edits, &err));
  CHECK_EQ(a->buffer().text(), std::string("// A\n// B\nint a;\nint bb;\nint cc;\n"));
  // The caret on line 2 followed the two inserted lines.
  CHECK_EQ(f.ed.cursor().pos.line, 4);
  // The unopened file became a document; the disk is untouched.
  Document* b = f.ed.document(dir.path / "src/b.cpp");
  REQUIRE(b != nullptr);
  CHECK_EQ(b->buffer().text(), std::string("#pragma once\nint x;\n"));
  CHECK(b->dirty());
  CHECK_EQ(slurp(dir.path / "src/b.cpp"), std::string("int x;\n"));
  CHECK_EQ(slurp(dir.path / "src/a.cpp"), before);
  // One undo step per document.
  CHECK(a->undo());
  CHECK_EQ(a->buffer().text(), before);
  CHECK(!a->canUndo());
  CHECK(b->undo());
  CHECK(!b->canUndo());
  // A file that cannot be opened aborts before anything changes.
  FileEdits bad;
  bad[a->path()] = {{{{0, 0}, {0, 0}}, "X"}};
  bad[dir.path / "src/missing.cpp"] = {{{{0, 0}, {0, 0}}, "Y"}};
  CHECK(!f.ed.applyEdits(bad, &err));
  CHECK_EQ(a->buffer().text(), before);
}

// ---------------------------------------------------------------------------
// Semantic tokens
// ---------------------------------------------------------------------------

TEST(lsp_semantic_tokens_vec2f_type_and_delta) {
  SKIP_WITHOUT_CLANGD();
  Fx f(kRoot);
  REQUIRE(f.ok);
  Document* d = f.openReady(kMain);
  REQUIRE(d != nullptr);
  const int line = markLine(kMain, "decl_v");
  const int col = columnOf(kMain, line, "Vec2f");
  const int vtLine = markLine(kMain, "use_traits_double");
  const int vtCol = columnOf(kMain, vtLine, "value_type");
  REQUIRE(line >= 0 && col >= 0 && vtLine >= 0 && vtCol >= 0);

  std::vector<TokenSpan> toks;
  std::string err;
  REQUIRE(f.call<std::vector<TokenSpan>>([&](auto done) { f.ed.semanticTokens(done); }, &toks, &err));
  CHECK(!toks.empty());
  // clangd resolves the `using Vec2f = Vec2<float>` alias to the class it names, so that usage is a `class`
  // token; an alias of a non-class (Traits<double>::value_type = long double) is the `type` token.
  CHECK(hasType(f, toks, line, col, "class"));
  CHECK(hasType(f, toks, vtLine, vtCol, "type"));
  for (auto& t : toks) CHECK(t.len > 0 && t.col >= 0);
  auto st = f.ed.semantic().tokenStats();
  CHECK_EQ(st.fullRequests, 1);
  CHECK_EQ(st.deltaRequests, 0);
  CHECK(!f.ed.semantic().tokenLegend().types.empty());

  // Edit: a new first line shifts everything down. The next request is a delta and the tokens survive it.
  d->insert({0, 0}, "// shifted\n");
  REQUIRE(f.call<std::vector<TokenSpan>>([&](auto done) { f.ed.semanticTokens(done); }, &toks, &err));
  st = f.ed.semantic().tokenStats();
  CHECK_EQ(st.deltaRequests, 1);
  CHECK(hasType(f, toks, line + 1, col, "class"));
  CHECK(hasType(f, toks, vtLine + 1, vtCol, "type"));
  CHECK(!hasType(f, toks, line, col, "class"));
  CHECK(st.lastDecodeMs >= 0);

  // From now on the engine pushes (debounced) tokens for every edit, tagged with the document version.
  std::vector<std::vector<TokenSpan>> pushed;
  f.ed.onSemanticTokens([&](Document&, const std::vector<TokenSpan>& t) { pushed.push_back(t); });
  d->insert({0, 0}, "// again\n");
  CHECK(f.ed.waitFor([&] { return !pushed.empty(); }, 20000));
  if (!pushed.empty()) CHECK(hasType(f, pushed.back(), line + 2, col, "class"));
}

// ---------------------------------------------------------------------------
// Hierarchies, highlights, folding, inlay hints
// ---------------------------------------------------------------------------

TEST(lsp_call_hierarchy_shape_area_has_main_caller) {
  SKIP_WITHOUT_CLANGD();
  Fx f(kRoot);
  REQUIRE(f.ok);
  REQUIRE(f.openReady(kMain) != nullptr);  // gives clangd main.cpp's references to area()
  f.ed.semantic().waitForClangdIndex(60000);
  const int line = markLine(kHeader, "Shape_area");
  Document* h = f.openReady(kHeader, {line, columnOf(kHeader, line, "area")});
  REQUIRE(h != nullptr);
  std::vector<HierarchyItem> items;
  std::string err;
  REQUIRE(f.call<std::vector<HierarchyItem>>([&](auto done) { f.ed.callHierarchy(done); }, &items, &err));
  REQUIRE(items.size() == 1u);
  CHECK_EQ(items[0].name, std::string("area"));
  CHECK_EQ(items[0].loc.pos.line, line);
  CHECK_EQ(items[0].loc.pos.column, columnOf(kHeader, line, "area"));
  std::vector<CallEdge> in;
  REQUIRE(f.call<std::vector<CallEdge>>([&](auto done) { f.ed.incomingCalls(items[0], done); }, &in, &err));
  const CallEdge* fromMain = nullptr;
  for (auto& e : in)
    if (e.item.name == "main") fromMain = &e;
  REQUIRE(fromMain != nullptr);
  CHECK_EQ(fromMain->item.loc.file.filename().string(), std::string("main.cpp"));
  const int useLine = markLine(kMain, "use_area");
  REQUIRE(!fromMain->ranges.empty());
  CHECK_EQ(fromMain->ranges[0].start.line, useLine);
  CHECK_EQ(fromMain->ranges[0].start.column, columnOf(kMain, useLine, "area"));
  // Jumpable through the editor (navigation history).
  CHECK(f.ed.gotoHierarchyItem(fromMain->item, &err));
  CHECK_EQ(f.ed.cursor().file.filename().string(), std::string("main.cpp"));
  CHECK_EQ(f.ed.cursor().pos.line, fromMain->item.loc.pos.line);
  CHECK(f.ed.back());
  CHECK_EQ(f.ed.cursor().file.filename().string(), std::string("geometry.h"));
  // Outgoing calls of main include make_default_shape.
  std::vector<CallEdge> out;
  REQUIRE(f.call<std::vector<CallEdge>>([&](auto done) { f.ed.outgoingCalls(fromMain->item, done); }, &out, &err));
  bool sawMake = false;
  for (auto& e : out) sawMake |= e.item.name == "make_default_shape";
  CHECK(sawMake);
}

TEST(lsp_type_hierarchy_shape_subtypes_and_supertypes) {
  SKIP_WITHOUT_CLANGD();
  Fx f(kRoot);
  REQUIRE(f.ok);
  REQUIRE(f.openReady(kMain) != nullptr);
  f.ed.semantic().waitForClangdIndex(60000);
  const int line = markLine(kHeader, "Shape");
  REQUIRE(f.openReady(kHeader, {line, columnOf(kHeader, line, "Shape")}) != nullptr);
  std::vector<HierarchyItem> items;
  std::string err;
  REQUIRE(f.call<std::vector<HierarchyItem>>([&](auto done) { f.ed.typeHierarchy(done); }, &items, &err));
  REQUIRE(items.size() == 1u);
  CHECK_EQ(items[0].name, std::string("Shape"));
  std::vector<HierarchyItem> subs;
  REQUIRE(f.call<std::vector<HierarchyItem>>([&](auto done) { f.ed.subtypes(items[0], done); }, &subs, &err));
  std::set<std::string> names;
  for (auto& s : subs) names.insert(s.name);
  CHECK(names.count("Circle"));
  CHECK(names.count("Box"));
  for (auto& s : subs)
    if (s.name == "Box") {
      CHECK_EQ(s.loc.pos.line, markLine(kHeader, "Box"));
      std::vector<HierarchyItem> supers;
      REQUIRE(f.call<std::vector<HierarchyItem>>([&](auto done) { f.ed.supertypes(s, done); }, &supers, &err));
      REQUIRE(supers.size() == 1u);
      CHECK_EQ(supers[0].name, std::string("Shape"));
    }
}

TEST(lsp_highlight_folding_inlay) {
  SKIP_WITHOUT_CLANGD();
  Fx f(kRoot);
  REQUIRE(f.ok);
  const int vLine = markLine(kMain, "decl_v");
  Document* d = f.openReady(kMain, {vLine, columnOf(kMain, vLine, "v{")});
  REQUIRE(d != nullptr);
  std::string err;
  std::vector<LspHighlight> hl;
  REQUIRE(f.call<std::vector<LspHighlight>>([&](auto done) { f.ed.documentHighlights(done); }, &hl, &err));
  // `v`: declaration, v.length_sq(), and both operands of v + v.
  CHECK_EQ(hl.size(), 4u);
  bool onDecl = false;
  for (auto& h : hl) onDecl |= h.range.start.line == vLine && h.range.start.column == columnOf(kMain, vLine, "v{");
  CHECK(onDecl);

  std::vector<LspFoldingRange> folds;
  REQUIRE(f.call<std::vector<LspFoldingRange>>([&](auto done) { f.ed.foldingRanges(done); }, &folds, &err));
  CHECK(!folds.empty());
  bool mainBody = false;
  for (auto& r : folds) mainBody |= r.startLine <= 5 && r.endLine >= 22;
  CHECK(mainBody);

  std::vector<LspInlayHint> hints;
  const int sLine = markLine(kMain, "use_make_default_shape");
  REQUIRE(f.call<std::vector<LspInlayHint>>(
      [&](auto done) { f.ed.inlayHints({{0, 0}, {d->buffer().lineCount(), 0}}, done); }, &hints, &err));
  CHECK(!hints.empty());
  bool deduced = false;
  for (auto& h : hints)
    if (h.pos.line == sLine && h.label.find("Shape") != std::string::npos) deduced = true;
  CHECK(deduced);
}

// ---------------------------------------------------------------------------
// Code actions, formatting
// ---------------------------------------------------------------------------

// Opens `src` as a new file of a temp copy of the sample project, takes the include quick fix whose title contains
// `wantTitle` for the first diagnostic on `diagLine`, applies it and checks it is one undo step that leaves the disk
// alone. `needle` must appear in the text afterwards.
static void quickFixCase(const std::string& src, int diagLine, const std::string& expectDiag,
                         const std::string& wantTitle, const std::string& needle, const std::string& gone,
                         bool skipIfDiagMissing) {
  TempDir dir;
  fs::copy(kRoot, dir.path, fs::copy_options::recursive);
  const fs::path file = dir.path / "src/needs_include.cpp";
  spit(file, src);
  Fx f(dir.path);
  REQUIRE(f.ok);
  Document* d = f.openReady(file);
  REQUIRE(d != nullptr);
  REQUIRE(f.ed.waitFor([&] { return !f.ed.diagnostics(file).empty(); }, 60000));
  const Diagnostic* diag = nullptr;
  for (auto& x : f.ed.diagnostics(file)) {
    if (!diag && x.range.start.line == diagLine) diag = &x;
  }
  if (diag && diag->message.find(expectDiag) == std::string::npos) diag = nullptr;
  if (!diag) {
    for (auto& x : f.ed.diagnostics(file)) std::cerr << "    diag " << x.range.start.line << ": " << x.message << "\n";
    if (skipIfDiagMissing) {
      std::cerr << "    SKIPPED: this platform's standard library does not produce the expected diagnostic\n";
      return;
    }
  }
  REQUIRE(diag != nullptr);
  std::vector<CodeAction> actions;
  std::string err;
  const CodeAction* inc = nullptr;
  REQUIRE(f.call<std::vector<CodeAction>>([&](auto done) { f.ed.codeActions(diag->range, done); }, &actions, &err));
  for (auto& a : actions)
    if (a.title.find(wantTitle) != std::string::npos) inc = &a;
  if (!inc) {
    std::cerr << "    diagnostic: " << diag->message << "\n";
    for (auto& a : actions) std::cerr << "    action: " << a.title << "\n";
      // clangd's include fixer needs its standard-library symbol index, which it builds for libstdc++ / libc++ but
      // not for the MSVC STL: the diagnostic is there, the include fix is not (windows-msvc and windows-clang-cl CI:
      // no action of any kind was offered). A platform limitation of clangd, not a failure of ours.
#ifdef _WIN32
    if (skipIfDiagMissing && actions.empty()) {
      std::cerr << "    SKIPPED: clangd offers no include fix with the MSVC standard library\n";
      return;
    }
#endif
  }
  REQUIRE(inc != nullptr);
  CHECK(inc->hasEdit);
  bool ok = false;
  f.ed.applyCodeAction(*inc, [&](bool applied, const std::string& e) {
    ok = applied;
    if (!e.empty()) std::cerr << "    " << e << "\n";
  });
  f.ed.waitFor([&] { return ok; }, 5000);
  CHECK(d->buffer().text().find(needle) != std::string::npos);
  if (!gone.empty()) CHECK(d->buffer().text().find(gone) == std::string::npos);
  CHECK(d->buffer().text() != src);
  CHECK(d->undo());  // ONE step
  CHECK_EQ(d->buffer().text(), src);
  CHECK(!d->canUndo());
  CHECK_EQ(slurp(file), src);  // nothing written to disk
}

// The classic: std::string without <string>. Whether the diagnostic exists depends on the standard library, not on
// us: libstdc++'s <vector> only forward-declares std::string (error + clangd's stdlib-header fix), while libc++'s and
// the MSVC STL's <vector> pull in the full <string> (no error to fix), so there the case is skipped with a message.
// The typo-correction test below covers the same edit/undo path on every platform.
TEST(lsp_code_action_adds_missing_include_one_undo_step) {
  SKIP_WITHOUT_CLANGD();
  quickFixCase("#include <vector>\n\nint use() {\n  std::string s;\n  return 0;\n}\n", 3, "'string' in namespace 'std'",
               "<string>", "#include <string>", "", /*skipIfDiagMissing=*/true);
}

// A fix-it that carries a plain edit, independent of any library: clang's typo correction.
TEST(lsp_code_action_typo_fixit_one_undo_step) {
  SKIP_WITHOUT_CLANGD();
  quickFixCase("int use() {\n  int length = 3;\n  return lenth;\n}\n", 2, "lenth", "length", "length", "lenth", false);
}

TEST(lsp_code_action_command_applies_server_edit) {
  SKIP_WITHOUT_CLANGD();
  TempDir dir;
  fs::copy(kRoot, dir.path, fs::copy_options::recursive);
  const std::string src = "int twice(int a) {\n  return a * 2 + 1;\n}\n";
  const fs::path file = dir.path / "src/tweak.cpp";
  spit(file, src);
  Fx f(dir.path);
  REQUIRE(f.ok);
  Document* d = f.openReady(file);
  REQUIRE(d != nullptr);
  std::vector<CodeAction> actions;
  std::string err;
  // Select `a * 2`: clangd offers "Extract subexpression to variable" as a tweak (a command, no edit).
  Range sel{{1, 9}, {1, 14}};
  REQUIRE(f.call<std::vector<CodeAction>>([&](auto done) { f.ed.codeActions(sel, done); }, &actions, &err));
  const CodeAction* tweak = nullptr;
  for (auto& a : actions)
    if (a.hasCommand && a.title.find("Extract") != std::string::npos) tweak = &a;
  if (!tweak) {
    for (auto& a : actions) std::cerr << "    action: " << a.title << " cmd=" << a.command << "\n";
  }
  REQUIRE(tweak != nullptr);
  CHECK(!tweak->hasEdit);
  bool ok = false;
  f.ed.applyCodeAction(*tweak, [&](bool applied, const std::string&) { ok = applied; });
  REQUIRE(f.ed.waitFor([&] { return ok; }, 20000));
  f.ed.pump();
  CHECK(d->buffer().text() != src);
  CHECK(d->buffer().text().find("auto") != std::string::npos);
  CHECK(d->undo());
  CHECK_EQ(d->buffer().text(), src);
}

TEST(lsp_formatting_document_and_range_are_undo_groups) {
  SKIP_WITHOUT_CLANGD();
  TempDir dir;
  fs::copy(kRoot, dir.path, fs::copy_options::recursive);
  const std::string src = "int   add(int a,int b){return a+b;}\nint  main( ){\nint x=add( 1,2 );\n    return x;}\n";
  const fs::path file = dir.path / "src/messy.cpp";
  spit(file, src);
  Fx f(dir.path);
  REQUIRE(f.ok);
  Document* d = f.openReady(file);
  REQUIRE(d != nullptr);
  size_t n = 0;
  std::string err;
  REQUIRE(f.call<size_t>([&](auto done) { f.ed.formatDocument(done); }, &n, &err));
  CHECK(n > 0);
  CHECK(d->buffer().text() != src);
  CHECK(d->buffer().text().find("int add(int a, int b) {") != std::string::npos);
  CHECK(d->undo());
  CHECK_EQ(d->buffer().text(), src);
  CHECK(!d->canUndo());

  // Range formatting only touches the lines asked for.
  REQUIRE(f.call<size_t>([&](auto done) { f.ed.formatRange({{2, 0}, {2, 14}}, done); }, &n, &err));
  CHECK(n > 0);
  CHECK_EQ(d->buffer().line(0), std::string("int   add(int a,int b){return a+b;}"));
  CHECK(d->buffer().line(2) != std::string("int x=add( 1,2 );"));
  CHECK(d->undo());
  CHECK_EQ(d->buffer().text(), src);
}

// ---------------------------------------------------------------------------
// workspace/didChangeWatchedFiles
// ---------------------------------------------------------------------------

TEST(lsp_did_change_watched_files_for_unopened_files) {
  SKIP_WITHOUT_CLANGD();
  TempDir dir;
  fs::copy(kRoot, dir.path, fs::copy_options::recursive);
  Fx f(dir.path, true, /*polling=*/true);
  REQUIRE(f.ok);
  REQUIRE(f.openReady(dir.path / "src/main.cpp") != nullptr);
  auto c = f.ed.semantic().clangd();
  REQUIRE(c != nullptr);
  auto has = [&](const fs::path& p, int type) {
    for (auto& e : c->sentFileEvents())
      if (e.uri == pathToUri(p) && e.type == type) return true;
    return false;
  };
  const fs::path hdr = dir.path / "include/geometry.h";
  std::this_thread::sleep_for(std::chrono::milliseconds(300));  // let the first poll snapshot settle
  spit(hdr, slurp(hdr) + "\n// touched\n");
  CHECK(f.ed.waitFor([&] { return has(hdr, 2); }, 10000));
  const fs::path added = dir.path / "include/extra.h";
  spit(added, "#pragma once\n");
  CHECK(f.ed.waitFor([&] { return has(added, 1); }, 10000));
  fs::remove(added);
  CHECK(f.ed.waitFor([&] { return has(added, 3); }, 10000));
  // A file that is open in the editor is the server's business through didChange, never through the watcher.
  const fs::path open = dir.path / "src/main.cpp";
  spit(open, slurp(open) + "\n// touched\n");
  f.ed.waitFor([] { return false; }, 600);
  CHECK(!has(open, 2));
}

// ---------------------------------------------------------------------------
// Benchmark (only with TENFOLD_BENCH_ROOT and TENFOLD_BENCH_FILE set): semantic tokens full / delta latency
// ---------------------------------------------------------------------------

TEST(lsp_bench_semantic_tokens) {
  const char* root = std::getenv("TENFOLD_BENCH_ROOT");
  const char* file = std::getenv("TENFOLD_BENCH_FILE");
  if (!root || !file || !clangdAvailable()) return;
  Fx f(root);
  REQUIRE(f.ok);
  Document* d = f.openReady(file);
  REQUIRE(d != nullptr);
  auto ms = [](auto t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  };
  std::vector<TokenSpan> toks;
  std::string err;
  auto t0 = std::chrono::steady_clock::now();
  REQUIRE(f.call<std::vector<TokenSpan>>([&](auto done) { f.ed.semanticTokens(done); }, &toks, &err, 300000));
  std::cerr << "    bench: " << d->buffer().lineCount() << " lines, " << toks.size() << " tokens\n";
  std::cerr << "    bench: full  " << ms(t0) << " ms (decode " << f.ed.semantic().tokenStats().lastDecodeMs << " ms)\n";
  for (int i = 0; i < 5; ++i) {
    d->insert({100, 0}, "// bench edit\n");
    t0 = std::chrono::steady_clock::now();
    REQUIRE(f.call<std::vector<TokenSpan>>([&](auto done) { f.ed.semanticTokens(done); }, &toks, &err, 300000));
    std::cerr << "    bench: delta " << ms(t0) << " ms (decode " << f.ed.semantic().tokenStats().lastDecodeMs << " ms, "
              << toks.size() << " tokens)\n";
  }
  auto st = f.ed.semantic().tokenStats();
  std::cerr << "    bench: requests full=" << st.fullRequests << " delta=" << st.deltaRequests << "\n";
}
