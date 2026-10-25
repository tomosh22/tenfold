#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/view/Scrollbar.h"
#include "ui/widgets/Widget.h"

namespace tf::ui {

// Build output panel (C1-UI), hosted by the Workbench's bottom dock under the name "Build". A header shows the
// state ("Building...", "Succeeded in 3.2 s", "Failed: 2 errors, 5 warnings", "Cancelled") with Build / Cancel / close
// buttons; below it the output streams in line by line (editor font, ANSI escapes stripped). It follows the end of
// the output until the user scrolls up and resumes when they scroll back down. Lines that ErrorParser recognises are
// coloured by severity (theme buildError / buildWarning); those with a source location are clickable and jump through
// Editor::gotoLocation, so Back returns (constraint 1.3).
//
// The panel starts the build itself (Editor::build takes one output callback) and parses the lines with its own
// ErrorParser, which sees the same lines as the Editor's and so finds the same messages: that is how a line and a
// message (F4: selectMessage) are paired. Keys when focused: Up/Down/PageUp/PageDown/Home/End select, Enter jumps,
// Escape closes.
class BuildPanel : public Widget {
 public:
  enum class State { Idle, Running, Succeeded, Failed, Cancelled };
  struct Line {
    std::string text;
    int message = -1;  // index into messages(), -1 for plain output
  };

  explicit BuildPanel(Editor& editor);
  ~BuildPanel() override;

  // Starts a build (Editor::build); false with `error` when no command applies or it cannot start (the panel then
  // shows the failure too). Replaces a running build; the previous output is cleared.
  bool start(const BuildOverrides& overrides = {}, std::string* error = nullptr);
  void cancel();

  State state() const { return state_; }
  std::string statusText() const;
  const std::vector<Line>& lines() const { return lines_; }
  const std::vector<BuildMessage>& messages() const { return messages_; }
  int selectedLine() const { return selected_; }
  bool following() const { return follow_; }
  float scrollY() const { return scrollY_; }

  // Selects (and scrolls to) the output line of `m`; false when the panel has no such line.
  bool selectMessage(const BuildMessage& m);
  // Jumps to the location of line `i` (a click or Enter); false for a line without one or a failed jump.
  bool activate(int i);

  // Geometry for tests: window coordinates.
  RectF headerRect() const;
  RectF bodyRect() const;
  RectF lineRect(int i) const;  // may be outside bodyRect when scrolled away
  RectF buildButtonRect() const;
  RectF cancelButtonRect() const;
  RectF closeButtonRect() const;

  // Output cleaned for display: ANSI escape sequences removed, tabs expanded, control characters dropped, text
  // before a carriage return discarded (progress lines), very long lines cut.
  static std::string clean(std::string_view raw);

  std::function<void()> onChanged;                    // state or output changed (status bar)
  std::function<void()> onJumped;                     // after a successful jump from a click / Enter
  std::function<void()> onClose;                      // the close button / Escape
  std::function<void(const std::string&)> onMessage;  // short notices (a jump to a missing file)

  bool acceptsFocus() const override { return true; }
  bool onEvent(const InputEvent& e) override;
  void onHover(bool entered) override;

 protected:
  void paint(PaintContext& ctx) override;
  void onLayout() override;

 private:
  struct Button {
    const char* label;
    RectF rect;
    bool enabled;
  };
  float rowHeight() const;
  float headerHeight() const;
  float scrollbarWidth() const;
  float maxScroll() const;
  void updateMetrics();
  void scrollTo(float y, bool user);  // `user`: a deliberate scroll, so following ends unless it reaches the end
  void appendLines(const std::vector<std::string>& raw);
  void finish(const BuildResult& r);
  void select(int i);
  void ensureVisible(int i);
  int lineAt(float y) const;
  void layoutButtons(std::vector<Button>& out) const;
  void changed();
  bool clickable(int i) const;

  Editor& editor_;
  std::vector<Line> lines_;
  std::vector<BuildMessage> messages_;
  std::unique_ptr<ErrorParser> parser_;
  Scrollbar* sb_ = nullptr;
  State state_ = State::Idle;
  BuildResult result_;
  std::string startError_;
  std::vector<int> errMarks_, warnMarks_;  // scrollbar marker lines
  int selected_ = -1, hover_ = -1, hotButton_ = -1;
  float scrollY_ = 0;
  bool follow_ = true, truncated_ = false;
  int gen_ = 0;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
