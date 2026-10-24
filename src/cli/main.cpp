// tenfold-cli: a scriptable, headless front end for the editor model.
// It exists to exercise and demonstrate the engine (navigation history,
// clangd-backed semantics, fast index) without a GUI, and to serve as the
// reference for how a graphical front end drives tf::Editor.
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "semantic/CompileCommands.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

void usage() {
  std::cerr <<
      "usage: tenfold-cli <workspace-root> [options]\n"
      "       tenfold-cli compdb <file.sln|file.vcxproj> [--config Debug] [--platform x64] [-o out.json]\n"
      "options:\n"
      "  --clangd <path>     clangd executable (default: from PATH)\n"
      "  --no-clangd         fast index only (approximate results)\n"
      "  --budget <ms>       how long to wait for clangd before showing approximate results (250)\n"
      "  --trace             log every LSP message to stderr\n"
      "  --quiet             suppress engine log\n"
      "commands (stdin, one per line):\n"
      "  open <file>[:line[:col]]   open a file and place the caret (1-based)\n"
      "  goto <line> [col]          move caret (ordinary caret motion)\n"
      "  def | decl | refs          go to definition / declaration / list references\n"
      "  sym <query>                workspace symbol search (fuzzy)\n"
      "  grep <text>                literal text search in the workspace (first 50 hits)\n"
      "  hover | complete           hover text / completion list at caret\n"
      "  switch                     switch between source and header\n"
      "  back | fwd                 navigate backward / forward\n"
      "  history                    print the navigation stack\n"
      "  insert <text>              insert text at caret (\\n for newline)\n"
      "  wait-index [ms]            wait for clangd background index\n"
      "  wait-file [ms]             wait for clangd to finish parsing the current file\n"
      "  wait-fast [ms]             wait for the fast index\n"
      "  status | where | diags | quit\n";
}

std::string unescape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char c = s[++i];
      out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
    } else out += s[i];
  }
  return out;
}

void printLocs(const std::vector<SymbolLocation>& locs, Confidence c) {
  std::cout << "  [" << toString(c) << "] " << locs.size() << " result(s)\n";
  for (auto& l : locs) {
    std::cout << "    " << toString(l.loc);
    if (!l.kind.empty()) std::cout << "  " << l.kind;
    if (!l.name.empty()) std::cout << " " << (l.container.empty() ? l.name : l.container + "::" + l.name);
    std::cout << "\n";
  }
}

