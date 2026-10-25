#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "lsp/Process.h"

namespace tf {

using Json = nlohmann::json;

struct RpcError {
  int code = 0;
  std::string message;
  Json data;
};

// JSON-RPC 2.0 over Content-Length framed stdio (the LSP base protocol).
// Owns the child process and a reader thread. All handlers run on the
// reader thread; callers must marshal to their own thread if needed.
class JsonRpcConnection {
 public:
  using ResponseHandler = std::function<void(const Json* result, const RpcError* error)>;
  using NotificationHandler = std::function<void(const Json& params)>;
  using RequestHandler = std::function<Json(const Json& params)>;  // may throw RpcError

  JsonRpcConnection() = default;
  ~JsonRpcConnection();

  bool start(const std::string& exe, const std::vector<std::string>& args, const std::filesystem::path& cwd,
             const std::filesystem::path& stderrLog, std::string* error, const Process::Env& env = {});
  void stop(int graceMs = 1500);
  bool alive() const;

  int request(const std::string& method, Json params, ResponseHandler handler);
  void notify(const std::string& method, Json params);
  void cancel(int id);

  void onNotification(const std::string& method, NotificationHandler h);
  void onRequest(const std::string& method, RequestHandler h);
  // Called when the connection dies (server exit / pipe closed).
  void onDisconnect(std::function<void()> h) { disconnect_ = std::move(h); }

  // Protocol-level trace (every message in/out) for debugging.
  void setTrace(std::function<void(const std::string& dir, const Json&)> t) { trace_ = std::move(t); }

 private:
  void readerLoop();
  void dispatch(Json msg);
  void send(const Json& msg);
  void failPending(const char* why);

  Process proc_;
  std::thread reader_;
  std::atomic<bool> running_{false};
  std::mutex writeMu_;
  std::mutex mu_;
  std::atomic<int> nextId_{1};
  std::map<int, ResponseHandler> pending_;
  std::map<std::string, NotificationHandler> notifHandlers_;
  std::map<std::string, RequestHandler> reqHandlers_;
  std::function<void()> disconnect_;
  std::function<void(const std::string&, const Json&)> trace_;
};

}  // namespace tf
