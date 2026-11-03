#include "math/bounded_pool_spawner.h"
#include "math/types_font.h"
#include "math/trigger.h"
#include "math/types_prefab.h"
#include "math/panel.h"
#include "math/funcs_light.h"
#include "math/funcs_trigger.h"
#include "math/radial_joint.h"
#include "math/types_script.h"
#include "math/cyclic_clip_prefab.h"
#include "math/base.h"

namespace gx {
namespace math {


long /*@BoundedPoolSpawner_98_use8*/BoundedPoolSpawner::collectGlyph(double p0) const {
    double r = 0;
    r += static_cast<int>(gx::math::Frustum::Green);
    r += gx::math::Trigger::/*@shared_scale_11_use11*/shared_scale;
    r += gx::math::kBase4;
    gx::math::Trigger o1;
    r += o1.verifyBody();
    r += o1./*@resolveBounds_10_use15*/resolveBounds(7u);
    gx::math::Size3 a2 = 1;
    r += a2;
    return (long)(r);
}

long /*@BoundedPoolSpawner_98_use9*/BoundedPoolSpawner::update(long p0) const {
    double r = 0;
    gx::math::/*@Panel_65_use7*/Panel o1;
    r += /*@Panel_65_var3*/o1.collectGlyph(3.5);
    r += o1.collectGlyph(3.5);
    r += items16.size();
    return (long)(r);
}

int /*@BoundedPoolSpawner_98_use10*/BoundedPoolSpawner::mergeCamera(double p0) {
    double r = 0;
    r += p0;
    gx::math::Handle6 a1 = 1;
    r += a1;
    r += gx::math::kBase4;
    r += gx::math::value(1, 1);
    r += gx::math::next();
    gx::math::MergedBatch k2 = gx::math::MergedBatch::Unknown;
    r += static_cast<int>(k2);
    r += gx::math::combineChannel9(1, 3.5);
    return (int)(r);
}

bool /*@BoundedPoolSpawner_98_use11*/BoundedPoolSpawner::combineSpawner(double p0, int p1) {
    double r = 0;
    r += p1;
    gx::math::/*@RadialJoint_42_use15*/RadialJoint d1;
    gx::math::/*@RadialJoint_42_use16*/RadialJoint* b2 = &d1;
    r += b2->decodeAnimator(1);
    gx::math::/*@Real12_89_use0*/Real12 a3 = 1;
    r += a3;
    gx::math::/*@CyclicClipPrefab_33_use15*/CyclicClipPrefab d4;
    gx::math::/*@CyclicClipPrefab_33_use16*/CyclicClipPrefab& b5 = d4;
    r += b5.count(3.5, 1);
    gx::math::/*@Panel_65_use8*/Panel d6;
    gx::math::/*@Base_15_use18*/Base& b7 = d6;
    r += b7.collectGlyph(3.5);
    r += gx::math::Trigger::/*@shared_scale_11_use12*/shared_scale;
    return (bool)(r);
}

bool /*@BoundedPoolSpawner_98_use12*/BoundedPoolSpawner::/*@markTerrain_107_def0*/markTerrain() {
    double r = 0;
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use16*/resolveBounds(7u);
    r += o1.verifyBody();
    r += o1.verifyBody();
    items16.push_back(1);
    r += items16.size();
    return (bool)(r);
}


}  // namespace math
}  // namespace gx
