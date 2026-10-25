#ifdef _WIN32
#include "ui/gfx/D3d11Renderer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "ui/gfx/WindowsSwapChain.h"

using Microsoft::WRL::ComPtr;

namespace tf::gfx {
namespace {

// Same maths as the GL shaders: pixel -> clip space, y down. R8 = coverage mask, RGBA8 = modulate.
const char* kHlsl = R"(
cbuffer View : register(b0) { float2 uView; float2 pad; };
Texture2D tex : register(t0);
SamplerState smp : register(s0);
struct VIn { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct VOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };
VOut vsMain(VIn i) {
  VOut o;
  o.pos = float4(i.pos.x / uView.x * 2.0 - 1.0, 1.0 - i.pos.y / uView.y * 2.0, 0.0, 1.0);
  o.uv = i.uv;
  o.col = i.col;
  return o;
}
float4 psCoverage(VOut i) : SV_Target { return float4(i.col.rgb, i.col.a * tex.Sample(smp, i.uv).r); }
float4 psModulate(VOut i) : SV_Target { return tex.Sample(smp, i.uv) * i.col; }
)";

uint8_t u8(float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }
int bpp(TextureFormat f) { return f == TextureFormat::R8 ? 1 : 4; }

std::string hrText(const char* what, HRESULT hr) {
  char b[128];
  std::snprintf(b, sizeof b, "%s failed (0x%08lx)", what, static_cast<unsigned long>(hr));
  return b;
}

}  // namespace

struct D3d11Renderer::Impl {
  struct Tex {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    TextureFormat fmt;
    int w, h;
  };
  ComPtr<ID3D11Device> dev;
  ComPtr<ID3D11DeviceContext> ctx;
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11PixelShader> psCov, psMod;
  ComPtr<ID3D11InputLayout> layout;
  ComPtr<ID3D11Buffer> vb, cb;
  ComPtr<ID3D11BlendState> blend;
  ComPtr<ID3D11RasterizerState> raster;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11Texture2D> offTex, staging;
  ComPtr<ID3D11RenderTargetView> offRtv;
  UINT vbCapacity = 0;  // in vertices
  int offW = 0, offH = 0, stagW = 0, stagH = 0;
  std::unordered_map<TextureId, Tex> textures;

  ID3D11RenderTargetView* rtv(const WindowsSwapChain* sc, bool offscreen) const {
    return offscreen || !sc ? offRtv.Get() : sc->rtv();
  }
};

D3d11Renderer::D3d11Renderer() : d_(new Impl) {}

D3d11Renderer::~D3d11Renderer() {
  if (d_ && d_->ctx) d_->ctx->ClearState();
  swap_.reset();  // before the device goes away
}

