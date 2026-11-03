#ifndef B_UTIL_H
#define B_UTIL_H
namespace b_ns {
int /*@b_helper_decl*/helper(int v);
struct /*@b_Settings*/Settings { double /*@b_Settings_size*/size; };
inline int /*@b_inline*/twice(int v) { return v * 3; }
}  // namespace b_ns
#endif
