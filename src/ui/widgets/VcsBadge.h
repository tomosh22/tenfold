#pragma once
#include <algorithm>
#include <cmath>

#include "ui/widgets/TabStrip.h"
#include "ui/widgets/Theme.h"
#include "ui/widgets/Widget.h"

// Version-control marks shared by the tab strip and the explorer (VS-VCS, OPEN_QUESTIONS #16, VS_BEHAVIOUR 4 VC3).
namespace tf::ui {

// The colour of `m` in `th`. Optional theme keys with fallbacks (the Perforce badge colours of P4VS are not sourced as
// values: red / blue / yellow are tenfold's choice, vs-dark.txt marks them [?]).
inline Color vcsMarkColor(const Theme& th, TabStrip::VcsMark m) {
  using M = TabStrip::VcsMark;
  switch (m) {
    case M::Modified: return Theme::orElse(th.tabVcsModified, th.dirtyMarker);
    case M::Added: return Theme::orElse(th.tabVcsAdded, th.vcsAdded());
    case M::CheckedOut: return Theme::orElse(th.tabVcsCheckedOut, th.vcsDeleted());
    case M::ReadOnly: return Theme::orElse(th.tabVcsReadOnly, th.textDim);
    case M::OpenedForAdd: return Theme::orElse(th.tabVcsOpenedForAdd, th.vcsDeleted());
    case M::NotInDepot: return Theme::orElse(th.tabVcsNotInDepot, Color{0.341f, 0.612f, 0.839f, 1});
    case M::OutOfDate: return Theme::orElse(th.tabVcsOutOfDate, Color{0.863f, 0.733f, 0.329f, 1});
    case M::Conflict: return Theme::orElse(th.tabVcsConflict, th.diagnosticError);
    case M::None: break;
  }
  return th.dirtyMarker;
}

// True for the marks the explorer draws as a P4VS-style glyph (check, plus, ?, triangle) instead of a square.
inline bool vcsMarkHasGlyph(TabStrip::VcsMark m) {
  using M = TabStrip::VcsMark;
  return m == M::CheckedOut || m == M::OpenedForAdd || m == M::NotInDepot || m == M::OutOfDate || m == M::Conflict;
}

// Draws the glyph of `m` in the square `box` with pixel rectangles (no font needed): a check mark, a plus, a question
// mark (NotInDepot and Conflict) or a triangle. Cells of a 5 x 7 bitmap.
inline void drawVcsGlyph(PaintContext& ctx, TabStrip::VcsMark m, const RectF& box, Color c) {
  using M = TabStrip::VcsMark;
  static const char* const kCheck[7] = {".....", "....#", "...##", "#.##.", ".###.", "..#..", "....."};
  static const char* const kPlus[7] = {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."};
  static const char* const kQuestion[7] = {".###.", "#...#", "....#", "..##.", "..#..", ".....", "..#.."};
  static const char* const kTriangle[7] = {".....", "..#..", "..#..", ".###.", ".###.", "#####", "#####"};
  const char* const* bm = m == M::CheckedOut ? kCheck
                          : m == M::OpenedForAdd ? kPlus
                          : m == M::OutOfDate ? kTriangle
                                              : kQuestion;
  const float cs = std::max(1.0f, std::floor(std::min(box.w / 5.0f, box.h / 7.0f)));
  const float x0 = box.x + std::floor((box.w - 5 * cs) / 2), y0 = box.y + std::floor((box.h - 7 * cs) / 2);
  for (int y = 0; y < 7; ++y)
    for (int x = 0; x < 5; ++x)
      if (bm[y][x] == '#') ctx.fill({x0 + x * cs, y0 + y * cs, cs, cs}, c);
}

}  // namespace tf::ui