std::unique_ptr<D3d11Renderer> D3d11Renderer::create(const Options& opt, std::string* err) {
  auto fail = [&](const std::string& m) -> std::unique_ptr<D3d11Renderer> {
    if (err) *err = m;
    return nullptr;
  };
  std::unique_ptr<D3d11Renderer> r(new D3d11Renderer);
  Impl& d = *r->d_;
  r->vsync_ = opt.vsync;

  // Device: hardware first, then WARP. No debug layer (needs the Graphics Tools optional feature).
  const char* warpEnv = std::getenv("TENFOLD_D3D11_WARP");
  const bool forceWarp = opt.forceWarp || (warpEnv && *warpEnv && *warpEnv != '0');
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  HRESULT hr = E_FAIL;
  for (int pass = forceWarp ? 1 : 0; pass < 2; ++pass) {
    hr = D3D11CreateDevice(nullptr, pass == 0 ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr, 0, levels,
                           static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &d.dev, nullptr, &d.ctx);
    if (SUCCEEDED(hr)) {
      r->warp_ = pass == 1;
      break;
    }
  }
  if (FAILED(hr)) return fail(hrText("D3D11CreateDevice (hardware and WARP)", hr));
  {
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> ad;
    DXGI_ADAPTER_DESC desc{};
    if (SUCCEEDED(d.dev.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&ad)) && SUCCEEDED(ad->GetDesc(&desc))) {
      char name[128] = {};
      WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof name - 1, nullptr, nullptr);
      r->adapter_ = name;
    }
  }

  // Shaders (runtime compile).
  auto compile = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& out) {
    ComPtr<ID3DBlob> errBlob;
    HRESULT h = D3DCompile(kHlsl, std::strlen(kHlsl), "tenfold.hlsl", nullptr, nullptr, entry, target,
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &errBlob);
    if (FAILED(h) && err)
      *err = std::string("HLSL ") + entry + ": " +
             (errBlob ? static_cast<const char*>(errBlob->GetBufferPointer()) : hrText("D3DCompile", h).c_str());
    return SUCCEEDED(h);
  };
  ComPtr<ID3DBlob> vsb, pcb, pmb;
  if (!compile("vsMain", "vs_4_0", vsb) || !compile("psCoverage", "ps_4_0", pcb) ||
      !compile("psModulate", "ps_4_0", pmb))
    return nullptr;
  if (FAILED(d.dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &d.vs)) ||
      FAILED(d.dev->CreatePixelShader(pcb->GetBufferPointer(), pcb->GetBufferSize(), nullptr, &d.psCov)) ||
      FAILED(d.dev->CreatePixelShader(pmb->GetBufferPointer(), pmb->GetBufferSize(), nullptr, &d.psMod)))
    return fail("creating shader objects failed");
  const D3D11_INPUT_ELEMENT_DESC il[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, u), D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(Vertex, r), D3D11_INPUT_PER_VERTEX_DATA, 0}};
  if (FAILED(d.dev->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &d.layout)))
    return fail("CreateInputLayout failed");

  // Fixed state: straight-alpha "over" (colour: src.a, 1-src.a; alpha: 1, 1-src.a), no culling, scissor on.
  D3D11_BLEND_DESC bd{};
  auto& rt = bd.RenderTarget[0];
  rt.BlendEnable = TRUE;
  rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
  rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  rt.BlendOp = D3D11_BLEND_OP_ADD;
  rt.SrcBlendAlpha = D3D11_BLEND_ONE;
  rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  D3D11_RASTERIZER_DESC rd{};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_NONE;
  rd.DepthClipEnable = TRUE;
  rd.ScissorEnable = TRUE;
  D3D11_SAMPLER_DESC sd{};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  D3D11_BUFFER_DESC cbd{};
  cbd.ByteWidth = 16;
  cbd.Usage = D3D11_USAGE_DEFAULT;
  cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  if (FAILED(d.dev->CreateBlendState(&bd, &d.blend)) || FAILED(d.dev->CreateRasterizerState(&rd, &d.raster)) ||
      FAILED(d.dev->CreateSamplerState(&sd, &d.sampler)) || FAILED(d.dev->CreateBuffer(&cbd, nullptr, &d.cb)))
    return fail("creating pipeline state failed");

  // 1x1 coverage-1 texture so rects and lines go through the same shader and batch as glyphs.
  r->white_ = r->createTexture(1, 1, TextureFormat::R8);
  const uint8_t one = 255;
  r->updateTexture(r->white_, 0, 0, 1, 1, &one, 1);
  if (!r->white_) return fail("creating the white texture failed");

  if (opt.hwnd) {
    r->swap_ = WindowsSwapChain::create(d.dev.Get(), opt.hwnd, 1, 1, err);
    if (!r->swap_) return nullptr;
    r->offscreen_ = false;
  }
  return r;
}

void D3d11Renderer::setOffscreen(bool on) { offscreen_ = on || !swap_; }

bool D3d11Renderer::present() {
  if (offscreen_ || !swap_) return true;
  return swap_->present(vsync_);
}

void D3d11Renderer::beginFrame(int widthPx, int heightPx, Color c) {
  w_ = std::max(widthPx, 1);
  h_ = std::max(heightPx, 1);
  verts_.clear();
  cmds_.clear();
  scissors_.clear();
  stats_ = {};
  Impl& d = *d_;
  if (offscreen_) {
    if (!d.offTex || d.offW != w_ || d.offH != h_) {
      d.offRtv.Reset();
      d.offTex.Reset();
      D3D11_TEXTURE2D_DESC td{};
      td.Width = static_cast<UINT>(w_);
      td.Height = static_cast<UINT>(h_);
      td.MipLevels = td.ArraySize = 1;
      td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      td.SampleDesc.Count = 1;
      td.Usage = D3D11_USAGE_DEFAULT;
      td.BindFlags = D3D11_BIND_RENDER_TARGET;
      if (FAILED(d.dev->CreateTexture2D(&td, nullptr, &d.offTex)) ||
          FAILED(d.dev->CreateRenderTargetView(d.offTex.Get(), nullptr, &d.offRtv)))
        return;
      d.offW = w_;
      d.offH = h_;
    }
  } else {
    swap_->resize(w_, h_);
    swap_->waitForFrame();  // frame-latency waitable: pace the CPU to the display before recording
  }
  const float rgba[4] = {c.r, c.g, c.b, c.a};
  if (auto* v = d.rtv(swap_.get(), offscreen_)) d.ctx->ClearRenderTargetView(v, rgba);
}

