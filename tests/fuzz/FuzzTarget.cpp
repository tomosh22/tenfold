#include "FuzzTarget.h"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace tf::fuzz {

void failInvariant(const char* file, int line, const std::string& what) {
  throw InvariantFailure(std::string(file) + ":" + std::to_string(line) + ": invariant violated: " + what);
}

std::vector<Target>& registry() {
  static std::vector<Target> r;
  return r;
}
Registrar::Registrar(Target t) { registry().push_back(std::move(t)); }

const Target* findTarget(const std::string& name) {
  for (const Target& t : registry())
    if (t.name == name) return &t;
  return nullptr;
}

namespace {
struct Scratch {
  std::filesystem::path dir;
  Scratch() {
    std::error_code ec;
    dir = std::filesystem::temp_directory_path(ec) / ("tenfold-fuzz-" + std::to_string(getpid()));
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
  }
  ~Scratch() {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};
Scratch& scratch() {
  static Scratch s;
  return s;
}
}  // namespace

std::string scratchDir() { return scratch().dir.string(); }

void writeScratch(const std::string& relPath, const uint8_t* data, size_t size) {
  const std::filesystem::path p = scratch().dir / relPath;
  std::error_code ec;
  std::filesystem::create_directories(p.parent_path(), ec);
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  if (size) out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

std::vector<std::string> splitParts(const uint8_t* data, size_t size, size_t maxParts) {
  static const std::string kMarker = "\n#--#\n";
  const std::string s(reinterpret_cast<const char*>(data), size);
  std::vector<std::string> parts;
  size_t pos = 0;
  while (parts.size() + 1 < maxParts) {
    const size_t at = s.find(kMarker, pos);
    if (at == std::string::npos) break;
    parts.push_back(s.substr(pos, at - pos));
    pos = at + kMarker.size();
  }
  parts.push_back(s.substr(pos));
  return parts;
}

}  // namespace tf::fuzz
