#include "std_macros.h"

// Same header, compiled with -DNDEBUG and without TRACE.
int use_b() {
  std::string t = /*@use_trace_b*/trace_name();
  bool e = /*@use_checks_b*/checks_enabled();
  /*@use_MaybeInt_b*/MaybeInt m = 4;
  return (int)t.size() + e + m;
}
