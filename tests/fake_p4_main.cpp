// A fake `p4` for the Perforce provider tests (built by CMake as tenfold-fake-p4, not part of the suite).
//
// It emulates `-ztag` info / where / fstat / print / edit / revert / add over a JSON state file named by the
// environment variable FAKE_P4_STATE:
//
//   {
//     "clientRoot": "/tmp/ws",            local root of the client; depot path of "a/b.txt" is "//depot/a/b.txt"
//     "serverDown": false,                every command fails with "Connect to server failed"
//     "hangMs": 0,                        every command sleeps this long first (a hung server)
//     "notUnderClient": false,            `where` finds nothing (the directory is outside the client view)
//     "noClient": false,                  `info` reports clientRoot "null"
//     "files": {                          keys: path relative to clientRoot
//        "a.txt": { "headRev": 3, "haveRev": 3, "type": "text", "headAction": "edit",
//                   "action": "",          open action: edit add delete move/add move/delete integrate branch
//                   "movedFile": "",       depot path of the other half of a move
//                   "unresolved": false, "lockedBy": "", "content": "text at the have revision",
//                   "inDepot": true }
//     },
//     "log": []                            every invocation appends its arguments (joined by a space)
//   }
//
// `edit` makes the file writable with chmod/attrib semantics of std::filesystem and opens it, `revert` restores
// `content` and makes the file read-only again. A file with "lockedBy" refuses `edit` with p4's message (exit 0,
// like a warning) unless "editExit" is set, which is then the exit code (1 = hard error).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "lsp/JsonRpc.h"

using tf::Json;
namespace fs = std::filesystem;

namespace {

Json state;
fs::path statePath;
std::string out;
std::string errOut;

std::string unescape(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      std::string h = s.substr(i + 1, 2);
      if (h == "40") {
        o += '@';
        i += 2;
        continue;
      }
      if (h == "23") {
        o += '#';
        i += 2;
        continue;
      }
      if (h == "2A" || h == "2a") {
        o += '*';
        i += 2;
        continue;
      }
      if (h == "25") {
        o += '%';
        i += 2;
        continue;
      }
    }
    o += s[i];
  }
  return o;
}

std::string norm(std::string s) {
  for (char& c : s)
    if (c == '\\') c = '/';
  while (s.size() > 1 && s.back() == '/') s.pop_back();
  return s;
}

fs::path localOf(const std::string& rel) { return fs::path(state["clientRoot"].get<std::string>()) / rel; }

void setWritable(const fs::path& p, bool w) {
  std::error_code ec;
  if (w) fs::permissions(p, fs::perms::owner_write, fs::perm_options::add, ec);
  else
    fs::permissions(p, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::remove, ec);
}

void save() {
  std::ofstream o(statePath, std::ios::binary | std::ios::trunc);
  o << state.dump(2);
}

void tag(const std::string& k, const std::string& v) { out += "... " + k + " " + v + "\n"; }

// A path argument as (relative path, trailing "/..." seen); empty relative + outside=true when not under the root.
struct Arg {
  std::string rel;
  bool recursive = false;
  bool outside = false;
  bool depot = false;
  std::string orig;
};

Arg parseArg(const std::string& raw) {
  Arg a;
  a.orig = raw;
  std::string s = norm(unescape(raw));
  if (s.size() >= 4 && s.compare(s.size() - 4, 4, "/...") == 0) {
    a.recursive = true;
    s.resize(s.size() - 4);
  }
  if (s.compare(0, 8, "//depot/") == 0 || s == "//depot") {
    a.depot = true;
    a.rel = s.size() > 8 ? s.substr(8) : "";
    return a;
  }
  std::string root = norm(state["clientRoot"].get<std::string>());
  if (s == root) {
    a.rel = "";
  } else if (s.size() > root.size() && s.compare(0, root.size(), root) == 0 && s[root.size()] == '/') {
    a.rel = s.substr(root.size() + 1);
  } else {
    a.outside = true;
  }
  return a;
}

std::vector<std::string> matching(const Arg& a) {
  std::vector<std::string> res;
  for (auto it = state["files"].begin(); it != state["files"].end(); ++it) {
    const std::string& k = it.key();
    if (a.recursive ? (a.rel.empty() ||
                       (k.size() > a.rel.size() && k.compare(0, a.rel.size(), a.rel) == 0 && k[a.rel.size()] == '/'))
                    : k == a.rel)
      res.push_back(k);
  }
  return res;
}