int runCompdb(int argc, char** argv) {
  if (argc < 3) { usage(); return 2; }
  fs::path in = argv[2];
  compdb::VcxprojOptions vo;
  fs::path out;
  for (int i = 3; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--config" && i + 1 < argc) vo.configuration = argv[++i];
    else if (a == "--platform" && i + 1 < argc) vo.platform = argv[++i];
    else if (a == "-o" && i + 1 < argc) out = argv[++i];
  }
  std::string err;
  std::vector<CompileCommand> cmds;
  if (in.extension() == ".sln") cmds = compdb::fromSolution(in, vo, &err);
  else cmds = compdb::fromVcxproj(in, vo, &err);
  if (!err.empty()) std::cerr << err;
  if (out.empty()) out = in.parent_path() / "compile_commands.json";
  if (!CompileDatabase::save(cmds, out, &err)) { std::cerr << err << "\n"; return 1; }
  std::cout << "wrote " << cmds.size() << " commands to " << out << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 2; }
  if (std::strcmp(argv[1], "compdb") == 0) return runCompdb(argc, argv);

  Editor::Options opt;
  bool quiet = false;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--clangd" && i + 1 < argc) opt.semantic.clangdPath = argv[++i];
    else if (a == "--no-clangd") opt.semantic.useClangd = false;
    else if (a == "--budget" && i + 1 < argc) opt.semantic.clangdBudgetMs = std::stoi(argv[++i]);
    else if (a == "--trace") opt.semantic.traceLsp = true;
    else if (a == "--quiet") quiet = true;
    else { usage(); return 2; }
  }
  if (!quiet) opt.log = [](const std::string& m) { std::cerr << "[tenfold] " << m << "\n"; };

  Editor ed(argv[1], opt);
  auto t0 = std::chrono::steady_clock::now();
  std::string err;
  bool started = ed.start(&err);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  std::cout << "workspace " << ed.workspace().root() << ": " << ed.workspace().sourceFiles().size() << " sources, "
            << ed.workspace().headerFiles().size() << " headers (" << ms << " ms)\n";
  if (!started) std::cout << "warning: " << err << " (continuing with approximate results)\n";

  auto printCursor = [&] {
    if (ed.currentDocument()) std::cout << "  at " << toString(ed.cursor()) << "\n";
    else std::cout << "  (no document)\n";
  };

  // Each semantic command may produce two callbacks (approximate, then
  // authoritative). We wait for the authoritative one unless clangd is off.
  auto runNav = [&](auto issue) {
    bool gotFinal = false, gotAny = false;
    issue([&](Confidence c, const std::vector<SymbolLocation>& locs, const std::string& e) {
      gotAny = true;
      if (!e.empty()) std::cout << "  error: " << e << "\n";
      printLocs(locs, c);
      if (c == Confidence::Authoritative || !ed.semantic().clangd()) gotFinal = true;
    });
    ed.waitFor([&] { return gotFinal; }, 30000);
    if (!gotAny) std::cout << "  (timed out)\n";
    printCursor();
  };

  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if (cmd.empty() || cmd[0] == '#') continue;
    std::cout << "> " << line << "\n";
    try {
      if (cmd == "quit" || cmd == "exit") break;
      if (cmd == "open") {
        std::string spec;
        ss >> spec;
        int l = 1, c = 1;
        // file[:line[:col]]
        size_t p1 = spec.rfind(':');
        if (p1 != std::string::npos && p1 > 0 && std::isdigit(static_cast<unsigned char>(spec[p1 + 1 < spec.size() ? p1 + 1 : p1]))) {
          size_t p0 = spec.rfind(':', p1 - 1);
          if (p0 != std::string::npos && p0 > 0 && std::isdigit(static_cast<unsigned char>(spec[p0 + 1]))) {
            l = std::stoi(spec.substr(p0 + 1, p1 - p0 - 1));
            c = std::stoi(spec.substr(p1 + 1));
            spec = spec.substr(0, p0);
          } else {
            l = std::stoi(spec.substr(p1 + 1));
            spec = spec.substr(0, p1);
          }
        }
        std::string e;
        if (!ed.gotoLocation({ed.workspace().root() / spec, {l - 1, c - 1}}, &e)) std::cout << "  error: " << e << "\n";
        printCursor();
      } else if (cmd == "goto") {
        int l = 1, c = 1;
        ss >> l >> c;
        if (c < 1) c = 1;
        ed.moveCursor({l - 1, c - 1});
        printCursor();
      } else if (cmd == "def") {
        runNav([&](auto cb) { ed.gotoDefinition(cb); });
      } else if (cmd == "decl") {
        runNav([&](auto cb) { ed.gotoDeclaration(cb); });
      } else if (cmd == "refs") {
        runNav([&](auto cb) { ed.findReferences(cb); });
      } else if (cmd == "sym") {
        std::string q;
        std::getline(ss, q);
        q.erase(0, q.find_first_not_of(' '));
        runNav([&](auto cb) { ed.findSymbols(q, cb); });
      } else if (cmd == "grep") {
        std::string q;
        std::getline(ss, q);
        q.erase(0, q.find_first_not_of(' '));
        SearchOptions so;
        so.maxResults = 50;
        bool done = false;
        ed.findInWorkspace(q, so, [](const std::vector<SearchMatch>& ms) {
          for (auto& m : ms) std::cout << m.file.string() << ":" << m.line + 1 << ":" << m.column + 1 << ": " << m.text << "\n";
        }, [&](const SearchSummary& s) {
          std::cout << s.matches << " matches" << (s.truncated ? " (truncated)" : "") << " in " << s.elapsedMs << " ms"
                    << (s.error.empty() ? "" : " error: " + s.error) << "\n";
          done = true;
        });
        ed.waitFor([&] { return done; }, 60000);
      } else if (cmd == "hover") {
        bool done = false;
        ed.hover([&](const std::string& t, const std::string& e) {
          if (!e.empty()) std::cout << "  error: " << e << "\n";
          std::istringstream ts(t);
          std::string tl;
          while (std::getline(ts, tl)) std::cout << "  | " << tl << "\n";
          done = true;
        });
        ed.waitFor([&] { return done; }, 30000);
      } else if (cmd == "complete") {
        bool done = false;
        ed.complete([&](const std::vector<LspCompletionItem>& items, const std::string& e) {
          if (!e.empty()) std::cout << "  error: " << e << "\n";
          std::cout << "  " << items.size() << " completion(s)\n";
          for (size_t i = 0; i < items.size() && i < 15; ++i) std::cout << "    " << items[i].label << "  " << items[i].detail << "\n";
          done = true;
        });
        ed.waitFor([&] { return done; }, 30000);
      } else if (cmd == "switch") {
        bool done = false;
        ed.switchSourceHeader([&](bool ok, const std::string& e) {
          if (!ok) std::cout << "  error: " << e << "\n";
          done = true;
        });
        ed.waitFor([&] { return done; }, 30000);
        printCursor();
      } else if (cmd == "back") {
        std::cout << (ed.back() ? "  ok" : "  (nothing to go back to)") << "\n";
        printCursor();
      } else if (cmd == "fwd") {
        std::cout << (ed.forward() ? "  ok" : "  (nothing to go forward to)") << "\n";
        printCursor();
      } else if (cmd == "history") {
        auto& nav = ed.navigation();
        size_t i = 0;
        for (auto& e : nav.entries()) {
          std::cout << (i == nav.cursor() ? "  * " : "    ") << i << ": " << toString(e.loc)
                    << (e.kind == NavigationHistory::Kind::Jump ? " (jump" : " (caret") << (e.label.empty() ? "" : " " + e.label) << ")\n";
          ++i;
        }
        if (nav.cursor() == nav.entries().size()) std::cout << "  * (live) " << toString(ed.cursor()) << "\n";
      } else if (cmd == "insert") {
        std::string t;
        std::getline(ss, t);
        t.erase(0, t.find_first_not_of(' '));
        ed.insertText(unescape(t));
        printCursor();
      } else if (cmd == "wait-index") {
        int ms2 = 60000; ss >> ms2;
        std::cout << (ed.semantic().waitForClangdIndex(ms2) ? "  index ready" : "  timeout") << "\n";
      } else if (cmd == "wait-file") {
        int ms2 = 60000; ss >> ms2;
        if (ed.currentDocument()) std::cout << (ed.semantic().waitForFile(ed.currentDocument()->path(), ms2) ? "  file ready" : "  timeout") << "\n";
      } else if (cmd == "wait-fast") {
        int ms2 = 60000; ss >> ms2;
        std::cout << (ed.semantic().waitForFastIndex(ms2) ? "  fast index ready" : "  timeout") << "\n";
      } else if (cmd == "status") {
        auto s = ed.semantic().status();
        std::cout << "  clangd: " << (s.clangdRunning ? "running" : "not running") << " " << s.clangdVersion
                  << " encoding=" << s.offsetEncoding << " index=" << (s.clangdIndex.active ? "indexing " : "")
                  << s.clangdIndex.percentage << "%\n"
                  << "  fast index: " << (s.fastIndexDone ? "done" : "running") << " " << s.fastFiles << " files, "
                  << s.fastSymbols << " symbols" << (s.fastIndexDone ? " in " + std::to_string(s.fastSeconds) + "s" : "") << "\n"
                  << "  compile db: " << s.compdbSource << " " << s.compdbEntries << " entries (" << s.compdbPath << ")\n";
      } else if (cmd == "where") {
        printCursor();
      } else if (cmd == "diags") {
        ed.pump();
        if (ed.currentDocument()) {
          for (auto& d : ed.diagnostics(ed.currentDocument()->path()))
            std::cout << "  " << toString(d.range.start) << " [" << d.severity << "] " << d.message << "\n";
        }
      } else {
        std::cout << "  unknown command\n";
      }
    } catch (const std::exception& e) {
      std::cout << "  error: " << e.what() << "\n";
    }
  }
  ed.shutdown();
  return 0;
}
