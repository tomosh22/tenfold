#pragma once
#include <memory>
#include <string>

// Forward declarations only: the Windows / D3D headers are included by the .cpp (this header is portable).
struct ID3D11Device;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;

namespace tf::gfx {

// A DXGI flip-model swap chain for one HWND (Windows only; WindowsSwapChain.cpp is empty elsewhere).
//
// DXGI_SWAP_EFFECT_FLIP_DISCARD with 2 buffers, created with DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT and
// SetMaximumFrameLatency(1): the caller blocks in waitForFrame() before it records a frame, so at most one frame is
// queued ahead of the display and input-to-photon latency stays low. The backbuffer is R8G8B8A8_UNORM (non-sRGB),
// matching the gamma-space blending of the other backends.
class WindowsSwapChain {
 public:
  static std::unique_ptr<WindowsSwapChain> create(ID3D11Device* device, void* hwnd, int widthPx, int heightPx,
                                                  std::string* err = nullptr);
  ~WindowsSwapChain();
  WindowsSwapChain(const WindowsSwapChain&) = delete;
  WindowsSwapChain& operator=(const WindowsSwapChain&) = delete;

  // Blocks until DXGI can accept another frame (<= timeoutMs; a timeout is not an error).
  void waitForFrame(unsigned timeoutMs = 1000);
  // ResizeBuffers (releases and recreates the render target view). No-op when the size is unchanged.
  bool resize(int widthPx, int heightPx, std::string* err = nullptr);
  // Returns false when the device was removed / reset (the renderer must be recreated).
  bool present(bool vsync);

  int width() const;
  int height() const;
  ID3D11Texture2D* backBuffer() const;  // buffer 0, valid until the next resize
  ID3D11RenderTargetView* rtv() const;  // view of backBuffer()

 private:
  WindowsSwapChain();
  struct Impl;
  std::unique_ptr<Impl> d_;
};

}  // namespace tf::gfx
