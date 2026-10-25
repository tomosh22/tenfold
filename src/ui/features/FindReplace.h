#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ui/features/PickerUtil.h"
#include "ui/widgets/InputLine.h"

namespace tf::ui {

class TextView;

// Find / replace in the current file (Ctrl+F / Ctrl+H, B8): a small non-modal bar at the top right of the editor with
// a find field, an optional replace field and the toggles case (Alt+C), whole word (Alt+W) and regex (Alt+R). Typing
// searches at once through Editor::findInDocument (TextSearch's matcher on the document text; regex needs PCRE2): all
// matches are highlighted in the views of the document (TextView::setFindMatches, scrollbar search lane too) and the
// first one at or after where the search started is selected. Enter / F3 select the next match, Shift+Enter /
// Shift+F3 the previous one (wrapping; far moves go through Editor::moveCursor so Back returns). In the replace
// field Enter replaces the current match and moves on, Ctrl+Enter replaces all (one undo step). Tab switches
// fields, Escape closes the bar and clears the highlights. Regex replacements do not expand group references.
class FindReplace {
 public:
  using ViewsFn = std::function<void(const std::function<void(TextView&)>&)>;  // EditorApp::forEachView
  using AreaFn = std::function<RectF()>;  // where the bar goes: the bounds of the editor view

  explicit FindReplace(PickerEnv& env);
  ~FindReplace();
  FindReplace(const FindReplace&) = delete;
  FindReplace& operator=(const FindReplace&) = delete;

  void setViewsFn(ViewsFn fn) { views_ = std::move(fn); }
  void setAreaFn(AreaFn fn) { area_ = std::move(fn); }

  // Shows the bar (with the replace row when `replace`) and focuses the find field. A single-line selection of the
  // primary caret becomes the query; otherwise the previous query stays.
  void open(bool replace);
  void close();
  bool isOpen() const { return open_; }
  // F3 / Shift+F3 (also while the bar is closed, with the last query): select the next / previous match.
  bool step(bool forward);
  bool replaceCurrent();     // replaces the selected match (or selects the next one first), then moves on
  size_t replaceAll();       // one undo step; reports the count in the status bar
  void tick(int64_t nowMs);  // re-searches when the document or the query changed

  void setQuery(const std::string& q);  // programmatic (tests); behaves like typing
  void setReplacement(const std::string& r);
  void setOptions(bool caseSensitive, bool wholeWord, bool regex);
  const std::string& query() const;
  bool caseSensitive() const { return cs_; }
  bool wholeWord() const { return word_; }
  bool regex() const { return regex_; }
  const std::vector<Range>& matches() const { return matches_; }
  int current() const { return cur_; }  // index into matches(), -1 = none
  std::string statusText() const;
  InputLine* findInput() const;
  InputLine* replaceInput() const;
  RectF barRect() const;

 private:
  class Bar;
  void refresh(bool select);
  void publish();
  void select(int index, bool viaHistory);
  SearchOptions options() const;

  PickerEnv& env_;
  ViewsFn views_;
  AreaFn area_;
  Bar* bar_ = nullptr;
  bool open_ = false, replaceRow_ = false, cs_ = false, word_ = false, regex_ = false;
  std::vector<Range> matches_;
  int cur_ = -1;
  std::string error_, query_;
  Position origin_;
  const Document* doc_ = nullptr;
  int version_ = -1;
  bool dirty_ = false;
};

}  // namespace tf::ui
