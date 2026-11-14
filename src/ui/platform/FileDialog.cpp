#include "ui/platform/FileDialog.h"

#include <SDL3/SDL.h>

#include <deque>
#include <memory>
#include <mutex>

#include "core/PathUtil.h"
#include "ui/platform/Window.h"

namespace tf::ui {

std::string sdlFilterPattern(const std::string& vs) {
  std::string out, cur;
  auto flush = [&] {
    if (cur.empty()) return;
    if (cur == "*" || cur == "*.*") out = "*";  // everything: SDL wants a lone "*"
    else if (out != "*") {
      if (cur.rfind("*.", 0) == 0) cur.erase(0, 2);
      if (!out.empty()) out += ';';
      out += cur;
    }
    cur.clear();
  };
  for (char c : vs) {
    if (c == ';' || c == ',' || c == ' ') flush();
    else cur += c;
  }
  flush();
  return out.empty() ? "*" : out;
}

namespace {

std::mutex g_mu;
std::deque<std::function<void()>> g_results;  // results that arrived on a foreign thread, run by the pump

struct Call {
  FileDialogDone done;
  std::vector<std::string> names, patterns;  // SDL keeps pointers into these until its callback ran
  std::vector<SDL_DialogFileFilter> filters;
  std::string startDir;
};

void SDLCALL onDialog(void* user, const char* const* list, int /*filter*/) {
  std::unique_ptr<Call> call(static_cast<Call*>(user));
  std::vector<std::filesystem::path> picked;
  if (list && list[0]) picked.push_back(pathFromUtf8(list[0]));  // null list: error; empty list: cancelled
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_results.push_back(
        [done = std::move(call->done), picked = std::move(picked)]() mutable { done(std::move(picked)); });
  }
  Window::wake();
}

}  // namespace

void pumpFileDialogResults() {
  for (;;) {
    std::function<void()> fn;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      if (g_results.empty()) return;
      fn = std::move(g_results.front());
      g_results.pop_front();
    }
    fn();
  }
}

FileDialogBackend sdlFileDialogBackend(Window& window) {
  return [&window](const FileDialogRequest& req, FileDialogDone done) {
    auto* call = new Call;
    call->done = std::move(done);
    call->startDir = pathUtf8(req.startDir);
    call->names.reserve(req.filters.size());
    call->patterns.reserve(req.filters.size());
    for (const FileDialogFilter& f : req.filters) {
      call->names.push_back(f.name);
      call->patterns.push_back(sdlFilterPattern(f.pattern));
    }
    for (size_t i = 0; i < call->names.size(); ++i)
      call->filters.push_back({call->names[i].c_str(), call->patterns[i].c_str()});
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window.sdlHandle());
    if (!call->startDir.empty())
      SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, call->startDir.c_str());
    if (!req.title.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, req.title.c_str());
    if (!req.folder && !call->filters.empty()) {
      SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, call->filters.data());
      SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, static_cast<Sint64>(call->filters.size()));
    }
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
    SDL_ShowFileDialogWithProperties(req.folder ? SDL_FILEDIALOG_OPENFOLDER : SDL_FILEDIALOG_OPENFILE, onDialog, call,
                                     props);
    SDL_DestroyProperties(props);
  };
}

}  // namespace tf::ui
