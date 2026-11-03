#include "container/funcs_buffer.h"
#include "math/funcs_trigger.h"

namespace gx::container {

static int pick(int p0) { return p0 + 8; }

bool selectTween(double p0) {
    double r = 0;
    r += p0;
    r += gx::math::combineChannel();
    r += pick(1);
    return (bool)(r);
}


}  // namespace gx::container
