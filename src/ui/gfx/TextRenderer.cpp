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

std::unique_ptr<TextRenderer> TextRenderer::fromFonts(FontStack fonts) {
  return std::unique_ptr<TextRenderer>(new TextRenderer(std::move(fonts)));
}

float TextRenderer::measure(std::string_view utf8) const {
  return static_cast<float>(layout_.columnCount(utf8)) * cellWidth();
}

std::string TextRenderer::fit(std::string_view utf8, float maxWidth) const {
  const int cols = static_cast<int>(maxWidth / cellWidth());
  if (layout_.columnCount(utf8) <= cols) return std::string(utf8);
  if (cols < 1) return std::string();
  return std::string(utf8.substr(0, layout_.byteForColumn(utf8, cols - 1))) + "\xE2\x80\xA6";
}

void TextRenderer::drawText(float x, float y, std::string_view utf8, Color color, const RectF& clip) {
  if (utf8.empty() || clip.w <= 0 || clip.h <= 0 || y + lineHeight() <= clip.y || y >= clip.y + clip.h ||
      x >= clip.x + clip.w)
    return;
  if (pending_.empty()) pendingGen_ = atlas_.generation();
  const FontMetrics& m = fonts_.metrics();
  const float base = y + std::round((lineHeight() - (m.ascender + m.descender)) / 2 + m.ascender);
  tmp_.clear();
  static const std::vector<ColorSpan> noSpans;
  layout_.layoutLine(utf8, x, base, 0, clip.x + clip.w - x, noSpans, 0, tmp_);
  Pending* p = nullptr;
  for (Pending& q : pending_)
    if (q.color.r == color.r && q.color.g == color.g && q.color.b == color.b && q.color.a == color.a) p = &q;
  if (!p) p = &pending_.emplace_back(Pending{color, {}});
  const float cx1 = clip.x + clip.w, cy1 = clip.y + clip.h;
  for (GlyphQuad q : tmp_.quads) {
    if (q.x1 <= clip.x || q.x0 >= cx1 || q.y1 <= clip.y || q.y0 >= cy1) continue;
    auto trim = [](float& a0, float& a1, float& t0, float& t1, float lo, float hi) {
      const float span = a1 - a0, k = (t1 - t0) / span;
      if (a0 < lo) t0 += (lo - a0) * k, a0 = lo;
      if (a1 > hi) t1 -= (a1 - hi) * k, a1 = hi;
    };
    trim(q.x0, q.x1, q.u0, q.u1, clip.x, cx1);
    trim(q.y0, q.y1, q.v0, q.v1, clip.y, cy1);
    p->quads.push_back(q);
  }
}

bool TextRenderer::flushText(Renderer& r) {
  const bool stale = !pending_.empty() && atlas_.generation() != pendingGen_;
  atlas_.flush(r);
  if (!stale)
    for (const Pending& p : pending_)
      if (!p.quads.empty()) r.drawGlyphs(atlas_.texture(), p.quads.data(), static_cast<int>(p.quads.size()), p.color);
  for (Pending& p : pending_) p.quads.clear();
  pending_.clear();
  return !stale;
}

}  // namespace tf::gfx
