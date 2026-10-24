#pragma once
#include <filesystem>
#include <string>

namespace tf {

// Path conventions that can be exercised on any host: the *Style overloads
// take the style explicitly so Linux tests can cover the Windows behaviour.
enum class PathStyle { Posix, Windows };

constexpr PathStyle nativePathStyle() {
#ifdef _WIN32
  return PathStyle::Windows;
#else
  return PathStyle::Posix;
#endif
}

// Canonical comparison key. Windows: separators become '/', repeated
// separators collapse (a leading "//" for UNC is kept), the trailing
// separator goes (roots excepted) and ASCII letters are lower-cased. Posix:
// the string is returned unchanged.
std::string pathKey(const std::string& p, PathStyle style);
std::string pathKey(const std::filesystem::path& p);
bool pathsEqual(const std::string& a, const std::string& b, PathStyle style);
bool pathsEqual(const std::filesystem::path& a, const std::filesystem::path& b);

// file:// URIs. Windows style understands drive letters (C:\x <-> file:///C:/x)
// and UNC (\\server\share\x <-> file://server/share/x). Results from
// uriToPathString in Windows style use backslashes.
std::string pathToUri(const std::string& path, PathStyle style);
std::string uriToPathString(const std::string& uri, PathStyle style);

}  // namespace tf
