#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "ui/features/LineEndingDialog.h"
#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Visual Studio's two line-ending dialogs (docs/VS_BEHAVIOUR.md section 1, L4 / L5 / L11).
//
// 1. "Check for consistent line endings on load" (setting editor.checkLineEndings, on): Editor::open() reports a
//    document read from disk whose lines do not all end the same way (Editor::onInconsistentLineEndings); the prompt
//    is queued and shown (LineEndingDialog, a modal dialog) when no other popup is open. Documents queued by the time
//    it is shown (several files opened in one action, e.g. a restored session) share ONE dialog that lists them all
//    and applies the answer to all. Text: "The line endings in the following files are not consistent. Do you want to
//    normalize the line endings?", the files, the drop-down "Normalize line endings:" (Windows (CR LF), Macintosh
//    (CR), Unix (LF), Unicode Line Separator (LS), Unicode Paragraph Separator (PS); the document's most frequent
//    ending is pre-selected, VS's own pre-selection is unknown), the check box "Do not show this message again" and
//    Yes / No. Yes converts each document as ONE undoable edit (it becomes modified); No and Escape keep them as they
//    are. A ticked check box turns editor.checkLineEndings off, for this run and (persistCheckOff) in the user
//    settings file, whichever button closes the dialog. The question is asked once per load; typing and pasting never
//    trigger it.
// 2. "Advanced Save Options" (File.SaveWithLineEndings): Line endings: Current Setting / Windows (CR LF) / Macintosh
//    (CR) / Unix (LF), a modal ListPopup. The choice converts the document (undoable, like the status bar's
//    conversion) and saves it; the next plain Save keeps what the buffer holds.
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
  bool isOpen() const { return popup_ != nullptr || dialog_ != nullptr; }
  ListPopup* popup() const { return popup_; }                // the Advanced Save Options list
  LineEndingDialog* dialog() const { return dialog_; }       // the consistency dialog
  // Called when the user ticked "Do not show this message again": the app writes editor.checkLineEndings = false to
  // the user settings file (the in-memory switch is already off). May be empty (headless).
  std::function<void()> persistCheckOff;
  size_t pending() const { return queue_.size(); }

  static const char* endingName(LineEnding e);

  // One row of the dialog's drop-down; no `ending` = listed but disabled (LS / PS, see LineEndingPrompt.cpp).
  struct Entry {
    const char* name;
    std::optional<LineEnding> ending;
  };
  static const Entry kEntries[5];
  static std::vector<DropDownItem> dropDownItems();  // "Windows (CR LF)", "Unix (LF)", "Macintosh (CR)"

 private:
  void openNormalize(const std::vector<std::filesystem::path>& candidates);
  void disableCheck();

  PickerEnv& env_;
  std::deque<std::filesystem::path> queue_;
  ListPopup* popup_ = nullptr;
  LineEndingDialog* dialog_ = nullptr;
  std::vector<std::filesystem::path> asking_;
  bool forced_ = false;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
