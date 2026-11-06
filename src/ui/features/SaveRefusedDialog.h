#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// "The file was not saved": shown when the engine refuses to write a document because its encoding cannot hold some
// of its characters (a Windows-1252 file that now contains U+6F22, OPEN_QUESTIONS "User decisions of 2026-11-06").
// An input-less modal ListPopup in the same style as the other prompts:
//
//   title    a.cpp was NOT saved                               (or "3 files were NOT saved")
//   dim      Windows-1252 cannot hold 2 characters in it. Nothing was written; the file on disk is unchanged.
//   dim      Enter goes to the selected character. Fix the text (or remove the character) and save again.
//   rows     the characters themselves, as rendered text, with U+XXXX on the left of "line 3, column 5"; at most
//            `kCharsPerFile` per file, then "... and M more"
//   last     (one file only) "Save with Encoding..." - opens the Advanced Save Options list with UTF-8 selected, the
//            way out of a refusal (Visual Studio's own fix for C4819); a tenfold convenience, VS has no such dialog
//
// Enter / click on a character row goes to that character (Editor::gotoLocation, so Back returns); on any other row
// to the first listed character. Escape closes. The same text is put in the status bar by `statusText`.
class SaveRefusedDialog {
 public:
  explicit SaveRefusedDialog(PickerEnv& env) : env_(env) {}
  static constexpr size_t kCharsPerFile = 8;       // listed per file when one file was refused
  static constexpr size_t kCharsPerManyFiles = 3;  // ... when several were
  static constexpr size_t kMaxFiles = 4;

  void show(std::vector<SaveRefusal> refusals);
  // Called with the file when the user takes the "Save with Encoding..." row (EditorApp opens EncodingPrompt).
  std::function<void(const std::filesystem::path&)> saveWithEncoding;
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }

  // One line for the status bar: "a.cpp was not saved: Windows-1252 cannot hold 1 character (first: U+6F22 at line 3,
  // column 5)", or "3 files were not saved: ..." for several.
  static std::string statusText(const std::vector<SaveRefusal>& refusals, const std::filesystem::path& root);

 private:
  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
