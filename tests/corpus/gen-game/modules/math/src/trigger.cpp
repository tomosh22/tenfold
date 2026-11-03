#include "math/trigger.h"
#include "math/types_prefab.h"

namespace gx {
namespace math {

int Trigger::/*@shared_scale_11_def0*/shared_scale = 0;
static int clamp(int p0) { return p0 + 6; }

double Trigger::/*@resolveBounds_10_def0*/resolveBounds(unsigned p0) {
    double r = 0;
    r += gx::math::QueuedPool_Unknown;
    r += clamp(1);
    r += gx::math::QueuedPool_Count;
    r += gx::math::/*@QueuedPool_Auto_2_use0*/QueuedPool_Auto;
    r += verifyBody();
    return (double)(r);
}


}  // namespace math
}  // namespace gx
