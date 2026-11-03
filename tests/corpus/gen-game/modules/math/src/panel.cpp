#include "math/panel.h"
#include "math/trigger.h"
#include "math/funcs_trigger.h"
#include "math/radial_joint.h"
#include "math/chain8/c0.h"
#include "math/cyclic_clip_prefab.h"
#include "math/base.h"
#include "math/types_font.h"

namespace gx {
namespace math {


bool /*@Panel_65_use9*/Panel::combineSpawner(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    gx::math::Trigger o1;
    r += o1.verifyBody();
    r += gx::math::combineChannel();
    gx::math::Trigger o2;
    r += o2./*@resolveBounds_10_use27*/resolveBounds(7u);
    r += o2./*@resolveBounds_10_use28*/resolveBounds(7u);
    r += o2.verifyBody();
    return (bool)(r);
}

int /*@Panel_65_use10*/Panel::resolveBuffer(int p0, int p1) {
    double r = 0;
    r += p0;
    gx::math::Trigger o1;
    r += o1.verifyBody();
    r += o1./*@resolveBounds_10_use29*/resolveBounds(7u);
    gx::math::/*@RadialJoint_42_use20*/RadialJoint o2;
    r += o2.load(7u);
    r += o2.loadLight(1, 1);
    r += o2.collectGlyph(3.5);
    return (int)(r);
}

long /*@Panel_65_use11*/Panel::sortChannel(double p0) {
    double r = 0;
    r += gx::math::chain8_entry(1);
    r += gx::math::chain8_entry(1);
    r += gx::math::combineChannel();
    gx::math::/*@CyclicClipPrefab_33_use26*/CyclicClipPrefab d1;
    gx::math::/*@Base_15_use19*/Base& b2 = d1;
    r += b2.collectGlyph(3.5);
    return (long)(r);
}

bool /*@Panel_65_use12*/Panel::bindMesh(double p0, int p1) const {
    double r = 0;
    r += gx::math::chain8_entry(1);
    r += gx::math::/*@kLimit5_31_use2*/kLimit5;
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use30*/resolveBounds(7u);
    r += o1.verifyBody();
    return (bool)(r);
}


}  // namespace math
}  // namespace gx
