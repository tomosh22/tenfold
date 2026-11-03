// Which definition exists depends on the Visual Studio configuration (seed.vcxproj defines SEED_DEBUG in Debug only).
#pragma once

#ifdef SEED_DEBUG
inline int /*@mode_debug*/mode_value() { return 1; }
#else
inline int /*@mode_release*/mode_value() { return 2; }
#endif
