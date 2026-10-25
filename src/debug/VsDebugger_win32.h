#pragma once
#include <memory>

#include "debug/Debugger.h"
#include "debug/VsDteUtil.h"

namespace tf {

// The Visual Studio debugger backend (C2b): drives a running (or launched) Visual Studio through its automation
// model (EnvDTE) with late-bound IDispatch calls. Windows only; this header has no COM includes, the factory is what
// the app installs with Editor::setDebuggerFactory.
//
// Threading: the backend is created on any thread but every call, and the destructor, must come from one thread (the
// DebugSession worker): connect() initialises an apartment-threaded COM apartment there and the destructor
// uninitialises it.
//
// `policy` bounds how long a call is retried while Visual Studio is busy: commandMs (default 15 s) for commands and
// connect, pollMs (default 1 s) for the whole of one poll(), which then fails with "Visual Studio is busy".
std::unique_ptr<Debugger> createVisualStudioDebugger(const VsBusyPolicy& policy = {});

}  // namespace tf
