#include "ui/platform/Window.h"

#include <SDL3/SDL.h>

#include "ui/platform/InputSdl.h"

namespace tf::ui {

namespace {
int g_sdlRefs = 0;
}

struct Window::Impl {
  SDL_Window* win = nullptr;
  SDL_GLContext ctx = nullptr;
};

Window::Window() : impl_(new Impl) {}

Window::~Window() {
  if (impl_->ctx) SDL_GL_DestroyContext(impl_->ctx);
  if (impl_->win) SDL_DestroyWindow(impl_->win);
  if (--g_sdlRefs == 0) SDL_Quit();
}

std::unique_ptr<Window> Window::create(const WindowOptions& opt, std::string* err) {
  auto fail = [&](const char* what) -> std::unique_ptr<Window> {
    if (err) *err = std::string(what) + ": " + SDL_GetError();
    return nullptr;
  };
  if (g_sdlRefs == 0 && !SDL_Init(SDL_INIT_VIDEO)) return fail("SDL_Init");
  std::unique_ptr<Window> w(new Window);
  ++g_sdlRefs;  // from here on the destructor balances it
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (opt.resizable) flags |= SDL_WINDOW_RESIZABLE;
  if (opt.hidden) flags |= SDL_WINDOW_HIDDEN;
  w->impl_->win = SDL_CreateWindow(opt.title.c_str(), opt.width, opt.height, flags);
  if (!w->impl_->win) return fail("SDL_CreateWindow");
  w->impl_->ctx = SDL_GL_CreateContext(w->impl_->win);
  if (!w->impl_->ctx) return fail("SDL_GL_CreateContext (OpenGL 3.3 core)");
  w->makeCurrent();
  w->setVsync(opt.vsync);
  SDL_StartTextInput(w->impl_->win);
  return w;
}

void* Window::glProc(const char* name) { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); }
void Window::makeCurrent() { SDL_GL_MakeCurrent(impl_->win, impl_->ctx); }
void Window::swap() { SDL_GL_SwapWindow(impl_->win); }
void Window::setVsync(bool on) { SDL_GL_SetSwapInterval(on ? 1 : 0); }
void Window::show() { SDL_ShowWindow(impl_->win); }
void Window::hide() { SDL_HideWindow(impl_->win); }
void Window::setTitle(const std::string& t) { SDL_SetWindowTitle(impl_->win, t.c_str()); }
void Window::sizePx(int& w, int& h) const { SDL_GetWindowSizeInPixels(impl_->win, &w, &h); }

float Window::pixelScale() const {
  float d = SDL_GetWindowPixelDensity(impl_->win);
  return d > 0 ? d : 1.0f;
}

float Window::displayScale() const {
  float d = SDL_GetWindowDisplayScale(impl_->win);
  return d > 0 ? d : 1.0f;
}

void Window::setTextInputRect(int x, int y, int w, int h) {
  float s = 1.0f / pixelScale();
  SDL_Rect r{static_cast<int>(x * s), static_cast<int>(y * s), static_cast<int>(w * s), static_cast<int>(h * s)};
  SDL_SetTextInputArea(impl_->win, &r, 0);
}

bool Window::waitEvent(int timeoutMs, std::vector<InputEvent>& out) {
  size_t before = out.size();
  SDL_Event e;
  bool got = timeoutMs == 0  ? SDL_PollEvent(&e)
             : timeoutMs < 0 ? SDL_WaitEvent(&e)
                             : SDL_WaitEventTimeout(&e, timeoutMs);
  while (got) {
    InputEvent ie;
    if (translateSdlEvent(e, pixelScale(), modsFromSdl(SDL_GetModState()), ie)) out.push_back(std::move(ie));
    got = SDL_PollEvent(&e);
  }
  return out.size() != before;
}

void Window::wake() {
  SDL_Event e{};
  e.type = SDL_EVENT_USER;
  SDL_PushEvent(&e);
}

}  // namespace tf::ui
