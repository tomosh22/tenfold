#pragma once
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/Document.h"
#include "nav/NavigationHistory.h"
#include "semantic/SemanticEngine.h"
#include "workspace/Workspace.h"

namespace tf {

// The UI-independent editor model: workspace + open documents + a caret +
// navigation history + semantic engine, with a main-thread work queue so
// background results are handed to the UI on its own thread.
//
// A front end (GPU/native window, TUI, test harness) drives this class and
// renders its state. Multi-caret, selections and rendering state belong to the
// front end's view layer; this model owns everything that must survive
// across views.
class Editor {
 public:
  struct Options {
    SemanticEngine::Options semantic;
    NavigationHistory::Options navigation;
    std::function<void(const std::string&)> log;
  };

  explicit Editor(std::filesystem::path root);
  Editor(std::filesystem::path root, Options opt);
  ~Editor();

  bool start(std::string* error = nullptr);  // scan workspace, start semantic engine
  void shutdown();

  Workspace& workspace() { return ws_; }
  SemanticEngine& semantic() { return *semantic_; }
  NavigationHistory& navigation() { return nav_; }

  // --- documents ---
  Document* open(const std::filesystem::path& path, std::string* error = nullptr);
  Document* document(const std::filesystem::path& path);
  std::vector<Document*> documents();
  bool close(const std::filesystem::path& path);
  bool save(Document& doc, std::string* error = nullptr);

  // --- caret & navigation ---
  Location cursor() const { return cursor_; }
  Document* currentDocument() { return current_; }
  // Ordinary caret motion within the current document (arrow keys, mouse).
  void moveCursor(Position p);
  // Jump somewhere (opens the file if needed) and record it in history.
  bool gotoLocation(const Location& loc, std::string* error = nullptr, const std::string& label = {});
  bool back();
  bool forward();

  // --- semantic commands (results arrive via post(), i.e. on the pump thread) ---
  // If the result is a single unambiguous location the editor jumps there and
  // then calls `done`. With several candidates it does not jump; the UI picks.
  using NavResult = std::function<void(Confidence, const std::vector<SymbolLocation>&, const std::string& error)>;
  void gotoDefinition(NavResult done);
  void gotoDeclaration(NavResult done);
  void findReferences(NavResult done);
  void findSymbols(const std::string& query, NavResult done);
  void hover(std::function<void(const std::string&, const std::string& error)> done);
  void complete(std::function<void(const std::vector<LspCompletionItem>&, const std::string& error)> done);
  void switchSourceHeader(std::function<void(bool jumped, const std::string& error)> done);

  // --- editing at the caret ---
  void insertText(const std::string& text);
  void deleteRange(Range r);
  void replaceRange(Range r, const std::string& text);

  // --- main-thread queue ---
  void post(std::function<void()> fn);
  size_t pump();                                                       // run queued callbacks; returns count
  bool waitFor(std::function<bool()> pred, int timeoutMs);             // pump until pred() or timeout

  const std::vector<Diagnostic>& diagnostics(const std::filesystem::path& file) const;

 private:
  void setCurrent(Document* d, Position p);
  void autoJump(Confidence c, std::vector<SymbolLocation> locs, const std::string& err, NavResult done);

  Workspace ws_;
  Options opt_;
  std::unique_ptr<SemanticEngine> semantic_;
  NavigationHistory nav_;
  std::map<std::string, std::unique_ptr<Document>> docs_;  // key: generic path
  Document* current_ = nullptr;
  Location cursor_;
  std::map<std::string, std::vector<Diagnostic>> diagnostics_;

  std::mutex qmu_;
  std::deque<std::function<void()>> queue_;
  std::condition_variable qcv_;
};

}  // namespace tf
