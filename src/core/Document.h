#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "core/PathUtil.h"
#include "core/TextBuffer.h"

namespace tf {

// An open file: text buffer + identity + version + undo/redo + change listeners.
class Document {
 public:
  struct Change {
    Range removed;       // range in the *old* text
    std::string newText; // what replaced it
    int lineDelta;
  };
  using Listener = std::function<void(Document&, const Change&)>;

  explicit Document(std::filesystem::path path);
  static Document fromText(std::filesystem::path path, std::string text);

  bool loadFromDisk(std::string* error = nullptr);
  bool saveToDisk(std::string* error = nullptr);

  const std::filesystem::path& path() const { return path_; }
  std::string uri() const;
  TextBuffer& buffer() { return buf_; }
  const TextBuffer& buffer() const { return buf_; }
  int version() const { return version_; }
  bool dirty() const { return dirty_; }
  bool existsOnDisk() const { return onDisk_; }

  // Editing API. All edits go through here so listeners always fire.
  void replace(Range r, std::string_view text);
  void insert(Position p, std::string_view text) { replace({p, p}, text); }
  void erase(Range r) { replace(r, {}); }
  bool undo();
  bool redo();

  int addListener(Listener l);
  void removeListener(int id);

 private:
  struct UndoRecord { Range range; std::string oldText; std::string newText; };
  void applyRaw(Range r, std::string_view text, UndoRecord* rec);

  std::filesystem::path path_;
  TextBuffer buf_;
  int version_ = 1;
  bool dirty_ = false;
  bool onDisk_ = false;
  std::vector<UndoRecord> undo_, redo_;
  std::vector<std::pair<int, Listener>> listeners_;
  int nextListener_ = 1;
};

std::string pathToUri(const std::filesystem::path& p);
std::filesystem::path uriToPath(const std::string& uri);

}  // namespace tf
