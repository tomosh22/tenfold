#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ui/features/PickerUtil.h"

namespace tf::ui {

// Diagnostics list (View.ShowDiagnostics, Ctrl+Shift+M): a modal ListPopup (like find in files) with the
// Editor::diagnostics of every open document, errors first, then file and line; the input filters by message / path.
// clangd only reports open files, so the status says so; the last build's messages (Editor::buildDiagnostics) cover
// the whole workspace and are labelled "build" against "clangd". The list follows live updates (refresh() on tick).
// Enter / click -> Editor::gotoLocation (navigation history).
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
  // Source label of shown()[i]: "clangd" or "build" (Editor::buildDiagnostics, C1).
  const std::vector<std::string>& sources() const { return sources_; }

 private:
  void rebuild();
  uint64_t signature() const;
  // Every diagnostic of the clangd (open documents) and build sources, files made absolute.
  std::vector<std::pair<Diagnostic, const char*>> collect() const;

  PickerEnv& env_;
  ListPopup* popup_ = nullptr;
  std::vector<Diagnostic> shown_;  // aligned with the popup rows
  std::vector<std::string> sources_;
  uint64_t sig_ = 0;
};

}  // namespace tf::ui