bool fieldWanted(const std::vector<std::string>& want, const std::string& f) {
  if (want.empty()) return true;
  for (auto& w : want)
    if (w == f) return true;
  return false;
}

std::vector<std::string> split(const std::string& s, char c) {
  std::vector<std::string> v;
  std::stringstream ss(s);
  std::string t;
  while (std::getline(ss, t, c)) v.push_back(t);
  return v;
}

void emitFstat(const std::string& rel, const Json& f, const std::vector<std::string>& want) {
  auto put = [&](const std::string& k, const std::string& v) {
    if (fieldWanted(want, k)) tag(k, v);
  };
  const bool inDepot = f.value("inDepot", true);
  if (inDepot) put("depotFile", "//depot/" + rel);
  put("clientFile", norm(localOf(rel).string()));
  if (f.value("isMapped", true)) put("isMapped", "");
  if (inDepot) {
    put("headRev", std::to_string(f.value("headRev", 1)));
    put("headAction", f.value("headAction", "edit"));
    put("headType", f.value("type", "text"));
    if (f.value("haveRev", 0) > 0) put("haveRev", std::to_string(f.value("haveRev", 0)));
  }
  const std::string act = f.value("action", "");
  if (!act.empty()) {
    put("action", act);
    put("type", f.value("type", "text"));
  }
  if (!f.value("movedFile", "").empty()) put("movedFile", f.value("movedFile", ""));
  if (f.value("unresolved", false)) put("unresolved", "");
  out += "\n";
}

int cmdFstat(const std::vector<std::string>& a) {
  std::vector<std::string> want;
  bool onlyOpened = false;
  std::vector<std::string> paths;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] == "-T" && i + 1 < a.size()) want = split(a[++i], ',');
    else if (a[i] == "-Ro") onlyOpened = true;
    else if (!a[i].empty() && a[i][0] == '-' && a[i].size() <= 4) continue;
    else paths.push_back(a[i]);
  }
  for (auto& p : paths) {
    Arg ar = parseArg(p);
    if (ar.outside) {
      errOut +=
          norm(unescape(p)) + " - file(s) not under client's root " + state["clientRoot"].get<std::string>() + ".\n";
      continue;
    }
    auto keys = matching(ar);
    if (keys.empty()) {
      errOut += norm(unescape(p)) + " - no such file(s).\n";
      continue;
    }
    for (auto& k : keys) {
      const Json& f = state["files"][k];
      if (onlyOpened && f.value("action", "").empty()) continue;
      emitFstat(k, f, want);
    }
  }
  return 0;
}

int cmdWhere(const std::vector<std::string>& a) {
  if (state.value("notUnderClient", false)) {
    errOut += (a.empty() ? "" : norm(a.back())) + " - file(s) not in client view.\n";
    return 0;
  }
  for (auto& p : a) {
    if (!p.empty() && p[0] == '-') continue;
    Arg ar = parseArg(p);
    if (ar.outside) {
      errOut += norm(p) + " - file(s) not in client view.\n";
      continue;
    }
    std::string rel = ar.rel + (ar.recursive ? "/..." : "");
    tag("depotFile", "//depot/" + rel);
    tag("clientFile", "//client/" + rel);
    tag("path", norm(localOf(rel).string()));
    out += "\n";
  }
  return 0;
}

int cmdPrint(const std::vector<std::string>& a) {
  for (auto& p : a) {
    if (!p.empty() && p[0] == '-') continue;
    size_t h = p.rfind('#');
    Arg ar = parseArg(p.substr(0, h));
    if (ar.outside || !state["files"].contains(ar.rel) || !state["files"][ar.rel].value("inDepot", true)) {
      errOut += norm(unescape(p)) + " - no such file(s).\n";
      return 0;
    }
    out += state["files"][ar.rel].value("content", "");
  }
  return 0;
}

int cmdEdit(const std::vector<std::string>& a) {
  for (auto& p : a) {
    if (!p.empty() && p[0] == '-') continue;
    Arg ar = parseArg(p);
    if (ar.outside) {
      errOut += norm(unescape(p)) + " - file(s) not under client's root.\n";
      return 1;
    }
    if (!state["files"].contains(ar.rel) || !state["files"][ar.rel].value("inDepot", true)) {
      errOut += norm(unescape(p)) + " - file(s) not on client.\n";
      return 0;
    }
    Json& f = state["files"][ar.rel];
    if (!f.value("lockedBy", "").empty()) {
      errOut +=
          "//depot/" + ar.rel + " - can't edit exclusive file already opened by " + f.value("lockedBy", "") + "\n";
      return f.value("editExit", 0);
    }
    if (f.value("action", "").empty()) f["action"] = "edit";
    setWritable(localOf(ar.rel), true);
    out += "... depotFile //depot/" + ar.rel + "\n... action edit\n\n";
  }
  return 0;
}

