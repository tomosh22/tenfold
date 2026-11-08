// Fuzz harnesses of the engine parsers that need nothing but tenfold_core: the fast lexer and symbol extractor, the
// persisted fast-index store, the XML reader, the MSBuild evaluator (.sln / .vcxproj / .props / Directory.Build.props
// / .filters), the compile_commands.json reader and command-line splitter, the build output parser, the git / p4
// output parsers, the line diff engine, the terminal parser, the find-in-files query / glob / regex search.
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "FuzzTarget.h"
#include "app/Bookmarks.h"
#include "build/AnsiStrip.h"
#include "build/ErrorParser.h"
#include "core/LspLineMap.h"
#include "core/TextBuffer.h"
#include "debug/Breakpoints.h"
#include "glob_reference.h"
#include "search/ReplaceInFiles.h"
#include "search/TextSearch.h"
#include "semantic/CompileCommands.h"
#include "semantic/CppLexer.h"
#include "semantic/FastIndex.h"
#include "semantic/FastIndexStore.h"
#include "semantic/PpCond.h"
#include "semantic/PpExpr.h"
#include "semantic/ScopeScan.h"
#include "semantic/XmlLite.h"
#include "term/VtScreen.h"
#include "vcs/GitProvider.h"
#include "vcs/LineDiff.h"
#include "vcs/PerforceProvider.h"
#include "workspace/Workspace.h"

using namespace std::string_literals;

namespace {
std::vector<std::string> concat(std::vector<std::string> a, const std::vector<std::string>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}
}  // namespace

