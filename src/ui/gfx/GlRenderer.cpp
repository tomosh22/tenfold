#include "ui/gfx/GlRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "ui/gfx/GlLoader.h"

namespace tf::gfx {
namespace {

const char* kVs = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
uniform vec2 uView;
out vec2 vUv;
out vec4 vColor;
void main() {
  vUv = aUv;
  vColor = aColor;
  gl_Position = vec4(aPos.x / uView.x * 2.0 - 1.0, 1.0 - aPos.y / uView.y * 2.0, 0.0, 1.0);
})";

// R8 textures are coverage masks (colour comes from the vertex); RGBA8 textures modulate the vertex colour.
const char* kFs = R"(#version 330 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uTex;
uniform int uRgba;
out vec4 oColor;
void main() {
  vec4 t = texture(uTex, vUv);
  oColor = uRgba != 0 ? t * vColor : vec4(vColor.rgb, vColor.a * t.r);
})";

uint8_t u8(float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }
int bpp(TextureFormat f) { return f == TextureFormat::R8 ? 1 : 4; }

unsigned compile(unsigned type, const char* src, std::string* err) {
  unsigned s = gl::CreateShader(type);
  gl::ShaderSource(s, 1, &src, nullptr);
  gl::CompileShader(s);
  int ok = 0;
  gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024] = {};
    gl::GetShaderInfoLog(s, sizeof log - 1, nullptr, log);
    if (err) *err = std::string("shader compile failed: ") + log;
    gl::DeleteShader(s);
    return 0;
  }
  return s;
}

}  // namespace

std::unique_ptr<GlRenderer> GlRenderer::create(std::string* err) {
  std::unique_ptr<GlRenderer> r(new GlRenderer);
  unsigned vs = compile(gl::VERTEX_SHADER, kVs, err), fs = compile(gl::FRAGMENT_SHADER, kFs, err);
  if (!vs || !fs) return nullptr;
  r->prog_ = gl::CreateProgram();
  gl::AttachShader(r->prog_, vs);
  gl::AttachShader(r->prog_, fs);
  gl::LinkProgram(r->prog_);
  gl::DeleteShader(vs);
  gl::DeleteShader(fs);
  int ok = 0;
  gl::GetProgramiv(r->prog_, gl::LINK_STATUS, &ok);
  if (!ok) {
    char log[1024] = {};
    gl::GetProgramInfoLog(r->prog_, sizeof log - 1, nullptr, log);
    if (err) *err = std::string("program link failed: ") + log;
    return nullptr;
  }
  r->uView_ = gl::GetUniformLocation(r->prog_, "uView");
  r->uTex_ = gl::GetUniformLocation(r->prog_, "uTex");
  r->uRgba_ = gl::GetUniformLocation(r->prog_, "uRgba");

  gl::GenVertexArrays(1, &r->vao_);
  gl::BindVertexArray(r->vao_);
  gl::GenBuffers(1, &r->vbo_);
  gl::BindBuffer(gl::ARRAY_BUFFER, r->vbo_);
  const int stride = sizeof(Vertex);
  gl::EnableVertexAttribArray(0);
  gl::VertexAttribPointer(0, 2, gl::FLOAT, 0, stride, reinterpret_cast<void*>(offsetof(Vertex, x)));
  gl::EnableVertexAttribArray(1);
  gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, stride, reinterpret_cast<void*>(offsetof(Vertex, u)));
  gl::EnableVertexAttribArray(2);
  gl::VertexAttribPointer(2, 4, gl::UNSIGNED_BYTE, 1, stride, reinterpret_cast<void*>(offsetof(Vertex, r)));

  // 1x1 coverage-1 texture so rects and lines go through the same shader and batch as glyphs.
  r->white_ = r->createTexture(1, 1, TextureFormat::R8);
  const uint8_t one = 255;
  r->updateTexture(r->white_, 0, 0, 1, 1, &one, 1);
  return r;
}

GlRenderer::~GlRenderer() {
  for (auto& [id, f] : textures_) gl::DeleteTextures(1, &id);
  if (fboTex_) gl::DeleteTextures(1, &fboTex_);
  if (fbo_) gl::DeleteFramebuffers(1, &fbo_);
  if (vbo_) gl::DeleteBuffers(1, &vbo_);
  if (vao_) gl::DeleteVertexArrays(1, &vao_);
  if (prog_) gl::DeleteProgram(prog_);
}

