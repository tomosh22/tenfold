#pragma once
#include <memory>
#include <string>
#include <vector>

#include "ui/gfx/Atlas.h"
#include "ui/gfx/Font.h"
#include "ui/gfx/Renderer.h"
#include "ui/gfx/TextLayout.h"

namespace tf::gfx {

struct TextViewStyle {
  Color background{0.118f, 0.125f, 0.145f, 1};
  Color gutterBackground{0.098f, 0.104f, 0.121f, 1};
  Color text{0.83f, 0.85f, 0.88f, 1};
  Color gutterText{0.42f, 0.45f, 0.50f, 1};
};

// B1c glue: draws a plain list of lines with a line-number gutter using FontStack + Atlas + TextLayout. This is the
// minimal "text on screen" path; the real TextView widget (B2) replaces the policy but can reuse this class.
//
// Per frame: layout all visible lines (may rasterize into the Atlas), Atlas::flush (once, before any glyph is drawn),
// then all background rects, then one drawGlyphs per colour run. Not thread-safe.
//
// Atlas upload policy: Atlas::flush uploads the whole texture after it is (re)created, because GlRenderer::createTexture
// leaves the contents undefined, and only dirty rects afterwards. Kept as is: growth is rare and 512^2 is 256 KiB.
class TextRenderer {
 public:
  // Loads the font at `fontPath` (empty = discoverMonospaceFont("")) plus a fallback font when one is installed.
  static std::unique_ptr<TextRenderer> create(const std::string& fontPath, float sizePx, float dpiScale,
                                              std::string* err = nullptr);

  float lineHeight() const { return layout_.lineHeight(); }
  float cellWidth() const { return layout_.cellWidth(); }
  const std::string& fontPath() const { return fontPath_; }
  void setStyle(const TextViewStyle& s) { style_ = s; }

  // Largest useful scrollY (pixels) for `lineCount` lines in a viewport `heightPx` tall.
  float maxScroll(size_t lineCount, int heightPx) const;

  // beginFrame + draw; the caller calls endFrame(). scrollY is clamped to [0, maxScroll].
  void draw(Renderer& r, int widthPx, int heightPx, const std::vector<std::string>& lines, float scrollY);

  void releaseTexture(Renderer& r) { atlas_.releaseTexture(r); }

 private:
  explicit TextRenderer(FontStack fonts);

  FontStack fonts_;  // declared before layout_, which keeps a reference to it
  Atlas atlas_;
  TextLayout layout_;
  TextViewStyle style_;
  LineLayout textQuads_, gutterQuads_;
  std::string fontPath_, number_;
};

}  // namespace tf::gfx
