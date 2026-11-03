#pragma once
// Force-included (-include) into forced.cpp only; no source file includes it.
#define /*@FORCED_MACRO*/FORCED_MACRO 42
inline int /*@forced_fn*/forced_fn() { return /*@FORCED_in_fn*/FORCED_MACRO; }
struct /*@ForcedType*/ForcedType { int /*@ForcedType_v*/v; };
