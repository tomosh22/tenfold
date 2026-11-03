#include "lsp/JsonRpc.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace tf {

namespace {
constexpr unsigned long long kMaxFrameBytes = 1ull << 30;  // far above any real LSP message
}

JsonRpcConnection::~JsonRpcConnection() { stop(200); }

bool JsonRpcConnection::start(const std::string& exe, const std::vector<std::string>& args,
                              const std::filesystem::path& cwd, const std::filesystem::path& stderrLog,
                              std::string* error, const Process::Env& env) {
  // The server runs as its own process tree: stop() must also end its children (clangd installed through pip is a
  // Python wrapper whose real clangd would otherwise outlive the kill and keep our pipe open, blocking the reader).
  ProcessOptions opts;
  opts.processTree = true;
  if (!proc_.start(exe, args, cwd, stderrLog, error, env, opts)) return false;
  running_ = true;
  reader_ = std::thread([this] { readerLoop(); });
  return true;
}

void JsonRpcConnection::stop(int graceMs) {
  if (!running_.exchange(false)) {
    if (reader_.joinable()) reader_.join();
    return;
  }
  proc_.closeStdin();
  if (proc_.wait(graceMs) < 0) {
    proc_.terminate();
    if (proc_.wait(300) < 0) proc_.kill();
    proc_.wait(300);
  }
  if (reader_.joinable()) reader_.join();
  failPending("connection closed");
}

void JsonRpcConnection::failPending(const char* why) {
  std::map<int, ResponseHandler> pending;
  {
    std::lock_guard<std::mutex> lk(mu_);
    pending.swap(pending_);
  }
  RpcError err{-32099, why, {}};
  for (auto& [id, h] : pending) h(nullptr, &err);
}

bool JsonRpcConnection::alive() const { return running_ && proc_.running(); }

void JsonRpcConnection::send(const Json& msg) {
  std::string body = msg.dump();
  std::string frame = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  if (trace_) trace_("->", msg);
  std::lock_guard<std::mutex> lk(writeMu_);
  proc_.write(frame.data(), frame.size());
}

int JsonRpcConnection::request(const std::string& method, Json params, ResponseHandler handler) {
  int id = nextId_++;
  {
    std::lock_guard<std::mutex> lk(mu_);
    pending_[id] = std::move(handler);
  }
  if (!running_) {
    RpcError err{-32099, "not connected", {}};
    ResponseHandler h;
    {
      std::lock_guard<std::mutex> lk(mu_);
      h = std::move(pending_[id]);
      pending_.erase(id);
    }
    if (h) h(nullptr, &err);
    return id;
  }
  send(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}});
  return id;
}

void JsonRpcConnection::notify(const std::string& method, Json params) {
  if (!running_) return;
  send(Json{{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}});
}

void JsonRpcConnection::cancel(int id) { notify("$/cancelRequest", Json{{"id", id}}); }

void JsonRpcConnection::onNotification(const std::string& method, NotificationHandler h) {
  std::lock_guard<std::mutex> lk(mu_);
  notifHandlers_[method] = std::move(h);
}

void JsonRpcConnection::onRequest(const std::string& method, RequestHandler h) {
  std::lock_guard<std::mutex> lk(mu_);
  reqHandlers_[method] = std::move(h);
}

