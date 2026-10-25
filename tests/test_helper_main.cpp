// Helper executable for the crash-handling tests (built by CMake as tenfold-test-helper, not part of the suite).
//
//   tenfold-test-helper crash <dir> segv|abort|fpe|ill
//       Installs the crash handler with <dir> and then crashes.
//   tenfold-test-helper --fake-clangd [options]    (the engine may put its own clangd flags before or after)
//       A minimal language server that crashes on demand. It stores every open document, answers
//       textDocument/hover with "v<version>:<text of the requested line>" (so a test can tell which text the server
//       holds), and logs what it receives. Options:
//         --launch-file=F        counts launches in F (needed by the *-launches options)
//         --log=F                append "launch N", "open <uri> v<version>" and "request <method>" lines
//         --pid-file=F           write the process id (to kill a hung server from a test)
//         --die-after-requests=N after the Nth non-lifecycle request is received, die without answering it
//         --die-mode=exit|segv   how (default exit)
//         --die-launches=K       only the first K launches die (default: all)
//         --die-on-init-launches=K  the first K launches die right after answering initialize
//         --hang-method=M        never answer requests of method M
//         --init-delay-ms=N      sleep before answering initialize
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <sys/resource.h>
#endif

#include "core/CrashHandler.h"
#include "core/Platform.h"
#include "lsp/JsonRpc.h"

using tf::Json;

namespace {

int runCrash(int argc, char** argv) {
  if (argc < 4) return 2;
  if (!tf::installCrashHandler(argv[2])) return 3;
  std::string how = argv[3];
  if (how == "abort") std::abort();
  if (how == "fpe") std::raise(SIGFPE);
  if (how == "ill") std::raise(SIGILL);
  volatile int* p = nullptr;
  *p = 1;
  return 0;
}

struct Doc {
  int version = 0;
  std::string text;
};

std::string lineOf(const std::string& text, int line) {
  size_t pos = 0;
  for (int i = 0; i < line; ++i) {
    pos = text.find('\n', pos);
    if (pos == std::string::npos) return {};
    ++pos;
  }
  size_t end = text.find('\n', pos);
  return text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

size_t offsetOf(const std::string& text, int line, int col) {
  size_t pos = 0;
  for (int i = 0; i < line; ++i) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) return text.size();
    pos = nl + 1;
  }
  return std::min(text.size(), pos + static_cast<size_t>(col));
}

void send(const Json& msg) {
  std::string body = msg.dump();
  std::string frame = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  std::fwrite(frame.data(), 1, frame.size(), stdout);
  std::fflush(stdout);
}

[[noreturn]] void die(const std::string& mode) {
  if (mode == "segv") {
#ifndef _WIN32
    struct rlimit rl = {0, 0};
    ::setrlimit(RLIMIT_CORE, &rl);  // no core files from the tests
#endif
    volatile int* p = nullptr;
    *p = 1;
  }
  std::fflush(stdout);
  std::_Exit(1);
}