void GlRenderer::bindTarget() {
  if (!offscreen_) {
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    return;
  }
  if (!fbo_) gl::GenFramebuffers(1, &fbo_);
  if (!fboTex_ || fboW_ != w_ || fboH_ != h_) {
    if (fboTex_) gl::DeleteTextures(1, &fboTex_);
    gl::GenTextures(1, &fboTex_);
    gl::BindTexture(gl::TEXTURE_2D, fboTex_);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w_, h_, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, fboTex_, 0);
    fboW_ = w_;
    fboH_ = h_;
  }
  gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
}

void GlRenderer::beginFrame(int widthPx, int heightPx, Color c) {
  w_ = std::max(widthPx, 1);
  h_ = std::max(heightPx, 1);
  verts_.clear();
  cmds_.clear();
  scissors_.clear();
  stats_ = {};
  bindTarget();
  gl::Viewport(0, 0, w_, h_);
  gl::Disable(gl::SCISSOR_TEST);
  gl::ClearColor(c.r, c.g, c.b, c.a);
  gl::Clear(gl::COLOR_BUFFER_BIT);
}

void GlRenderer::quad(unsigned tex, bool rgba, const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d) {
  const bool sc = !scissors_.empty();
  const IRect s = sc ? scissors_.back() : IRect{0, 0, 0, 0};
  if (sc && (s.x1 <= s.x0 || s.y1 <= s.y0)) return;  // fully clipped
  if (cmds_.empty() || cmds_.back().tex != tex || cmds_.back().scissor != sc ||
      (sc && std::memcmp(&cmds_.back().sc, &s, sizeof s) != 0))
    cmds_.push_back({tex, rgba, sc, s, static_cast<int>(verts_.size()), 0});
  cmds_.back().count += 6;
  const Vertex v[6] = {a, b, c, c, b, d};
  verts_.insert(verts_.end(), v, v + 6);
}

void GlRenderer::drawRect(const RectF& r, Color c) {
  const uint8_t R = u8(c.r), G = u8(c.g), B = u8(c.b), A = u8(c.a);
  const float x1 = r.x + r.w, y1 = r.y + r.h;
  quad(white_, false, {r.x, r.y, 0, 0, R, G, B, A}, {x1, r.y, 0, 0, R, G, B, A}, {r.x, y1, 0, 0, R, G, B, A},
       {x1, y1, 0, 0, R, G, B, A});
}

void GlRenderer::drawLine(float x0, float y0, float x1, float y1, float t, Color c) {
  const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0 || t <= 0) return;
  const float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
  const uint8_t R = u8(c.r), G = u8(c.g), B = u8(c.b), A = u8(c.a);
  quad(white_, false, {x0 + nx, y0 + ny, 0, 0, R, G, B, A}, {x1 + nx, y1 + ny, 0, 0, R, G, B, A},
       {x0 - nx, y0 - ny, 0, 0, R, G, B, A}, {x1 - nx, y1 - ny, 0, 0, R, G, B, A});
}

void GlRenderer::drawGlyphs(TextureId atlas, const GlyphQuad* q, int count, Color c) {
  auto it = textures_.find(atlas);
  if (it == textures_.end()) return;
  const bool rgba = it->second == TextureFormat::RGBA8;
  const uint8_t R = u8(c.r), G = u8(c.g), B = u8(c.b), A = u8(c.a);
  for (int i = 0; i < count; ++i, ++q)
    quad(atlas, rgba, {q->x0, q->y0, q->u0, q->v0, R, G, B, A}, {q->x1, q->y0, q->u1, q->v0, R, G, B, A},
         {q->x0, q->y1, q->u0, q->v1, R, G, B, A}, {q->x1, q->y1, q->u1, q->v1, R, G, B, A});
}

void GlRenderer::pushScissor(const RectF& r) {
  // Round outwards to whole pixels, then intersect with the enclosing scissor.
  IRect s{static_cast<int>(std::floor(r.x)), static_cast<int>(std::floor(r.y)), static_cast<int>(std::ceil(r.x + r.w)),
          static_cast<int>(std::ceil(r.y + r.h))};
  s.x0 = std::clamp(s.x0, 0, w_);
  s.x1 = std::clamp(s.x1, 0, w_);
  s.y0 = std::clamp(s.y0, 0, h_);
  s.y1 = std::clamp(s.y1, 0, h_);
  if (!scissors_.empty()) {
    const IRect& p = scissors_.back();
    s = {std::max(s.x0, p.x0), std::max(s.y0, p.y0), std::min(s.x1, p.x1), std::min(s.y1, p.y1)};
  }
  scissors_.push_back(s);
}

