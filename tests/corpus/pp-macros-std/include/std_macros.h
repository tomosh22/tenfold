// Macros that cooperate with the standard library (CMake / compile_commands variant of pp-macros).
#pragma once
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <version>

#define /*@FOR_EACH*/FOR_EACH(it, c) for (auto it = (c).begin(); it != (c).end(); ++it)
#define /*@REGISTER*/REGISTER(name) static bool reg_##name = /*@registry_call*/registry().emplace(#name, &name).second;
#define /*@CHECK*/CHECK(cond) assert(cond)
#define /*@ENUM_ITEM*/ENUM_ITEM(x) x,
#define /*@ENUM_NAME*/ENUM_NAME(x) #x,
#define /*@DEFINE_ENUM*/DEFINE_ENUM(Name, LIST) \
  enum class Name { LIST(ENUM_ITEM) }; \
  inline std::vector<std::string> Name##_names() { return { LIST(ENUM_NAME) }; }
#define /*@VEC_OF*/VEC_OF(T, ...) std::vector<T>{__VA_ARGS__ __VA_OPT__(,) T{}}

using /*@Registry*/Registry = std::map<std::string, int (*)()>;
inline Registry& /*@registry_fn*/registry() { static Registry r; return r; }

#if TRACE
inline std::string /*@trace_on*/trace_name() { return "on"; }
#else
inline std::string /*@trace_off*/trace_name() { return "off"; }
#endif

#ifdef NDEBUG
inline bool /*@ndebug_yes*/checks_enabled() { return false; }
#else
inline bool /*@ndebug_no*/checks_enabled() { return true; }
#endif

#if defined(__cpp_lib_optional)
#include <optional>
using /*@maybe_int_opt*/MaybeInt = std::optional<int>;
#else
using /*@maybe_int_raw*/MaybeInt = int;
#endif