void JsonRpcConnection::processBuffer(std::string& buf) {
  // Parse as many complete frames as we have.
  while (true) {
    size_t hdrEnd = buf.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) break;
    size_t clPos = buf.find("Content-Length:");
    if (clPos == std::string::npos || clPos > hdrEnd) {
      buf.erase(0, hdrEnd + 4);
      continue;
    }
    // The length must be plain digits (strtoul accepted "-5" as 2^64-5, and a value near 2^64 made bodyStart + len
    // wrap around) and is capped: a larger claim is a garbled header, not a message to wait for.
    const char* digits = buf.c_str() + clPos + 15;
    while (*digits == ' ' || *digits == '\t') ++digits;
    if (*digits < '0' || *digits > '9') {
      buf.erase(0, hdrEnd + 4);
      continue;
    }
    errno = 0;
    const unsigned long long claimed = std::strtoull(digits, nullptr, 10);
    if (errno == ERANGE || claimed > kMaxFrameBytes) {
      buf.erase(0, hdrEnd + 4);
      continue;
    }
    const size_t len = static_cast<size_t>(claimed);
    size_t bodyStart = hdrEnd + 4;
    if (buf.size() < bodyStart + len) break;
    std::string body = buf.substr(bodyStart, len);
    buf.erase(0, bodyStart + len);
    Json msg = Json::parse(body, nullptr, false);
    if (msg.is_discarded()) continue;
    if (trace_) trace_("<-", msg);
    try {
      dispatch(std::move(msg));
    } catch (const std::exception& e) {
      // A handler choked on a message of the wrong shape (the LspClient converters use value() / get<>(), which throw
      // on a wrong type). The reader thread must survive it: an exception leaving it terminates the process.
      if (trace_) trace_("!!", Json{{"exception", e.what()}});
    }
  }
}

void JsonRpcConnection::readerLoop() {
  std::string buf;
  char chunk[65536];
  while (running_) {
    processBuffer(buf);
    long n = proc_.read(chunk, sizeof(chunk));
    if (n <= 0) break;
    buf.append(chunk, static_cast<size_t>(n));
  }
  bool wasRunning = running_.exchange(false);
  // The server is gone: nothing will ever answer the requests still in flight, so fail them now (a request issued
  // after this point sees running_ == false and fails itself).
  failPending("connection lost");
  if (wasRunning && disconnect_) disconnect_();
}

void JsonRpcConnection::dispatch(Json msg) {
  if (!msg.is_object()) return;
  const bool hasId = msg.contains("id") && !msg["id"].is_null();
  if (msg.contains("method")) {
    // A peer's message must not be able to throw out of the reader thread (that terminates the process): a method
    // that is not a string, or an error object of the wrong shape, is dropped / read leniently.
    if (!msg["method"].is_string()) return;
    std::string method = msg["method"].get<std::string>();
    Json params = msg.contains("params") ? msg["params"] : Json::object();
    if (hasId) {
      RequestHandler h;
      {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = reqHandlers_.find(method);
        if (it != reqHandlers_.end()) h = it->second;
      }
      Json reply{{"jsonrpc", "2.0"}, {"id", msg["id"]}};
      if (!h) {
        reply["error"] = Json{{"code", -32601}, {"message", "method not found: " + method}};
      } else {
        try {
          reply["result"] = h(params);
        } catch (const RpcError& e) {
          reply["error"] = Json{{"code", e.code}, {"message", e.message}};
        } catch (const std::exception& e) { reply["error"] = Json{{"code", -32603}, {"message", e.what()}}; }
      }
      send(reply);
    } else {
      NotificationHandler h;
      {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = notifHandlers_.find(method);
        if (it != notifHandlers_.end()) h = it->second;
      }
      if (h) h(params);
    }
    return;
  }
  if (hasId && msg["id"].is_number_integer()) {
    int id = msg["id"].get<int>();
    ResponseHandler h;
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto it = pending_.find(id);
      if (it != pending_.end()) {
        h = std::move(it->second);
        pending_.erase(it);
      }
    }
    if (!h) return;
    if (msg.contains("error") && !msg["error"].is_null()) {
      RpcError err;
      const Json& e = msg["error"];
      if (e.is_object()) {
        if (e.contains("code") && e["code"].is_number_integer()) err.code = e["code"].get<int>();
        if (e.contains("message") && e["message"].is_string()) err.message = e["message"].get<std::string>();
        if (e.contains("data")) err.data = e["data"];
      } else if (e.is_string()) {
        err.message = e.get<std::string>();
      }
      h(nullptr, &err);
    } else {
      Json result = msg.contains("result") ? msg["result"] : Json();
      h(&result, nullptr);
    }
  }
}

}  // namespace tf
