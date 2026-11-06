// VS-EDIT Z19: .editorconfig trim_trailing_whitespace / insert_final_newline as extra defaults when saving.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "app/Editor.h"
#include "core/Document.h"
#include "core/EditorConfig.h"
#include "test_main.h"

using namespace tf;
namespace fs = std::filesystem;

namespace {

struct Fx {
  fs::path dir;
  std::unique_ptr<Editor> ed;
  Fx() {
    static int counter = 0;
    dir = fs::temp_directory_path() /
          ("tenfold_edcfg_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(counter++));
    fs::create_directories(dir / "src" / "deep");
    ed = std::make_unique<Editor>(dir);
  }
  ~Fx() {
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void write(const fs::path& rel, const std::string& text) {
    std::ofstream(dir / rel, std::ios::binary | std::ios::trunc) << text;
  }
  std::string read(const fs::path& rel) {
    std::ifstream in(dir / rel, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }
};

}  // namespace

TEST(editorconfig_globs) {
  CHECK(editorConfigGlobMatches("*", "a.cpp"));
  CHECK(editorConfigGlobMatches("*", "src/a.cpp"));  // no '/': any folder
  CHECK(editorConfigGlobMatches("*.cpp", "src/deep/a.cpp"));
  CHECK(!editorConfigGlobMatches("*.cpp", "a.h"));
  CHECK(editorConfigGlobMatches("*.{h,cpp}", "x/a.h"));
  CHECK(editorConfigGlobMatches("*.{h,cpp}", "a.cpp"));
  CHECK(!editorConfigGlobMatches("*.{h,cpp}", "a.c"));
  CHECK(editorConfigGlobMatches("src/**", "src/deep/a.cpp"));
  CHECK(!editorConfigGlobMatches("src/**", "other/a.cpp"));
  CHECK(editorConfigGlobMatches("/a.cpp", "a.cpp"));
  CHECK(!editorConfigGlobMatches("/a.cpp", "src/a.cpp"));
  CHECK(editorConfigGlobMatches("file?.txt", "file1.txt"));
  CHECK(!editorConfigGlobMatches("file?.txt", "file12.txt"));
  CHECK(editorConfigGlobMatches("[ab].txt", "a.txt"));
  CHECK(!editorConfigGlobMatches("[!ab].txt", "a.txt"));
}

TEST(editorconfig_closer_file_and_later_section_win_and_root_stops_the_walk) {
  Fx fx;
  fx.write(".editorconfig",
           "# top\nroot = true\n[*]\ntrim_trailing_whitespace = true\ninsert_final_newline = true\n"
           "[*.md]\ntrim_trailing_whitespace = false\n");
  fx.write("src/.editorconfig", "[*.cpp]\ninsert_final_newline = false\n");
  fx.write("src/deep/.editorconfig", "root = true\n[*]\ntrim_trailing_whitespace = false\n");
  const EditorConfigProps top = editorConfigFor(fx.dir / "a.cpp");
  CHECK(top.trimTrailingWhitespace == std::optional<bool>(true));
  CHECK(top.insertFinalNewline == std::optional<bool>(true));
  CHECK(editorConfigFor(fx.dir / "a.md").trimTrailingWhitespace == std::optional<bool>(false));  // later section
  const EditorConfigProps src = editorConfigFor(fx.dir / "src" / "b.cpp");
  CHECK(src.insertFinalNewline == std::optional<bool>(false));     // the closer file
  CHECK(src.trimTrailingWhitespace == std::optional<bool>(true));  // inherited
  const EditorConfigProps deep = editorConfigFor(fx.dir / "src" / "deep" / "c.cpp");
  CHECK(deep.trimTrailingWhitespace == std::optional<bool>(false));
  CHECK(!deep.insertFinalNewline.has_value());  // root = true: nothing above it counts
}

TEST(editorconfig_save_trims_and_adds_the_final_newline_in_one_undoable_step_each) {
  Fx fx;
  fx.write(".editorconfig", "root = true\n[*.cpp]\ntrim_trailing_whitespace = true\ninsert_final_newline = true\n");
  fx.write("a.cpp", "int a;  \r\nint b;\t");  // CRLF file, trailing blanks, no final newline
  fx.write("a.txt", "x  ");
  Document* d = fx.ed->open("a.cpp");
  REQUIRE(d != nullptr);
  fx.ed->gotoLocation({d->path(), {0, 0}});
  fx.ed->insertText("//");  // make it dirty
  CHECK(fx.ed->save(*d));
  CHECK_EQ(fx.read("a.cpp"), std::string("//int a;\r\nint b;\r\n"));
  Document* t = fx.ed->open("a.txt");  // not matched by the section
  REQUIRE(t != nullptr);
  fx.ed->gotoLocation({t->path(), {0, 0}});
  fx.ed->insertText("y");
  CHECK(fx.ed->save(*t));
  CHECK_EQ(fx.read("a.txt"), std::string("yx  "));
}

TEST(editorconfig_use_editor_config_off_ignores_the_file) {
  Fx fx;
  fx.write(".editorconfig", "[*]\ntrim_trailing_whitespace = true\ninsert_final_newline = true\n");
  fx.write("a.cpp", "int a;  ");
  fx.ed->editing().useEditorConfig = false;
  Document* d = fx.ed->open("a.cpp");
  REQUIRE(d != nullptr);
  fx.ed->gotoLocation({d->path(), {0, 0}});
  fx.ed->insertText("//");
  CHECK(fx.ed->save(*d));
  CHECK_EQ(fx.read("a.cpp"), std::string("//int a;  "));
  // the user's own setting still trims
  fx.ed->editing().trimTrailingWhitespace = true;
  fx.ed->insertText("/");
  CHECK(fx.ed->save(*d));
  CHECK_EQ(fx.read("a.cpp"), std::string("///int a;"));
}
