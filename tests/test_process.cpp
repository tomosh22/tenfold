#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lsp/JsonRpc.h"
#include "lsp/Process.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {
std::string readAll(Process& p) {
  std::string out;
  char buf[256];
  while (true) {
    long n = p.read(buf, sizeof buf);
    if (n <= 0) break;
    out.append(buf, static_cast<size_t>(n));
  }
  return out;
}
bool contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }
}  // namespace

TEST(process_echo) {
  Process p;
  std::string err;
#ifdef _WIN32
  REQUIRE(p.start("cmd", {"/c", "echo hi"}, {}, {}, &err));
#else
  REQUIRE(p.start("/bin/sh", {"-c", "echo hi"}, {}, {}, &err));
#endif
  std::string out = readAll(p);
  CHECK(contains(out, "hi"));
  CHECK_EQ(p.wait(5000), 0);
  CHECK(!p.running());
}

TEST(process_stdin_roundtrip) {
  Process p;
  std::string err;
#ifdef _WIN32
  REQUIRE(p.start("findstr", {"hello"}, {}, {}, &err));
#else
  REQUIRE(p.start("cat", {}, {}, {}, &err));
#endif
  const std::string msg = "hello world\r\n";
  REQUIRE(p.write(msg.data(), msg.size()));
  p.closeStdin();
  std::string out = readAll(p);
  CHECK(contains(out, "hello world"));
  CHECK_EQ(p.wait(5000), 0);
}

TEST(process_extra_env) {
  Process p;
  std::string err;
  Process::Env env = {{"TENFOLD_TEST_VAR", "forty two"}};
#ifdef _WIN32
  REQUIRE(p.start("cmd", {"/c", "set TENFOLD_TEST_VAR"}, {}, {}, &err, env));
#else
  REQUIRE(p.start("/bin/sh", {"-c", "echo $TENFOLD_TEST_VAR"}, {}, {}, &err, env));
#endif
  std::string out = readAll(p);
  CHECK(contains(out, "forty two"));
  p.wait(5000);
}

TEST(process_terminate_sleeping_child) {
  Process p;
  std::string err;
#ifdef _WIN32
  REQUIRE(p.start("ping", {"-n", "30", "127.0.0.1"}, {}, {}, &err));
#else
  REQUIRE(p.start("sleep", {"30"}, {}, {}, &err));
#endif
  CHECK(p.running());
  auto t0 = std::chrono::steady_clock::now();
  p.terminate();
  p.wait(5000);
  CHECK(!p.running());
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5));
}

TEST(process_start_failure) {
  Process p;
  std::string err;
  CHECK(!p.start("tenfold-no-such-executable-xyz", {}, {}, {}, &err) || p.wait(2000) != 0);
}

TEST(process_merge_stderr) {
  Process p;
  std::string err;
  ProcessOptions po;
  po.mergeStderr = true;
#ifdef _WIN32
  REQUIRE(p.start("cmd", {"/c", "echo out1& echo err1 1>&2& echo out2"}, {}, {}, &err, {}, po));
#else
  REQUIRE(p.start("/bin/sh", {"-c", "echo out1; echo err1 >&2; echo out2"}, {}, {}, &err, {}, po));
#endif
  std::string out = readAll(p);
  size_t a = out.find("out1"), b = out.find("err1"), c = out.find("out2");
  CHECK(a != std::string::npos && b != std::string::npos && c != std::string::npos);
  CHECK(a < b && b < c);  // one interleaved stream, in write order
  CHECK_EQ(p.wait(5000), 0);
}

TEST(process_stderr_not_merged_by_default) {
  Process p;
  std::string err;
#ifdef _WIN32
  REQUIRE(p.start("cmd", {"/c", "echo out1& echo err1 1>&2"}, {}, {}, &err));
#else
  REQUIRE(p.start("/bin/sh", {"-c", "echo out1; echo err1 >&2"}, {}, {}, &err));
#endif
  std::string out = readAll(p);
  CHECK(contains(out, "out1"));
  CHECK(!contains(out, "err1"));
  p.wait(5000);
}