void GlRenderer::popScissor() {
  if (!scissors_.empty()) scissors_.pop_back();
}

TextureId GlRenderer::createTexture(int w, int h, TextureFormat f) {
  unsigned id = 0;
  gl::GenTextures(1, &id);
  gl::BindTexture(gl::TEXTURE_2D, id);
  gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
  gl::PixelStorei(gl::UNPACK_ROW_LENGTH, 0);
  const std::vector<uint8_t> zero(static_cast<size_t>(w) * h * bpp(f), 0);
  const bool r8 = f == TextureFormat::R8;
  gl::TexImage2D(gl::TEXTURE_2D, 0, r8 ? gl::R8 : gl::RGBA8, w, h, 0, r8 ? gl::RED : gl::RGBA, gl::UNSIGNED_BYTE,
                 zero.data());
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
  textures_[id] = f;
  return id;
}

void GlRenderer::updateTexture(TextureId t, int x, int y, int w, int h, const void* pixels, int rowBytes) {
  auto it = textures_.find(t);
  if (it == textures_.end() || !pixels || w <= 0 || h <= 0) return;
  const int px = bpp(it->second);
  const bool r8 = it->second == TextureFormat::R8;
  if (rowBytes <= 0) rowBytes = w * px;
  gl::BindTexture(gl::TEXTURE_2D, t);
  gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
  if (rowBytes % px == 0) {
    gl::PixelStorei(gl::UNPACK_ROW_LENGTH, rowBytes / px);
    gl::TexSubImage2D(gl::TEXTURE_2D, 0, x, y, w, h, r8 ? gl::RED : gl::RGBA, gl::UNSIGNED_BYTE, pixels);
    gl::PixelStorei(gl::UNPACK_ROW_LENGTH, 0);
  } else {
    for (int row = 0; row < h; ++row)
      gl::TexSubImage2D(gl::TEXTURE_2D, 0, x, y + row, w, 1, r8 ? gl::RED : gl::RGBA, gl::UNSIGNED_BYTE,
                        static_cast<const uint8_t*>(pixels) + static_cast<size_t>(row) * rowBytes);
  }
}

void GlRenderer::destroyTexture(TextureId t) {
  if (textures_.erase(t)) gl::DeleteTextures(1, &t);
}

void GlRenderer::endFrame() {
  if (!verts_.empty()) {
    gl::Enable(gl::BLEND);
    gl::BlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(prog_);
    gl::Uniform2f(uView_, static_cast<float>(w_), static_cast<float>(h_));
    gl::Uniform1i(uTex_, 0);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(verts_.size() * sizeof(Vertex)), verts_.data(),
                   gl::STREAM_DRAW);
    bool scissorOn = false;
    for (const Cmd& c : cmds_) {
      if (c.scissor) {
        if (!scissorOn) gl::Enable(gl::SCISSOR_TEST);
        gl::Scissor(c.sc.x0, h_ - c.sc.y1, c.sc.x1 - c.sc.x0, c.sc.y1 - c.sc.y0);  // GL origin is bottom-left
      } else if (scissorOn) {
        gl::Disable(gl::SCISSOR_TEST);
      }
      scissorOn = c.scissor;
      gl::BindTexture(gl::TEXTURE_2D, c.tex);
      gl::Uniform1i(uRgba_, c.rgba ? 1 : 0);
      gl::DrawArrays(gl::TRIANGLES, c.first, c.count);
      ++stats_.drawCalls;
    }
    if (scissorOn) gl::Disable(gl::SCISSOR_TEST);
    stats_.vertices = static_cast<int>(verts_.size());
  }
  scissors_.clear();
}

bool GlRenderer::readPixels(int w, int h, uint8_t* rgba) {
  if (w != w_ || h != h_ || !rgba) return false;
  bindTarget();
  gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
  gl::ReadPixels(0, 0, w, h, gl::RGBA, gl::UNSIGNED_BYTE, rgba);
  const size_t row = static_cast<size_t>(w) * 4;
  std::vector<uint8_t> tmp(row);
  for (int y = 0; y < h / 2; ++y) {  // GL is bottom-up; callers want top-left origin
    uint8_t *a = rgba + y * row, *b = rgba + (h - 1 - y) * row;
    std::memcpy(tmp.data(), a, row);
    std::memcpy(a, b, row);
    std::memcpy(b, tmp.data(), row);
  }
  return gl::GetError() == gl::NO_ERROR;
}

}  // namespace tf::gfx
