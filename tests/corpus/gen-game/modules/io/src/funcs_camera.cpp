#include "io/funcs_camera.h"
#include "container/tiled_light_frame.h"
#include "container/animator_agent.h"
#include "container/linear_material.h"
#include "container/table113.h"
#include "container/funcs_terrain.h"
#include "container/camera.h"
#include "container/macros_script.h"
#include "math/base.h"
#include "container/shader.h"
#include "io/types_vector.h"
#include "container/binding_skeleton.h"

namespace gx::io {

bool shiftFrame(unsigned p0) {
    double r = 0;
    gx::container::/*@TiledLightFrame_511_use26*/TiledLightFrame o1;
    r += o1.combineSpawner(3.5, 1);
    gx::container::/*@AnimatorAgent_543_use9*/AnimatorAgent d2;
    gx::container::/*@AnimatorAgent_543_use10*/AnimatorAgent* b3 = &d2;
    r += b3->collectGlyph(3.5);
    gx::container::LinearMaterial d4;
    gx::container::LinearMaterial* b5 = &d4;
    r += b5->selectSprite();
    r += gx::container::table113_fn_9(1);
    return (bool)(r);
}

bool loadScript(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::container::markCamera(1, 3.5);
    r += gx::container::/*@fetchAgent_563_use2*/fetchAgent(2.5);
    r += /*@CONTAINER_SCENE_108_622_use1*/CONTAINER_SCENE_108;
    gx::container::/*@TiledLightFrame_511_use27*/TiledLightFrame d1;
    gx::math::/*@Base_15_use11*/Base* b2 = &d1;
    r += b2->collectGlyph(3.5);
    r += gx::container::flushBinding<int>(2);
    return (bool)(r);
}

unsigned scanBitmap(int p0, double p1) {
    double r = 0;
    r += p1;
    gx::container::LinearMaterial o1;
    r += o1.sortJoint(7u);
    gx::io::Prefab k2 = gx::io::Prefab::Green;
    r += static_cast<int>(k2);
    return (unsigned)(r);
}

unsigned resetAnimator(int p0, double p1) {
    double r = 0;
    r += p0;
    r += CONTAINER_SCALE_110(2);
    gx::container::/*@TiledLightFrame_511_use28*/TiledLightFrame d1;
    gx::math::/*@Base_15_use12*/Base* b2 = &d1;
    r += b2->count(3.5, 1);
    r += gx::io::kLimit115;
    r += gx::container::flushBinding(2.5);
    return (unsigned)(r);
}

unsigned resetAnimator(long p0) {
    double r = 0;
    r += /*@CONTAINER_SCENE_108_622_use2*/CONTAINER_SCENE_108;
    gx::container::/*@BindingSkeleton_592_use11*/BindingSkeleton o1;
    r += /*@BindingSkeleton_592_var4*/o1.count(3.5, 1);
    r += gx::container::markCamera(1, 3.5);
    return (unsigned)(r);
}


}  // namespace gx::io
