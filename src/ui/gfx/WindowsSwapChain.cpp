#ifdef _WIN32
#include "ui/gfx/WindowsSwapChain.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#include <dxgi1_3.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace tf::gfx {

namespace {
constexpr UINT kFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
std::string hrText(const char* what, HRESULT hr) {
  char b[96];
  std::snprintf(b, sizeof b, "%s failed (0x%08lx)", what, static_cast<unsigned long>(hr));
  return b;
}
}  // namespace

struct WindowsSwapChain::Impl {
  ComPtr<IDXGISwapChain1> sc;
  ComPtr<ID3D11Device> dev;
  ComPtr<ID3D11Texture2D> buf;
  ComPtr<ID3D11RenderTargetView> view;
  HANDLE waitable = nullptr;  // null when the waitable object is unavailable (then waitForFrame is a no-op)
  int w = 0, h = 0;

  bool makeView(std::string* err) {
    buf.Reset();
    view.Reset();
    HRESULT hr = sc->GetBuffer(0, IID_PPV_ARGS(&buf));
    if (SUCCEEDED(hr)) hr = dev->CreateRenderTargetView(buf.Get(), nullptr, &view);
    if (FAILED(hr) && err) *err = hrText("swap chain backbuffer view", hr);
    return SUCCEEDED(hr);
  }
};

WindowsSwapChain::WindowsSwapChain() : d_(new Impl) {}

WindowsSwapChain::~WindowsSwapChain() {
  d_->view.Reset();
  d_->buf.Reset();
  // The waitable handle is a duplicate owned by the caller (GetFrameLatencyWaitableObject): close it ourselves.
  if (d_->waitable) CloseHandle(d_->waitable);
}

std::unique_ptr<WindowsSwapChain> WindowsSwapChain::create(ID3D11Device* device, void* hwnd, int w, int h,
                                                           std::string* err) {
  auto fail = [&](const char* what, HRESULT hr) -> std::unique_ptr<WindowsSwapChain> {
    if (err) *err = hrText(what, hr);
    return nullptr;
  };
  std::unique_ptr<WindowsSwapChain> s(new WindowsSwapChain);
  Impl& d = *s->d_;
  d.dev = device;
  ComPtr<IDXGIDevice> dxgiDev;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIFactory2> factory;
  HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDev));
  if (SUCCEEDED(hr)) hr = dxgiDev->GetAdapter(&adapter);
  if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) return fail("DXGI factory lookup", hr);

  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = static_cast<UINT>(w > 0 ? w : 1);
  desc.Height = static_cast<UINT>(h > 0 ? h : 1);
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  desc.Flags = kFlags;
  hr = factory->CreateSwapChainForHwnd(device, static_cast<HWND>(hwnd), &desc, nullptr, nullptr, &d.sc);
  if (FAILED(hr)) return fail("CreateSwapChainForHwnd (flip model)", hr);
  factory->MakeWindowAssociation(static_cast<HWND>(hwnd), DXGI_MWA_NO_ALT_ENTER);  // the app owns fullscreen
  ComPtr<IDXGISwapChain2> sc2;
  if (SUCCEEDED(d.sc.As(&sc2))) {
    sc2->SetMaximumFrameLatency(1);
    d.waitable = sc2->GetFrameLatencyWaitableObject();
  }
  if (!d.waitable) {
    // Windows 8.0 or a translation layer without IDXGISwapChain2: no waitable object, so just cap the device's
    // queued frames. Pacing then relies on Present blocking.
    ComPtr<IDXGIDevice1> dev1;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dev1)))) dev1->SetMaximumFrameLatency(1);
  }
  d.w = static_cast<int>(desc.Width);
  d.h = static_cast<int>(desc.Height);
  if (!d.makeView(err)) return nullptr;
  return s;
}

void WindowsSwapChain::waitForFrame(unsigned timeoutMs) {
  if (d_->waitable) WaitForSingleObjectEx(d_->waitable, timeoutMs, TRUE);
}

bool WindowsSwapChain::resize(int w, int h, std::string* err) {
  w = w > 0 ? w : 1;
  h = h > 0 ? h : 1;
  if (w == d_->w && h == d_->h) return true;
  ComPtr<ID3D11DeviceContext> ctx;
  d_->dev->GetImmediateContext(&ctx);
  ID3D11RenderTargetView* none = nullptr;
  ctx->OMSetRenderTargets(1, &none, nullptr);  // no outstanding references may remain during ResizeBuffers
  d_->view.Reset();
  d_->buf.Reset();
  HRESULT hr = d_->sc->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, kFlags);
  if (FAILED(hr)) {
    if (err) *err = hrText("ResizeBuffers", hr);
    return false;
  }
  d_->w = w;
  d_->h = h;
  return d_->makeView(err);
}

bool WindowsSwapChain::present(bool vsync) {
  const HRESULT hr = d_->sc->Present(vsync ? 1 : 0, 0);
  return hr != DXGI_ERROR_DEVICE_REMOVED && hr != DXGI_ERROR_DEVICE_RESET;
}

int WindowsSwapChain::width() const { return d_->w; }
int WindowsSwapChain::height() const { return d_->h; }
ID3D11Texture2D* WindowsSwapChain::backBuffer() const { return d_->buf.Get(); }
ID3D11RenderTargetView* WindowsSwapChain::rtv() const { return d_->view.Get(); }

}  // namespace tf::gfx
#endif  // _WIN32
