#include "math/cyclic_clip_prefab.h"
#include "math/types_prefab.h"
#include "math/types_font.h"
#include "math/trigger.h"

namespace gx {
namespace math {


long /*@CyclicClipPrefab_33_use17*/CyclicClipPrefab::decodeAnimator(int p0) {
    double r = 0;
    r += p0;
    r += gx::math::kScale1;
    r += gx::math::kScale1;
    r += gx::math::QueuedPool_Blue;
    r += gx::math::kBase4;
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use17*/resolveBounds(7u);
    gx::math::Trigger o2;
    r += o2.verifyBody();
    r += o2./*@resolveBounds_10_use18*/resolveBounds(7u);
    r += o2.verifyBody();
    return (long)(r);
}

int /*@CyclicClipPrefab_33_use18*/CyclicClipPrefab::mergeBody(int p0, int p1) {
    double r = 0;
    r += p1;
    gx::math::/*@Id2_6_use1*/Id2 a1 = 1;
    r += a1;
    gx::math::Trigger o2;
    r += o2.verifyBody();
    gx::math::Trigger o3;
    r += o3.verifyBody();
    r += o3./*@resolveBounds_10_use19*/resolveBounds(7u);
    r += o3.verifyBody();
    gx::math::/*@Id2_6_use2*/Id2 a4 = 1;
    r += a4;
    return (int)(r);
}

long /*@CyclicClipPrefab_33_use19*/CyclicClipPrefab::encodeVector(double p0) {
    double r = 0;
    gx::math::Trigger o1;
    r += o1.verifyBody();
    r += o1./*@resolveBounds_10_use20*/resolveBounds(7u);
    gx::math::Trigger o2;
    r += o2./*@resolveBounds_10_use21*/resolveBounds(7u);
    r += o2./*@resolveBounds_10_use22*/resolveBounds(7u);
    r += count(3.5, 1);
    return (long)(r);
}

double /*@CyclicClipPrefab_33_use20*/CyclicClipPrefab::selectLight() {
    double r = 0;
    gx::math::Trigger o1;
    r += o1.verifyBody();
    r += combineSpawner(3.5, 1);
    r += count(3.5, 1);
    return (double)(r);
}

bool /*@CyclicClipPrefab_33_use21*/CyclicClipPrefab::name(int p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use23*/resolveBounds(7u);
    r += o1.verifyBody();
    r += o1./*@resolveBounds_10_use24*/resolveBounds(7u);
    gx::math::Trigger o2;
    r += o2.verifyBody();
    r += gx::math::kScale1;
    r += name(1, 1);
    return (bool)(r);
}


}  // namespace math
}  // namespace gx
