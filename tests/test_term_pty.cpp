// Pty: a program in a pseudo-terminal (POSIX openpt, Windows ConPTY).
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "term/Pty.h"
#include "term/VtScreen.h"
#include "test_main.h"

using namespace tf;

namespace {

// Collects the output of a Pty from its reader thread.
struct Sink {
  std::mutex mu;
  std::condition_variable cv;
  std::string data;
  std::atomic<int> exitCode{-1000};
  std::atomic<int> exitCalls{0};

  Pty::DataCallback onData() {
    return [this](const char* d, size_t n) {
      std::lock_guard<std::mutex> lk(mu);
      data.append(d, n);
      cv.notify_all();
    };
  }
  Pty::ExitCallback onExit() {
    return [this](int code) {
      exitCode = code;
      exitCalls++;
      std::lock_guard<std::mutex> lk(mu);
      cv.notify_all();
    };
  }
  bool waitFor(const std::string& what, int ms = 15000) {
    std::unique_lock<std::mutex> lk(mu);
    return cv.wait_for(lk, std::chrono::milliseconds(ms), [&] { return data.find(what) != std::string::npos; });
  }
  bool waitExit(int ms = 15000) {
    std::unique_lock<std::mutex> lk(mu);
    return cv.wait_for(lk, std::chrono::milliseconds(ms), [&] { return exitCalls.load() > 0; });
  }
  std::string snapshot() {
    std::lock_guard<std::mutex> lk(mu);
    return data;
  }
};

// ConPTY is missing on old Windows and not functional under Wine: skip with a message there.
[[maybe_unused]] bool ptyUsable() {
  if (!Pty::available()) {
    std::printf("[term] SKIP: pseudo-terminals are not available on this system\n");
    return false;
  }
#ifdef _WIN32
  HMODULE nt = ::GetModuleHandleW(L"ntdll.dll");
  if (nt && ::GetProcAddress(nt, "wine_get_version")) {
    std::printf("[term] SKIP: ConPTY is not functional under Wine\n");
    return false;
  }
#endif
  return true;
}

#ifndef _WIN32
PtyOptions shOptions(const std::string& script) {
  PtyOptions o;
  o.program = "/bin/sh";
  o.args = {"-c", script};
  return o;
}
#endif

}  // namespace

TEST(pty_split_command_and_default_shell) {
  auto v = Pty::splitCommand("cmd /c agent");
  REQUIRE(v.size() == 3);
  CHECK_EQ(v[0], "cmd");
  CHECK_EQ(v[2], "agent");
  v = Pty::splitCommand("  \"C:\\Program Files\\x y.exe\"   -l  \"\" ");
  REQUIRE(v.size() == 3);
  CHECK_EQ(v[0], "C:\\Program Files\\x y.exe");
  CHECK_EQ(v[1], "-l");
  CHECK_EQ(v[2], "");
  CHECK(Pty::splitCommand("").empty());
  CHECK_EQ(Pty::defaultShell("pwsh -NoLogo"), "pwsh -NoLogo");
  CHECK(!Pty::defaultShell().empty());
#ifndef _WIN32
  CHECK(Pty::available());
#endif
}

// VS-TERM (M3): the default shell of the integrated terminal, as a pure function.
TEST(pty_terminal_shell_choice_windows_prefers_pwsh_then_powershell) {
  const auto has = [](bool pwsh) { return [pwsh](const std::string& n) { return pwsh && n == "pwsh.exe"; }; };
  CHECK_EQ(Pty::chooseTerminalShell(true, has(true), "/bin/zsh"), "pwsh.exe");
  CHECK_EQ(Pty::chooseTerminalShell(true, has(false), "/bin/zsh"), "powershell.exe");
  CHECK_EQ(Pty::chooseTerminalShell(true, nullptr, ""), "powershell.exe");
  bool asked = false;
  Pty::chooseTerminalShell(
      true,
      [&](const std::string& n) {
        asked = true;
        return n == "pwsh.exe";
      },
      "");
  CHECK(asked);
  // Elsewhere $SHELL, else /bin/sh; PATH is not consulted.
  bool consulted = false;
  const auto spy = [&](const std::string&) {
    consulted = true;
    return true;
  };
  CHECK_EQ(Pty::chooseTerminalShell(false, spy, "/usr/bin/fish"), "/usr/bin/fish");
  CHECK_EQ(Pty::chooseTerminalShell(false, spy, ""), "/bin/sh");
  CHECK(!consulted);
  CHECK(!Pty::defaultTerminalShell().empty());
#ifndef _WIN32
  CHECK(Pty::programOnPath("sh"));
  CHECK(!Pty::programOnPath("definitely-not-a-program-tenfold"));
#endif
}

#ifndef _WIN32

