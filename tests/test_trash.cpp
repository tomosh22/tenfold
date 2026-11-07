// The freedesktop.org Trash used by the explorer's Delete (src/core/Trash). Linux only: XDG_DATA_HOME is a temp dir.
#include <fstream>
#include <sstream>

#include "core/PathUtil.h"
#include "core/Trash.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

#if !defined(_WIN32) && !defined(__APPLE__)
namespace {
fs::path fresh(const char* name) {
  const fs::path d = fs::temp_directory_path() / (std::string("tenfold_trash_") + name);
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d);
  return d;
}
std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
bool has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }
}  // namespace

TEST(trash_moves_a_file_with_a_trashinfo) {
  const fs::path root = fresh("file"), data = root / "data";
  fs::create_directories(root / "w");
  std::ofstream(root / "w" / "a b.cpp") << "x";
  CHECK(trashSupportedIn(root / "w" / "a b.cpp", data));
  fs::path to;
  std::string err;
  REQUIRE(moveToTrashIn(root / "w" / "a b.cpp", data, &err, &to));
  CHECK(!fs::exists(root / "w" / "a b.cpp"));
  CHECK(fs::exists(data / "Trash" / "files" / "a b.cpp"));
  CHECK(to == data / "Trash" / "files" / "a b.cpp");
  const std::string info = slurp(data / "Trash" / "info" / "a b.cpp.trashinfo");
  CHECK(info.rfind("[Trash Info]\nPath=", 0) == 0);
  CHECK(has(info, "Path=" + std::string(pathUtf8(fs::absolute(root / "w")).c_str()) + "/a%20b.cpp\n"));
  CHECK(has(info, "\nDeletionDate="));
  CHECK_EQ(info.size() - info.rfind("DeletionDate=") - 13, size_t(20));  // YYYY-MM-DDThh:mm:ss + newline
  fs::remove_all(root);
}

TEST(trash_name_collisions_get_a_number_and_folders_move_whole) {
  const fs::path root = fresh("coll"), data = root / "data";
  fs::create_directories(root / "d" / "sub");
  std::ofstream(root / "d" / "sub" / "f.txt") << "1";
  std::ofstream(root / "n") << "a";
  REQUIRE(moveToTrashIn(root / "n", data, nullptr));
  std::ofstream(root / "n") << "b";
  REQUIRE(moveToTrashIn(root / "n", data, nullptr));
  std::ofstream(root / "n") << "c";
  REQUIRE(moveToTrashIn(root / "n", data, nullptr));
  CHECK_EQ(slurp(data / "Trash" / "files" / "n"), "a");
  CHECK_EQ(slurp(data / "Trash" / "files" / "n.2"), "b");
  CHECK_EQ(slurp(data / "Trash" / "files" / "n.3"), "c");
  CHECK(fs::exists(data / "Trash" / "info" / "n.3.trashinfo"));
  REQUIRE(moveToTrashIn(root / "d", data, nullptr));
  CHECK(fs::exists(data / "Trash" / "files" / "d" / "sub" / "f.txt"));
  CHECK(!fs::exists(root / "d"));
  fs::remove_all(root);
}

TEST(trash_reports_what_it_cannot_do_and_never_deletes) {
  const fs::path root = fresh("fail"), data = root / "data";
  std::string err;
  CHECK(!moveToTrashIn(root / "missing", data, &err));
  CHECK(!err.empty());
  CHECK(!trashSupportedIn(root / "missing", data));
  // The data home is a file: no trash can be made there, and (same device) no fallback exists below the temp root
  // unless the top directory is writable; either way the file must still be there when the move fails.
  std::ofstream(root / "blocker") << "x";
  std::ofstream(root / "keep") << "x";
  std::string e2;
  const bool ok = moveToTrashIn(root / "keep", root / "blocker", &e2);
  if (!ok) CHECK(fs::exists(root / "keep"));
  fs::remove_all(root);
}
#endif
