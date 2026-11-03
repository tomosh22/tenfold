// Which templates exist depends on the Visual Studio configuration: tc.vcxproj defines TC_FAST in Release only.
// The corpus is std-free (sln mode).
#pragma once

#ifdef TC_FAST
template <class T> struct /*@Policy_fast*/Policy { static T /*@Policy_fast_run*/run(T v) { return v; } };
template <> struct /*@Policy_fast_int*/Policy<int> { static int /*@Policy_fast_int_run*/run(int v) { return v + 1; } };
#else
template <class T> struct /*@Policy_safe*/Policy { static T /*@Policy_safe_run*/run(T v) { return v ? v : T(); } };
#endif

template <class T> concept /*@Small*/Small = sizeof(T) <= 4;

#ifdef TC_FAST
template <class T> constexpr int /*@cost_fast*/cost() { return 1; }
#else
template <Small T> constexpr int /*@cost_small*/cost() { return 2; }
template <class T> constexpr int /*@cost_big*/cost() requires (!Small<T>) { return 3; }
#endif

// Present in both configurations, but the call inside resolves differently.
template <class T> T /*@twice*/twice(T v) { return Policy<T>::/*@use_run_in_twice*/run(v) + 0; }