namespace tf::fuzz {
namespace {

namespace fs = std::filesystem;

std::string_view view(const uint8_t* d, size_t n) { return {reinterpret_cast<const char*>(d), n}; }
std::string str(const uint8_t* d, size_t n) { return std::string(reinterpret_cast<const char*>(d), n); }

const std::vector<std::string> kCppDict = {"namespace",
                                           "class ",
                                           "struct ",
                                           "union ",
                                           "enum ",
                                           "enum class ",
                                           "template<",
                                           "template <typename T>",
                                           "typename",
                                           "operator",
                                           "operator()",
                                           "operator<<",
                                           "operator new",
                                           "operator\"\"_x",
                                           "using ",
                                           "typedef ",
                                           "static ",
                                           "const ",
                                           "constexpr",
                                           "consteval",
                                           "inline",
                                           "virtual",
                                           "override",
                                           "final",
                                           "noexcept",
                                           "friend ",
                                           "concept ",
                                           "requires",
                                           "decltype(",
                                           "auto ",
                                           "return ",
                                           "if (",
                                           "else",
                                           "for (",
                                           "while (",
                                           "do {",
                                           "switch (",
                                           "case ",
                                           "default:",
                                           "try",
                                           "catch (",
                                           "throw",
                                           "extern \"C\" {",
                                           "#include <",
                                           "#include \"",
                                           "#define ",
                                           "#define X(a,b) a##b",
                                           "#undef ",
                                           "#if ",
                                           "#ifdef ",
                                           "#ifndef ",
                                           "#elif ",
                                           "#else",
                                           "#endif",
                                           "#pragma once",
                                           "#line 1",
                                           "__VA_ARGS__",
                                           "\\\n",
                                           "::",
                                           "->",
                                           "->*",
                                           ".*",
                                           "...",
                                           "<<=",
                                           ">>=",
                                           "<=>",
                                           "&&",
                                           "||",
                                           "++",
                                           "--",
                                           "{",
                                           "}",
                                           "(",
                                           ")",
                                           "[",
                                           "]",
                                           "<",
                                           ">",
                                           ";",
                                           ",",
                                           "~",
                                           "=",
                                           "= default",
                                           "= delete",
                                           "= 0",
                                           "/*",
                                           "*/",
                                           "//",
                                           "R\"(",
                                           ")\"",
                                           "R\"x(",
                                           ")x\"",
                                           "u8\"",
                                           "L'",
                                           "u'",
                                           "U\"",
                                           "'",
                                           "\"",
                                           "\\",
                                           "\\\"",
                                           "0x",
                                           "0b",
                                           "1'000",
                                           "1e+",
                                           "0.",
                                           ".5",
                                           "1.f",
                                           "1ull",
                                           "\r\n",
                                           "\t",
                                           "\xEF\xBB\xBF",
                                           "\xE4\xB8\xAD",
                                           "\xC0\x80",
                                           "\xFF",
                                           "[[",
                                           "]]",
                                           "[[nodiscard]]",
                                           "__declspec(",
                                           "__attribute__((",
                                           "alignas(",
                                           "<::",
                                           "<:",
                                           ":>",
                                           "%:",
                                           "?\?=",
                                           "@",
                                           "$",
                                           "`"};

// ---------------------------------------------------------------------------------------------------------------------
// text buffer: NEL / LS / PS line breaks and the clangd position map
// ---------------------------------------------------------------------------------------------------------------------

void runLineMap(const uint8_t* d, size_t n) {
  const std::string src = str(d, n);
  TextBuffer b(src);
  FUZZ_REQUIRE(b.textForDisk() == src);  // every terminator kept: byte-exact round trip
  FUZZ_REQUIRE(b.lineCount() == TextBuffer::countBreaks(src) + 1);
  FUZZ_REQUIRE(b.endingCounts().total() == b.lineCount() - 1);
  // The lines are the pieces between the breaks of the shared finder.
  {
    linebreak::BreakFinder bf(src);
    size_t from = 0, len = 0;
    int line = 0;
    for (size_t at = bf.next(0, len); at != std::string::npos; at = bf.next(from, len)) {
      FUZZ_REQUIRE(line < b.lineCount() && b.line(line) == src.substr(from, at - from));
      from = at + len;
      ++line;
    }
    FUZZ_REQUIRE(line == b.lineCount() - 1 && b.line(line) == src.substr(from));
  }
  // Converting every break to one kind keeps the lines.
  for (LineEnding e :
       {LineEnding::LF, LineEnding::CRLF, LineEnding::CR, LineEnding::NEL, LineEnding::LS, LineEnding::PS}) {
    TextBuffer c(TextBuffer::withLineEnding(src, e));
    FUZZ_REQUIRE(c.lineCount() == b.lineCount());
    for (int i = 0; i < c.lineCount(); ++i) FUZZ_REQUIRE(c.line(i) == b.line(i));
    FUZZ_REQUIRE(!c.mixedLineEndings());
  }
  // The map: absent exactly when there is no separator; positions on line starts and ends round-trip, and the server
  // line count is what a server counting only '\n' finds in the text it was sent.
  const std::string lsp = b.textForLsp();
  const auto m = LspLineMap::build(b);
  FUZZ_REQUIRE((m != nullptr) == b.hasSeparators());
  if (!m) return;
  const int serverLines = 1 + static_cast<int>(std::count(lsp.begin(), lsp.end(), '\n'));
  FUZZ_REQUIRE(m->serverLineCount(b.lineCount()) == serverLines);
  int prevLine = -1, prevCol = -1;
  for (bool utf16 : {true, false})
    for (int l = 0; l < b.lineCount(); ++l)
      for (int ci = 0; ci < 2; ++ci) {
        const int col = ci == 0 ? 0 : static_cast<int>(b.line(l).size());
        const Position e{l, col};
        const Position s = m->toServer(e, b.lines(), utf16);
        FUZZ_REQUIRE(s.line >= 0 && s.line < serverLines && s.column >= 0);
        FUZZ_REQUIRE(m->fromServer(s, b.lines(), utf16) == e);
        if (utf16 && ci == 0) {  // monotonic: later editor lines never map before earlier ones
          FUZZ_REQUIRE(s.line > prevLine || (s.line == prevLine && s.column > prevCol));
          prevLine = s.line;
          prevCol = s.column;
        }
      }
}

// ---------------------------------------------------------------------------------------------------------------------
// lexer
// ---------------------------------------------------------------------------------------------------------------------

void runLexer(const uint8_t* d, size_t n) {
  const std::string_view src = view(d, n);
  CppLexer lx(src);
  const char* prevEnd = src.data();
  size_t count = 0;
  for (;;) {
    const Token t = lx.next();
    if (t.kind == Tok::End) break;
    ++count;
    FUZZ_REQUIRE(count <= n + 1);  // every token consumes at least one byte
    FUZZ_REQUIRE(t.text.data() >= prevEnd && t.text.data() + t.text.size() <= src.data() + n);
    FUZZ_REQUIRE(!t.text.empty());
    FUZZ_REQUIRE(t.line >= 0 && t.col >= 0);
    prevEnd = t.text.data();  // tokens never go backwards (they may start inside the previous one: none do)
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// fast index: symbol extractor, and the persisted store
// ---------------------------------------------------------------------------------------------------------------------

void runExtract(const uint8_t* d, size_t n) {
  const std::string_view src = view(d, n);
  std::vector<std::string> inc;
  const std::vector<Symbol> syms = extractSymbols(src, &inc);
  // Lines as the document and the extractor count them: CRLF, LF, a lone CR and the Unicode separators NEL / LS / PS
  // (Visual Studio's line breaks, LE-UNICODE) each end one.
  const size_t lines = 1 + static_cast<size_t>(TextBuffer::countBreaks(src));
  for (const Symbol& s : syms) {
    FUZZ_REQUIRE(s.line >= 0 && static_cast<size_t>(s.line) < lines);
    FUZZ_REQUIRE(s.col >= 0);
    FUZZ_REQUIRE(!s.name.empty());
  }
  // Deterministic.
  std::vector<std::string> inc2;
  const std::vector<Symbol> again = extractSymbols(src, &inc2);
  FUZZ_REQUIRE(again.size() == syms.size() && inc2 == inc);
}

// lexical scope maps of the scope-aware fast tier (ScopeScan): any text maps to regions that nest and lie in the text
void runScopeScan(const uint8_t* d, size_t n) {
  const std::string_view src = view(d, n);
  const size_t lines = 1 + static_cast<size_t>(TextBuffer::countBreaks(src));
  const ScopeMap m = scanScopes(src);
  FUZZ_REQUIRE(!m.regions.empty());
  for (size_t i = 0; i < m.regions.size(); ++i) {
    const ScanRegion& r = m.regions[i];
    FUZZ_REQUIRE(i == 0 ? r.parent == -1 : (r.parent >= 0 && static_cast<size_t>(r.parent) < i));
    FUZZ_REQUIRE(r.begin.line >= 0 && static_cast<size_t>(r.begin.line) < lines);
    FUZZ_REQUIRE(!r.closed || r.begin <= r.end);
    for (const ScanVar& v : r.vars)
      FUZZ_REQUIRE(!v.name.empty() && v.at.line >= 0 && static_cast<size_t>(v.at.line) < lines);
  }
  // every position maps to a region that contains it (or the global scope)
  for (int line = 0; line < static_cast<int>(std::min<size_t>(lines, 64)); ++line) {
    const int r = m.regionAt({line, 0});
    FUZZ_REQUIRE(r >= 0 && static_cast<size_t>(r) < m.regions.size());
  }
  // deterministic
  FUZZ_REQUIRE(scanScopes(src).regions.size() == m.regions.size());
}

// The store checks a size and a hash of the payload before it looks at anything else, so a mutated file would be
// rejected at once. The harness repairs those header fields (and the extractor fingerprint) first, so that the
// mutations reach the record parsing; the magic, the format version and the counts stay as mutated.
uint64_t storeHash(const char* p, size_t n) {  // same function as FastIndexStore.cpp's hashBytes
  uint64_t h = 0x9E3779B97F4A7C15ull ^ n;
  auto mix = [&](uint64_t v) {
    h ^= v;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 32;
  };
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t v;
    std::memcpy(&v, p + i, 8);
    mix(v);
  }
  uint64_t tail = 0;
  if (i < n) std::memcpy(&tail, p + i, n - i);
  mix(tail);
  return h;
}

// ---------------------------------------------------------------------------------------------------------------------
// preprocessor conditionals (W12-FAST-PP): the #if expression evaluator and the condition extraction
// ---------------------------------------------------------------------------------------------------------------------

pp::MacroState fuzzState(std::string_view name, unsigned salt, bool allowUnknown) {
  const size_t h = std::hash<std::string_view>()(name) ^ (salt * 0x9E3779B97F4A7C15ull);
  switch ((h >> 8) % (allowUnknown ? 5 : 4)) {
    case 0: return pp::MacroState::undefined();
    case 1: return pp::MacroState::integer(static_cast<int64_t>((h >> 16) % 9) - 4, ((h >> 24) & 1) != 0);
    case 2: return pp::MacroState::defined();
    case 3: return pp::MacroState::integer(0);
    default: return pp::MacroState::unknown();
  }
}

void runPpCond(const uint8_t* d, size_t n) {
  const std::string_view src = view(d, n);
  using pp::Tri;
  // 1. the whole input as an #if expression. Three-valued soundness: a definite answer given that nothing is known
  // is the answer whatever the macros turn out to be; the same expression twice gives the same answer.
  const Tri none = pp::evalCondition(src, [](std::string_view) { return pp::MacroState::unknown(); });
  FUZZ_REQUIRE(none == pp::evalCondition(src, [](std::string_view) { return pp::MacroState::unknown(); }));
  for (unsigned salt = 1; salt <= 4; ++salt) {
    const Tri t = pp::evalCondition(src, [&](std::string_view nm) { return fuzzState(nm, salt, salt % 2 == 0); });
    if (none != Tri::Unknown) FUZZ_REQUIRE(t == none || t == Tri::Unknown);
    if (t != Tri::Unknown && salt % 2 == 1) {
      // fully known macros (no Unknown state): the answer does not depend on the order of evaluation
      FUZZ_REQUIRE(t == pp::evalCondition(src, [&](std::string_view nm) { return fuzzState(nm, salt, false); }));
    }
  }
  (void)pp::evalValue(src, [](std::string_view) { return pp::MacroState::undefined(); });
  // 2. every line as a directive
  for (size_t at = 0; at < src.size();) {
    size_t e = src.find('\n', at);
    if (e == std::string_view::npos) e = src.size();
    const pp::Directive dir = pp::parseDirective(src.substr(at, e - at));
    if (dir.kind == pp::Directive::Kind::Define) (void)pp::parseDefine(dir.arg);
    at = e + 1;
  }
  // 3. the input as a source file: arm tables are acyclic and in range, the store keeps them, the evaluator answers
  const size_t lines = 1 + static_cast<size_t>(TextBuffer::countBreaks(src));
  auto f = std::make_shared<FileSymbols>();
  f->file = "/fuzz/a.h";
  extractFile(src, *f);
  for (size_t i = 0; i < f->arms.size(); ++i) {
    const CondArm& a = f->arms[i];
    FUZZ_REQUIRE(a.parent <= i && a.prev <= i);  // ids are 1-based and point backwards
    FUZZ_REQUIRE(a.line >= 0 && static_cast<size_t>(a.line) < lines);
  }
  uint32_t lastSym = 0;
  bool first = true;
  for (const auto& sa : f->symArm) {
    FUZZ_REQUIRE(sa.first < f->symbols.size() && sa.second >= 1 && sa.second <= f->arms.size());
    FUZZ_REQUIRE(first || sa.first > lastSym);
    lastSym = sa.first;
    first = false;
  }
  for (const MacroDef& m : f->macros) {
    FUZZ_REQUIRE(!m.name.empty() && m.arm <= f->arms.size());
    FUZZ_REQUIRE(m.body >= -1 && m.body < static_cast<int64_t>(f->bodies.size()));
  }
  FastIndexStore::Entries entries{f};
  const std::string blob = FastIndexStore::serialize(entries);
  std::string err;
  auto back = FastIndexStore::deserialize(blob.data(), blob.size(), &err);
  FUZZ_REQUIRE(back.has_value() && back->size() == 1);
  FUZZ_REQUIRE((*back)[0]->arms.size() == f->arms.size() && (*back)[0]->symArm == f->symArm &&
               (*back)[0]->macros.size() == f->macros.size() && (*back)[0]->bodies == f->bodies);
  for (int variant = 0; variant < 2; ++variant) {
    pp::TuEnv::Input in;
    if (variant == 1) {
      CompileCommand cc;
      cc.directory = "/fuzz";
      cc.file = "/fuzz/a.cpp";
      cc.arguments = {"g++", "-DA", "-DB=2", "-UC", "-std=c++17"};
      in.command = pp::macrosOfCommand(cc);
    }
    in.files = {f};
    const pp::TuEnv env(std::move(in));
    pp::Evaluator ev(env), ev2(env);
    for (uint32_t a = 0; a <= f->arms.size(); ++a) FUZZ_REQUIRE(ev.armActive(*f, a) == ev2.armActive(*f, a));
    for (const MacroDef& m : f->macros) (void)ev.macroAt(m.name, f.get(), m.line + 1);
  }
}

void runIndexStore(const uint8_t* d, size_t n) {
  std::string err;
  std::string data = str(d, n);
  for (int pass = 0; pass < 2; ++pass) {  // pass 0: as given, pass 1: with the header repaired
    if (pass == 1) {
      if (data.size() < 48) break;
      const uint32_t fp = FastIndexStore::extractorFingerprint();
      const uint64_t payloadSize = data.size() - 48;
      const uint64_t hash = storeHash(data.data() + 48, payloadSize);
      std::memcpy(&data[12], &fp, 4);
      std::memcpy(&data[16], &payloadSize, 8);
      std::memcpy(&data[24], &hash, 8);
    }
    auto loaded = FastIndexStore::deserialize(data.data(), data.size(), &err);
    if (loaded) {
      // Whatever was accepted must survive a round trip.
      const std::string again = FastIndexStore::serialize(*loaded);
      auto second = FastIndexStore::deserialize(again.data(), again.size(), &err);
      FUZZ_REQUIRE(second.has_value() && second->size() == loaded->size());
      // The condition tables of an accepted store are acyclic and in range: the evaluator terminates on them.
      pp::TuEnv::Input in;
      in.files.assign(loaded->begin(), loaded->end());
      const pp::TuEnv env(std::move(in));
      pp::Evaluator ev(env);
      for (const auto& f : *loaded)
        for (uint32_t a = 0; a <= f->arms.size(); ++a) (void)ev.armActive(*f, a);
    }
  }
}

std::vector<std::string> indexStoreSeeds() {
  auto fs1 = std::make_shared<FileSymbols>();
  fs1->file = "/w/a.cpp";
  fs1->mtime = 1234;
  fs1->size = 99;
  fs1->includes = {"a.h", "vector"};
  Symbol s;
  s.name = "f";
  s.scope = "ns::C";
  s.kind = SymbolKind::Method;
  s.line = 3;
  s.col = 4;
  fs1->symbols.push_back(s);
  s.name = "g";
  s.scope = "";
  s.kind = SymbolKind::Function;
  s.isTemplate = true;
  s.definition = false;
  fs1->symbols.push_back(s);
  auto fs2 = std::make_shared<FileSymbols>();
  fs2->file = "/w/b.h";
  auto fs3 = std::make_shared<FileSymbols>();
  fs3->file = "/w/c.h";
  extractFile(
      "#ifndef C_H\n#define C_H\n#define LVL (1 + 2)\n#if LVL > 2 && defined(X)\nint a;\n#elif Y\nint b;\n#else\nint "
      "c;\n"
      "#endif\n#undef LVL\n#endif\n",
      *fs3);
  FastIndexStore::Entries both{fs1, fs2}, one{fs1}, none, cond{fs3, fs1};
  return {FastIndexStore::serialize(both), FastIndexStore::serialize(one), FastIndexStore::serialize(none),
          FastIndexStore::serialize(cond)};
}

// ---------------------------------------------------------------------------------------------------------------------
// XML
// ---------------------------------------------------------------------------------------------------------------------

size_t countNodes(const xml::Node& n, size_t depth) {
  FUZZ_REQUIRE(depth < 100000);
  size_t c = 1;
  for (const auto& ch : n.children) c += countNodes(*ch, depth + 1);
  return c;
}

void runXml(const uint8_t* d, size_t n) {
  std::string err;
  auto doc = xml::parse(view(d, n), &err);
  if (doc) countNodes(*doc, 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// MSBuild: project, imports, Directory.Build.props, .sln, expressions
// ---------------------------------------------------------------------------------------------------------------------

// Seeds for the msbuild harness from the Visual Studio fixtures: per fixture project directory, the project, the first
// .props next to or above it, the nearest Directory.Build.props, the solution and the .filters file.
std::vector<std::string> msbuildFixtureSeeds() {
  std::vector<std::string> out;
#ifdef TENFOLD_SOURCE_DIR
  std::error_code ec;
  const fs::path root = fs::path(TENFOLD_SOURCE_DIR) / "tests" / "fixtures" / "vs";
  auto slurp = [](const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  };
  for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (it->path().extension() != ".vcxproj") continue;
    const fs::path dir = it->path().parent_path();
    std::string props, dbp, sln, filters;
    for (const fs::path& base : {dir, dir.parent_path()}) {
      for (auto f = fs::directory_iterator(base, ec); !ec && f != fs::directory_iterator(); f.increment(ec)) {
        const std::string ext = f->path().extension().string();
        if (ext == ".props" && f->path().filename() != "Directory.Build.props" && props.empty())
          props = slurp(f->path());
        if (ext == ".sln" && sln.empty()) sln = slurp(f->path());
        if (f->path().filename() == "Directory.Build.props" && dbp.empty()) dbp = slurp(f->path());
      }
    }
    filters = slurp(it->path().string() + ".filters");
    out.push_back(slurp(it->path()) + "\n#--#\n" + props + "\n#--#\n" + dbp + "\n#--#\n" + sln + "\n#--#\n" + filters);
  }
#endif
  return out;
}

// Parts: 0 = App/App.vcxproj, 1 = common.props (imported by name), 2 = Directory.Build.props, 3 = the .sln,
// 4 = App/App.vcxproj.filters (read by the explorer, here only parsed as XML).
void runMsbuild(const uint8_t* d, size_t n) {
  const std::vector<std::string> parts = splitParts(d, n, 5);
  writeScratch("sln/App/App.vcxproj", parts[0]);
  writeScratch("sln/common.props", parts.size() > 1 ? parts[1] : "");
  writeScratch("sln/Directory.Build.props", parts.size() > 2 ? parts[2] : "");
  writeScratch("sln/App.sln", parts.size() > 3 ? parts[3] : "");
  writeScratch("sln/App/App.vcxproj.filters", parts.size() > 4 ? parts[4] : "");
  const fs::path root = fs::path(scratchDir()) / "sln";
  compdb::VcxprojOptions opt;
  opt.useProcessEnvironment = false;
  opt.environment = {{"FuzzEnv", "value"}};
  opt.solutionDir = root;
  std::string err;
  for (const char* cfg : {"Debug", "Release"}) {
    opt.configuration = cfg;
    opt.platform = cfg[0] == 'D' ? "x64" : "Win32";
    compdb::fromVcxproj(root / "App" / "App.vcxproj", opt, &err);
    compdb::fromSolution(root / "App.sln", opt, &err);
  }
  compdb::readSolution(root / "App.sln", &err);
  compdb::solutionConfigurations(root / "App.sln", &err);
  compdb::projectsInSolution(root / "App.sln", &err);
  xml::parse(parts.size() > 4 ? parts[4] : "", &err);
}

// Parts: 0 = the condition, 1 = the expression to expand, 2 = properties ("name=value" per line).
void runMsbuildExpr(const uint8_t* d, size_t n) {
  const std::vector<std::string> parts = splitParts(d, n, 3);
  std::map<std::string, std::string> props;
  if (parts.size() > 2) {
    const std::string& p = parts[2];
    for (size_t b = 0; b < p.size();) {
      size_t e = p.find('\n', b);
      if (e == std::string::npos) e = p.size();
      const size_t eq = p.find('=', b);
      if (eq != std::string::npos && eq < e) props[p.substr(b, eq - b)] = p.substr(eq + 1, e - eq - 1);
      b = e + 1;
    }
  }
  props["Configuration"] = "Debug";
  props["Platform"] = "x64";
  const fs::path base = fs::path(scratchDir());
  compdb::evaluateCondition(parts[0], props, base);
  compdb::expandProperties(parts[0], props);
  if (parts.size() > 1) {
    compdb::expandProperties(parts[1], props);
    compdb::evaluateCondition(parts[1], props, base);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// compile_commands.json
// ---------------------------------------------------------------------------------------------------------------------

void runCompdb(const uint8_t* d, size_t n) {
  writeScratch("cc/compile_commands.json", d, n);
  const fs::path file = fs::path(scratchDir()) / "cc" / "compile_commands.json";
  std::string err;
  const std::vector<CompileCommand> cmds = CompileDatabase::load(file, &err);
  // Whatever loaded must save and load back with the same arguments.
  const fs::path again = fs::path(scratchDir()) / "cc" / "again.json";
  if (CompileDatabase::save(cmds, again, &err)) {
    const std::vector<CompileCommand> second = CompileDatabase::load(again, &err);
    FUZZ_REQUIRE(second.size() == cmds.size());
    for (size_t i = 0; i < cmds.size(); ++i) FUZZ_REQUIRE(second[i].arguments == cmds[i].arguments);
  }
}

// The input is one command line: wrapped into a compile_commands.json entry as the "command" string.
void runCompdbCommand(const uint8_t* d, size_t n) {
  const nlohmann::json cmd = str(d, n);  // no UTF-8 validation at construction
  std::string text = "[{\"directory\":\"/w\",\"file\":\"a.cpp\",\"command\":";
  text += cmd.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
  text += "}]";
  writeScratch("cc/cmd.json", text);
  CompileDatabase::load(fs::path(scratchDir()) / "cc" / "cmd.json");
}

// ---------------------------------------------------------------------------------------------------------------------
// Build output
// ---------------------------------------------------------------------------------------------------------------------

void runBuildOutput(const uint8_t* d, size_t n) {
  const std::string text = str(d, n);
  const std::string plain = stripAnsiEscapes(text);
  FUZZ_REQUIRE(plain.size() <= text.size());
  for (const char* cwd : {"", "/work/proj", "C:\\work\\proj"}) {
    ErrorParser p(cwd);
    size_t b = 0;
    while (b <= plain.size()) {
      size_t e = plain.find('\n', b);
      if (e == std::string::npos) e = plain.size();
      std::string_view line(plain.data() + b, e - b);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      BuildMessage m;
      if (p.parseLine(line, &m)) {
        FUZZ_REQUIRE(m.line >= 0 && m.col >= 0);
        for (const BuildMessage& note : m.notes) FUZZ_REQUIRE(note.line >= 0);
      }
      b = e + 1;
    }
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// git / p4
// ---------------------------------------------------------------------------------------------------------------------

void runGitStatus(const uint8_t* d, size_t n) {
  const auto entries = GitProvider::parseStatusV2(str(d, n));
  for (const auto& e : entries) FUZZ_REQUIRE(e.origPath.size() <= n && e.path.size() <= n);
  for (int x = 0; x < 256; x += 17)
    for (int y = 0; y < 256; y += 13) GitProvider::statusOfXY(static_cast<char>(x), static_cast<char>(y));
}

void runP4Output(const uint8_t* d, size_t n) {
  const std::string text = str(d, n);
  const auto records = PerforceProvider::parseTagged(text);
  for (const auto& r : records)
    for (bool exists : {false, true})
      for (bool writable : {false, true}) PerforceProvider::classify(r, exists, writable);
  const auto revs = PerforceProvider::parseFilelog(text);
  FUZZ_REQUIRE(revs.size() <= n);
  PerforceProvider::escapePath(text);
  PerforceProvider::isBinaryType(text);
}

// ---------------------------------------------------------------------------------------------------------------------
// Line diff
// ---------------------------------------------------------------------------------------------------------------------

// Parts: 0 = the base text, 1 = the live text, 2 = options (first byte: maxEditDistance bucket).
void runLineDiff(const uint8_t* d, size_t n) {
  const std::vector<std::string> parts = splitParts(d, n, 3);
  const std::vector<std::string> base = splitLines(parts[0]);
  const std::vector<std::string> cur = splitLines(parts.size() > 1 ? parts[1] : "");
  FUZZ_REQUIRE(!base.empty() && !cur.empty());
  DiffOptions opt;
  // Small caps keep the harness fast (Myers costs O(cap^2); the default 3000 takes ~100 ms under ASan) and make the
  // fallback to one big hunk common.
  static const int kCaps[] = {1, 3, 8, 40, 300};
  opt.maxEditDistance =
      kCaps[(parts.size() > 2 && !parts[2].empty() ? static_cast<unsigned char>(parts[2][0]) : n) % 5];
  const std::vector<DiffHunk> hunks = diffLines(base, cur, opt);
  // Hunks are ordered, in range and never touch; applying them to the base must give the live text.
  int bPos = 0, cPos = 0;
  std::vector<std::string> rebuilt;
  for (const DiffHunk& h : hunks) {
    FUZZ_REQUIRE(h.baseStart >= bPos && h.newStart >= cPos);
    FUZZ_REQUIRE(h.baseCount >= 0 && h.newCount >= 0 && (h.baseCount > 0 || h.newCount > 0));
    FUZZ_REQUIRE(static_cast<size_t>(h.baseStart + h.baseCount) <= base.size());
    FUZZ_REQUIRE(static_cast<size_t>(h.newStart + h.newCount) <= cur.size());
    // the gap before the hunk is identical on both sides
    FUZZ_REQUIRE(h.baseStart - bPos == h.newStart - cPos);
    for (int i = 0; i < h.baseStart - bPos; ++i) {
      FUZZ_REQUIRE(base[static_cast<size_t>(bPos + i)] == cur[static_cast<size_t>(cPos + i)]);
      rebuilt.push_back(base[static_cast<size_t>(bPos + i)]);
    }
    for (int i = 0; i < h.newCount; ++i) rebuilt.push_back(cur[static_cast<size_t>(h.newStart + i)]);
    bPos = h.baseStart + h.baseCount;
    cPos = h.newStart + h.newCount;
    FUZZ_REQUIRE(h.kind == (h.baseCount == 0  ? HunkKind::Added
                            : h.newCount == 0 ? HunkKind::Deleted
                                              : HunkKind::Modified));
    hunkBaseLines(h, base);
    revertHunkEdit(h, base, cur);
  }
  FUZZ_REQUIRE(base.size() - static_cast<size_t>(bPos) == cur.size() - static_cast<size_t>(cPos));
  for (size_t i = static_cast<size_t>(cPos); i < cur.size(); ++i) {
    FUZZ_REQUIRE(base[static_cast<size_t>(bPos) + (i - static_cast<size_t>(cPos))] == cur[i]);
    rebuilt.push_back(cur[i]);
  }
  FUZZ_REQUIRE(rebuilt == cur);
  if (hunks.empty()) FUZZ_REQUIRE(base == cur);
  joinLines(cur, 0, cur.size());
}

// ---------------------------------------------------------------------------------------------------------------------
// Terminal
// ---------------------------------------------------------------------------------------------------------------------

// Byte 0: rows, 1: cols, 2: scrollback limit, 3: chunk size. In the stream 0xFF + 2 bytes resizes, 0xFE + 1 byte
// changes the scrollback.
void runVt(const uint8_t* d, size_t n) {
  if (n < 4) return;
  const int rows = 1 + d[0] % 40, cols = 1 + d[1] % 120;
  const size_t limit = d[2] % 64;
  const size_t chunk = 1 + d[3] % 40;
  VtScreen s(rows, cols, limit);
  size_t replyBytes = 0;
  s.setReplyCallback([&](const std::string& r) { replyBytes += r.size(); });
  size_t scrollbackLimit = limit;
  std::string pending;
  auto flush = [&] {
    for (size_t b = 0; b < pending.size(); b += chunk) s.feed(pending.data() + b, std::min(chunk, pending.size() - b));
    pending.clear();
  };
  for (size_t i = 4; i < n; ++i) {
    if (d[i] == 0xFF && i + 2 < n) {
      flush();
      s.resize(1 + d[i + 1] % 60, 1 + d[i + 2] % 150);
      i += 2;
    } else if (d[i] == 0xFE && i + 1 < n) {
      flush();
      if (d[i + 1] & 1) s.clearScrollback();
      else {
        scrollbackLimit = d[i + 1] % 64;
        s.setScrollbackLimit(scrollbackLimit);
      }
      ++i;
    } else {
      pending += static_cast<char>(d[i]);
    }
  }
  flush();
  FUZZ_REQUIRE(s.cursorRow() >= 0 && s.cursorRow() < s.rows());
  FUZZ_REQUIRE(s.cursorCol() >= 0 && s.cursorCol() < s.cols());
  FUZZ_REQUIRE(s.scrollTop() >= 0 && s.scrollBottom() < s.rows() && s.scrollTop() <= s.scrollBottom());
  FUZZ_REQUIRE(s.scrollbackSize() <= std::max<size_t>(scrollbackLimit, s.scrollbackLimit()));
  for (int r = 0; r < s.rows(); ++r) {
    FUZZ_REQUIRE(static_cast<int>(s.row(r).cells.size()) == s.cols());
    s.rowText(r);
    for (int c = 0; c < s.cols(); ++c) {
      const VtCell& k = s.cell(r, c);
      if (k.width == 2) {
        FUZZ_REQUIRE(c + 1 < s.cols());
        FUZZ_REQUIRE(s.cell(r, c + 1).width == 0);
      }
      if (k.width == 0) {
        FUZZ_REQUIRE(c > 0);
        FUZZ_REQUIRE(s.cell(r, c - 1).width == 2);
      }
    }
  }
  for (size_t i = 0; i < s.scrollbackSize(); ++i) s.scrollbackRow(i);
}

// ---------------------------------------------------------------------------------------------------------------------
// Find in files: query, options, globs, regex
// ---------------------------------------------------------------------------------------------------------------------

// Parts: 0 = query, 1 = options (byte 0 flags: 1 case, 2 whole word, 4 regex; the following lines are include globs,
// lines starting with '!' are exclude globs), 2 = the text searched.
void runSearch(const uint8_t* d, size_t n) {
  const std::vector<std::string> parts = splitParts(d, n, 3);
  SearchOptions opt;
  opt.query = parts[0];
  opt.threads = 1;
  opt.maxResults = 200;
  opt.batchSize = 16;
  if (parts.size() > 1 && !parts[1].empty()) {
    const unsigned f = static_cast<unsigned char>(parts[1][0]);
    opt.caseSensitive = f & 1;
    opt.wholeWord = f & 2;
    opt.regex = f & 4;
    const std::string& g = parts[1];
    for (size_t b = 1; b < g.size();) {
      size_t e = g.find('\n', b);
      if (e == std::string::npos) e = g.size();
      std::string line = g.substr(b, e - b);
      if (!line.empty() && line[0] == '!') opt.excludeGlobs.push_back(line.substr(1));
      else if (!line.empty()) opt.includeGlobs.push_back(line);
      b = e + 1;
    }
  }
  SearchInput in;
  in.root = "/w";
  in.files = {"/w/a.txt", "/w/src/b.cpp"};
  const std::string text = parts.size() > 2 ? parts[2] : std::string("hello world\nfoo bar\r\nbaz\n");
  in.overrides["a.txt"] = text;
  in.overrides["src/b.cpp"] = text;
  std::vector<SearchMatch> out;
  const SearchSummary sum = TextSearch::run(in, opt, &out);
  FUZZ_REQUIRE(out.size() <= opt.maxResults + opt.batchSize);
  for (const SearchMatch& m : out) FUZZ_REQUIRE(m.line >= 0 && m.column >= 0 && m.length >= 0 && m.textOffset >= 0);
  (void)sum;
  // Replace-in-files over the matches that were found (the replacement is the options text).
  ReplaceCounts counts;
  std::vector<SearchMatch> one;
  for (const SearchMatch& m : out)
    if (m.file == "/w/a.txt") one.push_back(m);
  const std::string replaced = replaceMatchesInText(text, one, parts.size() > 1 ? parts[1] : std::string(), &counts);
  FUZZ_REQUIRE(counts.replaced + counts.stale <= one.size());
  (void)replaced;
  // The glob matcher on its own: pattern = query, path = the first line of the text.
  const std::string path = text.substr(0, text.find('\n'));
  const bool globbed = TextSearch::globMatch(parts[0], path);
  // (the reference is exponential in the number of `*`: only small cases)
  if (parts[0].size() <= 12 && path.size() <= 16 && std::count(parts[0].begin(), parts[0].end(), '*') <= 3)
    FUZZ_REQUIRE(globbed == referenceGlob(parts[0].data(), parts[0].data() + parts[0].size(), path.data(),
                                          path.data() + path.size()));
  TextSearch::pathMatchesGlob(parts[0], path);
}

// ---------------------------------------------------------------------------------------------------------------------
// Persisted state files of a workspace (.tenfold/*.json)
// ---------------------------------------------------------------------------------------------------------------------

// Parts: 0 = workspace.json, 1 = breakpoints.json, 2 = bookmarks.json.
void runStateFiles(const uint8_t* d, size_t n) {
  const std::vector<std::string> parts = splitParts(d, n, 3);
  writeScratch("ws/.tenfold/workspace.json", parts[0]);
  writeScratch("ws/.tenfold/breakpoints.json", parts.size() > 1 ? parts[1] : "");
  writeScratch("ws/.tenfold/bookmarks.json", parts.size() > 2 ? parts[2] : "");
  const fs::path root = fs::path(scratchDir()) / "ws";
  std::string err;
  Workspace ws(root);
  ws.loadSettings(&err);
  ws.saveSettings(&err);
  Breakpoints bp;
  if (bp.load(root / ".tenfold" / "breakpoints.json", root, &err)) {
    bp.save(root / ".tenfold" / "breakpoints2.json", root, &err);
    Breakpoints again;
    FUZZ_REQUIRE(again.load(root / ".tenfold" / "breakpoints2.json", root, &err));
    FUZZ_REQUIRE(again.all().size() == bp.all().size());
  }
  Bookmarks bm;
  if (bm.load(root / ".tenfold" / "bookmarks.json", root, &err)) {
    bm.save(root / ".tenfold" / "bookmarks2.json", root, &err);
    Bookmarks again;
    FUZZ_REQUIRE(again.load(root / ".tenfold" / "bookmarks2.json", root, &err));
    FUZZ_REQUIRE(again.all().size() == bm.all().size());
  }
}

Registrar r1(
    {"lexer",
     runLexer,
     {"src/semantic:.cpp,.h", "examples:.cpp,.h,.c", "src/term:.cpp"},
     {"int main() { return 0; }\n", "#include <vector>\nnamespace a::b { template <class T> struct S { T v; }; }\n",
      "auto s = R\"x(raw\nstring)x\"; char c = u8'a'; int n = 1'000'000;\n",
      "#define M(a, ...) a##__VA_ARGS__ \\\n  continued\n", "/* unterminated", "\"unterminated\n", "'", "R\"("},
     kCppDict,
     4096});
Registrar r2(
    {"extract",
     runExtract,
     {"src/semantic:.cpp,.h", "src/app:.cpp,.h", "examples:.cpp,.h,.c,.hpp,.inl", "tests/corpus:.cpp,.h,.c,.hpp,.inl"},
     {"namespace n { class C : public B { public: C(); ~C(); int f() const; static int x; enum E { A, B }; }; }\n"
      "template <typename T> concept Big = sizeof(T) > 4;\n"
      "int C::f() const { return x; }\nvoid g(int (*p)(int), int a[3]);\nusing U = int; typedef int T;\n"
      "#define MAX(a, b) ((a) > (b) ? (a) : (b))\n#include \"x.h\"\n#include <y>\nextern \"C\" { void c(); }\n"
      "struct S { operator bool() const; S& operator=(const S&) = default; };\n"},
     kCppDict,
     4096});
Registrar r2b({"scopescan",
               runScopeScan,
               {"src/semantic:.cpp,.h", "src/app:.cpp,.h", "examples:.cpp,.h,.c,.hpp,.inl",
                "tests/corpus:.cpp,.h,.c,.hpp,.inl"},
               {"namespace n::m { class C : public B<int>, private ns::D { int x{1}; C() : x{2} {} int f(int a) const "
                "{ int l = a; for (auto i : v) { T t; } return l; } }; }\n"
                "void n::m::C::g(Foo* p, std::unique_ptr<Bar> q = {}) { auto s = new C(); s->f(1); }\n"
                "extern \"C\" { typedef struct { int x; } P; static int fn(void) { return 0; } }\n"
                "using namespace std; namespace fs = std::filesystem; auto lam = [&](int z) { return z; };\n"},
               kCppDict,
               4096});
Registrar r2c({"ppcond",
               runPpCond,
               {"src/semantic:.cpp,.h", "tests/corpus:.cpp,.h,.c,.hpp,.inl", "examples:.cpp,.h,.c"},
               {"#if defined(A) && B > 1 || (C ? 2 : 0)\nint x;\n#elif !defined X\nint y;\n#else\nint z;\n#endif\n",
                "#ifndef G_H\n#define G_H\n#define LVL (1 + 2)\n#if LVL >= 3\nint a;\n#endif\n#undef LVL\n#endif\n",
                "#if __has_include(<a/b.h>) && __cplusplus >= 201703L\n#endif\n",
                "defined(A) && (1 << 3) == 8 || 0x10 == 16u && -1 < 0", "'a' == 97 && 10UL > 9 && FN(1, 2) || !0",
                "#if 1\n#if 0\n#else\n#endif\n#endif\n#endif\n#else\n"},
               {"#if ",
                "#ifdef ",
                "#ifndef ",
                "#elif ",
                "#else",
                "#endif",
                "#define ",
                "#undef ",
                "defined",
                "defined(",
                "&&",
                "||",
                "!",
                "?",
                ":",
                "<<",
                ">>",
                "==",
                "!=",
                "0x",
                "1u",
                "'a'",
                "__has_include(",
                "__cplusplus",
                " \\\n",
                "/*",
                "*/",
                "//"},
               1024});
Registrar r3({"indexstore",
              runIndexStore,
              {},
              indexStoreSeeds(),
              {"TFFIDX", "\xff\xff\xff\xff", "\0\0\0\0"s, "\x01"s, "\x00\x00\x00\x80"s},
              512});
Registrar r4({"xml",
              runXml,
              {"tests/fixtures/vs:.vcxproj,.props,.filters,.xml", "assets:.xml"},
              {"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Project DefaultTargets=\"Build\" "
               "xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
               "  <ItemGroup><ClCompile Include=\"a.cpp\"><Filter>Src</Filter></ClCompile></ItemGroup>\n  <!-- c -->\n"
               "  <PropertyGroup "
               "Condition=\"'$(Configuration)'=='Debug'\"><X><![CDATA[a<b]]></X><Y>&lt;&amp;&#65;&#x42;</Y></"
               "PropertyGroup>\n</Project>\n"},
              {"<",
               ">",
               "</",
               "/>",
               "<?xml ",
               "?>",
               "<!--",
               "-->",
               "<![CDATA[",
               "]]>",
               "<!DOCTYPE ",
               "&amp;",
               "&lt;",
               "&#",
               "&#x",
               "&",
               ";",
               "=\"",
               "='",
               "\"",
               "'",
               "xmlns:",
               "a:b",
               "<Project>",
               "<ItemGroup>",
               "</ItemGroup>",
               "<ClCompile Include=\"",
               "Condition=\"",
               "\xEF\xBB\xBF",
               "\xFF\xFE"},
              2048});
Registrar r5(
    {"msbuild",
     runMsbuild,
     {},
     concat(msbuildFixtureSeeds(),
            {"<Project>\n<Import Project=\"..\\common.props\"/>\n<PropertyGroup "
             "Label=\"Globals\"><ProjectGuid>{11111111-2222-3333-4444-555555555555}</ProjectGuid></PropertyGroup>\n"
             "<ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>FOO;BAR=1;%(PreprocessorDefinitions)</"
             "PreprocessorDefinitions>"
             "<AdditionalIncludeDirectories>$(SolutionDir)inc;..\\x;%(AdditionalIncludeDirectories)</"
             "AdditionalIncludeDirectories>"
             "<AdditionalOptions>/std:c++20 /DX=\"a b\" "
             "%(AdditionalOptions)</AdditionalOptions></ClCompile></ItemDefinitionGroup>\n"
             "<ItemGroup><ClCompile Include=\"a.cpp;b\\*.cpp\"/><ClCompile Include=\"c.cpp\" "
             "Condition=\"'$(Platform)'=='x64' and "
             "Exists('c.cpp')\"><PreprocessorDefinitions>C</PreprocessorDefinitions></ClCompile></ItemGroup>\n"
             "<Import Project=\"$(MSBuildThisFileDirectory)..\\Directory.Build.props\" "
             "Condition=\"Exists('$(MSBuildThisFileDirectory)..\\Directory.Build.props')\"/>\n</Project>\n"
             "\n#--#\n<Project><PropertyGroup><Common>1</"
             "Common><IncludePath>$([MSBuild]::EnsureTrailingSlash('$(SolutionDir)'))inc;$(IncludePath)</IncludePath></"
             "PropertyGroup></Project>\n"
             "\n#--#\n<Project><PropertyGroup><DbpProp>yes</DbpProp></PropertyGroup></Project>\n"
             "\n#--#\nMicrosoft Visual Studio Solution File, Format Version 12.00\n"
             "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"App\", \"App\\App.vcxproj\", "
             "\"{11111111-2222-3333-4444-555555555555}\"\nEndProject\n"
             "Global\n\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n\t\tDebug|x64 = "
             "Debug|x64\n\t\tRelease|Win32 = Release|Win32\n\tEndGlobalSection\n"
             "\tGlobalSection(ProjectConfigurationPlatforms) = "
             "postSolution\n\t\t{11111111-2222-3333-4444-555555555555}.Debug|x64.ActiveCfg = Debug|x64\n"
             "\t\t{11111111-2222-3333-4444-555555555555}.Debug|x64.Build.0 = Debug|x64\n\tEndGlobalSection\nEndGlobal\n"
             "\n#--#\n<Project><ItemGroup><ClCompile Include=\"a.cpp\"><Filter>Source "
             "Files</Filter></ClCompile></ItemGroup></Project>\n"}),
     {"<Import Project=\"",
      "<ImportGroup Label=\"PropertySheets\">",
      "</ImportGroup>",
      "<Choose>",
      "<When Condition=\"",
      "<Otherwise>",
      "<Target Name=\"",
      "<ItemGroup>",
      "<PropertyGroup Condition=\"",
      "<ItemDefinitionGroup>",
      "<ClCompile Include=\"",
      "<ClInclude Include=\"",
      "Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\"",
      "Exists('",
      "HasTrailingSlash('",
      "!Exists(",
      " and ",
      " or ",
      "==",
      "!=",
      "$(",
      ")",
      "%(",
      "$([MSBuild]::EnsureTrailingSlash(",
      "$([MSBuild]::NormalizeDirectory(",
      "$([MSBuild]::GetDirectoryNameOfFileAbove(",
      "$([System.IO.Path]::Combine(",
      "$(Registry:HKEY_LOCAL_MACHINE\\",
      "$([MSBuild]::Add(1,2))",
      "$(Name.Replace('a','b'))",
      "$(SolutionDir)",
      "$(MSBuildThisFileDirectory)",
      "$(MSBuildProjectDirectory)",
      "$(Configuration)",
      "$(Platform)",
      "$(VCInstallDir)",
      "$(IncludePath)",
      "$(FuzzEnv)",
      "$(FuzzSelf)",
      "<FuzzSelf>$(FuzzSelf)x</FuzzSelf>",
      "..\\",
      "..\\..\\",
      "*.cpp",
      "**\\*.cpp",
      ";",
      "\\",
      "/",
      "%3B",
      "%24",
      "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = ",
      "EndProject",
      "GlobalSection(",
      "EndGlobalSection",
      ".ActiveCfg = ",
      ".Build.0 = ",
      "|",
      "Any CPU",
      "SolutionConfigurationPlatforms",
      "ProjectConfigurationPlatforms",
      "NestedProjects",
      "\n#--#\n"},
     3072});
Registrar r6({"msbuildexpr",
              runMsbuildExpr,
              {},
              {"'$(Configuration)'=='Debug' and "
               "Exists('x')\n#--#\n$(Platform)/"
               "$(Configuration)|$([MSBuild]::EnsureTrailingSlash('$(Dir)'))\n#--#\nDir=C:\\a\nName=value",
               "!('$(A)'=='') or ($(B) &gt;= 3)", "HasTrailingSlash('$(Dir)') And '$(x)' != 'y'"},
              {"(",
               ")",
               "'",
               "\"",
               "==",
               "!=",
               "<",
               ">",
               "<=",
               ">=",
               " and ",
               " or ",
               " AND ",
               " OR ",
               "!",
               "Exists(",
               "HasTrailingSlash(",
               "$(",
               "$([",
               "])",
               "::",
               "[MSBuild]::",
               "[System.",
               ".Replace(",
               ".Contains(",
               ".Substring(",
               "EnsureTrailingSlash(",
               "NormalizeDirectory(",
               "GetDirectoryNameOfFileAbove(",
               "Registry:",
               "@(",
               "%(",
               "$$",
               "true",
               "false",
               "0",
               "1",
               "\\",
               ",",
               "\n#--#\n",
               "Name=",
               "\n"},
              512});
Registrar r7({"compdb",
              runCompdb,
              {},
              {"[{\"directory\":\"/w\",\"file\":\"a.cpp\",\"arguments\":[\"clang++\",\"-c\",\"a.cpp\"]},"
               "{\"directory\":\"/w\",\"file\":\"/abs/b.cpp\",\"command\":\"g++ -DX=\\\"a b\\\" -I'/x y' -c b.cpp\"}]",
               "[]", "{}", "[1,2,3]", "[{}]", "[null]", "[{\"file\":1}]", "[{\"arguments\":5}]", "[{\"command\":[]}]"},
              {"[",
               "]",
               "{",
               "}",
               ":",
               ",",
               "\"directory\"",
               "\"file\"",
               "\"command\"",
               "\"arguments\"",
               "\"output\"",
               "null",
               "true",
               "false",
               "-0",
               "1e999",
               "\"\\u0000\"",
               "\"\\ud800\"",
               "\\\"",
               "\\\\",
               "\"",
               "/",
               "\xFF",
               "\xC0\x80",
               "12345678901234567890",
               "\"\"",
               "\"a b\"",
               "\"-I\"",
               "\"@rsp\""},
              1024});
Registrar r8({"compdbcmd",
              runCompdbCommand,
              {},
              {"clang++ -std=c++20 -DNAME=\"a b\" -I'dir with space' -c a.cpp -o a.o",
               "cl.exe /nologo /Fo\"x y\\\\\" a.cpp", "a\\ b \"c\\\"d\" 'e f' \"\"", "\\", "\"", "'"},
              {"\"", "'", "\\", "\\\"", " ", "\t", "\n", "-I", "-D", "-include", "@", "\\\\", "\xFF"},
              256});
Registrar r9({"buildoutput",
              runBuildOutput,
              {},
              {"a.cpp:12:5: error: use of undeclared identifier 'x' [-Wundeclared]\n  12 |     x = 1;\n     |     ^\n",
               "C:\\proj\\a.cpp(34,7): error C2065: 'y': undeclared identifier [C:\\proj\\p.vcxproj]\n",
               "C:\\proj\\a.cpp(34): warning C4100: 'z': unreferenced formal parameter\n",
               "In file included from a.h:3:\n                 from b.cpp:1:\nb.h:9:1: note: declared here\n",
               "LINK : fatal error LNK1104: cannot open file 'x.lib'\nmake: *** [all] Error 2\n",
               "NMAKE : fatal error U1077: 'cl' : return code '0x2'\n",
               "\x1b[01m\x1b[Ka.cpp:1:1:\x1b[m\x1b[K \x1b[01;31merror: \x1b[m\x1b[Kx\n",
               "1>------ Build started: Project: p, Configuration: Debug x64 ------\n1>a.cpp\n",
               "ld: error: undefined symbol: foo\n>>> referenced by a.o:(main)\n"},
              {"error",
               "warning",
               "note",
               "fatal error",
               ": ",
               ":",
               "(",
               ")",
               ",",
               "[",
               "]",
               "C2065",
               "LNK1104",
               "-Wunused",
               "In file included from ",
               "                 from ",
               "make: ***",
               "Error 2",
               "\x1b[",
               "\x1b[01m",
               "\x1b]8;;",
               "\x1b\\",
               "C:\\",
               "C:/",
               "/usr/include/",
               "\\\\server\\share\\",
               "..\\",
               "1>",
               "2>",
               "MSBuild",
               "\r",
               "\n",
               "\t",
               "0",
               "99999999999",
               "-1",
               " : ",
               "error C",
               "warning C",
               "Build FAILED.",
               "NMAKE",
               "clang: error:",
               "fatal: "},
              1024});
Registrar r10({"gitstatus",
               runGitStatus,
               {},
               {"# branch.oid abc\0# branch.head main\0"s + "1 .M N... 100644 100644 100644 aaa bbb src/a.cpp\0"s +
                    "2 R. N... 100644 100644 100644 aaa bbb R100 new.cpp\0old.cpp\0"s +
                    "? untracked.txt\0! ignored\0u UU N... 1 2 3 4 aaa bbb ccc conflict.c\0"s,
                "1 .M N... 100644 100644 100644 aaa bbb a b\n", "? x\n"},
               {"\0"s,   "1 ",   "2 ", "u ", "? ", "! ", "# ", "# branch.", " N... ", "100644 ",
                "R100 ", "C75 ", ".M", "M.", "MM", "A.", "D.", "??",        "UU",     "AA",
                "DD",    "R.",   ".R", "T.", "\n", "\\", "/",  "..",        "\xFF"},
               256});
Registrar r11({"p4output",
               runP4Output,
               {},
               {"... depotFile //depot/a.txt\n... clientFile /work/a.txt\n... isMapped \n... headAction edit\n... "
                "headType text\n... haveRev 3\n... headRev 4\n... action edit\n\n"
                "... depotFile //depot/b.txt\n... movedFile //depot/c.txt\n... ourLock \n\n",
                "... depotFile //depot/a.txt\n... rev0 3\n... change0 100\n... action0 edit\n... time0 1700000000\n... "
                "user0 u\n... client0 c\n... desc0 fix\n... type0 text\n"
                "... rev1 2\n... change1 90\n... action1 add\n",
                "... code error\n... data No such file(s).\n", "... "},
               {"... ",
                "...",
                " ",
                "depotFile ",
                "clientFile ",
                "headAction ",
                "headType ",
                "haveRev ",
                "headRev ",
                "action ",
                "rev0 ",
                "change0 ",
                "time0 ",
                "user0 ",
                "client0 ",
                "desc0 ",
                "type0 ",
                "rev1 ",
                "change1 ",
                "movedFile ",
                "unresolved ",
                "ourLock ",
                "otherOpen ",
                "binary",
                "text+x",
                "apple",
                "ktext",
                "delete",
                "move/delete",
                "edit",
                "add",
                "\n",
                "\n\n",
                "\r\n",
                "0",
                "-1",
                "99999999999999999999",
                "#",
                "@",
                "%",
                "*"},
               512});
Registrar r12({"linediff",
               runLineDiff,
               {"src/vcs:.cpp"},
               {"a\nb\nc\n\n#--#\na\nc\nd\n\n#--#\n\x08", "x\n\n#--#\n\n", "\n#--#\n\n#--#\n\x01",
                "a\r\nb\r\n\n#--#\na\nb\n\n#--#\n"},
               {"\n", "\r\n", "\r", "\n\n\n", "a\n", "b\n", "a\nb\nc\nd\ne\n", "\n#--#\n"},
               2048});
Registrar r12b({"linemap",
                runLineMap,
                {"src/core:.cpp"},
                {"a\xE2\x80\xA8"
                 "b\xE2\x80\xA9"
                 "c\xC2\x85"
                 "d\ne\r\nf\rg",
                 "// c\xE2\x80\xA8int x;\xC2\x85\n/* \xF0\x9F\x98\x80\xE2\x80\xA9 */\n", "\xE2\x80\xA8\xE2\x80\xA8",
                 "\xC2", "\xE2\x80", "x\xC2\x85", "\r\n\xE2\x80\xA9\r"},
                {"\n", "\r\n", "\r", "\xC2\x85", "\xE2\x80\xA8", "\xE2\x80\xA9", "\xE2\x80", "\xC2", "\xC3\xA9",
                 "\xF0\x9F\x98\x80"},
                1024});
Registrar r13({"vtscreen",
               runVt,
               {},
               {"\x08\x20\x10\x07"s + "hello\r\nworld\x1b[1;32mgreen\x1b[0m\x1b[2J\x1b[H\xe4\xb8\xad\xe6\x96\x87",
                "\x05\x0a\x05\x03"s + "\x1b[?1049h\x1b[3;3Halt\x1b[?1049l\x1b]0;title\x07\x1b[6n\x1b[2;"
                                      "4r\r\n\r\n\r\n\r\n\x1b[3L\x1b[2M\x1b[4@\x1b[2P",
                "\x03\x04\x02\x01"s + "\x1b(0lqk\x1b(B\x1b[38;2;1;2;3m\x1b[48;5;200m\x1b[4:3m\xff\x04\x05 "
                                      "\xfe\x01x\x1b[999999999;999999999H\x1b[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;"
                                      "17;18;19;20;21;22;23;24;25;26;27;28;29;30;31;32;33;34;35m"},
               {"\x1b[",
                "\x1b]",
                "\x1bP",
                "\x1b_",
                "\x1b^",
                "\x1bX",
                "\x1b(",
                "\x1b)",
                "\x1b#8",
                "\x1b"
                "7",
                "\x1b"
                "8",
                "\x1b"
                "c",
                "\x1b"
                "M",
                "\x1b"
                "D",
                "\x1b"
                "E",
                "\x1b"
                "H",
                "\x1b=",
                "\x1b>",
                "\x1b\\",
                "\x07",
                "\x18",
                "\x1a",
                "\x9b",
                "\x9d",
                "\x90",
                "\x9c",
                "?",
                ";",
                ":",
                "!",
                " ",
                "\"",
                "$",
                "'",
                "0",
                "1",
                "2",
                "3",
                "4",
                "5",
                "6",
                "7",
                "8",
                "9",
                "10",
                "38;2;255;255;255",
                "48;5;255",
                "1049",
                "47",
                "1047",
                "1048",
                "1000",
                "1002",
                "1003",
                "1006",
                "2004",
                "1004",
                "25",
                "7",
                "4",
                "20",
                "999999999",
                "h",
                "l",
                "m",
                "H",
                "f",
                "A",
                "B",
                "C",
                "D",
                "E",
                "F",
                "G",
                "d",
                "J",
                "K",
                "L",
                "M",
                "P",
                "S",
                "T",
                "X",
                "@",
                "r",
                "s",
                "u",
                "n",
                "c",
                "t",
                "p",
                "q",
                "b",
                "g",
                "I",
                "Z",
                "`",
                "e",
                "a",
                "\r",
                "\n",
                "\t",
                "\b",
                "\x0b",
                "\x0c",
                "\x0e",
                "\x0f",
                "\xe4\xb8\xad",
                "\xf0\x9f\x98\x80",
                "\xcc\x81",
                "\xe2\x80\x8d",
                "\xed\xa0\x80",
                "\xf4\x90\x80\x80",
                "\xc0\xaf",
                "\xff\x01\x01",
                "\xfe\x00",
                "\xfe\x01"},
               2048});
Registrar r14({"search",
               runSearch,
               {},
               {"hello\n#--#\n\x01\n*.cpp\n!third_party\n#--#\nhello world\nHello again\n",
                "h[a-z]+o\n#--#\x05\n#--#\nhello hello\n", "(?i)w\\w+\n#--#\x06\n#--#\nWorld\nword\n",
                "**/*.cpp\n#--#\n\n#--#\nsrc/a/b.cpp\n", "a?c\n#--#\n\n#--#\nabc\n"},
               {"(",
                ")",
                "[",
                "]",
                "{",
                "}",
                "(?",
                "(?:",
                "(?=",
                "(?!",
                "(?<=",
                "(?<!",
                "(?i)",
                "(?s)",
                "(?m)",
                "(?x)",
                "(?#",
                "(?P<n>",
                "(?<n>",
                "\\k<n>",
                "\\1",
                "\\b",
                "\\B",
                "\\w+",
                "\\d{2,}",
                "\\p{L}",
                "\\X",
                "\\R",
                "\\K",
                "\\G",
                "\\Q",
                "\\E",
                "\\x{",
                "\\N{",
                "\\",
                "*",
                "+",
                "?",
                "*?",
                "+?",
                "{1,}",
                "{0,99999}",
                "{99999999999}",
                "|",
                "^",
                "$",
                ".",
                ".*",
                "(.*)*",
                "(a+)+",
                "(a|aa)+",
                "[^",
                "[[:alpha:]]",
                "[a-",
                "\\u",
                "\\U",
                "\\c",
                "(*",
                "(*UTF8)",
                "(*FAIL)",
                "(*ACCEPT)",
                "(?R)",
                "(?1)",
                "(?&n)",
                "(?(1)",
                "**",
                "**/",
                "/**",
                "*.",
                "?",
                "!",
                "\n",
                "\r\n",
                "\n#--#\n",
                "\x01",
                "\x02",
                "\x04",
                "\x05",
                "\x07",
                "\xe4\xb8\xad",
                "\xff",
                "\xc0\x80"},
               512});

Registrar r15(
    {"statefiles",
     runStateFiles,
     {},
     {"{\"cppStandard\":\"c++20\",\"sourceExtensions\":[\".cpp\"],\"ignoreDirs\":[\"x\"],\"undoGroupPauseMs\":500,"
      "\"vcs\":{\"provider\":\"git\"},\"debug.attachAtStartup\":true}"
      "\n#--#\n{\"version\":1,\"breakpoints\":{\"a.cpp\":[{\"line\":3,\"enabled\":false,\"condition\":\"i>2\"}]}}"
      "\n#--#\n{\"version\":1,\"bookmarks\":{\"a.cpp\":[1,5,9]}}",
      "{\"sourceExtensions\":[1,2],\"ignoreDirs\":[null],\"cppStandard\":5,\"runCMake\":\"x\",\"undoGroupPauseMs\":"
      "1e30}\n#--#\n{\"breakpoints\":{\"a\":5,\"b\":[1,null,{\"line\":\"x\"}]}}\n#--#\n{\"bookmarks\":{\"a\":[1.5,-1,"
      "\"s\"]}}"},
     {"{",
      "}",
      "[",
      "]",
      ":",
      ",",
      "\"",
      "\"line\"",
      "\"enabled\"",
      "\"condition\"",
      "\"breakpoints\"",
      "\"bookmarks\"",
      "\"version\"",
      "\"sourceExtensions\"",
      "\"ignoreDirs\"",
      "\"ignoreDirNames\"",
      "\"ignoreDirPrefixes\"",
      "\"clangdExtraArgs\"",
      "\"cppStandard\"",
      "\"undoGrouping\"",
      "\"undoGroupPauseMs\"",
      "\"vcs\"",
      "\"debug.attachAtStartup\"",
      "null",
      "true",
      "false",
      "-1",
      "2147483648",
      "1e999",
      "99999999999999999999",
      "\\u0000",
      "\n#--#\n",
      "\xff",
      "\xc0\x80",
      "\"/abs\"",
      "\"..\\\\x\""},
     768});

}  // namespace
}  // namespace tf::fuzz
