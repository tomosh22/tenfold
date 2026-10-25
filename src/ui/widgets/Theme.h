#pragma once
#include "ui/gfx/Renderer.h"

namespace tf::ui {

// Placeholder theme: named colours and metrics with a neutral dark default. B7 loads this from text files; the real
// palette is an open question (OPEN_QUESTIONS #7), so nothing here is meant to look like any other editor.
// Metrics are physical pixels; the app multiplies by the display scale via scaled().
struct Theme {
  using Color = gfx::Color;
  Color background{0.118f, 0.125f, 0.145f, 1};  // behind everything
  Color panelBackground{0.137f, 0.145f, 0.168f, 1};
  Color tabStripBackground{0.098f, 0.104f, 0.121f, 1};
  Color tabActive{0.137f, 0.145f, 0.168f, 1};
  Color tabInactive{0.157f, 0.165f, 0.188f, 1};
  Color tabHover{0.19f, 0.20f, 0.225f, 1};
  Color tabAccent{0.32f, 0.54f, 0.86f, 1};  // line under the active tab
  Color tabInsert{0.32f, 0.54f, 0.86f, 1};
  Color text{0.83f, 0.85f, 0.88f, 1};
  Color textDim{0.52f, 0.55f, 0.60f, 1};
  Color dirtyMarker{0.90f, 0.72f, 0.35f, 1};
  Color closeHover{0.75f, 0.28f, 0.28f, 1};
  Color splitter{0.07f, 0.075f, 0.09f, 1};
  Color splitterHot{0.32f, 0.54f, 0.86f, 1};
  Color statusBackground{0.21f, 0.29f, 0.45f, 1};
  Color statusText{0.93f, 0.95f, 0.98f, 1};
  Color statusHover{0.30f, 0.40f, 0.58f, 1};
  Color gutterBackground{0.098f, 0.104f, 0.121f, 1};
  Color gutterText{0.42f, 0.45f, 0.50f, 1};

  float padding = 8;           // horizontal padding inside tabs and status segments
  float verticalPad = 4;       // added above and below text for bars
  float splitterWidth = 4;     // divider thickness
  float tabMaxWidth = 240;     // longest tab (title is truncated with an ellipsis)
  float accentHeight = 2;      // active tab underline
  float dragThreshold = 4;     // pixels before a press becomes a drag
  float dragOutDistance = 28;  // distance outside a tab strip that starts a drag-out

  Theme scaled(float s) const {
    Theme t = *this;
    for (float* f : {&t.padding, &t.verticalPad, &t.splitterWidth, &t.tabMaxWidth, &t.accentHeight, &t.dragThreshold,
                     &t.dragOutDistance})
      *f *= s;
    return t;
  }
  static Theme darkDefault() { return Theme(); }
};

}  // namespace tf::ui