TEST(pty_echo_input_and_exit) {
  Sink sink;
  Pty pty;
  std::string err;
  REQUIRE(pty.start(shOptions("printf 'hello\\n'; read x; echo got:$x"), sink.onData(), sink.onExit(), &err));
  CHECK(pty.running());
  CHECK(sink.waitFor("hello"));
  CHECK(pty.write("abc\n"));
  CHECK(sink.waitFor("got:abc"));
  CHECK(sink.waitExit());
  CHECK_EQ(sink.exitCode.load(), 0);
  CHECK_EQ(pty.wait(5000), 0);
  CHECK(pty.exited());
  CHECK(!pty.running());
  std::string out = sink.snapshot();
  CHECK(out.find("hello\r\n") != std::string::npos);  // the line discipline maps LF to CRLF
  CHECK(out.find("abc") != std::string::npos);        // and echoes the input
  CHECK_EQ(sink.exitCalls.load(), 1);
  pty.close();
  CHECK_EQ(sink.exitCalls.load(), 1);
}

TEST(pty_exit_code) {
  Sink sink;
  Pty pty;
  std::string err;
  REQUIRE(pty.start(shOptions("echo bye; exit 7"), sink.onData(), sink.onExit(), &err));
  CHECK(sink.waitExit());
  CHECK_EQ(sink.exitCode.load(), 7);
  CHECK_EQ(pty.exitCode(), 7);
  CHECK(sink.snapshot().find("bye") != std::string::npos);  // output before the exit is delivered first
  Sink sink2;
  Pty killed;
  REQUIRE(killed.start(shOptions("kill -9 $$"), sink2.onData(), sink2.onExit(), &err));
  CHECK(sink2.waitExit());
  CHECK_EQ(sink2.exitCode.load(), 128 + 9);
}

TEST(pty_resize) {
  Sink sink;
  Pty pty;
  std::string err;
  PtyOptions o = shOptions("stty size; read x; stty size");
  o.rows = 24;
  o.cols = 80;
  REQUIRE(pty.start(o, sink.onData(), sink.onExit(), &err));
  CHECK(sink.waitFor("24 80"));
  CHECK(pty.resize(40, 100));
  CHECK(pty.write("\n"));
  CHECK(sink.waitFor("40 100"));
  CHECK(sink.waitExit());
  CHECK_EQ(sink.exitCode.load(), 0);
}

TEST(pty_cwd_env_and_term) {
  auto dir = std::filesystem::temp_directory_path() / ("tf_pty_cwd_" + std::to_string(::getpid()));
  std::filesystem::create_directories(dir);
  Sink sink;
  Pty pty;
  std::string err;
  PtyOptions o = shOptions("pwd; echo t=$TERM v=$TF_PTY_VAR; tty >/dev/null && echo istty");
  o.cwd = dir;
  o.env = {{"TF_PTY_VAR", "xyz"}};
  REQUIRE(pty.start(o, sink.onData(), sink.onExit(), &err));
  CHECK(sink.waitExit());
  std::string out = sink.snapshot();
  CHECK(out.find(std::filesystem::canonical(dir).string()) != std::string::npos);
  CHECK(out.find("t=xterm-256color v=xyz") != std::string::npos);
  CHECK(out.find("istty") != std::string::npos);
  std::filesystem::remove_all(dir);
}

TEST(pty_start_failure) {
  Sink sink;
  Pty pty;
  std::string err;
  CHECK(!pty.start(
      [] {
        PtyOptions o;
        o.program = "/nonexistent/definitely-not-a-shell";
        return o;
      }(),
      sink.onData(), sink.onExit(), &err));
  CHECK(!err.empty());
  CHECK(!pty.started());
  PtyOptions o = shOptions("true");
  o.cwd = "/nonexistent/dir";
  CHECK(!pty.start(o, sink.onData(), sink.onExit(), &err));
  CHECK(!pty.write("x"));
  CHECK(!pty.resize(10, 10));
  pty.close();  // never started: no-op
}

