#include "container/linear_material.h"
#include "container/tiled_light_frame.h"
#include "math/base.h"
#include "memory/funcs_terrain.h"
#include "math/macros_skeleton.h"
#include "container/funcs_terrain.h"
#include "math/types_prefab.h"
#include "container/animator_agent.h"
#include "container/types_channel.h"
#include "memory/rolling_ray_glyph.h"
#include "container/camera.h"
#include "math/radial_joint.h"

namespace gx::container {


long LinearMaterial::visitMatrix(unsigned p0) const {
    double r = 0;
    r += p0;
    gx::container::/*@TiledLightFrame_511_use20*/TiledLightFrame d1;
    gx::math::/*@Base_15_use7*/Base* b2 = &d1;
    r += b2->combineSpawner(3.5, 1);
    r += gx::memory::measureAnimator(1, 3.5);
    r += visitMatrix(7u);
    r += /*@MATH_JOINT_19_143_use3*/MATH_JOINT_19;
    return (long)(r);
}

long LinearMaterial::mergeLight(long p0) const {
    double r = 0;
    r += p0;
    r += gx::container::markCamera(1, 3.5);
    gx::container::/*@TiledLightFrame_511_use21*/TiledLightFrame d1;
    gx::math::/*@Base_15_use8*/Base* b2 = &d1;
    r += b2->count(3.5, 1);
    r += gx::math::kScale1;
    gx::container::/*@AnimatorAgent_543_use8*/AnimatorAgent d3;
    gx::math::/*@Base_15_use9*/Base* b4 = &d3;
    r += b4->collectGlyph(3.5);
    return (long)(r);
}

long LinearMaterial::markFrustum(long p0) const {
    double r = 0;
    gx::container::Handle94 a1 = 1;
    r += a1;
    r += /*@MATH_JOINT_19_143_use4*/MATH_JOINT_19;
    return (long)(r);
}

bool LinearMaterial::selectSprite() {
    double r = 0;
    r += encodeBitmap(1, 3.5);
    gx::memory::/*@RollingRayGlyph_392_use4*/RollingRayGlyph o1;
    r += /*@RollingRayGlyph_392_var1*/o1.rotateAnimator(1, 3.5);
    return (bool)(r);
}

unsigned LinearMaterial::encodeBitmap(int p0, double p1) {
    double r = 0;
    r += p1;
    gx::container::Camera<int> t1;
    r += t1.get();
    t1.set(3);
    gx::math::/*@RadialJoint_42_use8*/RadialJoint d2;
    gx::math::/*@RadialJoint_42_use9*/RadialJoint* b3 = &d2;
    r += b3->decodeAnimator(1);
    return (unsigned)(r);
}


}  // namespace gx::container
