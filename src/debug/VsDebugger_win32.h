#pragma once
#include <memory>

#include "debug/Debugger.h"

namespace tf {

// The Visual Studio debugger backend (C2b): drives a running (or launched) Visual Studio through its automation
// model (EnvDTE) with late-bound IDispatch calls. Windows only; this header has no COM includes, the factory is what
// the app installs with Editor::setDebuggerFactory.
//
// Threading: the backend is created on any thread but every call, and the destructor, must come from one thread (the
// DebugSession worker): connect() initialises an apartment-threaded COM apartment there and the destructor
// uninitialises it.
std::unique_ptr<Debugger> createVisualStudioDebugger();

}  // namespace tf
