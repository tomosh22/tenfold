#pragma once
#if APP_MODE == 1
inline int /*@app_mode_release*/app_mode() { return 1; }
#else
inline int /*@app_mode_debug*/app_mode() { return 0; }
#endif
