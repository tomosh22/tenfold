#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Diagnostics list (View.ShowDiagnostics, Ctrl+Shift+M): a modal ListPopup (like find in files) with the
// Editor::diagnostics of every open document, errors first, then file and line; the input filters by message / path.
// clangd only reports open files, so the status says so (OPEN_QUESTIONS: whole-workspace diagnostics need C1's build
// diagnostics, which are not on this branch). The list follows live updates (refresh() on tick). Enter / click ->
// Editor::gotoLocation (navigation history).
class DiagnosticsPanel {
 public:
  explicit DiagnosticsPanel(PickerEnv& env) : env_(env) {}
  ~DiagnosticsPanel() = default;
  DiagnosticsPanel(const DiagnosticsPanel&) = delete;
  DiagnosticsPanel& operator=(const DiagnosticsPanel&) = delete;

  void open();
  void close();
  bool isOpen() const { return popup_ != nullptr; }
  ListPopup* popup() const { return popup_; }
  void refresh();  // rebuild when the diagnostics or the filter changed (cheap check; call from tick)
  const std::vector<Diagnostic>& shown() const { return shown_; }

 private:
  void rebuild();
  uint64_t signature() const;

  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  std::vector<Diagnostic> shown_;  // aligned with the popup rows
  uint64_t sig_ = 0;
};

}  // namespace tf::ui
