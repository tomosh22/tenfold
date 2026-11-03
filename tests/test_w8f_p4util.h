#pragma once
// W8f test helper: a workspace directory plus the state file of the fake `p4` (tests/fake_p4_main.cpp) with a file
// that has a revision history.
#include <filesystem>
#include <fstream>
#include <string>

#include "lsp/JsonRpc.h"
#include "test_vcs_util.h"

namespace w8f {

namespace fs = std::filesystem;
using tf::Json;

struct P4Ws {
  fs::path ws = tfvcs::tempDir("w8fws");
  fs::path stateDir = tfvcs::tempDir("w8fstate");
  fs::path stateFile = stateDir / "state.json";
  Json state;

  P4Ws() {
    state = {{"clientRoot", ws.generic_string()}, {"files", Json::object()}, {"log", Json::array()}};
    save();
    tfvcs::setEnvVar("FAKE_P4_STATE", stateFile.string().c_str());
  }
  ~P4Ws() {
    std::error_code ec;
    fs::remove_all(ws, ec);
    fs::remove_all(stateDir, ec);
  }
  P4Ws(const P4Ws&) = delete;
  P4Ws& operator=(const P4Ws&) = delete;

  void save() const {
    std::ofstream o(stateFile, std::ios::trunc);
    o << state.dump(2);
  }
  // A synced, read-only depot file `rel` with three revisions (newest first); the head text is `head`.
  void depotFile(const std::string& rel, const std::string& head = "third\n") {
    tfvcs::writeFile(ws / rel, head);
    std::error_code ec;
    fs::permissions(ws / rel, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::remove, ec);
    Json hist = Json::array();
    hist.push_back({{"rev", 3},
                    {"change", 300},
                    {"action", "edit"},
                    {"time", 1700000000},
                    {"user", "carol"},
                    {"client", "cws"},
                    {"desc", "Third change\nsecond line of text"},
                    {"type", "text"},
                    {"content", head}});
    hist.push_back({{"rev", 2},
                    {"change", 200},
                    {"action", "edit"},
                    {"time", 1690000000},
                    {"user", "bob"},
                    {"client", "bws"},
                    {"desc", "Second change"},
                    {"type", "text"},
                    {"content", "second\n"}});
    hist.push_back({{"rev", 1},
                    {"change", 100},
                    {"action", "add"},
                    {"time", 1680000000},
                    {"user", "alice"},
                    {"client", "aws"},
                    {"desc", "First"},
                    {"type", "text"},
                    {"content", "first\n"}});
    state["files"][rel] = {{"headRev", 3},         {"haveRev", 3},    {"type", "text"},
                           {"headAction", "edit"}, {"content", head}, {"history", hist}};
  }
  std::vector<std::string> log() {
    std::ifstream in(stateFile);
    Json j = Json::parse(in, nullptr, false);
    return j.value("log", Json::array()).get<std::vector<std::string>>();
  }
};

}  // namespace w8f
