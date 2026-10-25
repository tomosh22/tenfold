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

  // Optional colours (B7b). Fully transparent (the default) means "keep the built-in look", so the dark and light
  // themes paint exactly as before; a theme that sets one (alpha > 0) overrides it.
  Color editorBackground{0, 0, 0, 0};      // text area; unset = panelBackground
  Color currentLineFill{0, 0, 0, 0};       // current-line band; unset = panelBackground mixed 5% towards text
  Color currentLineBorder{0, 0, 0, 0};     // 1px outline of the current-line band; unset = none
  Color selectionActive{0, 0, 0, 0};       // editor selection in the focused view; unset = tabAccent at 38%
  Color selectionInactive{0, 0, 0, 0};     // editor selection in an unfocused view; unset = not drawn
  Color findHighlight{0, 0, 0, 0};         // find / replace matches; unset = dirtyMarker at 30%
  Color findHighlightCurrent{0, 0, 0, 0};  // the current match; unset = dirtyMarker at 65%
  Color editorCaret{0, 0, 0, 0};           // editor caret in the focused view; unset = text
  Color tabActiveText{0, 0, 0, 0};         // title of the active tab; unset = text
  Color tabInactiveText{0, 0, 0, 0};       // title of an inactive, un-hovered tab; unset = textDim
  Color popupBackground{0, 0, 0, 0};       // pickers, completion list, find box; unset = tabInactive
  Color popupBorder{0, 0, 0, 0};           // their 1px border; unset = splitterHot
  // Debugger (C2c). Breakpoint glyph: unset = #E51400; current statement line / arrow: unset = dirtyMarker at 25% /
  // dirtyMarker.
  Color debugBreakpoint{0, 0, 0, 0};
  Color debugCurrentLine{0, 0, 0, 0};
  Color debugCurrentArrow{0, 0, 0, 0};
  // Status bar while the session is Running or Break: unset = unchanged look.
  Color statusBackgroundDebugging{0, 0, 0, 0};
  Color statusTextDebugging{0, 0, 0, 0};
  static Color orElse(const Color& c, const Color& fallback) { return c.a > 0 ? c : fallback; }

  // Version control (C3c), optional: unset keeps the built-in fallback named in the comment, so dark and light still
  // show change marks (they need a colour), while vs-dark carries the Visual Studio values.
  Color diffAdded{0, 0, 0, 0};            // gutter strip / scrollbar mark of added lines; unset = green #57a64a
  Color diffModified{0, 0, 0, 0};         // modified lines; unset = tabAccent
  Color diffDeleted{0, 0, 0, 0};          // the wedge of deleted lines; unset = diagnosticError
  Color diffBlockBackground{0, 0, 0, 0};  // rows of an inline diff block; unset = panelBackground 20% towards deleted
  Color diffBlockText{0, 0, 0, 0};        // the base (old) lines in that block; unset = textDim
  Color diffBlockHeader{0, 0, 0, 0};      // the block's header row with the buttons; unset = tabInactive
  Color tabVcsModified{0, 0, 0, 0};       // tab marker, file modified (Git / Perforce); unset = dirtyMarker
  Color tabVcsAdded{0, 0, 0, 0};          // new / untracked / opened for add; unset = the added colour
  Color tabVcsCheckedOut{0, 0, 0, 0};     // Perforce checked out; unset = the deleted colour (red)
  Color tabVcsReadOnly{0, 0, 0, 0};       // Perforce read-only, not checked out (outlined marker); unset = textDim
  Color vcsAdded() const { return orElse(diffAdded, {0.341f, 0.651f, 0.290f, 1}); }
  Color vcsModified() const { return orElse(diffModified, tabAccent); }
  Color vcsDeleted() const { return orElse(diffDeleted, diagnosticError); }

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
