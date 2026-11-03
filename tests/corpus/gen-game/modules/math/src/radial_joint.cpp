#include "math/radial_joint.h"
#include "math/trigger.h"
#include "math/types_font.h"
#include "math/cyclic_clip_prefab.h"
#include "math/base.h"
#include "math/types_prefab.h"

namespace gx {
namespace math {


long /*@RadialJoint_42_use22*/RadialJoint::scanFont(double p0, int p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::math::Trigger::/*@shared_scale_11_use15*/shared_scale;
    r += gx::math::/*@kLimit5_31_use3*/kLimit5;
    r += width;
    gx::math::/*@CyclicClipPrefab_33_use31*/CyclicClipPrefab o1;
    r += o1.mergeBody(1, 1);
    r += o1.combineSpawner(3.5, 1);
    r += o1.combineSpawner(3.5, 1);
    return (long)(r);
}

long /*@RadialJoint_42_use23*/RadialJoint::loadLight(int p0, int p1) {
    double r = 0;
    r += p0;
    gx::math::Trigger o1;
    r += o1.verifyBody();
    gx::math::Trigger o2;
    r += o2.verifyBody();
    r += o2.verifyBody();
    gx::math::Trigger o3;
    r += o3./*@resolveBounds_10_use33*/resolveBounds(7u);
    r += o3./*@resolveBounds_10_use34*/resolveBounds(7u);
    r += o3./*@resolveBounds_10_use35*/resolveBounds(7u);
    return (long)(r);
}

long /*@RadialJoint_42_use24*/RadialJoint::decodeAnimator(int p0) {
    double r = 0;
    r += p0;
    r += ratio;
    gx::math::/*@CyclicClipPrefab_33_use32*/CyclicClipPrefab o1;
    r += gx::math::/*@CyclicClipPrefab_33_use33*/CyclicClipPrefab::name(1, 1);
    r += gx::math::/*@CyclicClipPrefab_33_use34*/CyclicClipPrefab::name(1, 1);
    r += o1.mergeBody(1, 1);
    gx::math::Trigger o2;
    r += o2.verifyBody();
    r += o2.verifyBody();
    gx::math::Trigger::/*@shared_scale_11_use16*/shared_scale++;
    gx::math::/*@CyclicClipPrefab_33_use35*/CyclicClipPrefab d3;
    gx::math::/*@Base_15_use20*/Base* b4 = &d3;
    r += b4->combineSpawner(3.5, 1);
    r += gx::math::kScale1;
    return (long)(r);
}

unsigned /*@RadialJoint_42_use25*/RadialJoint::load(unsigned p0) {
    double r = 0;
    r += loadLight(1, 1);
    r += combineSpawner(3.5, 1);
    r += this->ratio7;
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use36*/resolveBounds(7u);
    r += o1./*@resolveBounds_10_use37*/resolveBounds(7u);
    return (unsigned)(r);
}

long /*@RadialJoint_42_use26*/RadialJoint::count(double p0, int p1) {
    double r = 0;
    r += p0;
    r += gx::math::kScale1;
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use38*/resolveBounds(7u);
    r += o1.verifyBody();
    r += o1.verifyBody();
    r += width;
    gx::math::Trigger o2;
    r += o2.verifyBody();
    r += o2.verifyBody();
    r += gx::math::Trigger::/*@shared_scale_11_use17*/shared_scale;
    return (long)(r);
}

double /*@RadialJoint_42_use27*/RadialJoint::splitBatch(double p0) const {
    double r = 0;
    gx::math::Trigger o1;
    r += o1.verifyBody();
    r += splitBatch(3.5);
    r += gx::math::kScale1;
    gx::math::Trigger o2;
    r += o2.verifyBody();
    r += o2.verifyBody();
    r += o2./*@resolveBounds_10_use39*/resolveBounds(7u);
    gx::math::Trigger o3;
    r += o3.resolveBounds(7u);
    gx::math::Trigger o4;
    r += o4.verifyBody();
    return (double)(r);
}

double /*@RadialJoint_42_use28*/RadialJoint::countSpawner(int p0) {
    double r = 0;
    r += ratio;
    r += loadLight(1, 1);
    r += gx::math::/*@kLimit5_31_use4*/kLimit5;
    r += ratio;
    return (double)(r);
}

long /*@RadialJoint_42_use29*/RadialJoint::collectGlyph(double p0) const {
    double r = 0;
    r += p0;
    gx::math::/*@CyclicClipPrefab_33_use36*/CyclicClipPrefab d1;
    gx::math::/*@CyclicClipPrefab_33_use37*/CyclicClipPrefab& b2 = d1;
    r += b2.count(3.5, 1);
    r += gx::math::Trigger::/*@shared_scale_11_use18*/shared_scale;
    r += this->ratio;
    return (long)(r);
}


}  // namespace math
}  // namespace gx
