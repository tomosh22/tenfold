// Conditional compilation: #if / #elif chains, defined(), __has_include, command line macros (-DFEATURE_X=2).
#define MODE_A 1
#define VERSION 3

#if VERSION >= 5
int /*@ver_ge5*/ver_fn() { return 5; }
#elif VERSION >= 3 && defined(MODE_A)
int /*@ver_ge3*/ver_fn() { return 3; }
#elif defined(MODE_B)
int /*@ver_modeb*/ver_fn() { return 4; }
#else
int /*@ver_else*/ver_fn() { return 0; }
#endif
int use_ver = /*@use_ver_fn*/ver_fn();

#if defined(MODE_B) || !defined(MODE_A)
int /*@mode_off*/mode_fn() { return 0; }
#else
int /*@mode_on*/mode_fn() { return 1; }
#endif
int use_mode = /*@use_mode_fn*/mode_fn();

// -DFEATURE_X=2 comes from the compile command.
#if FEATURE_X == 1
int /*@fx_one*/fx() { return 1; }
#elif FEATURE_X == 2
int /*@fx_two*/fx() { return 2; }
#else
int /*@fx_other*/fx() { return 0; }
#endif
int use_fx = /*@use_fx*/fx();

#ifdef FEATURE_UNSET
int /*@unset_branch*/unset_fn() { return 1; }
#endif

#if __has_include("present.h")
#include "present.h"
int use_present = /*@use_present*/present_fn();
#endif
#if __has_include("absent.h")
#include "absent.h"
int /*@absent_branch*/absent_marker;
#else
int /*@fallback_branch*/fallback_marker;
#endif
#if __has_include(<no/such/thing.h>) || __has_include("also_missing.h")
int /*@missing_branch*/missing_marker;
#elif defined(__has_include)
int /*@has_include_defined*/has_include_marker;
#endif
int use_markers = /*@use_fallback*/fallback_marker + /*@use_has_include*/has_include_marker;

#if __cplusplus >= 202002L
int /*@cxx20_branch*/lang_fn() { return 20; }
#else
int /*@cxx17_branch*/lang_fn() { return 17; }
#endif
int use_lang = /*@use_lang_fn*/lang_fn();

#if 0
int /*@dead_fn*/dead_fn() { return 0; }
#endif
#if 1
int /*@live_fn*/live_fn() { return 1; }
#endif
int use_live = /*@use_live_fn*/live_fn();

// Nested conditionals and a comparison of two macros.
#define LOW 2
#define HIGH 5
#if (HIGH - LOW) > 2
#  if defined(MODE_A) && MODE_A == 1
int /*@nested_yes*/nested_fn() { return 1; }
#  else
int /*@nested_no_inner*/nested_fn() { return 2; }
#  endif
#else
int /*@nested_no_outer*/nested_fn() { return 3; }
#endif
int use_nested = /*@use_nested_fn*/nested_fn();