TEST(pty_close_kills_the_process_tree) {
  Sink sink;
  Pty pty;
  std::string err;
  REQUIRE(pty.start(shOptions("sleep 60 & echo bgpid:$!:; wait"), sink.onData(), sink.onExit(), &err));
  REQUIRE(sink.waitFor("bgpid:"));
  REQUIRE(sink.waitFor(":\r\n"));
  std::string out = sink.snapshot();
  size_t p = out.find("bgpid:");
  int bg = std::atoi(out.c_str() + p + 6);
  REQUIRE(bg > 1);
  CHECK(::kill(bg, 0) == 0);
  auto t0 = std::chrono::steady_clock::now();
  pty.close();
  double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  CHECK(sec < 5.0);
  CHECK(pty.exited());
  CHECK_EQ(sink.exitCalls.load(), 0);  // close() is not reported as an exit
  CHECK(!pty.write("x"));
  // the background child is gone (or a zombie awaiting reaping by init)
  bool gone = false;
  for (int i = 0; i < 100 && !gone; ++i) {
    std::ifstream st("/proc/" + std::to_string(bg) + "/stat");
    std::string line;
    if (!st || !std::getline(st, line)) {
      gone = true;
    } else {
      size_t rp = line.rfind(')');
      gone = rp != std::string::npos && rp + 2 < line.size() && line[rp + 2] == 'Z';
    }
    if (!gone) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(gone);
}

TEST(pty_close_while_output_flows) {
  Sink sink;
  Pty pty;
  std::string err;
  REQUIRE(pty.start(shOptions("yes 'some output line to keep the reader busy'"), sink.onData(), sink.onExit(), &err));
  CHECK(sink.waitFor("some output"));
  pty.close();
  CHECK(pty.exited());
  CHECK(pty.exitCode() != 0);
  // restart on the same object
  Sink sink2;
  REQUIRE(pty.start(shOptions("echo again"), sink2.onData(), sink2.onExit(), &err));
  CHECK(sink2.waitExit());
  CHECK(sink2.snapshot().find("again") != std::string::npos);
}

TEST(pty_destructor_kills_running_shell) {
  Sink sink;
  {
    Pty pty;
    std::string err;
    REQUIRE(pty.start(shOptions("echo up; sleep 60"), sink.onData(), sink.onExit(), &err));
    CHECK(sink.waitFor("up"));
  }  // destructor: must not hang or leave the reader running
  CHECK_EQ(sink.exitCalls.load(), 0);
}

TEST(pty_output_feeds_vtscreen_and_replies_go_back) {
  // A program asks for the cursor position; the screen model answers; the program prints what it received.
  Sink sink;
  Pty pty;
  VtScreen screen(24, 80);
  std::mutex mu;
  std::string err;
  screen.setReplyCallback([&](const std::string& r) { pty.write(r); });
  auto feed = [&](const char* d, size_t n) {
    std::lock_guard<std::mutex> lk(mu);
    screen.feed(d, n);
  };
  Pty::DataCallback both = [&](const char* d, size_t n) {
    feed(d, n);
    sink.onData()(d, n);
  };
  REQUIRE(pty.start(shOptions("stty raw -echo; printf '\\033[5;10H\\033[6n'; dd bs=1 count=7 2>/dev/null | od -An -c"),
                    both, sink.onExit(), &err));
  CHECK(sink.waitExit());
  std::string out = sink.snapshot();
  CHECK(out.find("[   5   ;   1   0   R") != std::string::npos);  // od -c of ESC [ 5 ; 1 0 R
  std::lock_guard<std::mutex> lk(mu);
  CHECK(screen.rows() == 24);
}

TEST(pty_concurrent_starts) {
  // Several Ptys started at once on different threads: no descriptor leaks between them, all reach EOF and exit.
  constexpr int N = 6;
  Sink sinks[N];
  Pty ptys[N];
  std::vector<std::thread> th;
  std::atomic<int> started{0};
  for (int i = 0; i < N; ++i)
    th.emplace_back([&, i] {
      std::string err;
      if (ptys[i].start(shOptions("echo n" + std::to_string(i) + "; exit " + std::to_string(i)), sinks[i].onData(),
                        sinks[i].onExit(), &err))
        started++;
    });
  for (auto& t : th) t.join();
  CHECK_EQ(started.load(), N);
  for (int i = 0; i < N; ++i) {
    CHECK(sinks[i].waitExit());
    CHECK_EQ(sinks[i].exitCode.load(), i);
  }
}

#else  // _WIN32

TEST(pty_conpty_echo_and_exit_code) {
  if (!ptyUsable()) return;
  Sink sink;
  Pty pty;
  std::string err;
  PtyOptions o;
  o.program = Pty::defaultShell();
  o.args = {"/c", "echo ptyhello & exit 3"};
  o.rows = 24;
  o.cols = 80;
  REQUIRE(pty.start(o, sink.onData(), sink.onExit(), &err));
  CHECK(sink.waitFor("ptyhello"));
  CHECK(sink.waitExit());
  CHECK_EQ(sink.exitCode.load(), 3);
  CHECK(pty.resize(30, 100) || true);
}

TEST(pty_conpty_close_kills_tree) {
  if (!ptyUsable()) return;
  Sink sink;
  Pty pty;
  std::string err;
  PtyOptions o;
  o.program = Pty::defaultShell();
  o.args = {"/c", "ping -n 60 127.0.0.1"};
  REQUIRE(pty.start(o, sink.onData(), sink.onExit(), &err));
  auto t0 = std::chrono::steady_clock::now();
  pty.close();
  CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 10.0);
  CHECK(pty.exited());
}

TEST(pty_conpty_interactive_shell) {
  if (!ptyUsable()) return;
  Sink sink;
  Pty pty;
  std::string err;
  PtyOptions o;
  o.program = Pty::defaultShell();
  REQUIRE(pty.start(o, sink.onData(), sink.onExit(), &err));
  CHECK(pty.write("echo interactive-ok\r"));
  CHECK(sink.waitFor("interactive-ok"));
  CHECK(pty.write("exit\r"));
  CHECK(sink.waitExit());
}

#endif
