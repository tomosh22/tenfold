#include "ui/settings/RecentSolutions.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "core/PathUtil.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {
// Identity of an entry: Windows file names compare case-insensitively, elsewhere exactly.
std::string identity(const fs::path& p) {
  std::string k = pathKeyUtf8(p);
  while (k.size() > 1 && k.back() == '/') k.pop_back();
#ifdef _WIN32
  for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
  return k;
}
fs::path normal(const fs::path& p) {
  std::error_code ec;
  fs::path a = fs::absolute(p, ec);
  if (ec) a = p;
  a = a.lexically_normal();
  if (a.has_relative_path() && !a.has_filename()) a = a.parent_path();  // "dir/" -> "dir"
  return a;
}
}  // namespace

fs::path defaultRecentFile(const fs::path& userDir) { return userDir / "recent.txt"; }

RecentSolutions::RecentSolutions(fs::path file, size_t max)
    : file_(std::move(file)), max_(std::clamp<size_t>(max, 1, kMaxMax)) {}

void RecentSolutions::setMax(size_t n) {
  max_ = std::clamp<size_t>(n, 1, kMaxMax);
  if (entries_.size() > max_) entries_.resize(max_);
}

bool RecentSolutions::load() {
  entries_.clear();
  if (file_.empty()) return false;
  std::ifstream in(file_, std::ios::binary);
  if (!in) return false;
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    fs::path p = pathFromUtf8(line);
    if (!p.is_absolute()) continue;  // only absolute paths are ever written; anything else is a damaged line
    const std::string id = identity(p);
    bool dup = false;
    for (const fs::path& e : entries_) dup = dup || identity(e) == id;
    if (!dup && entries_.size() < max_) entries_.push_back(p.lexically_normal());
  }
  return true;
}

bool RecentSolutions::save() const {
  if (file_.empty()) return false;
  std::error_code ec;
  fs::create_directories(file_.parent_path(), ec);
  const fs::path tmp = file_.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "# tenfold recent projects, solutions and folders: newest first, one absolute path per line\n";
    for (const fs::path& e : entries_) out << pathUtf8(e) << "\n";
    out.flush();
    if (!out) {
      fs::remove(tmp, ec);
      return false;
    }
  }
  fs::rename(tmp, file_, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

void RecentSolutions::add(const fs::path& p) {
  if (p.empty()) return;
  const fs::path n = normal(p);
  drop(n);
  entries_.insert(entries_.begin(), n);
  if (entries_.size() > max_) entries_.resize(max_);
}

bool RecentSolutions::drop(const fs::path& p) {
  const std::string id = identity(normal(p));
  const size_t before = entries_.size();
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const fs::path& e) { return identity(e) == id; }),
                 entries_.end());
  return entries_.size() != before;
}

bool RecentSolutions::exists(const fs::path& p) {
  std::error_code ec;
  return fs::exists(p, ec);
}

size_t RecentSolutions::pruneMissing() {
  const size_t before = entries_.size();
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const fs::path& e) { return !exists(e); }),
                 entries_.end());
  return before - entries_.size();
}

}  // namespace tf::ui
