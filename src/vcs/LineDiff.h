#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/Position.h"

namespace tf {

// Line diff between a base text (the committed / have revision) and the live buffer.
//
// Texts are lines WITHOUT terminators, split exactly like TextBuffer::setText: "a\nb\n" is {"a","b",""} (so there is
// always at least one line), a '\r' directly before a '\n' is dropped. Comparing two texts that went through this
// split is therefore independent of the line-ending convention (core.autocrlf) of either side.
std::vector<std::string> splitLines(std::string_view text);
// `count` lines starting at `first`, joined with '\n' (no trailing terminator).
std::string joinLines(const std::vector<std::string>& lines, size_t first, size_t count);

enum class HunkKind { Added, Modified, Deleted };

// One contiguous change. All indices are 0-based line indices.
//  * baseStart/baseCount: the lines of the base that were replaced (baseCount 0 = pure insertion; baseStart is the
//    base line the inserted text precedes).
//  * newStart/newCount: the lines of the live text that replace them (newCount 0 = pure deletion; newStart is the
//    live line that now follows the deleted block, i.e. where the deleted lines used to be).
// Hunks are ordered and never overlap or touch each other.
struct DiffHunk {
  int baseStart = 0, baseCount = 0;
  int newStart = 0, newCount = 0;
  HunkKind kind = HunkKind::Modified;
  bool operator==(const DiffHunk& o) const {
    return baseStart == o.baseStart && baseCount == o.baseCount && newStart == o.newStart && newCount == o.newCount &&
           kind == o.kind;
  }
};

struct DiffOptions {
  // Myers edit-distance cap (inserted + deleted lines in the part between the common prefix and suffix). Beyond it
  // the whole middle part degrades to ONE hunk. The search needs O(maxEditDistance^2 / 2) ints of memory.
  int maxEditDistance = 3000;
};

// Myers O(ND) diff with a common prefix / suffix fast path and the cap above.
std::vector<DiffHunk> diffLines(const std::vector<std::string>& base, const std::vector<std::string>& current,
                                const DiffOptions& opt = {});

// The base lines a hunk replaced (for the inline diff display).
std::vector<std::string> hunkBaseLines(const DiffHunk& h, const std::vector<std::string>& base);

// The edit that reverts one hunk: replace `range` of the live text by `text`. `current` must be the lines the hunk
// was computed against (for the line lengths of the range end).
struct HunkRevert {
  Range range;
  std::string text;
};
HunkRevert revertHunkEdit(const DiffHunk& h, const std::vector<std::string>& base,
                          const std::vector<std::string>& current);

}  // namespace tf
