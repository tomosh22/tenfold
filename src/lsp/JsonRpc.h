#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
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

  // The reader thread's framing step: consumes every complete Content-Length frame at the front of `buf` and
  // dispatches it; a partial frame stays in `buf`. Public so that the fuzz harness (tests/fuzz) can feed garbled
  // streams without a child process.
  void processBuffer(std::string& buf);

 private:
  void readerLoop();
  void dispatch(Json msg);
  void send(const Json& msg);
  void writerLoop();
  void stopWriter();  // drops what is still queued and joins the writer thread
  void failPending(const char* why);

  Process proc_;
  std::thread reader_;
  std::atomic<bool> running_{false};
  // Outgoing frames. send() only queues; the writer thread does the (blocking) pipe write, so a server that is busy
  // and not reading its stdin (clangd indexing 7k files) never blocks the UI thread, nor the reader thread when it
  // answers the server's requests. Order is the order of send().
  std::thread writer_;
  std::mutex writeMu_;
  std::condition_variable writeCv_;
  std::deque<std::string> out_;
  size_t outBytes_ = 0;
  bool writing_ = false, writerStop_ = false, warnedBacklog_ = false;
  std::mutex mu_;
  std::atomic<int> nextId_{1};
  std::map<int, ResponseHandler> pending_;
  std::map<std::string, NotificationHandler> notifHandlers_;
  std::map<std::string, RequestHandler> reqHandlers_;
  std::function<void()> disconnect_;
  std::function<void(const std::string&, const Json&)> trace_;
};

}  // namespace tf
