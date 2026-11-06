#pragma once
#include <filesystem>
#include <memory>
#include <optional>

#include "ui/features/PickerUtil.h"
#include "ui/features/UnsavedPrompt.h"

namespace tf::ui {

// Visual Studio's two encoding dialogs (docs/VS_BEHAVIOUR.md section 2, E3-E5), as modal ListPopups. Both are reached
// from the status bar's encoding segment and from the commands File.ReopenWithEncoding / File.SaveWithEncoding.
//
// 1. Reopen with Encoding: one row per encoding tenfold can decode (UTF-8, UTF-16 LE, UTF-16 BE, Windows-1252; the
//    current one is marked and pre-selected). The file's bytes are read again with the chosen encoding
//    (Editor::reopenWithEncoding). A modified document first asks "Save changes to a.cpp?" (UnsavedPrompt: Save /
//    Don't Save / Cancel); Save that is refused (a character its encoding cannot hold) shows SaveRefusedDialog and the
//    reopen does not happen. Undo history is reset like a reload.
// 2. Advanced Save Options (Save with Encoding): ONE list with the Save row on top, the Encoding group (UTF-8, UTF-8
//    with signature, UTF-16 LE, UTF-16 BE, Windows-1252; "(*)" marks the choice) and the Line endings group (Current
//    Setting, Windows (CR LF), Macintosh (CR), Unix (LF)). Enter on a choice selects it and shows the list again; Enter
//    on Save writes the file (Editor::saveWithEncoding). An encoding that cannot hold the text is refused with
//    SaveRefusedDialog and nothing changes. An untitled document only takes the encoding (Save As writes it).
class EncodingPrompt {
 public:
  EncodingPrompt(PickerEnv& env, UnsavedPrompt& unsaved);
  ~EncodingPrompt();
  EncodingPrompt(const EncodingPrompt&) = delete;
  EncodingPrompt& operator=(const EncodingPrompt&) = delete;

  void askReopen(Document& doc);
  // `preselect`: the encoding to start from instead of the document's (SaveRefusedDialog offers UTF-8).
  void askSave(Document& doc, std::optional<EncodingChoice> preselect = std::nullopt);
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }

 private:
  void openSave(int selected);
  void doSave();

  PickerEnv& env_;
  UnsavedPrompt& unsaved_;
  ListPopup* popup_ = nullptr;
  std::filesystem::path file_;  // the document the Save dialog is for
  EncodingChoice enc_ = EncodingChoice::Utf8;
  int ending_ = 0;  // 0 Current Setting, 1 Windows, 2 Macintosh, 3 Unix
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