int cmdRevert(const std::vector<std::string>& a) {
  for (auto& p : a) {
    if (!p.empty() && p[0] == '-') continue;
    Arg ar = parseArg(p);
    if (ar.outside || !state["files"].contains(ar.rel) || state["files"][ar.rel].value("action", "").empty()) {
      errOut += norm(unescape(p)) + " - file(s) not opened on this client.\n";
      return 0;
    }
    Json& f = state["files"][ar.rel];
    const std::string act = f.value("action", "");
    if (act != "add" && act != "branch" && act != "move/add") {
      std::ofstream o(localOf(ar.rel), std::ios::binary | std::ios::trunc);
      o << f.value("content", "");
    }
    f["action"] = "";
    f["unresolved"] = false;
    f["movedFile"] = "";
    if (act != "add" && act != "branch" && act != "move/add") { setWritable(localOf(ar.rel), false); }
    out += "... depotFile //depot/" + ar.rel + "\n... action reverted\n\n";
  }
  return 0;
}

int cmdAdd(const std::vector<std::string>& a) {
  for (auto& p : a) {
    if (!p.empty() && p[0] == '-') continue;
    Arg ar = parseArg(p);
    if (ar.outside) return 1;
    Json& f = state["files"][ar.rel];
    f["inDepot"] = false;
    f["action"] = "add";
    out += "... depotFile //depot/" + ar.rel + "\n... action add\n\n";
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  // Emulate exactly the bytes we are told to print: no \n -> \r\n translation.
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  const char* sp = std::getenv("FAKE_P4_STATE");
  if (!sp) {
    std::fputs("fake p4: FAKE_P4_STATE is not set\n", stderr);
    return 3;
  }
  statePath = sp;
  {
    std::ifstream in(statePath, std::ios::binary);
    state = Json::parse(in, nullptr, false);
    if (!state.is_object()) {
      std::fputs("fake p4: cannot read the state file\n", stderr);
      return 3;
    }
  }
  std::vector<std::string> args(argv + 1, argv + argc);
  {
    std::string line;
    for (auto& a : args) line += (line.empty() ? "" : " ") + a;
    state["log"].push_back(line);
  }
  const int hang = state.value("hangMs", 0);
  if (hang > 0) std::this_thread::sleep_for(std::chrono::milliseconds(hang));
  if (state.value("serverDown", false)) {
    save();
    std::fputs(
        "Perforce client error:\n\tConnect to server failed; check $P4PORT.\n\tTCP connect to perforce:1666 failed.\n",
        stderr);
    return 1;
  }
  // global options
  size_t i = 0;
  std::string format;
  while (i < args.size() && !args[i].empty() && args[i][0] == '-') {
    if (args[i] == "-F" && i + 1 < args.size()) format = args[++i];
    ++i;
  }
  if (i >= args.size()) return 2;
  const std::string cmd = args[i];
  std::vector<std::string> rest(args.begin() + static_cast<long>(i) + 1, args.end());
  int rc = 0;
  if (cmd == "info") {
    const std::string cr = state.value("noClient", false) ? "null" : state["clientRoot"].get<std::string>();
    if (!format.empty()) {
      std::string f = format;
      size_t p;
      while ((p = f.find("%clientRoot%")) != std::string::npos) f.replace(p, 12, cr);
      out += f + "\n";
    } else {
      tag("userName", "tester");
      tag("clientName", "fake-client");
      tag("clientRoot", cr);
    }
  } else if (cmd == "where") rc = cmdWhere(rest);
  else if (cmd == "fstat") rc = cmdFstat(rest);
  else if (cmd == "print") rc = cmdPrint(rest);
  else if (cmd == "edit") rc = cmdEdit(rest);
  else if (cmd == "revert") rc = cmdRevert(rest);
  else if (cmd == "add") rc = cmdAdd(rest);
  else {
    errOut += "Unknown command '" + cmd + "'.\n";
    rc = 1;
  }
  save();
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fwrite(errOut.data(), 1, errOut.size(), stderr);
  return rc;
}
