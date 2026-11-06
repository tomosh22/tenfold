// W8b: untitled documents (Editor::newUntitled / saveAs).
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "app/Editor.h"
#include "core/Document.h"
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
          ("tenfold_untitled_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
           std::to_string(counter++));
    fs::create_directories(dir);
    ed = std::make_unique<Editor>(dir);
  }
  ~Fx() {
    ed.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  static std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }
};

}  // namespace

TEST(untitled_documents_are_numbered_and_keep_their_line_ending) {
  Fx fx;
  Document* a = fx.ed->newUntitled(LineEnding::CRLF);
  Document* b = fx.ed->newUntitled(LineEnding::LF);
  REQUIRE(a && b && a != b);
  CHECK(a->isUntitled() && b->isUntitled());
  CHECK_EQ(a->path().filename().string(), std::string("TextFile1.txt"));
  CHECK_EQ(b->path().filename().string(), std::string("TextFile2.txt"));
  CHECK(a->buffer().lineEnding() == LineEnding::CRLF);
  CHECK(b->buffer().lineEnding() == LineEnding::LF);
  CHECK(!a->dirty());
  CHECK_EQ(fx.ed->document(a->path()), a);
  // A freed number is used again; a file of that name on disk is skipped.
  fx.ed->close(a->path());
  std::ofstream(fx.dir / "TextFile1.txt") << "x";
  Document* c = fx.ed->newUntitled(LineEnding::LF);
  CHECK_EQ(c->path().filename().string(), std::string("TextFile3.txt"));
}

TEST(untitled_document_cannot_be_saved_in_place_and_is_not_written) {
  Fx fx;
  Document* d = fx.ed->newUntitled(LineEnding::LF);
  d->insert({0, 0}, "hello");
  CHECK(d->dirty());
  std::string err;
  CHECK(!fx.ed->save(*d, &err));
  CHECK(!err.empty());
  CHECK(!fs::exists(d->path()));
}

TEST(save_as_gives_an_untitled_document_its_file_and_keeps_history) {
  Fx fx;
  Document* d = fx.ed->newUntitled(LineEnding::CRLF);
  fx.ed->activateDocument(*d);
  fx.ed->insertText("first\nsecond");
  const int versionBefore = d->version();
  std::string err;
  CHECK(fx.ed->saveAs(*d, "notes.txt", &err));
  CHECK(err.empty());
  CHECK(!d->isUntitled());
  CHECK(!d->dirty());
  CHECK_EQ(d->path().filename().string(), std::string("notes.txt"));
  CHECK_EQ(Fx::slurp(fx.dir / "notes.txt"), std::string("first\r\nsecond"));  // the default line ending is CRLF
  CHECK_EQ(fx.ed->document(fx.dir / "notes.txt"), d);
  CHECK(fx.ed->document(fx.dir / "TextFile1.txt") == nullptr);
  CHECK_EQ(fx.ed->currentDocument(), d);
  CHECK(fx.ed->cursor().file == d->path());
  CHECK(d->canUndo());
  CHECK(d->version() >= versionBefore);
  // Now an ordinary document: save writes again.
  d->insert({0, 0}, "x");
  CHECK(fx.ed->save(*d, &err));
  CHECK_EQ(Fx::slurp(fx.dir / "notes.txt"), std::string("xfirst\r\nsecond"));
}

TEST(save_as_refuses_existing_files_missing_folders_and_other_documents) {
  Fx fx;
  std::ofstream(fx.dir / "taken.txt") << "keep";
  Document* d = fx.ed->newUntitled(LineEnding::LF);
  d->insert({0, 0}, "mine");
  std::string err;
  CHECK(!fx.ed->saveAs(*d, "taken.txt", &err));
  CHECK(err.find("already exists") != std::string::npos);
  CHECK_EQ(Fx::slurp(fx.dir / "taken.txt"), std::string("keep"));
  CHECK(!fx.ed->saveAs(*d, "nope/sub.txt", &err));
  CHECK(err.find("does not exist") != std::string::npos);
  CHECK(!fx.ed->saveAs(*d, "", &err));
  Document* other = fx.ed->newUntitled(LineEnding::LF);
  CHECK(!fx.ed->saveAs(*d, other->path(), &err));  // an open document's path
  CHECK(d->isUntitled());                          // nothing changed
  CHECK(d->dirty());
  CHECK(fx.ed->saveAs(*d, fx.dir / "abs.txt", &err));  // absolute paths are fine
  CHECK(!fx.ed->saveAs(*d, "again.txt", &err));        // no longer untitled
}
