#include "lsp/LspClient.h"

#include <condition_variable>

#include "core/Document.h"
#include "core/Platform.h"

namespace tf {

LspClient::~LspClient() { stop(); }

Json LspClient::toJson(Position p) { return Json{{"line", p.line}, {"character", p.column}}; }
Json LspClient::toJson(Range r) { return Json{{"start", toJson(r.start)}, {"end", toJson(r.end)}}; }

Position LspClient::positionFromJson(const Json& j) { return {j.value("line", 0), j.value("character", 0)}; }
Range LspClient::rangeFromJson(const Json& j) {
  return {positionFromJson(j.value("start", Json::object())), positionFromJson(j.value("end", Json::object()))};
}
LspLocation LspClient::locationFromJson(const Json& j) {
  LspLocation l;
  if (j.contains("targetUri")) {  // LocationLink
    l.uri = j.value("targetUri", "");
    l.range = rangeFromJson(j.contains("targetSelectionRange") ? j["targetSelectionRange"]
                                                               : j.value("targetRange", Json::object()));
  } else {
    l.uri = j.value("uri", "");
    l.range = rangeFromJson(j.value("range", Json::object()));
  }
  return l;
}
std::vector<LspLocation> LspClient::locationsFromJson(const Json& j) {
  std::vector<LspLocation> out;
  if (j.is_array()) {
    for (auto& e : j) out.push_back(locationFromJson(e));
  } else if (j.is_object()) {
    out.push_back(locationFromJson(j));
  }
  return out;
}

bool LspClient::start(const Options& opt, std::string* error, int initTimeoutMs) {
  if (opt.trace) conn_.setTrace(opt.trace);
  installHandlers();
  if (!conn_.start(opt.serverPath, opt.args, opt.rootPath, opt.logFile, error, opt.env)) return false;

  Json caps = {
      {"textDocument",
       {{"synchronization", {{"didSave", true}, {"dynamicRegistration", false}}},
        {"completion",
         {{"completionItem",
           {{"snippetSupport", false},
            {"documentationFormat", Json::array({"plaintext", "markdown"})},
            {"deprecatedSupport", true}}},
          {"contextSupport", true}}},
        {"hover", {{"contentFormat", Json::array({"plaintext", "markdown"})}}},
        {"signatureHelp", {{"signatureInformation", {{"parameterInformation", {{"labelOffsetSupport", true}}}}}}},
        {"definition", {{"linkSupport", true}}},
        {"declaration", {{"linkSupport", true}}},
        {"typeDefinition", {{"linkSupport", true}}},
        {"implementation", {{"linkSupport", true}}},
        {"references", Json::object()},
        {"documentSymbol", {{"hierarchicalDocumentSymbolSupport", true}}},
        {"publishDiagnostics", {{"relatedInformation", true}, {"codeDescriptionSupport", true}}},
        {"rename", {{"prepareSupport", true}}},
        {"semanticTokens",
         {{"requests", {{"range", false}, {"full", {{"delta", true}}}}},
          {"tokenTypes", Json::array({"namespace",     "type",      "class",    "enum",     "interface",  "struct",
                                      "typeParameter", "parameter", "variable", "property", "enumMember", "event",
                                      "function",      "method",    "macro",    "keyword",  "modifier",   "comment",
                                      "string",        "number",    "regexp",   "operator", "decorator"})},
          {"tokenModifiers", Json::array({"declaration", "definition", "readonly", "static", "deprecated", "abstract",
                                          "async", "modification", "documentation", "defaultLibrary"})},
          {"formats", Json::array({"relative"})},
          {"multilineTokenSupport", false},
          {"overlappingTokenSupport", false}}},
        {"codeAction",
         {{"isPreferredSupport", true},
          {"codeActionLiteralSupport",
           {{"codeActionKind",
             {{"valueSet", Json::array({"", "quickfix", "refactor", "refactor.extract", "refactor.inline",
                                        "refactor.rewrite", "source", "source.organizeImports"})}}}}}}},
        {"inlayHint", Json::object()},
        {"foldingRange", {{"lineFoldingOnly", false}}},
        {"documentHighlight", Json::object()},
        {"callHierarchy", Json::object()},
        {"typeHierarchy", Json::object()},
        {"formatting", Json::object()},
        {"rangeFormatting", Json::object()}}},
      {"workspace",
       {{"symbol", Json::object()},
        {"workspaceFolders", false},
        {"configuration", false},
        {"applyEdit", true},
        {"workspaceEdit", {{"documentChanges", true}}},
        {"executeCommand", Json::object()},
        {"didChangeWatchedFiles", {{"dynamicRegistration", true}}}}},
      {"window", {{"workDoneProgress", true}}},
      {"general", {{"positionEncodings", Json::array({"utf-8", "utf-16"})}}},
      {"offsetEncoding", Json::array({"utf-8", "utf-16"})},  // clangd extension
  };
  Json initOpts = {{"clangdFileStatus", opt.clangdFileStatus}};
  if (!opt.fallbackFlags.empty()) initOpts["fallbackFlags"] = opt.fallbackFlags;
  Json params = {
      {"processId", currentProcessId()},   {"rootUri", opt.rootPath.empty() ? Json() : Json(pathToUri(opt.rootPath))},
      {"rootPath", opt.rootPath.string()}, {"capabilities", caps},
      {"initializationOptions", initOpts}, {"trace", "off"}};

  std::mutex m;
  std::condition_variable cv;
  bool done = false, ok = false;
  std::string err;
  conn_.request("initialize", params, [&](const Json* result, const RpcError* e) {
    std::lock_guard<std::mutex> lk(m);
    if (e) {
      err = e->message;
    } else {
      ok = true;
      serverCaps_ = result->value("capabilities", Json::object());
      if (result->contains("offsetEncoding")) offsetEncoding_ = (*result)["offsetEncoding"].get<std::string>();
      else if (serverCaps_.contains("positionEncoding"))
        offsetEncoding_ = serverCaps_["positionEncoding"].get<std::string>();
      if (result->contains("serverInfo")) {
        serverVersion_ = (*result)["serverInfo"].value("name", "") + " " + (*result)["serverInfo"].value("version", "");
      }
    }
    done = true;
    cv.notify_all();
  });
  {
    std::unique_lock<std::mutex> lk(m);
    if (!cv.wait_for(lk, std::chrono::milliseconds(initTimeoutMs), [&] { return done; })) {
      lk.unlock();
      if (error) *error = "initialize timed out";
      conn_.stop(200);
      return false;
    }
  }
  if (!ok) {
    if (error) *error = "initialize failed: " + err;
    conn_.stop(200);
    return false;
  }
  conn_.notify("initialized", Json::object());
  initialized_ = true;
  return true;
}

void LspClient::stop() {
  if (!conn_.alive()) {
    conn_.stop(0);
    return;
  }
  if (initialized_) {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    conn_.request("shutdown", Json(), [&](const Json*, const RpcError*) {
      std::lock_guard<std::mutex> lk(m);
      done = true;
      cv.notify_all();
    });
    {
      std::unique_lock<std::mutex> lk(m);
      cv.wait_for(lk, std::chrono::milliseconds(2000), [&] { return done; });
    }
    conn_.notify("exit", Json());
    initialized_ = false;
  }
  conn_.stop(1500);
}

void LspClient::installHandlers() {
  conn_.onRequest("window/workDoneProgress/create", [](const Json&) { return Json(); });
  conn_.onRequest("client/registerCapability", [](const Json&) { return Json(); });
  conn_.onRequest("client/unregisterCapability", [](const Json&) { return Json(); });
  conn_.onRequest("window/showMessageRequest", [](const Json&) { return Json(); });
  conn_.onRequest("workspace/semanticTokens/refresh", [](const Json&) { return Json(); });
  conn_.onRequest("workspace/inlayHint/refresh", [](const Json&) { return Json(); });
  conn_.onRequest("workspace/configuration", [](const Json& p) {
    Json out = Json::array();
    for (size_t i = 0; i < p.value("items", Json::array()).size(); ++i) out.push_back(Json());
    return out;
  });

  conn_.onRequest("workspace/applyEdit", [this](const Json& p) {
    std::function<void(const std::string&, const Json&)> h;
    {
      std::lock_guard<std::mutex> lk(mu_);
      h = applyEditHandler_;
    }
    if (h) h(p.value("label", ""), p.value("edit", Json::object()));
    return Json{{"applied", static_cast<bool>(h)}};
  });

  conn_.onNotification("textDocument/publishDiagnostics", [this](const Json& p) {
    std::function<void(const std::string&, int, std::vector<LspDiagnostic>)> h;
    {
      std::lock_guard<std::mutex> lk(mu_);
      rawDiagnostics_[p.value("uri", "")] = p.value("diagnostics", Json::array());
      h = diagHandler_;
    }
    if (!h) return;
    std::vector<LspDiagnostic> diags;
    for (auto& d : p.value("diagnostics", Json::array())) {
      LspDiagnostic x;
      x.range = rangeFromJson(d.value("range", Json::object()));
      x.severity = d.value("severity", 1);
      x.message = d.value("message", "");
      if (d.contains("code")) x.code = d["code"].is_string() ? d["code"].get<std::string>() : d["code"].dump();
      x.source = d.value("source", "");
      diags.push_back(std::move(x));
    }
    h(p.value("uri", ""), p.value("version", -1), std::move(diags));
  });
  conn_.onNotification("textDocument/clangd.fileStatus", [this](const Json& p) {
    std::function<void(const std::string&, const std::string&)> h;
    {
      std::lock_guard<std::mutex> lk(mu_);
      h = fileStatusHandler_;
    }
    if (h) h(p.value("uri", ""), p.value("state", ""));
  });
  conn_.onNotification("$/progress", [this](const Json& p) {
    std::string token =
        p.contains("token") ? (p["token"].is_string() ? p["token"].get<std::string>() : p["token"].dump()) : "";
    if (token != "backgroundIndexProgress") return;
    const Json& v = p.value("value", Json::object());
    std::string kind = v.value("kind", "");
    std::function<void(const IndexProgress&)> h;
    IndexProgress snapshot;
    {
      std::lock_guard<std::mutex> lk(mu_);
      if (kind == "begin") {
        indexProgress_.active = true;
        indexProgress_.percentage = v.value("percentage", 0);
        indexProgress_.message = v.value("title", "indexing");
      } else if (kind == "report") {
        indexProgress_.active = true;
        if (v.contains("percentage")) indexProgress_.percentage = v["percentage"].get<int>();
        if (v.contains("message")) indexProgress_.message = v["message"].get<std::string>();
      } else if (kind == "end") {
        indexProgress_.active = false;
        indexProgress_.percentage = 100;
        indexProgress_.message = "done";
      }
      snapshot = indexProgress_;
      h = progressHandler_;
    }
    if (h) h(snapshot);
  });
  conn_.onNotification("window/logMessage", [this](const Json& p) {
    std::function<void(int, const std::string&)> h;
    {
      std::lock_guard<std::mutex> lk(mu_);
      h = logHandler_;
    }
    if (h) h(p.value("type", 4), p.value("message", ""));
  });
  conn_.onNotification("window/showMessage", [this](const Json& p) {
    std::function<void(int, const std::string&)> h;
    {
      std::lock_guard<std::mutex> lk(mu_);
      h = logHandler_;
    }
    if (h) h(p.value("type", 4), p.value("message", ""));
  });
}

IndexProgress LspClient::indexProgress() const {
  std::lock_guard<std::mutex> lk(mu_);
  return indexProgress_;
}

void LspClient::onDiagnostics(std::function<void(const std::string&, int, std::vector<LspDiagnostic>)> h) {
  std::lock_guard<std::mutex> lk(mu_);
  diagHandler_ = std::move(h);
}
void LspClient::onFileStatus(std::function<void(const std::string&, const std::string&)> h) {
  std::lock_guard<std::mutex> lk(mu_);
  fileStatusHandler_ = std::move(h);
}
void LspClient::onIndexProgress(std::function<void(const IndexProgress&)> h) {
  std::lock_guard<std::mutex> lk(mu_);
  progressHandler_ = std::move(h);
}
void LspClient::onLogMessage(std::function<void(int, const std::string&)> h) {
  std::lock_guard<std::mutex> lk(mu_);
  logHandler_ = std::move(h);
}

// ---- document sync ----

void LspClient::didOpen(const std::string& uri, const std::string& languageId, int version, const std::string& text) {
  conn_.notify(
      "textDocument/didOpen",
      Json{{"textDocument", {{"uri", uri}, {"languageId", languageId}, {"version", version}, {"text", text}}}});
}

void LspClient::didChangeFull(const std::string& uri, int version, const std::string& text) {
  conn_.notify("textDocument/didChange", Json{{"textDocument", {{"uri", uri}, {"version", version}}},
                                              {"contentChanges", Json::array({Json{{"text", text}}})}});
}

void LspClient::didChangeIncremental(const std::string& uri, int version, const std::vector<ContentChange>& changes) {
  Json arr = Json::array();
  for (auto& c : changes) arr.push_back(Json{{"range", toJson(c.range)}, {"text", c.text}});
  conn_.notify("textDocument/didChange",
               Json{{"textDocument", {{"uri", uri}, {"version", version}}}, {"contentChanges", arr}});
}

void LspClient::didSave(const std::string& uri) {
  conn_.notify("textDocument/didSave", Json{{"textDocument", {{"uri", uri}}}});
}

void LspClient::didClose(const std::string& uri) {
  conn_.notify("textDocument/didClose", Json{{"textDocument", {{"uri", uri}}}});
}

// ---- requests ----

int LspClient::rawRequest(const std::string& method, Json params, Callback<Json> cb) {
  return conn_.request(method, std::move(params), [cb = std::move(cb)](const Json* r, const RpcError* e) {
    if (e) cb(Json(), e->message);
    else cb(*r, {});
  });
}

int LspClient::locationRequest(const char* method, const std::string& uri, Position p,
                               Callback<std::vector<LspLocation>> cb, Json extra) {
  Json params = {{"textDocument", {{"uri", uri}}}, {"position", toJson(p)}};
  for (auto& [k, v] : extra.items()) params[k] = v;
  return rawRequest(method, std::move(params), [cb = std::move(cb)](Json r, const std::string& err) {
    if (!err.empty()) return cb({}, err);
    cb(locationsFromJson(r), {});
  });
}

int LspClient::definition(const std::string& uri, Position p, Callback<std::vector<LspLocation>> cb) {
  return locationRequest("textDocument/definition", uri, p, std::move(cb));
}
int LspClient::declaration(const std::string& uri, Position p, Callback<std::vector<LspLocation>> cb) {
  return locationRequest("textDocument/declaration", uri, p, std::move(cb));
}
int LspClient::typeDefinition(const std::string& uri, Position p, Callback<std::vector<LspLocation>> cb) {
  return locationRequest("textDocument/typeDefinition", uri, p, std::move(cb));
}
int LspClient::implementation(const std::string& uri, Position p, Callback<std::vector<LspLocation>> cb) {
  return locationRequest("textDocument/implementation", uri, p, std::move(cb));
}
int LspClient::references(const std::string& uri, Position p, bool includeDeclaration,
                          Callback<std::vector<LspLocation>> cb) {
  return locationRequest("textDocument/references", uri, p, std::move(cb),
                         Json{{"context", {{"includeDeclaration", includeDeclaration}}}});
}

int LspClient::hover(const std::string& uri, Position p, Callback<std::string> cb) {
  Json params = {{"textDocument", {{"uri", uri}}}, {"position", toJson(p)}};
  return rawRequest("textDocument/hover", params, [cb = std::move(cb)](Json r, const std::string& err) {
    if (!err.empty()) return cb({}, err);
    if (!r.is_object()) return cb({}, {});
    const Json& c = r["contents"];
    std::string text;
    auto one = [&](const Json& x) {
      if (x.is_string()) text += x.get<std::string>();
      else if (x.is_object()) text += x.value("value", "");
      text += "\n";
    };
    if (c.is_array())
      for (auto& x : c) one(x);
    else one(c);
    cb(text, {});
  });
}

int LspClient::completion(const std::string& uri, Position p, Callback<std::vector<LspCompletionItem>> cb) {
  Json params = {{"textDocument", {{"uri", uri}}}, {"position", toJson(p)}, {"context", {{"triggerKind", 1}}}};
  return rawRequest("textDocument/completion", params, [cb = std::move(cb)](Json r, const std::string& err) {
    if (!err.empty()) return cb({}, err);
    const Json& items = r.is_array() ? r : r.value("items", Json::array());
    std::vector<LspCompletionItem> out;
    for (auto& it : items) {
      LspCompletionItem c;
      c.label = it.value("label", "");
      c.detail = it.value("detail", "");
      c.insertText = it.value("insertText", "");
      if (c.insertText.empty() && it.contains("textEdit")) c.insertText = it["textEdit"].value("newText", "");
      if (c.insertText.empty()) c.insertText = c.label;
      c.filterText = it.value("filterText", c.label);
      c.sortText = it.value("sortText", c.label);
      c.kind = it.value("kind", 0);
      c.deprecated = it.value("deprecated", false);
      if (it.contains("documentation")) {
        const Json& d = it["documentation"];
        c.documentation = d.is_string() ? d.get<std::string>() : d.value("value", "");
      }
      out.push_back(std::move(c));
    }
    cb(std::move(out), {});
  });
}

int LspClient::signatureHelp(const std::string& uri, Position p, Callback<LspSignatureHelp> cb) {
  Json params = {{"textDocument", {{"uri", uri}}}, {"position", toJson(p)}};
  return rawRequest("textDocument/signatureHelp", params, [cb = std::move(cb)](Json r, const std::string& err) {
    if (!err.empty()) return cb({}, err);
    LspSignatureHelp sh;
    if (r.is_object()) {
      sh.activeSignature = r.value("activeSignature", 0);
      sh.activeParameter = r.value("activeParameter", 0);
      for (auto& s : r.value("signatures", Json::array())) {
        LspSignatureHelp::Signature sig;
        sig.label = s.value("label", "");
        if (s.contains("documentation")) {
          const Json& d = s["documentation"];
          sig.documentation = d.is_string() ? d.get<std::string>() : d.value("value", "");
        }
        for (auto& pr : s.value("parameters", Json::array())) {
          const Json& l = pr["label"];
          if (l.is_string()) sig.parameters.push_back(l.get<std::string>());
          else if (l.is_array() && l.size() == 2)
            sig.parameters.push_back(sig.label.substr(l[0].get<size_t>(), l[1].get<size_t>() - l[0].get<size_t>()));
        }
        sh.signatures.push_back(std::move(sig));
      }
    }
    cb(std::move(sh), {});
  });
}

namespace {
LspDocumentSymbol docSymbolFromJson(const Json& j) {
  LspDocumentSymbol s;
  s.name = j.value("name", "");
  s.detail = j.value("detail", "");
  s.kind = j.value("kind", 0);
  if (j.contains("location")) {  // SymbolInformation flavour
    s.range = LspClient::rangeFromJson(j["location"].value("range", Json::object()));
    s.selectionRange = s.range;
  } else {
    s.range = LspClient::rangeFromJson(j.value("range", Json::object()));
    s.selectionRange = LspClient::rangeFromJson(j.value("selectionRange", Json::object()));
  }
  for (auto& c : j.value("children", Json::array())) s.children.push_back(docSymbolFromJson(c));
  return s;
}
}  // namespace

int LspClient::documentSymbols(const std::string& uri, Callback<std::vector<LspDocumentSymbol>> cb) {
  Json params = {{"textDocument", {{"uri", uri}}}};
  return rawRequest("textDocument/documentSymbol", params, [cb = std::move(cb)](Json r, const std::string& err) {
    if (!err.empty()) return cb({}, err);
    std::vector<LspDocumentSymbol> out;
    if (r.is_array())
      for (auto& s : r) out.push_back(docSymbolFromJson(s));
    cb(std::move(out), {});
  });
}

int LspClient::workspaceSymbols(const std::string& query, Callback<std::vector<LspWorkspaceSymbol>> cb) {
  return rawRequest("workspace/symbol", Json{{"query", query}}, [cb = std::move(cb)](Json r, const std::string& err) {
    if (!err.empty()) return cb({}, err);
    std::vector<LspWorkspaceSymbol> out;
    if (r.is_array()) {
      for (auto& s : r) {
        LspWorkspaceSymbol w;
        w.name = s.value("name", "");
        w.containerName = s.value("containerName", "");
        w.kind = s.value("kind", 0);
        if (s.contains("location")) w.location = locationFromJson(s["location"]);
        out.push_back(std::move(w));
      }
    }
    cb(std::move(out), {});
  });
}

int LspClient::rename(const std::string& uri, Position p, const std::string& newName, Callback<Json> cb) {
  Json params = {{"textDocument", {{"uri", uri}}}, {"position", toJson(p)}, {"newName", newName}};
  return rawRequest("textDocument/rename", params, std::move(cb));
}

int LspClient::switchSourceHeader(const std::string& uri, Callback<std::string> cb) {
  return rawRequest("textDocument/switchSourceHeader", Json{{"uri", uri}},
                    [cb = std::move(cb)](Json r, const std::string& err) {
                      if (!err.empty()) return cb({}, err);
                      cb(r.is_string() ? r.get<std::string>() : std::string(), {});
                    });
}

// ---- clangd feature set ----

void LspClient::onApplyEdit(std::function<void(const std::string&, const Json&)> h) {
  std::lock_guard<std::mutex> lk(mu_);
  applyEditHandler_ = std::move(h);
}

LspSemanticTokensLegend LspClient::semanticTokensLegend() const {
  LspSemanticTokensLegend out;
  const Json& legend = serverCaps_.value("semanticTokensProvider", Json::object()).value("legend", Json::object());
  for (auto& t : legend.value("tokenTypes", Json::array())) out.types.push_back(t.get<std::string>());
  for (auto& t : legend.value("tokenModifiers", Json::array())) out.modifiers.push_back(t.get<std::string>());
  return out;
}

bool LspClient::supportsSemanticTokens() const { return serverCaps_.contains("semanticTokensProvider"); }

bool LspClient::supportsTokenDelta() const {
  const Json full = serverCaps_.value("semanticTokensProvider", Json::object()).value("full", Json());
  return full.is_object() && full.value("delta", false);
}

namespace {
std::vector<uint32_t> uintArray(const Json& j) {
  std::vector<uint32_t> out;
  if (!j.is_array()) return out;
  out.reserve(j.size());
  for (auto& v : j) out.push_back(v.get<uint32_t>());
  return out;
}

std::vector<LspTextEdit> textEditsFromJson(const Json& r) {
  std::vector<LspTextEdit> out;
  if (r.is_array())
    for (auto& e : r)
      out.push_back({LspClient::rangeFromJson(e.value("range", Json::object())), e.value("newText", "")});
  return out;
}

void itemsFromJson(const Json& r, std::vector<LspHierarchyItem>& out) {
  if (r.is_array())
    for (auto& i : r) out.push_back(LspClient::hierarchyItemFromJson(i));
}
}  // namespace

LspHierarchyItem LspClient::hierarchyItemFromJson(const Json& j) {
  LspHierarchyItem it;
  it.name = j.value("name", "");
  it.detail = j.value("detail", "");
  it.uri = j.value("uri", "");
  it.kind = j.value("kind", 0);
  it.range = rangeFromJson(j.value("range", Json::object()));
  it.selectionRange = rangeFromJson(j.value("selectionRange", Json::object()));
  it.raw = j;
  return it;
}

int LspClient::semanticTokensFull(const std::string& uri, Callback<LspSemanticTokens> cb) {
  return rawRequest("textDocument/semanticTokens/full", Json{{"textDocument", {{"uri", uri}}}},
                    [cb = std::move(cb)](Json r, const std::string& err) {
                      if (!err.empty()) return cb({}, err);
                      LspSemanticTokens t;
                      t.resultId = r.value("resultId", "");
                      t.data = uintArray(r.value("data", Json::array()));
                      cb(std::move(t), {});
                    });
}

int LspClient::semanticTokensDelta(const std::string& uri, const std::string& prev, Callback<LspSemanticTokens> cb) {
  return rawRequest("textDocument/semanticTokens/full/delta",
                    Json{{"textDocument", {{"uri", uri}}}, {"previousResultId", prev}},
                    [cb = std::move(cb)](Json r, const std::string& err) {
                      if (!err.empty()) return cb({}, err);
                      LspSemanticTokens t;
                      t.resultId = r.value("resultId", "");
                      if (r.contains("edits")) {
                        t.full = false;
                        for (auto& e : r["edits"]) {
                          LspSemanticTokens::Edit x;
                          x.start = e.value("start", 0u);
                          x.deleteCount = e.value("deleteCount", 0u);
                          x.data = uintArray(e.value("data", Json::array()));
                          t.edits.push_back(std::move(x));
                        }
                      } else {
                        t.data = uintArray(r.value("data", Json::array()));
                      }
                      cb(std::move(t), {});
                    });
}

int LspClient::inlayHints(const std::string& uri, Range rg, Callback<std::vector<LspInlayHint>> cb) {
  return rawRequest("textDocument/inlayHint", Json{{"textDocument", {{"uri", uri}}}, {"range", toJson(rg)}},
                    [cb = std::move(cb)](Json r, const std::string& err) {
                      if (!err.empty()) return cb({}, err);
                      std::vector<LspInlayHint> out;
                      if (r.is_array()) {
                        for (auto& h : r) {
                          LspInlayHint x;
                          x.pos = positionFromJson(h.value("position", Json::object()));
                          const Json l = h.value("label", Json());
                          if (l.is_string()) x.label = l.get<std::string>();
                          else if (l.is_array())
                            for (auto& part : l) x.label += part.value("value", "");
                          x.kind = h.value("kind", 0);
                          x.paddingLeft = h.value("paddingLeft", false);
                          x.paddingRight = h.value("paddingRight", false);
                          out.push_back(std::move(x));
                        }
                      }
                      cb(std::move(out), {});
                    });
}

int LspClient::foldingRanges(const std::string& uri, Callback<std::vector<LspFoldingRange>> cb) {
  return rawRequest("textDocument/foldingRange", Json{{"textDocument", {{"uri", uri}}}},
                    [cb = std::move(cb)](Json r, const std::string& err) {
                      if (!err.empty()) return cb({}, err);
                      std::vector<LspFoldingRange> out;
                      if (r.is_array()) {
                        for (auto& f : r) {
                          LspFoldingRange x;
                          x.startLine = f.value("startLine", 0);
                          x.endLine = f.value("endLine", 0);
                          x.startColumn = f.value("startCharacter", -1);
                          x.endColumn = f.value("endCharacter", -1);
                          x.kind = f.value("kind", "");
                          out.push_back(std::move(x));
                        }
                      }
                      cb(std::move(out), {});
                    });
}

int LspClient::documentHighlights(const std::string& uri, Position p, Callback<std::vector<LspHighlight>> cb) {
  return rawRequest("textDocument/documentHighlight", Json{{"textDocument", {{"uri", uri}}}, {"position", toJson(p)}},
                    [cb = std::move(cb)](Json r, const std::string& err) {
                      if (!err.empty()) return cb({}, err);
                      std::vector<LspHighlight> out;
                      if (r.is_array())
                        for (auto& h : r)
                          out.push_back({rangeFromJson(h.value("range", Json::object())), h.value("kind", 1)});
                      cb(std::move(out), {});
                    });
}

int LspClient::callHierarchyPrepare(const std::string& uri, Position p, Callback<std::vector<LspHierarchyItem>> cb) {
  return rawRequest("textDocument/prepareCallHierarchy",
                    Json{{"textDocument", {{"uri", uri}}}, {"position", toJson(p)}},
                    [cb = std::move(cb)](Json r, const std::string& err) {
                      if (!err.empty()) return cb({}, err);
                      std::vector<LspHierarchyItem> out;
                      itemsFromJson(r, out);
                      cb(std::move(out), {});
                    });
}

int LspClient::callHierarchyCalls(const LspHierarchyItem& item, bool incoming, Callback<std::vector<LspCall>> cb) {
  return rawRequest(incoming ? "callHierarchy/incomingCalls" : "callHierarchy/outgoingCalls", Json{{"item", item.raw}},
                    [cb = std::move(cb), incoming](Json r, const std::string& err) {
                      if (!err.empty()) return cb({}, err);
                      std::vector<LspCall> out;
                      if (r.is_array()) {
                        for (auto& c : r) {
                          LspCall x;
                          x.item = hierarchyItemFromJson(c.value(incoming ? "from" : "to", Json::object()));
                          for (auto& fr : c.value("fromRanges", Json::array())) x.ranges.push_back(rangeFromJson(fr));
                          out.push_back(std::move(x));
                        }
                      }
                      cb(std::move(out), {});
                    });
}

int LspClient::typeHierarchyPrepare(const std::string& uri, Position p, Callback<std::vector<LspHierarchyItem>> cb) {
  return rawRequest("textDocument/prepareTypeHierarchy",
                    Json{{"textDocument", {{"uri", uri}}}, {"position", toJson(p)}},
                    [cb = std::move(cb)](Json r, const std::string& err) {
                      if (!err.empty()) return cb({}, err);
                      std::vector<LspHierarchyItem> out;
                      itemsFromJson(r, out);
                      cb(std::move(out), {});
                    });
}

int LspClient::typeHierarchyItems(const LspHierarchyItem& item, bool supertypes,
                                  Callback<std::vector<LspHierarchyItem>> cb) {
  return rawRequest(supertypes ? "typeHierarchy/supertypes" : "typeHierarchy/subtypes", Json{{"item", item.raw}},
                    [cb = std::move(cb)](Json r, const std::string& err) {
                      if (!err.empty()) return cb({}, err);
                      std::vector<LspHierarchyItem> out;
                      itemsFromJson(r, out);
                      cb(std::move(out), {});
                    });
}

int LspClient::formatting(const std::string& uri, Callback<std::vector<LspTextEdit>> cb) {
  return rawRequest("textDocument/formatting",
                    Json{{"textDocument", {{"uri", uri}}}, {"options", {{"tabSize", 4}, {"insertSpaces", true}}}},
                    [cb = std::move(cb)](Json r, const std::string& err) {
                      if (!err.empty()) return cb({}, err);
                      cb(textEditsFromJson(r), {});
                    });
}

int LspClient::rangeFormatting(const std::string& uri, Range rg, Callback<std::vector<LspTextEdit>> cb) {
  return rawRequest("textDocument/rangeFormatting",
                    Json{{"textDocument", {{"uri", uri}}},
                         {"range", toJson(rg)},
                         {"options", {{"tabSize", 4}, {"insertSpaces", true}}}},
                    [cb = std::move(cb)](Json r, const std::string& err) {
                      if (!err.empty()) return cb({}, err);
                      cb(textEditsFromJson(r), {});
                    });
}

int LspClient::codeActions(const std::string& uri, Range rg, Callback<std::vector<LspCodeAction>> cb) {
  Json diags = Json::array();
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = rawDiagnostics_.find(uri);
    if (it != rawDiagnostics_.end()) {
      for (auto& d : it->second) {
        Range dr = rangeFromJson(d.value("range", Json::object()));
        if (!(dr.end < rg.start) && !(rg.end < dr.start)) diags.push_back(d);
      }
    }
  }
  Json params = {{"textDocument", {{"uri", uri}}}, {"range", toJson(rg)}, {"context", {{"diagnostics", diags}}}};
  return rawRequest("textDocument/codeAction", std::move(params), [cb = std::move(cb)](Json r, const std::string& err) {
    if (!err.empty()) return cb({}, err);
    std::vector<LspCodeAction> out;
    if (r.is_array()) {
      for (auto& a : r) {
        LspCodeAction x;
        x.title = a.value("title", "");
        x.kind = a.value("kind", "");
        x.preferred = a.value("isPreferred", false);
        if (a.contains("edit")) x.edit = a["edit"];
        if (a.contains("command")) {
          const Json& c = a["command"];
          x.hasCommand = true;
          if (c.is_object()) {  // CodeAction.command is a Command
            x.command = c.value("command", "");
            x.arguments = c.value("arguments", Json::array());
          } else {  // a bare Command: {title, command: "name", arguments}
            x.command = c.get<std::string>();
            x.arguments = a.value("arguments", Json::array());
          }
        }
        out.push_back(std::move(x));
      }
    }
    cb(std::move(out), {});
  });
}

int LspClient::executeCommand(const std::string& command, Json arguments, Callback<Json> cb) {
  return rawRequest("workspace/executeCommand", Json{{"command", command}, {"arguments", std::move(arguments)}},
                    std::move(cb));
}

void LspClient::didChangeWatchedFiles(const std::vector<LspFileEvent>& events) {
  if (events.empty()) return;
  Json changes = Json::array();
  for (auto& e : events) changes.push_back(Json{{"uri", e.uri}, {"type", e.type}});
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& e : events)
      if (sentFileEvents_.size() < 1000) sentFileEvents_.push_back(e);
  }
  conn_.notify("workspace/didChangeWatchedFiles", Json{{"changes", changes}});
}

std::vector<LspFileEvent> LspClient::sentFileEvents() const {
  std::lock_guard<std::mutex> lk(mu_);
  return sentFileEvents_;
}

}  // namespace tf
