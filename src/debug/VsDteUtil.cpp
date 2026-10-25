#include "debug/VsDteUtil.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace tf {

namespace {
constexpr uint32_t u(long hr) { return static_cast<uint32_t>(hr); }
}  // namespace

std::optional<DteMoniker> parseDteMoniker(const std::string& s) {
  static const std::string kPrefix = "!VisualStudio.DTE.";
  if (s.size() <= kPrefix.size()) return std::nullopt;
  for (size_t i = 0; i < kPrefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(kPrefix[i])))
      return std::nullopt;
  }
  size_t colon = s.find(':', kPrefix.size());
  if (colon == std::string::npos) return std::nullopt;
  DteMoniker m;
  m.versionText = s.substr(kPrefix.size(), colon - kPrefix.size());
  if (m.versionText.empty()) return std::nullopt;
  size_t i = 0;  // version: digits separated by dots
  while (i <= m.versionText.size()) {
    size_t dot = m.versionText.find('.', i);
    if (dot == std::string::npos) dot = m.versionText.size();
    std::string part = m.versionText.substr(i, dot - i);
    if (part.empty() || part.size() > 6) return std::nullopt;
    int v = 0;
    for (char c : part) {
      if (c < '0' || c > '9') return std::nullopt;
      v = v * 10 + (c - '0');
    }
    m.version.push_back(v);
    i = dot + 1;
  }
  std::string pidText = s.substr(colon + 1);
  if (pidText.empty() || pidText.size() > 10) return std::nullopt;
  uint64_t pid = 0;
  for (char c : pidText) {
    if (c < '0' || c > '9') return std::nullopt;
    pid = pid * 10 + static_cast<uint64_t>(c - '0');
  }
  if (pid == 0 || pid > 0xFFFFFFFFull) return std::nullopt;
  m.pid = static_cast<uint32_t>(pid);
  return m;
}

bool dteNewer(const DteMoniker& a, const DteMoniker& b) {
  if (a.version != b.version)
    return std::lexicographical_compare(b.version.begin(), b.version.end(), a.version.begin(), a.version.end());
  return a.pid > b.pid;
}

void sortDteNewestFirst(std::vector<DteMoniker>& v) {
  std::stable_sort(v.begin(), v.end(), [](const DteMoniker& a, const DteMoniker& b) { return dteNewer(a, b); });
}

std::string normalizeSolutionPath(const std::string& path) {
  std::string p = path;
  for (char& c : p)
    if (c == '/') c = '\\';
  // \\?\C:\x -> C:\x ; \\?\UNC\srv\share -> \\srv\share
  if (p.rfind("\\\\?\\UNC\\", 0) == 0) p = "\\\\" + p.substr(8);
  else if (p.rfind("\\\\?\\", 0) == 0) p = p.substr(4);
  const bool unc = p.rfind("\\\\", 0) == 0;
  std::vector<std::string> parts;
  size_t i = 0;
  while (i <= p.size()) {
    size_t sep = p.find('\\', i);
    if (sep == std::string::npos) sep = p.size();
    std::string seg = p.substr(i, sep - i);
    i = sep + 1;
    if (seg.empty() || seg == ".") continue;
    if (seg == "..") {
      // never pops a drive ("C:")
      if (!parts.empty() && parts.back() != ".." && !(parts.size() == 1 && parts[0].size() == 2 && parts[0][1] == ':'))
        parts.pop_back();
      continue;
    }
    parts.push_back(seg);
  }
  std::string out = unc ? "\\\\" : (!p.empty() && p[0] == '\\' ? "\\" : "");
  for (size_t k = 0; k < parts.size(); ++k) {
    if (k) out += '\\';
    for (char c : parts[k]) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool solutionPathsEqual(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) return false;
  return normalizeSolutionPath(a) == normalizeSolutionPath(b);
}

DebugMode modeFromDte(long m) {
  switch (m) {
    case 2: return DebugMode::Break;
    case 3: return DebugMode::Running;
    default: return DebugMode::Design;
  }
}

bool isServerBusyHr(long hr) {
  const uint32_t h = u(hr);
  return h == 0x80010001u /*RPC_E_CALL_REJECTED*/ || h == 0x8001010Au /*RPC_E_SERVERCALL_RETRYLATER*/;
}

bool isServerDeadHr(long hr) {
  switch (u(hr)) {
    case 0x80010108u:  // RPC_E_DISCONNECTED
    case 0x800706BAu:  // RPC_S_SERVER_UNAVAILABLE
    case 0x800706BEu:  // RPC_S_CALL_FAILED
    case 0x80010007u:  // RPC_E_SERVER_DIED
    case 0x80010012u:  // RPC_E_SERVER_DIED_DNE
    case 0x8001010Eu:  // RPC_E_CONNECTION_TERMINATED
    case 0x800401FDu:  // CO_E_OBJNOTCONNECTED
      return true;
    default: return false;
  }
}

long retryDelayMs(bool retryLater, unsigned long elapsedMs, unsigned long limitMs, bool pastDeadline, bool cancelled) {
  if (cancelled || pastDeadline || !retryLater || elapsedMs >= limitMs) return -1;
  return 100;
}

std::string firstOutputLine(const std::string& output) {
  size_t i = 0;
  while (i < output.size()) {
    size_t e = output.find_first_of("\r\n", i);
    if (e == std::string::npos) e = output.size();
    size_t b = i;
    while (b < e && std::isspace(static_cast<unsigned char>(output[b]))) ++b;
    size_t t = e;
    while (t > b && std::isspace(static_cast<unsigned char>(output[t - 1]))) --t;
    if (t > b) return output.substr(b, t - b);
    i = e + 1;
  }
  return {};
}

std::string describeHr(const std::string& what, long hr, const std::string& detail) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "0x%08X", static_cast<unsigned>(u(hr)));
  std::string s = what + " failed (" + buf + ")";
  if (!detail.empty()) s += ": " + detail;
  return s;
}

}  // namespace tf
