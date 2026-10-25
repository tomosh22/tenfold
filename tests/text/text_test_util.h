#pragma once
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "test_main.h"
#include "ui/gfx/Atlas.h"
#include "ui/gfx/Font.h"
#include "ui/gfx/Renderer.h"

namespace tftext {
using namespace tf::gfx;

// Real monospace font on this machine, or nullptr (tests then print a skip message).
inline std::shared_ptr<FtFont> loadMono(float px = 16, float dpi = 1) {
  std::string path = discoverMonospaceFont("");
  if (path.empty()) {
    std::cerr << "    SKIP: no monospace font found\n";
    return nullptr;
  }
  FontOptions o;
  o.sizePx = px;
  o.dpiScale = dpi;
  std::string err;
  auto f = FtFont::fromFile(path, o, &err);
  if (!f) std::cerr << "    SKIP: cannot load " << path << ": " << err << "\n";
  return f;
}

// Deterministic font: has glyphs for the codepoints in `cps` (index = position + 1), every glyph an 5x8 solid block.
class MockFont : public FontSource {
 public:
  MockFont(std::u32string cps, float advance = 8) : cps_(std::move(cps)) {
    m_.ascender = 10;
    m_.descender = 3;
    m_.lineHeight = 14;
    m_.advance = advance;
    m_.underlinePos = 2;
    m_.underlineThickness = 1;
  }
  const FontMetrics& metrics() const override { return m_; }
  uint32_t sizeKey() const override { return 16 * 64; }
  uint32_t glyphIndex(char32_t cp) const override {
    size_t i = cps_.find(cp);
    return i == std::u32string::npos ? 0 : static_cast<uint32_t>(i + 1);
  }

 protected:
  GlyphBitmap rasterizeGlyph(uint32_t g) override {
    GlyphBitmap b;
    if (cps_[g - 1] == U' ') return b;
    b.width = 5;
    b.height = 8;
    b.bearingX = 1;
    b.bearingY = 8;
    b.advance = m_.advance;
    b.pixels.assign(40, static_cast<uint8_t>(g));
    return b;
  }

 private:
  std::u32string cps_;
  FontMetrics m_;
};

struct FakeRenderer : Renderer {
  struct Create {
    int w, h;
  };
  struct Update {
    TextureId t;
    int x, y, w, h, rowBytes;
  };
  std::vector<Create> creates;
  std::vector<Update> updates;
  int destroys = 0;
  TextureId nextId = 1;
  void beginFrame(int, int, Color) override {}
  void drawRect(const RectF&, Color) override {}
  void drawLine(float, float, float, float, float, Color) override {}
  void drawGlyphs(TextureId, const GlyphQuad*, int, Color) override {}
  void pushScissor(const RectF&) override {}
  void popScissor() override {}
  TextureId createTexture(int w, int h, TextureFormat) override {
    creates.push_back({w, h});
    return nextId++;
  }
  void updateTexture(TextureId t, int x, int y, int w, int h, const void*, int rowBytes) override {
    updates.push_back({t, x, y, w, h, rowBytes});
  }
  void destroyTexture(TextureId) override { ++destroys; }
  void endFrame() override {}
  bool readPixels(int, int, uint8_t*) override { return false; }
};
}  // namespace tftext
