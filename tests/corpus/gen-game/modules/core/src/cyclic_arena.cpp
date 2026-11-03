#include "core/cyclic_arena.h"
#include "cyclic_arena_priv.h"
#include "math/radial_joint.h"
#include "core/macros_channel.h"
#include "core/weighted_mesh_collider.h"
#include "math/macros_skeleton.h"
#include "core/info.h"
#include "math/chain8/c0.h"
#include "core/types_button.h"
#include "core/types_channel.h"
#include "math/funcs_light.h"

namespace gx::core {

static int mix(int p0) { return p0 + 1; }

unsigned CyclicArena::process() {
    double r = 0;
    r += process();
    gx::math::/*@RadialJoint_42_use13*/RadialJoint o1;
    r += o1.count(3.5, 1);
    r += o1.decodeAnimator(1);
    r += o1.ratio;
    r += gx::core::splitWidget48(1);
    r += process();
    gx::math::/*@RadialJoint_42_use14*/RadialJoint o2;
    r += o2.ratio;
    r += o2.count(3.5, 1);
    r += o2.ratio;
    return (unsigned)(r);
}

unsigned CyclicArena::saveButton(int p0, double p1) {
    double r = 0;
    r += p0;
    gx::core::WeightedMeshCollider<int> t1;
    r += t1.get();
    t1.set(3);
    r += MATH_SCALE_21(2);
    gx::core::/*@Info_221_use0*/Info<bool> t2;
    r += t2.get();
    t2.set(true);
    r += gx::math::chain8_entry(1);
    r += gx::core::PrimaryCamera_None;
    r += gx::core::storeTrigger50(1);
    return (unsigned)(r);
}

double CyclicArena::resolveShader() const {
    double r = 0;
    r += mix(1);
    r += process();
    r += flushEmitter(1);
    r += get(7u);
    r += gx::core::splitWidget48(1);
    r += resolveShader();
    return (double)(r);
}

int CyclicArena::mergeArena(int p0, double p1) {
    double r = 0;
    r += static_cast<int>(gx::core::PooledPacketTerrain::Last);
    r += gx::math::value(1, 1);
    r += gx::core::splitWidget48(1);
    return (int)(r);
}


}  // namespace gx::core
