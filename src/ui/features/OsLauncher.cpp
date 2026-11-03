#include "ui/features/OsLauncher.h"

#include <cctype>
#include <memory>
#include <vector>

#include "lsp/Process.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {

// Programs started here keep running after the editor lets go of them (destroying a Process kills its child), so
// they are parked; the finished ones are dropped at the next launch.
std::vector<std::unique_ptr<Process>>& parked() {
  static auto* p = new std::vector<std::unique_ptr<Process>>();
  return *p;
}

bool launch(const std::string& exe, const std::vector<std::string>& args, const std::string& rawCommandLine,
            std::string* err) {
  auto& list = parked();
  for (size_t i = list.size(); i-- > 0;)
    if (!list[i]->running()) list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
  auto proc = std::make_unique<Process>();
  ProcessOptions opt;
  opt.rawCommandLine = rawCommandLine;
  if (!proc->start(exe, args, {}, {}, err, {}, opt)) return false;
  list.push_back(std::move(proc));
  return true;
}

}  // namespace

OsLauncher systemLauncher() {
  OsLauncher l;
  l.openUrl = [](const std::string& url, std::string* err) {
#ifdef _WIN32
    return launch("explorer.exe", {url}, "explorer.exe \"" + url + "\"", err);
#elif defined(__APPLE__)
    return launch("open", {url}, {}, err);
#else
    return launch("xdg-open", {url}, {}, err);
#endif
  };
  l.showInFolder = [](const fs::path& file, std::string* err) {
#ifdef _WIN32
    fs::path preferred = file;
    std::string native = preferred.make_preferred().string();
    return launch("explorer.exe", {"/select," + native}, "explorer.exe /select,\"" + native + "\"", err);
#elif defined(__APPLE__)
    return launch("open", {"-R", file.string()}, {}, err);
#else
    return launch("xdg-open", {file.parent_path().string()}, {}, err);
#endif
  };
  l.startProgram = [](const std::string& exe, const std::vector<std::string>& args, std::string* err) {
    std::string raw;
#ifdef _WIN32
    auto quote = [](const std::string& a) {
      return a.find_first_of(" \t\"") == std::string::npos ? a : "\"" + a + "\"";
    };
    raw = quote(exe);
    for (const std::string& a : args) raw += " " + quote(a);
#endif
    return launch(exe, args, raw, err);
  };
  return l;
}

std::string urlEncode(const std::string& text) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : text) {
    if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string internetSearchUrl(const std::string& command, const std::string& text) {
  std::string t;
  bool gap = false;
  for (char c : text) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      gap = !t.empty();
    } else {
      if (gap) t += ' ';
      gap = false;
      t += c;
    }
  }
  if (t.empty()) return {};
  return command + urlEncode(t);
}

}  // namespace tf::ui
