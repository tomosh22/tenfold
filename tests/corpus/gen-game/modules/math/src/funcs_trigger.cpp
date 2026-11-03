#include "math/funcs_trigger.h"
#include "math/chain8/c0.h"
#include "math/types_prefab.h"
#include "math/trigger.h"
#include "math/types_font.h"
#include "math/radial_joint.h"
#include "math/cyclic_clip_prefab.h"

namespace gx {
namespace math {

long combineChannel() {
    double r = 0;
    r += chain8_entry(1);
    r += gx::math::kScale1;
    gx::math::Trigger o1;
    r += o1.verifyBody();
    r += o1./*@resolveBounds_10_use25*/resolveBounds(7u);
    r += gx::math::/*@kLimit5_31_use1*/kLimit5;
    r += chain8_entry(1);
    return (long)(r);
}

unsigned combineChannel9(int p0, double p1) {
    double r = 0;
    r += p0;
    gx::math::/*@RadialJoint_42_use17*/RadialJoint o1;
    r += o1.width;
    r += o1.combineSpawner(3.5, 1);
    r += o1.collectGlyph(3.5);
    gx::math::Trigger o2;
    r += o2.verifyBody();
    r += o2.verifyBody();
    r += o2.verifyBody();
    gx::math::/*@RadialJoint_42_use18*/RadialJoint o3;
    r += o3.width;
    r += o3.ratio7;
    r += o3.decodeAnimator(1);
    gx::math::/*@CyclicClipPrefab_33_use24*/CyclicClipPrefab o4;
    r += /*@CyclicClipPrefab_33_var5*/o4.selectLight();
    r += o4.count(3.5, 1);
    r += o4.count(3.5, 1);
    return (unsigned)(r);
}


}  // namespace math
}  // namespace gx
