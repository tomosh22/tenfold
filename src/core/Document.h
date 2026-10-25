#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "core/PathUtil.h"
#include "core/Selection.h"
#include "core/TextBuffer.h"

namespace tf {

// An open file: text buffer + identity + version + undo/redo + change listeners.
class Document {
 public:
  struct Change {
    Range removed;        // range in the *old* text
    std::string newText;  // what replaced it
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
  // Switches the line ending written on save (LF / CRLF) and marks the document dirty. Not an undo step.
  void setLineEnding(LineEnding e) {
    if (buf_.lineEnding() == e) return;
    buf_.setLineEnding(e);
    dirty_ = true;
  }

  // Editing API. All edits go through here so listeners always fire.
  TextBuffer::EditResult replace(Range r, std::string_view text);
  void insert(Position p, std::string_view text) { replace({p, p}, text); }
  void erase(Range r) { replace(r, {}); }
  // Several edits as ONE undo step, applied in a single pass over the buffer (O(lines + inserted text), not O(lines)
  // per edit). `edits` must be sorted by position and non-overlapping, in the coordinates before the call. Listeners
  // still get one Change per edit, from the last edit to the first (each range valid in the state it describes) and
  // with a version number that increases per event; but while they run the buffer already holds ALL the edits.
  // Edits with an empty range and empty text are skipped. Returns, per edit, the range its text occupies afterwards.
  struct BatchEdit {
    Range range;
    std::string text;
  };
  std::vector<Range> replaceMany(std::vector<BatchEdit> edits);
  bool undo();
  bool redo();

  // Undo grouping. Edits made between beginGroup() and the matching endGroup() undo / redo as ONE step. Groups
  // nest (only the outermost pair delimits the step). Outside a group every replace() is its own step.
  // `carets` is opaque caret state to hand back after undo (before-state, stored by the outermost begin) and redo
  // (after-state, the last non-empty value passed to endGroup). `joinPrevious` re-opens the most recent undo step
  // and appends to it (used by the typing coalescer); ignored when nested or when there is no step to join.
  // A group that recorded no edit leaves no step behind.
  void beginGroup(std::vector<Selection> carets = {}, bool joinPrevious = false);
  void endGroup(std::vector<Selection> carets = {});
  int groupDepth() const { return depth_; }
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  // Caret state stored with the step the last successful undo() / redo() reverted / re-applied (empty if the
  // step was recorded without carets).
  const std::vector<Selection>& restoredCarets() const { return restored_; }

  int addListener(Listener l);
  void removeListener(int id);

 private:
  // An undo step is a list of batches (a plain replace() is a batch of one edit). A record says where text now sits
  // and what the same place held / will hold: undoing and redoing are the same "swap" applied to the whole batch.
  struct UndoRec {
    Range range;          // where `present` sits in the current state (records of a batch ascend, disjoint)
    std::string present;  // text currently there
    std::string alt;      // text to swap in
  };
  using Batch = std::vector<UndoRec>;
  struct UndoGroup {
    std::vector<Batch> batches;  // in the order applied
    std::vector<Selection> before, after;
  };
  struct BatchResult {
    std::vector<TextBuffer::EditResult> edits;
    std::vector<Range> after;
  };
  BatchResult applyBatch(const std::vector<BatchEdit>& edits, Batch* rec, bool skipNoops);
  void record(Batch&& b);
  UndoGroup flipGroup(UndoGroup&& g);

  std::filesystem::path path_;
  TextBuffer buf_;
  int version_ = 1;
  bool dirty_ = false;
  bool onDisk_ = false;
  std::vector<UndoGroup> undo_, redo_;
  UndoGroup open_;  // the group being built while depth_ > 0
  int depth_ = 0;
  std::vector<Selection> restored_;
  std::vector<std::pair<int, Listener>> listeners_;
  int nextListener_ = 1;
};

std::string pathToUri(const std::filesystem::path& p);
std::filesystem::path uriToPath(const std::string& uri);

}  // namespace tf
