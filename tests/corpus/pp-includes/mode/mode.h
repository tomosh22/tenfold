#pragma once
// One header, compiled differently by two translation units (mode_fast.cpp has -DMODE_FAST, mode_slow.cpp has not).
#ifdef MODE_FAST
inline int /*@mode_fast_impl*/impl_mode() { return 1; }
struct /*@FastOnly*/FastOnly { int /*@FastOnly_v*/v; };
#else
inline int /*@mode_slow_impl*/impl_mode() { return 2; }
struct /*@SlowOnly*/SlowOnly { int /*@SlowOnly_v*/v; };
#endif
inline int /*@mode_common*/common_mode() { return /*@use_impl_in_common*/impl_mode(); }
