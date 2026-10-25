#include "lsp/JsonRpc.h"

#include <cstring>
#include <vector>

namespace tf {

JsonRpcConnection::~JsonRpcConnection() { stop(200); }

bool JsonRpcConnection::start(const std::string& exe, const std::vector<std::string>& args,
                              const std::filesystem::path& cwd, const std::filesystem::path& stderrLog,
                              std::string* error, const Process::Env& env) {
  if (!proc_.start(exe, args, cwd, stderrLog, error, env)) return false;
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

void JsonRpcConnection::readerLoop() {
  std::string buf;
  char chunk[65536];
  while (running_) {
    // Parse as many complete frames as we have.
    while (true) {
      size_t hdrEnd = buf.find("\r\n\r\n");
      if (hdrEnd == std::string::npos) break;
      size_t clPos = buf.find("Content-Length:");
      if (clPos == std::string::npos || clPos > hdrEnd) {
        buf.erase(0, hdrEnd + 4);
        continue;
      }
      size_t len = std::strtoul(buf.c_str() + clPos + 15, nullptr, 10);
      size_t bodyStart = hdrEnd + 4;
      if (buf.size() < bodyStart + len) break;
      std::string body = buf.substr(bodyStart, len);
      buf.erase(0, bodyStart + len);
      Json msg = Json::parse(body, nullptr, false);
      if (msg.is_discarded()) continue;
      if (trace_) trace_("<-", msg);
      dispatch(std::move(msg));
    }
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
      err.code = msg["error"].value("code", 0);
      err.message = msg["error"].value("message", std::string());
      if (msg["error"].contains("data")) err.data = msg["error"]["data"];
      h(nullptr, &err);
    } else {
      Json result = msg.contains("result") ? msg["result"] : Json();
      h(&result, nullptr);
    }
  }
}

}  // namespace tf
