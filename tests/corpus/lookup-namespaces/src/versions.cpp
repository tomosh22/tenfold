#include "versions.h"

namespace api {
inline namespace v1 {
int /*@v1_call_def*/call(int x) { return x; }
} // namespace v1
namespace v2 {
int /*@v2_call_def*/call(int x) { return x * 2; }
} // namespace v2
} // namespace api
