#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/Position.h"
#include "lsp/JsonRpc.h"

namespace tf {

// NOTE on columns: positions passed to / received from LspClient are in the
// *server's* offset encoding (see offsetEncoding(); we ask clangd for UTF-8
// so they normally match TextBuffer byte columns directly). SemanticEngine
// does the conversion when the server insists on UTF-16.

struct LspLocation {
  std::string uri;
  Range range;
};

struct LspDiagnostic {
  Range range;
  int severity = 1;  // 1 error, 2 warning, 3 info, 4 hint
  std::string message;
  std::string code;
  std::string source;
};

struct LspCompletionItem {
  std::string label;
  std::string detail;
  std::string insertText;
  std::string filterText;
  std::string sortText;
  std::string documentation;
  int kind = 0;  // LSP CompletionItemKind
  bool deprecated = false;
};

struct LspDocumentSymbol {
  std::string name;
  std::string detail;
  int kind = 0;  // LSP SymbolKind
  Range range;
  Range selectionRange;
  std::vector<LspDocumentSymbol> children;
};

struct LspWorkspaceSymbol {
  std::string name;
  std::string containerName;
  int kind = 0;
  LspLocation location;
};

struct LspSignatureHelp {
  struct Signature { std::string label; std::string documentation; std::vector<std::string> parameters; };
  std::vector<Signature> signatures;
  int activeSignature = 0;
  int activeParameter = 0;
};

struct IndexProgress {
  bool active = false;
  int percentage = -1;
  std::string message;
};

// Typed LSP client specialised for clangd (but protocol-generic where it can
// be). All callbacks run on the connection's reader thread.
class LspClient {
 public:
  template <class T>
  using Callback = std::function<void(T result, const std::string& error)>;

  struct Options {
    std::string serverPath = "clangd";
    std::vector<std::string> args;
    std::filesystem::path rootPath;
    std::filesystem::path logFile;
    std::vector<std::string> fallbackFlags;  // used by clangd when no compile command matches
    Process::Env env;                         // extra environment for the server process
    bool clangdFileStatus = true;
    std::function<void(const std::string&, const Json&)> trace;
  };

  LspClient() = default;
  ~LspClient();

  // Starts the server and runs the initialize handshake (blocking, with timeout).
  bool start(const Options& opt, std::string* error, int initTimeoutMs = 15000);
  void stop();
  bool alive() const { return conn_.alive(); }
  bool initialized() const { return initialized_; }
  const std::string& offsetEncoding() const { return offsetEncoding_; }
  const Json& serverCapabilities() const { return serverCaps_; }
  const std::string& serverVersion() const { return serverVersion_; }

  // --- document sync ---
  void didOpen(const std::string& uri, const std::string& languageId, int version, const std::string& text);
  void didChangeFull(const std::string& uri, int version, const std::string& text);
  struct ContentChange { Range range; std::string text; };
  void didChangeIncremental(const std::string& uri, int version, const std::vector<ContentChange>& changes);
  void didSave(const std::string& uri);
  void didClose(const std::string& uri);

  // --- navigation / information ---
  int definition(const std::string& uri, Position p, Callback<std::vector<LspLocation>> cb);
  int declaration(const std::string& uri, Position p, Callback<std::vector<LspLocation>> cb);
  int typeDefinition(const std::string& uri, Position p, Callback<std::vector<LspLocation>> cb);
  int implementation(const std::string& uri, Position p, Callback<std::vector<LspLocation>> cb);
  int references(const std::string& uri, Position p, bool includeDeclaration, Callback<std::vector<LspLocation>> cb);
  int hover(const std::string& uri, Position p, Callback<std::string> cb);
  int completion(const std::string& uri, Position p, Callback<std::vector<LspCompletionItem>> cb);
  int signatureHelp(const std::string& uri, Position p, Callback<LspSignatureHelp> cb);
  int documentSymbols(const std::string& uri, Callback<std::vector<LspDocumentSymbol>> cb);
  int workspaceSymbols(const std::string& query, Callback<std::vector<LspWorkspaceSymbol>> cb);
  int rename(const std::string& uri, Position p, const std::string& newName, Callback<Json> cb);  // WorkspaceEdit
  int switchSourceHeader(const std::string& uri, Callback<std::string> cb);                     // clangd extension
  int rawRequest(const std::string& method, Json params, Callback<Json> cb);
  void cancel(int id) { conn_.cancel(id); }

  // --- server -> client notifications ---
  void onDiagnostics(std::function<void(const std::string& uri, int version, std::vector<LspDiagnostic>)> h);
  void onFileStatus(std::function<void(const std::string& uri, const std::string& state)> h);  // clangd ext
  void onIndexProgress(std::function<void(const IndexProgress&)> h);
  void onLogMessage(std::function<void(int type, const std::string&)> h);
  void onDisconnect(std::function<void()> h) { conn_.onDisconnect(std::move(h)); }
  IndexProgress indexProgress() const;

  // Helpers for (de)serialising positions in the server's encoding.
  static Json toJson(Position p);
  static Json toJson(Range r);
  static Position positionFromJson(const Json& j);
  static Range rangeFromJson(const Json& j);
  static LspLocation locationFromJson(const Json& j);
  static std::vector<LspLocation> locationsFromJson(const Json& j);

 private:
  int locationRequest(const char* method, const std::string& uri, Position p, Callback<std::vector<LspLocation>> cb,
                      Json extra = Json::object());
  void installHandlers();

  JsonRpcConnection conn_;
  std::atomic<bool> initialized_{false};
  std::string offsetEncoding_ = "utf-16";
  std::string serverVersion_;
  Json serverCaps_;

  mutable std::mutex mu_;
  IndexProgress indexProgress_;
  std::function<void(const std::string&, int, std::vector<LspDiagnostic>)> diagHandler_;
  std::function<void(const std::string&, const std::string&)> fileStatusHandler_;
  std::function<void(const IndexProgress&)> progressHandler_;
  std::function<void(int, const std::string&)> logHandler_;
};

}  // namespace tf
