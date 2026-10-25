#pragma once
#include <map>
#include <string>
#include <string_view>
#include <vector>

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
  // Editor colours (B7). Not used by the B3a widgets; TextView reads them.
  Color currentLine{0.149f, 0.161f, 0.188f, 1};
  Color selection{0.184f, 0.290f, 0.471f, 1};
  Color caret{0.83f, 0.85f, 0.88f, 1};
  Color columnGuide{0.173f, 0.184f, 0.212f, 1};
  Color whitespace{0.227f, 0.243f, 0.278f, 1};
  Color panelBorder{0.07f, 0.075f, 0.09f, 1};
  // Editor decorations (B2b): diagnostics squiggles and scrollbar marks, scrollbar, matching bracket.
  Color diagnosticError{0.945f, 0.298f, 0.298f, 1};      // #f14c4c
  Color diagnosticWarning{0.898f, 0.722f, 0.231f, 1};    // #e5b83b
  Color diagnosticInfo{0.302f, 0.667f, 0.988f, 1};       // #4daafc
  Color matchMarker{0.322f, 0.541f, 0.859f, 1};          // search matches in the scrollbar lane, #528adb
  Color scrollbarTrack{0.098f, 0.106f, 0.122f, 1};       // #191b1f
  Color scrollbarThumb{0.349f, 0.376f, 0.439f, 0.549f};  // #596070 at 55%
  Color bracketMatch{0.322f, 0.541f, 0.859f, 0.349f};    // box behind a matching bracket pair

  // Build output (C1): squiggles, scrollbar marks and panel lines of the last build, apart from clangd's.
  Color buildError{1.0f, 0.478f, 0.271f, 1};      // #ff7a45
  Color buildWarning{0.784f, 0.847f, 0.290f, 1};  // #c8d84a

  // Token colours by name: lexical kinds ("keyword", "comment", ...) and clangd semantic token types ("method"), each
  // optionally narrowed by a modifier ("method.static"). Filled by the theme loader; empty means "use text".
  std::map<std::string, Color> tokens;

  // Colour for a token of `type` with `modifiers` (e.g. {"static", "readonly"}). Order: type.modifier for each
  // modifier, then type, then the fallback parent (method -> function -> identifier), ..., finally `text`.
  Color tokenColor(std::string_view type, const std::vector<std::string>& modifiers = {}) const {
    std::string t(type);
    for (int guard = 0; !t.empty() && guard < 16; ++guard) {
      for (const auto& m : modifiers) {
        auto it = tokens.find(t + "." + m);
        if (it != tokens.end()) return it->second;
      }
      auto it = tokens.find(t);
      if (it != tokens.end()) return it->second;
      t = tokenFallback(t);
    }
    return text;
  }
  // Parent of a token kind in the fallback chain; empty at the end of the chain.
  static std::string tokenFallback(std::string_view t) {
    static const std::map<std::string, std::string, std::less<>> parents = {{"method", "function"},
                                                                            {"function", "identifier"},
                                                                            {"macro", "preprocessor"},
                                                                            {"property", "variable"},
                                                                            {"parameter", "variable"},
                                                                            {"variable", "identifier"},
                                                                            {"enumMember", "variable"},
                                                                            {"namespace", "identifier"},
                                                                            {"type", "identifier"},
                                                                            {"class", "type"},
                                                                            {"struct", "type"},
                                                                            {"interface", "type"},
                                                                            {"enum", "type"},
                                                                            {"typeParameter", "type"},
                                                                            {"concept", "type"},
                                                                            {"char", "string"},
                                                                            {"punctuation", "operator"},
                                                                            {"event", "variable"},
                                                                            {"decorator", "preprocessor"},
                                                                            {"modifier", "keyword"},
                                                                            {"regexp", "string"}};
    auto it = parents.find(t);
    return it == parents.end() ? std::string() : it->second;
  }

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
