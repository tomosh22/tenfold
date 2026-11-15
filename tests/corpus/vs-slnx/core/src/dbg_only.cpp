#include "core_api.h"
// Excluded from the build in every configuration but Debug.
#ifdef CORE_DEBUG
int /*@dbg_helper_def*/dbg_helper() { return 99; }
#endif
int /*@dbg_only_free*/dbg_only_free() { return 98; }
