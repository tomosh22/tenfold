#include "ui/gfx/TextRenderer.h"

#include <algorithm>
#include <cmath>

namespace tf::gfx {

TextRenderer::TextRenderer(FontStack fonts) : fonts_(std::move(fonts)), layout_(fonts_, atlas_, 4) {}

std::unique_ptr<TextRenderer> TextRenderer::create(const std::string& fontPath, float sizePx, float dpiScale,
                                                   std::string* err) {
  const std::string path = discoverMonospaceFont(fontPath);
  if (path.empty()) {
    if (err) *err = "no monospace font found";
    return nullptr;
  }
  FontOptions opt;
  opt.sizePx = sizePx;
  opt.dpiScale = dpiScale;
  std::shared_ptr<FtFont> primary = FtFont::fromFile(path, opt, err);
  if (!primary) return nullptr;
  FontStack stack;
  stack.add(std::move(primary));
  const std::string fb = discoverFallbackFont();
  if (!fb.empty())
    if (std::shared_ptr<FtFont> f = FtFont::fromFile(fb, opt)) stack.add(std::move(f));
  std::unique_ptr<TextRenderer> tr(new TextRenderer(std::move(stack)));
  tr->fontPath_ = path;
  return tr;
}

float TextRenderer::maxScroll(size_t lineCount, int heightPx) const {
  const float content = static_cast<float>(lineCount) * lineHeight();
  return std::max(0.0f, content - static_cast<float>(heightPx) * 0.5f);  // scroll until half a page is left
}

void TextRenderer::draw(Renderer& r, int widthPx, int heightPx, const std::vector<std::string>& lines, float scrollY) {
  const float lh = lineHeight(), cw = cellWidth(), h = static_cast<float>(heightPx);
  const FontMetrics& m = fonts_.metrics();
  scrollY = std::floor(std::clamp(scrollY, 0.0f, maxScroll(lines.size(), heightPx)));

  int digits = 3;
  for (size_t n = lines.size(); n >= 1000; n /= 10) ++digits;
  const float gutterW = (static_cast<float>(digits) + 2) * cw, textX = gutterW + cw / 2;
  const float baseOff = std::round((lh - (m.ascender + m.descender)) / 2 + m.ascender);

  const size_t first = static_cast<size_t>(scrollY / lh);
  const size_t last = std::min(lines.size(), static_cast<size_t>((scrollY + h) / lh) + 1);
  textQuads_.clear();
  gutterQuads_.clear();
  const float visW = std::max(0.0f, static_cast<float>(widthPx) - textX);
  const std::vector<ColorSpan> noSpans;
  for (size_t i = first; i < last; ++i) {
    const float base = static_cast<float>(i) * lh - scrollY + baseOff;
    layout_.layoutLine(lines[i], textX, base, 0, visW, noSpans, 0, textQuads_);
    number_ = std::to_string(i + 1);
    const float nx = gutterW - cw - static_cast<float>(number_.size()) * cw;  // right-aligned, one cell of padding
    layout_.layoutLine(number_, nx, base, 0, gutterW, noSpans, 0, gutterQuads_);
  }

  atlas_.flush(r);  // after all layout (which may add glyphs), before any glyph draw

  r.beginFrame(widthPx, heightPx, style_.background);
  r.drawRect({0, 0, gutterW, h}, style_.gutterBackground);
  // Glyphs after all rects: GlRenderer keeps draw order and would split batches if they interleaved.
  auto emit = [&](const LineLayout& l, Color c) {
    for (const ColorRun& run : l.runs)
      r.drawGlyphs(atlas_.texture(), &l.quads[run.firstQuad], static_cast<int>(run.count), c);
  };
  emit(gutterQuads_, style_.gutterText);
  r.pushScissor({textX, 0, visW, h});
  emit(textQuads_, style_.text);
  r.popScissor();
}

}  // namespace tf::gfx
