#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/Document.h"
#include "ui/view/WrapLayout.h"

namespace tf::ui {

// Code folding (W8f, the 10x CollapseRegions commands): which regions a C/C++ file has and which of them are collapsed.
//
// Regions come from the lexer (lexLine, the Highlighter's scanner), so braces in strings, comments and character
// literals do not count: a brace block { ... } spanning at least two lines, a multi-line block comment, and
// #pragma region / #pragma endregion pairs. (10x documents no region syntax; #if / #endif blocks are not regions.)
// A region is a pair of lines, start < end: the head line (shown, followed by a marker) and the last line (hidden
// with the lines in between, so the closing brace is part of the marker). Regions that start on the same line keep
// only the outermost one.
//
// Detection is a pure function of the text (detect), so it can run on a copy of the lines off the main thread. The
// model never applies a result to a changed buffer on its own: the owner passes only results computed for the
// version it has now, and every edit marks the set stale (fresh() = false) after patching it for what the edit
// cannot have changed (regions entirely before the edit stay, regions after it move with the lines, regions around
// it keep their start and move their end; regions the edit touches go).
//
// Collapsed regions are the model's own list, independent of the region set: they move with the lines like
// bookmarks, and an edit that touches any line of one (head line, hidden lines, or the line the hidden range ends
// on) expands it. When a fresh region set arrives each collapsed region follows its region (matched by the head
// line); one whose head line is no longer a region start is dropped (expanded).
//
// Pure value logic, no widgets. hidden() is what the view hands to WrapLayout::setHidden.
class FoldModel {
 public:
  enum class Kind : uint8_t { Brace, Comment, Pragma };
  struct Region {
    int start = 0, end = 0;  // lines, start < end
    Kind kind = Kind::Brace;
    bool type = false;  // Brace only: the body of a namespace / class / struct / union / enum / extern "C" block
    bool operator==(const Region& o) const {
      return start == o.start && end == o.end && kind == o.kind && type == o.type;
    }
  };

  // The C++ outlining options of Visual Studio (Tools > Options > Text Editor > C/C++ > View > Outlining), all on by
  // default. enabled = Enable Outlining (off: no regions at all); pragmaRegions = Outline Pragma Regions;
  // statementBlocks = Outline Statement Blocks (off: brace blocks nested in a function body are not regions; function,
  // class and namespace bodies and comments still are).
  struct Options {
    bool enabled = true, pragmaRegions = true, statementBlocks = true;
    bool operator==(const Options& o) const {
      return enabled == o.enabled && pragmaRegions == o.pragmaRegions && statementBlocks == o.statementBlocks;
    }
  };

  // Regions of `lines`, sorted by start (one per start line, the outermost).
  static std::vector<Region> detect(const std::vector<std::string>& lines);  // all options on
  static std::vector<Region> detect(const std::vector<std::string>& lines, const Options& opt);

  // ---- regions ----
  // Installs a result computed for the current text and reconciles the collapsed regions with it. True when the
  // collapsed set (hence the hidden lines) changed.
  bool setRegions(std::vector<Region> regions);
  const std::vector<Region>& regions() const { return regions_; }
  bool fresh() const { return fresh_; }
  void invalidate() {
    fresh_ = false;
  }  // the options changed: the set must be recomputed  // regions() were computed for the text as it is now
  const Region* regionStartingAt(int line) const;
  // The innermost region with start <= line <= end that is not collapsed (null if none).
  const Region* innermostAt(int line) const;
  // Nesting depth of each of regions() (0 = outermost).
  std::vector<int> depths() const;

  // ---- collapsed state ----
  const std::vector<Region>& collapsed() const { return collapsed_; }
  bool isCollapsedAt(int headLine) const;  // a collapsed region starts on this line
  const Region* collapsedAt(int headLine) const;
  bool hasCollapsed() const { return !collapsed_.empty(); }
  // Hidden lines: the merged ranges of [start + 1, end] over the collapsed regions. isHidden / headOf answer for a
  // line (headOf: the visible line the line folds into, the line itself when visible).
  const std::vector<WrapLayout::HiddenRange>& hidden() const { return hidden_; }
  bool isHidden(int line) const;
  int headOf(int line) const;
  uint32_t generation() const { return gen_; }  // moves whenever collapsed() changes

  // Commands. All return true when something changed. `line` is the caret line.
  bool collapse(const Region& r);
  bool collapseAt(int line, Region* done = nullptr);  // the innermost uncollapsed region around the line
  bool expandAt(int line);                            // the collapsed region that starts on the line
  bool toggleAt(int line, Region* collapsedNow = nullptr);
  bool collapseAll(bool recursive);  // recursive: every region; else the outermost ones (not inside another region)
  bool expandAll(bool recursive);    // recursive: every region; else the collapsed ones whose head line is visible
  // Visual Studio's Toggle All Outlining: "sets all regions to the same state; if some are expanded and some
  // collapsed, the collapsed ones are expanded". Nothing collapsed -> collapse all, otherwise expand all.
  bool toggleAll();
  // Visual Studio's Collapse to Definitions: exactly the member bodies are collapsed (function bodies and other brace
  // blocks that are not a namespace / class / struct / union / enum body and not inside a function), everything
  // else is expanded.
  bool collapseToDefinitions();
  bool expandCovering(int line);  // expands every collapsed region hiding `line`; true if any

  // Document change: moves / expands collapsed regions, patches the region set, marks it stale.
  // True when the collapsed set changed.
  bool onChange(const Document::Change& c);
  void clear();  // another document: no regions, nothing collapsed

 private:
  void rebuildHidden();

  std::vector<Region> regions_, collapsed_;
  std::vector<WrapLayout::HiddenRange> hidden_;
  bool fresh_ = false;
  uint32_t gen_ = 1;
};

}  // namespace tf::ui
