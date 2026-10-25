#pragma once
#include <memory>
#include <string>
#include <vector>

#include "ui/platform/Input.h"

namespace tf::ui {

struct WindowOptions {
  std::string title = "tenfold";
  int width = 1280, height = 800;  // in points (pixels = points * pixelScale())
  bool hidden = false;             // for headless rendering
  bool resizable = true;
  bool vsync = true;
};

// An OpenGL 3.3 core window. Create and use it from the main thread only.
class Window {
 public:
  // Returns null (and fills *err) when no display / GL context is available.
  static std::unique_ptr<Window> create(const WindowOptions& opt, std::string* err = nullptr);
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // Loader hook for gfx::gl::load(). The window's context must be current.
  static void* glProc(const char* name);

  void makeCurrent();
  void swap();
  void setVsync(bool on);
  void show();
  void hide();
  void setTitle(const std::string& title);
  void sizePx(int& w, int& h) const;                  // drawable size in pixels
  float pixelScale() const;                           // pixels per point (high-DPI)
  float displayScale() const;                         // OS UI scale for the current display
  void setTextInputRect(int x, int y, int w, int h);  // IME candidate window anchor, in pixels

  // Blocks up to timeoutMs (-1 = forever, 0 = poll) for an event, then drains all pending ones
  // into `out` (appended). Returns true if anything was appended; render only when it does.
  bool waitEvent(int timeoutMs, std::vector<InputEvent>& out);
  // Thread-safe: makes a waiting waitEvent return with an InputType::Wake event.
  static void wake();

 private:
  Window();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tf::ui
