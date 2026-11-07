#include "core/Trash.h"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>

#include "core/PathUtil.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// After <windows.h>, which it needs (separate block so include sorting keeps the order).
#include <shellapi.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tf {

namespace fs = std::filesystem;

#ifdef _WIN32

bool trashSupported(const fs::path&) { return true; }

bool moveToTrash(const fs::path& path, std::string* error) {
  std::wstring from = fs::absolute(path).make_preferred().wstring();
  from.push_back(L'\0');  // SHFileOperation wants a double-NUL-terminated list
  SHFILEOPSTRUCTW op = {};
  op.wFunc = FO_DELETE;
  op.pFrom = from.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
  const int rc = SHFileOperationW(&op);
  if (rc != 0 || op.fAnyOperationsAborted) {
    if (error) *error = "the Recycle Bin refused it (code " + std::to_string(rc) + ")";
    return false;
  }
  return true;
}

#elif defined(__APPLE__)

bool trashSupported(const fs::path&) { return false; }

bool moveToTrash(const fs::path&, std::string* error) {
  if (error) *error = "the Trash is not supported on this platform";
  return false;
}

#else

namespace {

std::string percentEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
      out.push_back(static_cast<char>(c));
    else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 15]);
    }
  }
  return out;
}

fs::path defaultDataHome() {
  if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x && x[0] == '/') return pathFromUtf8(x);
  if (const char* h = std::getenv("HOME"); h && *h) return pathFromUtf8(h) / ".local" / "share";
  return {};
}

bool sameDevice(const fs::path& a, const fs::path& b) {
  struct stat sa, sb;
  return ::stat(a.c_str(), &sa) == 0 && ::stat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev;
}

bool makePrivateDir(const fs::path& d) {
  std::error_code ec;
  if (fs::is_directory(d, ec)) return true;
  fs::create_directories(d.parent_path(), ec);
  return ::mkdir(d.c_str(), 0700) == 0 || fs::is_directory(d, ec);
}

// The trash directory (holding files/ and info/) that serves `path`, "" when there is none.
fs::path trashFor(const fs::path& path, const fs::path& dataHome) {
  const fs::path home = (dataHome.empty() ? defaultDataHome() : dataHome) / "Trash";
  if (!home.parent_path().empty() && makePrivateDir(home) && makePrivateDir(home / "files") &&
      makePrivateDir(home / "info") && sameDevice(path, home))
    return home;
  // Another file system: the top directory of the one `path` is on.
  std::error_code ec;
  fs::path top = fs::absolute(path, ec).lexically_normal();
  if (top.has_filename() == false) top = top.parent_path();
  while (top.has_parent_path() && top.parent_path() != top && sameDevice(top.parent_path(), path))
    top = top.parent_path();
  if (!fs::is_directory(top, ec)) top = top.parent_path();
  const fs::path alt = top / (".Trash-" + std::to_string(::getuid()));
  if (makePrivateDir(alt) && makePrivateDir(alt / "files") && makePrivateDir(alt / "info") && sameDevice(path, alt))
    return alt;
  return {};
}

}  // namespace

bool trashSupportedIn(const fs::path& path, const fs::path& dataHome) {
  std::error_code ec;
  if (!fs::exists(fs::symlink_status(path, ec))) return false;
  return !trashFor(fs::absolute(path, ec), dataHome).empty();
}

bool moveToTrashIn(const fs::path& path, const fs::path& dataHome, std::string* error, fs::path* trashedTo) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    return false;
  };
  std::error_code ec;
  const fs::path abs = fs::absolute(path, ec).lexically_normal();
  if (!fs::exists(fs::symlink_status(abs, ec))) return fail("it does not exist");
  const fs::path trash = trashFor(abs, dataHome);
  if (trash.empty()) return fail("no trash folder is available on its file system");
  const std::string base = pathUtf8(abs.filename());
  std::string name = base;
  for (int n = 2;; ++n) {
    if (!fs::exists(fs::symlink_status(trash / "files" / pathFromUtf8(name), ec)) &&
        !fs::exists(fs::symlink_status(trash / "info" / pathFromUtf8(name + ".trashinfo"), ec)))
      break;
    if (n > 100000) return fail("too many items of that name in the trash");
    name = base + "." + std::to_string(n);
  }
  // Non-home trashes record the path relative to the top directory; the home trash records the absolute path.
  const fs::path infoPath = trash / "info" / pathFromUtf8(name + ".trashinfo");
  std::string recorded = pathUtf8(abs);
  if (trash.filename().string().rfind(".Trash-", 0) == 0) {
    const std::string rel = pathUtf8(abs.lexically_relative(trash.parent_path()));
    if (!rel.empty() && rel.rfind("..", 0) != 0) recorded = rel;
  }
  char date[32];
  const std::time_t t = std::time(nullptr);
  std::tm tmv{};
  localtime_r(&t, &tmv);
  std::strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", &tmv);
  {
    const int fd = ::open(infoPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return fail("cannot write the trash information");
    ::close(fd);
    std::ofstream out(infoPath, std::ios::binary | std::ios::trunc);
    out << "[Trash Info]\nPath=" << percentEncode(recorded) << "\nDeletionDate=" << date << "\n";
    if (!out) {
      fs::remove(infoPath, ec);
      return fail("cannot write the trash information");
    }
  }
  const fs::path dest = trash / "files" / pathFromUtf8(name);
  fs::rename(abs, dest, ec);
  if (ec) {
    fs::remove(infoPath, ec);
    return fail("cannot move it to the trash");
  }
  if (trashedTo) *trashedTo = dest;
  return true;
}

bool trashSupported(const fs::path& path) { return trashSupportedIn(path, {}); }
bool moveToTrash(const fs::path& path, std::string* error) { return moveToTrashIn(path, {}, error, nullptr); }

#endif

}  // namespace tf