TEST(process_tree_kill_takes_grandchild) {
  Process p;
  std::string err;
  ProcessOptions po;
  po.processTree = true;
#ifdef _WIN32
  REQUIRE(p.start("cmd", {"/c", "echo started& ping -n 120 127.0.0.1 >nul"}, {}, {}, &err, {}, po));
#else
  REQUIRE(p.start("/bin/sh", {"-c", "sleep 120 & echo started; wait"}, {}, {}, &err, {}, po));
#endif
  char buf[64];
  long n = p.read(buf, sizeof buf);  // "started": the grandchild exists now
  REQUIRE(n > 0);
  auto t0 = std::chrono::steady_clock::now();
  p.kill();
  // The grandchild holds the pipe's write end: EOF arrives only if it died with its parent.
  readAll(p);
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
  p.wait(5000);
  CHECK(!p.running());
}

// A child started on one thread must not inherit the pipes of a process started concurrently on another: if it did,
// the long-lived child would keep the short-lived one's stdout open and the reader would never see EOF.
TEST(process_concurrent_spawn_does_not_leak_pipes) {
  std::atomic<bool> stopSpawner{false};
  std::mutex mu;
  std::vector<std::unique_ptr<Process>> longLived;
  std::thread spawner([&] {
    for (int i = 0; i < 150 && !stopSpawner; ++i) {
      auto p = std::make_unique<Process>();
      std::string err;
#ifdef _WIN32
      bool ok = p->start("ping", {"-n", "60", "127.0.0.1"}, {}, {}, &err);
#else
      bool ok = p->start("sleep", {"60"}, {}, {}, &err);
#endif
      if (!ok) continue;
      std::lock_guard<std::mutex> lk(mu);
      longLived.push_back(std::move(p));
    }
  });
  std::atomic<int> done{0}, wrong{0};
  std::thread reader([&] {
    for (int i = 0; i < 60; ++i) {
      Process p;
      std::string err;
#ifdef _WIN32
      bool ok = p.start("cmd", {"/c", "echo hi"}, {}, {}, &err);
#else
      bool ok = p.start("/bin/sh", {"-c", "echo hi"}, {}, {}, &err);
#endif
      if (!ok) {
        ++wrong;
        continue;
      }
      if (!contains(readAll(p), "hi")) ++wrong;  // returns only at EOF
      p.wait(5000);
      ++done;
    }
  });
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (done + wrong < 60 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  CHECK_EQ(done.load(), 60);
  CHECK_EQ(wrong.load(), 0);
  stopSpawner = true;
  spawner.join();
  {
    // Releasing the long-lived children also unblocks a reader stuck by a leak, so the join below cannot hang.
    std::lock_guard<std::mutex> lk(mu);
    for (auto& p : longLived) p->kill();
  }
  reader.join();
  std::lock_guard<std::mutex> lk(mu);
  longLived.clear();
}

#ifndef _WIN32
// W9i: a language server that does not read its input (clangd busy indexing 7k files) must not block whoever sends:
// the pipe holds 64 KiB and a write past that used to block the UI thread inside `didClose` for 30 to 47 s (300 tabs
// closing while clangd indexed). Messages are queued and written by a thread; stop() still ends the process.
TEST(jsonrpc_send_does_not_block_on_a_server_that_does_not_read) {
  JsonRpcConnection c;
  std::string err;
  REQUIRE(c.start("/bin/sleep", {"60"}, {}, {}, &err));
  const std::string big(1 << 20, 'x');
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 12; ++i) c.notify("textDocument/didOpen", Json{{"text", big}});  // 12 MiB, far past the pipe
  const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(ms < 5000);  // the sends only queued (they took 0 to 60 ms of encoding here; a blocked write takes for ever)
  const auto t1 = std::chrono::steady_clock::now();
  c.stop(200);  // the writer is stuck in the pipe write: the process is terminated, the write fails, the thread ends
  CHECK(std::chrono::steady_clock::now() - t1 < std::chrono::seconds(10));
  CHECK(!c.alive());
}

