// Fuzz harness of the LSP transport: Content-Length framing (partial / garbled frames) and the JSON-RPC dispatch of
// requests, notifications and responses, driven through JsonRpcConnection::processBuffer without a language server.
#include <string>
#include <vector>

#include "FuzzTarget.h"
#include "lsp/JsonRpc.h"
#include "lsp/LspClient.h"

namespace tf::fuzz {
namespace {

// One connection for the life of the process, attached to a server that reads and discards its stdin: responses to the
// requests the fuzzed stream makes of us are written to a real pipe, and requests we issue stay pending so that fuzzed
// responses reach the handler code.
JsonRpcConnection& connection() {
  static JsonRpcConnection c;
  static bool started = [] {
    std::string err;
    c.onRequest("echo", [](const Json& p) { return p; });
    c.onRequest("rpcerror", [](const Json&) -> Json { throw RpcError{-32000, "boom", Json::object()}; });
    c.onRequest("stderror", [](const Json&) -> Json { throw std::runtime_error("boom"); });
    c.onRequest("window/workDoneProgress/create", [](const Json&) { return Json(); });
    c.onNotification("n", [](const Json& p) { (void)p.dump(); });
    // Handlers that read the message the way LspClient does (these converters throw on a value of the wrong type; the
    // transport has to survive that).
    c.onNotification("textDocument/publishDiagnostics", [](const Json& p) {
      for (const Json& d : p.value("diagnostics", Json::array()))
        LspClient::rangeFromJson(d.value("range", Json::object()));
    });
    c.onNotification("loc", [](const Json& p) { LspClient::locationsFromJson(p); });
    c.onNotification("item", [](const Json& p) { LspClient::hierarchyItemFromJson(p); });
    return c.start("sh", {"-c", "cat >/dev/null"}, {}, {}, &err);
  }();
  FUZZ_REQUIRE(started);
  return c;
}

// Parts (split at "\n#--#\n"): a part that starts with "H:" is appended to the stream verbatim (raw header bytes);
// any other part is the JSON body and is framed with a correct Content-Length. Bytes 0x01-0x04 inside a part stand
// for the ids of four requests the harness has pending (so that fuzzed responses find a handler). The stream is
// delivered in two pieces, so frames are split at arbitrary points.
void runJsonRpc(const uint8_t* d, size_t n) {
  JsonRpcConnection& c = connection();
  int ids[4];
  for (int& id : ids)
    id = c.request("fuzz", Json::object(), [](const Json* result, const RpcError* error) {
      if (result) LspClient::locationsFromJson(*result);
      if (error) (void)(error->message.size() + error->data.dump().size());
    });
  const std::vector<std::string> parts = splitParts(d, n, 16);
  std::string stream;
  for (const std::string& raw : parts) {
    std::string p;
    for (char ch : raw) {
      const unsigned char u = static_cast<unsigned char>(ch);
      if (u >= 1 && u <= 4) p += std::to_string(ids[u - 1]);
      else p += ch;
    }
    if (p.compare(0, 2, "H:") == 0) {
      stream += p.substr(2);
    } else {
      stream += "Content-Length: " + std::to_string(p.size()) + "\r\n\r\n" + p;
    }
  }
  std::string buf;
  const size_t cut = stream.size() / 3;
  buf.append(stream, 0, cut);
  c.processBuffer(buf);
  buf.append(stream, cut, std::string::npos);
  c.processBuffer(buf);
  c.processBuffer(buf);
  // Answer what the fuzzed stream left pending (the map would grow without bound over millions of runs).
  std::string rest;
  for (int id : ids) {
    const std::string body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"result\":null}";
    rest += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  }
  c.processBuffer(rest);
}

Registrar r1({"jsonrpc",
              runJsonRpc,
              {},
              {"{\"jsonrpc\":\"2.0\",\"id\":\x01,\"result\":{\"ok\":true}}\n#--#\n{\"jsonrpc\":\"2.0\",\"id\":\x02,"
               "\"error\":{\"code\":-32601,\"message\":\"nope\",\"data\":[1]}}\n#--#\n"
               "{\"jsonrpc\":\"2.0\",\"method\":\"n\",\"params\":{}}\n#--#\n{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":"
               "\"echo\",\"params\":[1,2]}\n#--#\n"
               "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"rpcerror\"}\n#--#\n{\"jsonrpc\":\"2.0\",\"id\":\"s\","
               "\"method\":\"stderror\"}\n#--#\n"
               "{\"jsonrpc\":\"2.0\",\"id\":\x03,\"result\":null}",
               "H:Content-Length: 18\r\nContent-Type: application/vscode-jsonrpc; "
               "charset=utf-8\r\n\r\n{\"id\":1,\"result\":1}\n#--#\nH:Content-Length: 99999999999999999999\r\n\r\n{}",
               "H:Content-Length: 18446744073709551615\r\n\r\n{}\n#--#\nH:Content-Length: "
               "-5\r\n\r\n{}\n#--#\nH:garbage\r\n\r\n{\"id\":1}",
               "{\"id\":\x04,\"error\":5}\n#--#\n{\"id\":\x01,\"error\":{\"code\":\"x\",\"message\":7}}\n#--#\n{"
               "\"method\":5,\"id\":1}\n#--#\n{\"method\":null}"},
              {"Content-Length:",
               "Content-Length: ",
               "Content-Type: ",
               "\r\n",
               "\r\n\r\n",
               "\n#--#\n",
               "\nH:",
               "H:",
               "\x01",
               "\x02",
               "\x03",
               "\x04",
               "{",
               "}",
               "[",
               "]",
               ":",
               ",",
               "\"",
               "\"jsonrpc\":\"2.0\"",
               "\"id\":",
               "\"method\":",
               "\"params\":",
               "\"result\":",
               "\"error\":",
               "\"code\":",
               "\"message\":",
               "\"data\":",
               "null",
               "true",
               "false",
               "-1",
               "0",
               "2147483648",
               "4294967296",
               "18446744073709551615",
               "99999999999999999999",
               "1e999",
               "\"echo\"",
               "\"rpcerror\"",
               "\"stderror\"",
               "\"n\"",
               "\"$/cancelRequest\"",
               "\"textDocument/publishDiagnostics\"",
               "\"loc\"",
               "\"item\"",
               "\"uri\"",
               "\"range\"",
               "\"start\"",
               "\"end\"",
               "\"line\"",
               "\"character\"",
               "\"targetUri\"",
               "\"targetSelectionRange\"",
               "\"diagnostics\"",
               "\\u0000",
               "\\ud800",
               "\\",
               "\xff",
               "\xc0\x80",
               "{\"id\":",
               "{\"method\":"},
              1024});

}  // namespace
}  // namespace tf::fuzz