void D3d11Renderer::quad(TextureId tex, bool rgba, const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d) {
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

void D3d11Renderer::drawRect(const RectF& r, Color c) {
  const uint8_t R = u8(c.r), G = u8(c.g), B = u8(c.b), A = u8(c.a);
  const float x1 = r.x + r.w, y1 = r.y + r.h;
  quad(white_, false, {r.x, r.y, 0, 0, R, G, B, A}, {x1, r.y, 0, 0, R, G, B, A}, {r.x, y1, 0, 0, R, G, B, A},
       {x1, y1, 0, 0, R, G, B, A});
}

void D3d11Renderer::drawLine(float x0, float y0, float x1, float y1, float t, Color c) {
  const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0 || t <= 0) return;
  const float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
  const uint8_t R = u8(c.r), G = u8(c.g), B = u8(c.b), A = u8(c.a);
  quad(white_, false, {x0 + nx, y0 + ny, 0, 0, R, G, B, A}, {x1 + nx, y1 + ny, 0, 0, R, G, B, A},
       {x0 - nx, y0 - ny, 0, 0, R, G, B, A}, {x1 - nx, y1 - ny, 0, 0, R, G, B, A});
}

void D3d11Renderer::drawGlyphs(TextureId atlas, const GlyphQuad* q, int count, Color c) {
  auto it = d_->textures.find(atlas);
  if (it == d_->textures.end()) return;
  const bool rgba = it->second.fmt == TextureFormat::RGBA8;
  const uint8_t R = u8(c.r), G = u8(c.g), B = u8(c.b), A = u8(c.a);
  for (int i = 0; i < count; ++i, ++q)
    quad(atlas, rgba, {q->x0, q->y0, q->u0, q->v0, R, G, B, A}, {q->x1, q->y0, q->u1, q->v0, R, G, B, A},
         {q->x0, q->y1, q->u0, q->v1, R, G, B, A}, {q->x1, q->y1, q->u1, q->v1, R, G, B, A});
}

void D3d11Renderer::pushScissor(const RectF& r) {
  // Round outwards to whole pixels, then intersect with the enclosing scissor (identical to GlRenderer).
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

void D3d11Renderer::popScissor() {
  if (!scissors_.empty()) scissors_.pop_back();
}

TextureId D3d11Renderer::createTexture(int w, int h, TextureFormat f) {
  if (w <= 0 || h <= 0) return 0;
  D3D11_TEXTURE2D_DESC td{};
  td.Width = static_cast<UINT>(w);
  td.Height = static_cast<UINT>(h);
  td.MipLevels = td.ArraySize = 1;
  td.Format = f == TextureFormat::R8 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  const std::vector<uint8_t> zero(static_cast<size_t>(w) * h * bpp(f), 0);  // GL path zero-fills too
  D3D11_SUBRESOURCE_DATA init{zero.data(), static_cast<UINT>(w * bpp(f)), 0};
  Impl::Tex t{{}, {}, f, w, h};
  if (FAILED(d_->dev->CreateTexture2D(&td, &init, &t.tex)) ||
      FAILED(d_->dev->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv)))
    return 0;
  const TextureId id = nextId_++;
  d_->textures[id] = std::move(t);
  return id;
}

void D3d11Renderer::updateTexture(TextureId id, int x, int y, int w, int h, const void* pixels, int rowBytes) {
  auto it = d_->textures.find(id);
  if (it == d_->textures.end() || !pixels || w <= 0 || h <= 0) return;
  const Impl::Tex& t = it->second;
  if (x < 0 || y < 0 || x + w > t.w || y + h > t.h) return;
  const int px = bpp(t.fmt);
  if (rowBytes <= 0) rowBytes = w * px;
  // For a box update the source pointer addresses the box's first texel; the pitch may exceed the row width.
  const D3D11_BOX box{static_cast<UINT>(x),     static_cast<UINT>(y),     0,
                      static_cast<UINT>(x + w), static_cast<UINT>(y + h), 1};
  d_->ctx->UpdateSubresource(t.tex.Get(), 0, &box, pixels, static_cast<UINT>(rowBytes), 0);
}

void D3d11Renderer::destroyTexture(TextureId id) { d_->textures.erase(id); }

