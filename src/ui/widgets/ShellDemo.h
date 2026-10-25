#pragma once
#include <string>

#include "ui/widgets/Panel.h"
#include "ui/widgets/Splitter.h"
#include "ui/widgets/StatusBar.h"
#include "ui/widgets/Widget.h"

namespace tf::ui {

// `tenfold-ui --shell`: two panels side by side in a Splitter (3 tabs each, LinesView content from the sample
// project's files), plus a status bar. Throwaway scaffolding for B3a; B3b's Workbench replaces it.
class ShellDemo : public RootWidget {
 public:
  // `dir` is the sample project directory (contains src/main.cpp, src/geometry.cpp, include/geometry.h).
  ShellDemo(gfx::TextRenderer& text, const std::string& dir, const Theme& theme = Theme::darkDefault());
  Splitter& splitter() { return *split_; }
  Panel& left() { return *left_; }
  Panel& right() { return *right_; }
  StatusBar& status() { return *status_; }

 protected:
  void onLayout() override;

 private:
  Splitter* split_;
  Panel *left_, *right_;
  StatusBar* status_;
};

}  // namespace tf::ui
