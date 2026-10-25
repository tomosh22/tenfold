#include <chrono>
#include <string>

#include "lsp/Process.h"
#include "test_main.h"

using namespace tf;

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