int runFake(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  std::map<std::string, std::string> opt;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--", 0) != 0) continue;
    size_t eq = a.find('=');
    if (eq == std::string::npos) opt[a.substr(2)] = "";
    else opt[a.substr(2, eq - 2)] = a.substr(eq + 1);
  }
  auto num = [&](const char* k, int def) { return opt.count(k) ? std::atoi(opt[k].c_str()) : def; };
  auto logLine = [&](const std::string& s) {
    if (!opt.count("log")) return;
    std::ofstream(opt["log"], std::ios::app) << s << "\n";
  };
  int launch = 1;
  if (opt.count("launch-file")) {
    {
      std::ifstream in(opt["launch-file"]);
      int n = 0;
      if (in >> n) launch = n + 1;
    }
    std::ofstream(opt["launch-file"], std::ios::trunc) << launch;
  }
  if (opt.count("pid-file")) std::ofstream(opt["pid-file"], std::ios::trunc) << tf::currentProcessId();
  logLine("launch " + std::to_string(launch) + " t=" +
          std::to_string(
              std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                  .count()));
  const std::string dieMode = opt.count("die-mode") ? opt["die-mode"] : "exit";
  const bool dying =
      opt.count("die-after-requests") && (!opt.count("die-launches") || launch <= num("die-launches", 0));
  const bool dieOnInit = launch <= num("die-on-init-launches", 0);

  std::map<std::string, Doc> docs;
  int requests = 0;
  std::string buf;
  char chunk[4096];
  while (true) {
    size_t hdrEnd = buf.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) {
      size_t n = std::fread(chunk, 1, 1, stdin);  // byte-wise: never blocks past a complete frame
      if (n == 0) return 0;
      buf.append(chunk, n);
      continue;
    }
    size_t clPos = buf.find("Content-Length:");
    size_t len = clPos == std::string::npos ? 0 : std::strtoul(buf.c_str() + clPos + 15, nullptr, 10);
    size_t need = hdrEnd + 4 + len;
    while (buf.size() < need) {
      size_t n = std::fread(chunk, 1, std::min(sizeof chunk, need - buf.size()), stdin);
      if (n == 0) return 0;
      buf.append(chunk, n);
    }
    Json msg = Json::parse(buf.substr(hdrEnd + 4, len), nullptr, false);
    buf.erase(0, need);
    if (!msg.is_object() || !msg.contains("method")) continue;
    const std::string method = msg["method"].get<std::string>();
    const Json params = msg.value("params", Json::object());
    const bool isRequest = msg.contains("id");
    if (method == "initialize") {
      if (int ms = num("init-delay-ms", 0)) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
      send({{"jsonrpc", "2.0"},
            {"id", msg["id"]},
            {"result",
             {{"capabilities", {{"textDocumentSync", 2}, {"hoverProvider", true}}},
              {"offsetEncoding", "utf-8"},
              {"serverInfo", {{"name", "fake-clangd"}, {"version", std::to_string(launch)}}}}}});
      if (dieOnInit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        die(dieMode);
      }
    } else if (method == "shutdown") {
      send({{"jsonrpc", "2.0"}, {"id", msg["id"]}, {"result", nullptr}});
    } else if (method == "exit") {
      return 0;
    } else if (method == "textDocument/didOpen") {
      const Json& td = params["textDocument"];
      Doc& d = docs[td["uri"].get<std::string>()];
      d.version = td.value("version", 0);
      d.text = td.value("text", "");
      logLine("open " + td["uri"].get<std::string>() + " v" + std::to_string(d.version));
    } else if (method == "textDocument/didChange") {
      Doc& d = docs[params["textDocument"]["uri"].get<std::string>()];
      d.version = params["textDocument"].value("version", d.version);
      for (auto& c : params["contentChanges"]) {
        if (c.contains("range")) {
          size_t a = offsetOf(d.text, c["range"]["start"]["line"], c["range"]["start"]["character"]);
          size_t b = offsetOf(d.text, c["range"]["end"]["line"], c["range"]["end"]["character"]);
          d.text.replace(a, b - a, c.value("text", ""));
        } else {
          d.text = c.value("text", "");
        }
      }
    } else if (isRequest) {
      logLine("request " + method);
      ++requests;
      if (dying && requests >= num("die-after-requests", 1)) die(dieMode);
      if (opt.count("hang-method") && opt["hang-method"] == method) continue;
      if (method == "textDocument/hover") {
        auto it = docs.find(params["textDocument"]["uri"].get<std::string>());
        std::string value = "unknown document";
        if (it != docs.end())
          value = "v" + std::to_string(it->second.version) + ":" + lineOf(it->second.text, params["position"]["line"]);
        send({{"jsonrpc", "2.0"},
              {"id", msg["id"]},
              {"result", {{"contents", {{"kind", "plaintext"}, {"value", value}}}}}});
      } else {
        send({{"jsonrpc", "2.0"}, {"id", msg["id"]}, {"result", nullptr}});
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--fake-clangd") == 0) return runFake(argc, argv);
  if (argc >= 2 && std::strcmp(argv[1], "crash") == 0) return runCrash(argc, argv);
  std::fprintf(stderr, "usage: tenfold-test-helper crash <dir> segv|abort|fpe|ill | --fake-clangd [options]\n");
  return 2;
}
