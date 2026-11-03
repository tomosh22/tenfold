#pragma once
// Everything here depends on the Visual Studio configuration of the project that compiles it.
#if defined(CORE_DEBUG)
inline int /*@level_dbg*/core_level() { return 2; }
#elif defined(CORE_PROFILE)
inline int /*@level_prof*/core_level() { return 3; }
#else
inline int /*@level_rel*/core_level() { return 1; }
#endif

#ifdef CORE_32BIT
typedef int /*@word_t32*/word_t;
#else
typedef long long /*@word_t64*/word_t;
#endif

// _DEBUG comes from the runtime library (/MDd) that UseDebugLibraries selects; _WIN64 from the x64 platform.
#ifdef _DEBUG
inline int /*@rt_debug*/runtime_kind() { return 1; }
#else
inline int /*@rt_release*/runtime_kind() { return 0; }
#endif
#if defined(_WIN64)
inline int /*@arch_bits_64*/arch_bits() { return 64; }
#elif defined(_WIN32)
inline int /*@arch_bits_32*/arch_bits() { return 32; }
#endif
#ifdef _MSC_VER
inline int /*@msvc_yes*/compiler_kind() { return 1; }
#else
inline int /*@msvc_no*/compiler_kind() { return 0; }
#endif

#ifdef CORE_DEBUG
int /*@dbg_helper_decl*/dbg_helper();
#endif

inline int /*@core_twice*/core_twice() { return /*@use_level_in_header*/core_level() * 2; }