void D3d11Renderer::endFrame() {
  Impl& d = *d_;
  ID3D11RenderTargetView* target = d.rtv(swap_.get(), offscreen_);
  if (!verts_.empty() && target) {
    const UINT need = static_cast<UINT>(verts_.size());
    if (!d.vb || d.vbCapacity < need) {
      d.vb.Reset();
      d.vbCapacity = std::max<UINT>(need, std::max<UINT>(d.vbCapacity * 2, 6 * 1024));
      D3D11_BUFFER_DESC bd{};
      bd.ByteWidth = d.vbCapacity * sizeof(Vertex);
      bd.Usage = D3D11_USAGE_DYNAMIC;
      bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
      bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      if (FAILED(d.dev->CreateBuffer(&bd, nullptr, &d.vb))) {
        d.vbCapacity = 0;
        scissors_.clear();
        return;
      }
    }
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(d.ctx->Map(d.vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
      scissors_.clear();
      return;
    }
    std::memcpy(m.pData, verts_.data(), verts_.size() * sizeof(Vertex));
    d.ctx->Unmap(d.vb.Get(), 0);

    const float view[4] = {static_cast<float>(w_), static_cast<float>(h_), 0, 0};
    d.ctx->UpdateSubresource(d.cb.Get(), 0, nullptr, view, 0, 0);
    const D3D11_VIEWPORT vp{0, 0, static_cast<float>(w_), static_cast<float>(h_), 0, 1};
    d.ctx->RSSetViewports(1, &vp);
    d.ctx->RSSetState(d.raster.Get());
    d.ctx->OMSetRenderTargets(1, &target, nullptr);
    d.ctx->OMSetBlendState(d.blend.Get(), nullptr, 0xffffffff);
    d.ctx->IASetInputLayout(d.layout.Get());
    d.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const UINT stride = sizeof(Vertex), offset = 0;
    ID3D11Buffer* vbs[1] = {d.vb.Get()};
    d.ctx->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    d.ctx->VSSetShader(d.vs.Get(), nullptr, 0);
    ID3D11Buffer* cbs[1] = {d.cb.Get()};
    d.ctx->VSSetConstantBuffers(0, 1, cbs);
    ID3D11SamplerState* smp[1] = {d.sampler.Get()};
    d.ctx->PSSetSamplers(0, 1, smp);
    const D3D11_RECT full{0, 0, w_, h_};
    for (const Cmd& c : cmds_) {
      auto it = d.textures.find(c.tex);
      if (it == d.textures.end()) continue;  // destroyed since it was recorded
      const D3D11_RECT sc{c.sc.x0, c.sc.y0, c.sc.x1, c.sc.y1};
      d.ctx->RSSetScissorRects(1, c.scissor ? &sc : &full);  // top-left origin, same as our IRect
      d.ctx->PSSetShader(c.rgba ? d.psMod.Get() : d.psCov.Get(), nullptr, 0);
      ID3D11ShaderResourceView* srv[1] = {it->second.srv.Get()};
      d.ctx->PSSetShaderResources(0, 1, srv);
      d.ctx->Draw(static_cast<UINT>(c.count), static_cast<UINT>(c.first));
      ++stats_.drawCalls;
    }
    ID3D11ShaderResourceView* none[1] = {nullptr};
    d.ctx->PSSetShaderResources(0, 1, none);  // textures may be updated/destroyed before the next frame
    stats_.vertices = static_cast<int>(verts_.size());
  }
  scissors_.clear();
}

bool D3d11Renderer::readPixels(int w, int h, uint8_t* rgba) {
  Impl& d = *d_;
  if (w != w_ || h != h_ || !rgba) return false;
  ID3D11Texture2D* src = offscreen_ ? d.offTex.Get() : swap_->backBuffer();
  if (!src) return false;
  if (!d.staging || d.stagW != w || d.stagH != h) {
    d.staging.Reset();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(d.dev->CreateTexture2D(&td, nullptr, &d.staging))) return false;
    d.stagW = w;
    d.stagH = h;
  }
  d.ctx->CopyResource(d.staging.Get(), src);
  D3D11_MAPPED_SUBRESOURCE m{};
  if (FAILED(d.ctx->Map(d.staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
  for (int y = 0; y < h; ++y)  // D3D is already top-down
    std::memcpy(rgba + static_cast<size_t>(y) * w * 4, static_cast<const uint8_t*>(m.pData) + y * m.RowPitch,
                static_cast<size_t>(w) * 4);
  d.ctx->Unmap(d.staging.Get(), 0);
  return true;
}

}  // namespace tf::gfx
#endif  // _WIN32
