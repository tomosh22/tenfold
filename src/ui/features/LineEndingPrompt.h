#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Visual Studio's two line-ending dialogs (docs/VS_BEHAVIOUR.md section 1, L4 / L5 / L11), as modal ListPopups.
//
// 1. "Check for consistent line endings on load" (setting editor.checkLineEndings, on): Editor::open() reports a
//    document read from disk whose lines do not all end the same way (Editor::onInconsistentLineEndings); the prompt
//    is queued and shown when no other popup is open. Title: "The line endings in the following files are not
//    consistent. Do you want to normalize the line endings?", the file under it, then one row per "Yes" with the ending
//    to normalize to (Windows (CR LF), Unix (LF), Macintosh (CR); the row of the document's dominant ending is
//    pre-selected, VS's own pre-selection is unknown) and a "No" row. Yes converts the whole document as ONE undoable
//    edit (the document becomes modified); No, Escape and a click outside keep it as it is. The question is asked once
//    per load; typing and pasting never trigger it.
// 2. "Advanced Save Options" (File.SaveWithLineEndings): Line endings: Current Setting / Windows (CR LF) / Macintosh
//    (CR) / Unix (LF). The choice converts the document (undoable, like the status bar's conversion) and saves it; the
//    next plain Save keeps what the buffer holds.
class LineEndingPrompt {
 public:
  explicit LineEndingPrompt(PickerEnv& env);
  ~LineEndingPrompt();
  LineEndingPrompt(const LineEndingPrompt&) = delete;
  LineEndingPrompt& operator=(const LineEndingPrompt&) = delete;

  // Shows the next queued consistency question when no popup is open. Returns ms until it should be called again
  // (-1: nothing pending).
  int tick(int64_t nowMs);
  void askNormalize(const std::filesystem::path& file);  // queue a question for an open document
  void askSave(Document& doc);                           // the Advanced Save Options list for `doc`
  void close();                                          // closes an open prompt; a consistency question is asked again
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  size_t pending() const { return queue_.size(); }

  static const char* endingName(LineEnding e);  // "Windows (CR LF)", "Unix (LF)", "Macintosh (CR)"

 private:
  void openNormalize(const std::filesystem::path& file);

  PickerEnv& env_;
  std::deque<std::filesystem::path> queue_;
  ListPopup* popup_ = nullptr;
  std::filesystem::path asking_;
  bool forced_ = false;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
