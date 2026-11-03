#pragma once
#include <filesystem>
#include <functional>
#include <string>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Save As for an untitled document (NewTextFile, then Ctrl+S): a one-row modal ListPopup whose input holds the path
// (relative to the workspace root, or absolute). The row says where the file will go, or why it cannot (the file
// exists, its folder does not). Enter calls Editor::saveAs; a failure is reported in the status bar and the prompt
// opens again with the same text. `onSaved` runs after the document has its file.
class SaveAsPrompt {
 public:
  explicit SaveAsPrompt(PickerEnv& env) : env_(env) {}

  std::function<void(const std::filesystem::path& from, const std::filesystem::path& to)> onSaved;

  // `suggestion` pre-fills the input (selected).
  void open(const std::filesystem::path& untitled, const std::string& suggestion);
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }

  // Where `text` points: absolute as is, otherwise under the workspace root.
  std::filesystem::path resolve(const std::string& text) const;
  // Empty when `target` can be created, else the reason.
  std::string problem(const std::filesystem::path& target) const;

 private:
  void update(const std::string& text);

  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  std::filesystem::path doc_;
  std::string text_;
};

}  // namespace tf::ui
