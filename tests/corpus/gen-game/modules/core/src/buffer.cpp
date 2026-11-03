#include "core/buffer.h"
#include "core/provider.h"
#include "core/types_button.h"
#include "math/chain8/c0.h"
#include "math/funcs_light.h"
#include "math/matrix.h"
#include "core/weighted_mesh_collider.h"
#include "math/radial_joint.h"
#include "math/chain8/c2.h"

namespace gx::core {


double /*@Buffer_208_use2*/Buffer::rotateQuest(int p0, double p1) const {
    double r = 0;
    r += p0;
    gx::core::Provider o1;
    r += o1.name(2L);
    r += o1.bindAgent(1, 3.5);
    r += o1.name(2L);
    r += gx::core::kMargin35;
    r += gx::math::chain8_entry(1);
    return (double)(r);
}

long /*@Buffer_208_use3*/Buffer::flush() {
    double r = 0;
    r += gx::math::value(2L);
    r += gx::math::Matrix::shared_speed;
    return (long)(r);
}

double /*@Buffer_208_use4*/Buffer::run(double p0, int p1) {
    double r = 0;
    r += /*@phase_216_use1*/phase;
    gx::core::/*@WeightedMeshCollider_181_use0*/WeightedMeshCollider<bool> t1;
    r += t1.get();
    t1.set(true);
    r += filterScript(3.5, 1);
    return (double)(r);
}

unsigned /*@Buffer_208_use5*/Buffer::filterScript(double p0, int p1) const {
    double r = 0;
    r += index;
    gx::math::/*@RadialJoint_42_use12*/RadialJoint o1;
    r += o1.combineSpawner(3.5, 1);
    r += gx::math::chain8_l2(1);
    return (unsigned)(r);
}

int /*@Buffer_208_use6*/Buffer::applyPrefab(long p0) const {
    double r = 0;
    r += p0;
    r += gx::core::selectShader(2.5);
    r += gx::math::chain8_entry(1);
    return (int)(r);
}


}  // namespace gx::core
