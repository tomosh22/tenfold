#include "math/matrix.h"
#include "matrix_priv.h"
#include "math/radial_joint.h"
#include "math/trigger.h"
#include "math/types_prefab.h"
#include "math/cyclic_clip_prefab.h"

namespace gx {
namespace math {

int Matrix::shared_speed = 0;

int Matrix::apply() {
    double r = 0;
    r += deriveChannel(1);
    r += storeBinding(1, 3.5);
    gx::math::/*@RadialJoint_42_use19*/RadialJoint o1;
    r += o1.width;
    r += o1.ratio;
    gx::math::Trigger o2;
    r += o2.verifyBody();
    r += storeBinding(1, 3.5);
    return (int)(r);
}

int Matrix::storeBinding(int p0, double p1) const {
    double r = 0;
    r += p0;
    gx::math::Trigger o1;
    r += o1.verifyBody();
    r += o1.verifyBody();
    r += o1.verifyBody();
    gx::math::Trigger o2;
    r += o2./*@resolveBounds_10_use26*/resolveBounds(7u);
    r += deriveChannel(1);
    return (int)(r);
}

unsigned Matrix::resolvePath(long p0) const {
    double r = 0;
    r += gx::math::QueuedPool_Unknown;
    gx::math::Trigger o1;
    r += o1.verifyBody();
    gx::math::/*@Id2_6_use4*/Id2 a2 = 1;
    r += a2;
    r += deriveChannel(1);
    gx::math::/*@CyclicClipPrefab_33_use25*/CyclicClipPrefab o3;
    r += o3.combineSpawner(3.5, 1);
    return (unsigned)(r);
}


}  // namespace math
}  // namespace gx