TEST(jsonrpc_queued_messages_reach_a_reading_server_in_order_before_stop_closes_the_pipe) {
  JsonRpcConnection c;
  std::string err;
  std::string got;
  REQUIRE(c.start("/bin/cat", {}, {}, {}, &err));  // echoes the frames back: they arrive as messages from the "server"
  std::mutex mu;
  std::vector<int> seen;
  for (const char* m : {"a", "b", "c"})
    c.onNotification(m, [&](const Json& p) {
      std::lock_guard<std::mutex> lk(mu);
      seen.push_back(p.value("n", -1));
    });
  for (int i = 0; i < 300; ++i) c.notify(std::string(1, "abc"[i % 3]), Json{{"n", i}});
  for (int i = 0; i < 400; ++i) {
    {
      std::lock_guard<std::mutex> lk(mu);
      if (seen.size() == 300) break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::lock_guard<std::mutex> lk(mu);
  REQUIRE(seen.size() == 300);
  for (int i = 0; i < 300; ++i) CHECK_EQ(seen[static_cast<size_t>(i)], i);
  c.stop(500);
}
#endif

#ifdef __linux__
// Process::start uses posix_spawn where it can (a fork() of a process with a big heap costs milliseconds and stalls the
// other threads, COMPL-PERF); these pin what the child must still get exactly as with fork/exec.
TEST(process_spawn_child_gets_cwd_env_default_sigpipe_and_group) {
  const fs::path dir = fs::temp_directory_path() / "tf_proc_spawn_cwd";
  fs::create_directories(dir);
  {
    Process p;
    std::string err;
    Process::Env env = {{"TENFOLD_SPAWN_A", "one"}, {"HOME", "/tenfold/home"}};  // one new, one overriding
    REQUIRE(p.start("/bin/sh", {"-c", "pwd; echo $TENFOLD_SPAWN_A $HOME; grep SigIgn /proc/$$/status"}, dir, {}, &err,
                    env));
    const std::string out = readAll(p);
    CHECK(contains(out, fs::canonical(dir).string().c_str()));
    CHECK(contains(out, "one /tenfold/home"));
    const size_t at = out.find("SigIgn:");
    REQUIRE(at != std::string::npos);
    const unsigned long long ign = std::stoull(out.substr(at + 7), nullptr, 16);
    CHECK_EQ(ign & (1ull << 12), 0ull);  // SIGPIPE (13) is back to the default in the child
    p.wait(5000);
  }
  {
    Process p;
    std::string err;
    ProcessOptions po;
    po.processTree = true;
    REQUIRE(p.start("/bin/sh", {"-c", "echo $$; ps -o pgid= -p $$"}, {}, {}, &err, {}, po));
    const std::string out = readAll(p);
    const long pid = std::stol(out);
    const size_t nl = out.find('\n');
    REQUIRE(nl != std::string::npos);
    CHECK_EQ(std::stol(out.substr(nl + 1)), pid);  // its own process group
    p.wait(5000);
  }
  {
    Process p;
    std::string err;
    // A program that cannot run still reports exit code 127 through the fork fallback, as before.
    if (p.start("tenfold-no-such-executable-xyz", {}, {}, {}, &err)) {
      readAll(p);
      CHECK_EQ(p.wait(5000), 127);
    }
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(process_start_cost_does_not_grow_with_the_heap) {
  // 400 MB of touched heap: fork() copies its page tables (~10 ms of CPU and a stall of every other thread);
  // posix_spawn does not. The budget is on this thread's CPU time, so a loaded machine does not matter.
  std::vector<char> heap(400u << 20);
  for (size_t i = 0; i < heap.size(); i += 4096) heap[i] = 1;
  auto cpuMs = [] {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
  };
  double worst = 0;
  for (int i = 0; i < 5; ++i) {
    Process p;
    std::string err;
    const double t0 = cpuMs();
    REQUIRE(p.start("/bin/sh", {"-c", "exit 0"}, {}, {}, &err));
    worst = std::max(worst, cpuMs() - t0);
    readAll(p);
    p.wait(5000);
  }
  std::cerr << "    PERF: Process::start with a 400 MB heap: worst " << worst << " ms CPU (budget 2 ms)\n";
  PERF_CHECK(worst < 2.0, "Process::start with a 400 MB heap (2 ms CPU)");
  CHECK(heap[4096] == 1);
}
#endif
