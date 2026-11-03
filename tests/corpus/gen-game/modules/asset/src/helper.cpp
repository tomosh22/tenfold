#include "asset/helper.h"
#include "helper_priv.h"
#include "math/radial_joint.h"
#include "math/chain8/c0.h"
#include "math/macros_skeleton.h"
#include "math/chain8/c2.h"
#include "math/trigger.h"
#include "math/funcs_light.h"
#include "math/cyclic_clip_prefab.h"

namespace gx::asset {


double Helper::visitWidget() const {
    double r = 0;
    gx::math::/*@RadialJoint_42_use3*/RadialJoint d1;
    gx::math::/*@RadialJoint_42_use4*/RadialJoint* b2 = &d1;
    r += b2->decodeAnimator(1);
    r += gx::math::chain8_l0(1);
    r += gx::math::chain8_entry(1);
    gx::math::/*@RadialJoint_42_use5*/RadialJoint o3;
    r += /*@RadialJoint_42_var2*/o3.width;
    r += o3.combineSpawner(3.5, 1);
    r += o3.countSpawner(1);
    return (double)(r);
}

long Helper::saveFrame(int p0) const {
    double r = 0;
    r += p0;
    r += /*@MATH_SCRIPT_20_144_use2*/MATH_SCRIPT_20;
    r += gx::math::chain8_entry(1);
    r += gx::math::storeMesh24(1);
    r += gx::math::chain8_l2(1);
    return (long)(r);
}

bool Helper::load(double p0, int p1) {
    double r = 0;
    r += gx::math::Trigger::/*@shared_scale_11_use3*/shared_scale;
    r += gx::math::chain8_entry(1);
    r += gx::math::next(1);
    gx::math::/*@CyclicClipPrefab_33_use10*/CyclicClipPrefab o1;
    r += /*@CyclicClipPrefab_33_var2*/o1.selectLight();
    r += /*@measureQuest_918_use0*/measureQuest(1);
    r += visitWidget();
    return (bool)(r);
}


}  // namespace gx::asset
