#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace tf::ui {

class Window;

// A native "open" dialog (SLN-OPEN: File > Open > Project/Solution, Open Folder). The UI talks to a FileDialogBackend
// so tests can inject a fake that answers at once and no real window opens; sdlFileDialogBackend() is the real one.
struct FileDialogFilter {
  std::string name;     // "All Project Files"
  std::string pattern;  // "*.sln;*.slnx;*.vcxproj" (Visual Studio's notation); "*.*" or "*" matches everything
};

struct FileDialogRequest {
  bool folder = false;  // pick a directory instead of a file
  std::string title;
  std::vector<FileDialogFilter> filters;  // files only; the first is the one shown initially
  std::filesystem::path startDir;         // where the dialog opens (may be empty or missing: the OS decides)
};

// `picked` is empty when the dialog was cancelled or could not be shown; otherwise one path. ALWAYS called on the UI
// thread (the real backend marshals SDL's callback through pumpFileDialogResults), exactly once, possibly before
// the backend function returns (fakes).
using FileDialogDone = std::function<void(std::vector<std::filesystem::path> picked)>;
using FileDialogBackend = std::function<void(const FileDialogRequest&, FileDialogDone)>;

// SDL3's SDL_ShowOpenFileDialog / SDL_ShowOpenFolderDialog, parented to `window` (modal where the OS supports it).
// The dialog is asynchronous and its result arrives on an SDL-internal thread on some platforms: the callback only
// queues the result and wakes the event loop, and the program's loop calls pumpFileDialogResults() on the UI thread.
// `window` must outlive the dialog. Real windows are only shown here; CI exercises the pieces around it.
FileDialogBackend sdlFileDialogBackend(Window& window);
// Runs the queued results (on the calling thread). The main loop calls it once per iteration.
void pumpFileDialogResults();

// The SDL filter pattern for a Visual Studio one: "*.sln;*.slnx" -> "sln;slnx", "*.*" -> "*". Exposed for tests.
std::string sdlFilterPattern(const std::string& vsPattern);

}  // namespace tf::ui
