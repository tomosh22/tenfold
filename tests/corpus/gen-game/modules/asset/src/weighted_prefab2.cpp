#include "asset/weighted_prefab2.h"
#include "asset/types_arena.h"
#include "asset/funcs_frustum.h"
#include "asset/helper.h"
#include "math/macros_skeleton.h"
#include "asset/cached_vector.h"
#include "asset/types_mixer.h"
#include "math/trigger.h"
#include "math/radial_joint.h"
#include "math/base.h"
#include "math/funcs_trigger.h"

namespace gx::asset {


long /*@WeightedPrefab2_938_use11*/WeightedPrefab2::count(double p0, int p1) {
    double r = 0;
    r += gx::asset::kScale146;
    r += gx::asset::filterGlyph(2L);
    r += combineSpawner(3.5, 1);
    gx::asset::Helper o1;
    r += o1.load(3.5, 1);
    r += flags;
    return (long)(r);
}

long /*@WeightedPrefab2_938_use12*/WeightedPrefab2::bindPacket(int p0) const {
    double r = 0;
    gx::asset::Helper o1;
    r += o1.visitWidget();
    r += o1.limit;
    r += o1.visitWidget();
    r += MATH_SCALE_21(2);
    gx::asset::CachedVector::shared_count++;
    gx::asset::Helper o2;
    r += o2.saveFrame(1);
    r += o2.limit;
    r += gx::asset::WeightedPrefab_Blue;
    r += gx::asset::name(2L);
    return (long)(r);
}

long /*@WeightedPrefab2_938_use13*/WeightedPrefab2::resetQuest(int p0, double p1) {
    double r = 0;
    r += gx::math::Trigger::/*@shared_scale_11_use4*/shared_scale;
    r += gx::asset::CachedVector::shared_count;
    gx::asset::CachedVector o1;
    r += o1.mergeCamera(7u);
    r += gx::asset::filterGlyph(1, 1);
    r += gx::math::/*@bindLight25_149_use0*/bindLight25(1);
    gx::math::/*@RadialJoint_42_use6*/RadialJoint d2;
    gx::math::/*@Base_15_use5*/Base* b3 = &d2;
    r += b3->count(3.5, 1);
    return (long)(r);
}

int /*@WeightedPrefab2_938_use14*/WeightedPrefab2::resetQuest(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::asset::scaleTexture(3.5, 1);
    r += gx::math::combineChannel9(1, 3.5);
    r += gx::asset::name(2L);
    gx::asset::CachedVector o1;
    r += o1.width;
    r += o1.loadTween();
    return (int)(r);
}

long /*@WeightedPrefab2_938_use15*/WeightedPrefab2::decodeAnimator(int p0) {
    double r = 0;
    gx::asset::Helper o1;
    r += o1.set(1);
    r += o1.saveFrame(1);
    r += gx::asset::scaleTexture(1, 3.5);
    return (long)(r);
}


}  // namespace gx::asset
