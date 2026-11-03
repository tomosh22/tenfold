#pragma once
// Versioned API: v1 is inline (the default), v2 is opt-in.
namespace api {
inline namespace /*@v1_ns*/v1 {
int /*@v1_call*/call(int);
struct /*@v1_Opts*/Opts {
  int level = 1;
};
} // namespace v1
namespace /*@v2_ns*/v2 {
int /*@v2_call*/call(int);
struct /*@v2_Opts*/Opts {
  int level = 2;
  int extra = 0;
};
} // namespace v2
} // namespace api
