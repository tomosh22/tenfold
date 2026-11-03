#pragma once
// Shared by both projects. core defines USE_FANCY everywhere, app only in Release.
#ifdef USE_FANCY
inline int /*@log_fancy*/log_line() { return 1; }
#else
inline int /*@log_plain*/log_line() { return 0; }
#endif
