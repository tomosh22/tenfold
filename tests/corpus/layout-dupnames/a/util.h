#ifndef A_UTIL_H
#define A_UTIL_H
namespace a_ns {
int /*@a_helper_decl*/helper(int v);
struct /*@a_Settings*/Settings { int /*@a_Settings_size*/size; };
inline int /*@a_inline*/twice(int v) { return v * 2; }
}  // namespace a_ns
#endif
