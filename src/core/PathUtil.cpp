#include "core/PathUtil.h"

#include <cctype>

namespace tf {

namespace {
int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
bool isDrivePrefix(const std::string& s) {
  return s.size() > 2 && s[0] == '/' && std::isalpha(static_cast<unsigned char>(s[1])) && s[2] == ':';
}
}  // namespace

std::string pathKey(const std::string& p, PathStyle style) {
  if (style == PathStyle::Posix) return p;
  bool unc =
      p.size() > 2 && (p[0] == '/' || p[0] == '\\') && (p[1] == '/' || p[1] == '\\') && p[2] != '/' && p[2] != '\\';
  std::string out;
  out.reserve(p.size() + 1);
  for (char c : p) {
    if (c == '/' || c == '\\') {
      if (out.empty() || out.back() != '/') out += '/';
    } else {
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  if (out.size() > 1 && out.back() == '/' && !(out.size() == 3 && out[1] == ':')) out.pop_back();
  if (unc) out.insert(0, "/");
  return out;
}

std::string pathKey(const std::filesystem::path& p) { return pathKey(p.generic_string(), nativePathStyle()); }

bool pathsEqual(const std::string& a, const std::string& b, PathStyle style) {
  return pathKey(a, style) == pathKey(b, style);
}
bool pathsEqual(const std::filesystem::path& a, const std::filesystem::path& b) { return pathKey(a) == pathKey(b); }

std::string pathToUri(const std::string& pathIn, PathStyle style) {
  std::string s = pathIn;
  if (style == PathStyle::Windows)
    for (auto& c : s)
      if (c == '\\') c = '/';
  std::string out = "file://";
  size_t from = 0;
  if (style == PathStyle::Windows && s.size() > 2 && s[0] == '/' && s[1] == '/' && s[2] != '/') {
    from = 2;  // UNC: the server becomes the URI authority
  } else if (!s.empty() && s[0] != '/') {
    out += '/';  // drive letters: file:///C:/...
  }
  static const char* hex = "0123456789ABCDEF";
  for (size_t i = from; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string uriToPathString(const std::string& uri, PathStyle style) {
  std::string s = uri;
  if (s.rfind("file://", 0) == 0) s = s.substr(7);
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && hexVal(s[i + 1]) >= 0 && hexVal(s[i + 2]) >= 0) {
      out += static_cast<char>(hexVal(s[i + 1]) * 16 + hexVal(s[i + 2]));
      i += 2;
    } else {
      out += s[i];
    }
  }
  if (isDrivePrefix(out)) out.erase(0, 1);  // file:///C:/x -> C:/x
  if (style == PathStyle::Windows) {
    if (!out.empty() && out[0] != '/' && !(out.size() > 1 && out[1] == ':')) out = "//" + out;  // UNC authority
    for (auto& c : out)
      if (c == '/') c = '\\';
  }
  return out;
}

}  // namespace tf
