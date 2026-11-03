#include "container/funcs_agent.h"
#include "container/macros_script.h"
#include "container/animator_agent.h"
#include "container/tiled_light_frame.h"
#include "math/macros_skeleton.h"
#include "container/binding_skeleton.h"
#include "math/bounded_pool_spawner.h"
#include "math/radial_joint.h"
#include "container/funcs_terrain.h"
#include "container/camera.h"
#include "container/linear_material.h"

namespace gx::container {

long /*@top_640_def0*/top(int p0, int p1) {
    double r = 0;
    r += CONTAINER_SCALE_109(2);
    gx::container::/*@AnimatorAgent_543_use7*/AnimatorAgent o1;
    r += /*@AnimatorAgent_543_var3*/o1.weight;
    r += o1.sortInventory(1);
    r += o1.mergeEmitter(3.5, 1);
    gx::container::/*@TiledLightFrame_511_use12*/TiledLightFrame o2;
    r += o2.save(2L);
    r += o2.decodeAnimator(1);
    r += /*@MATH_SCRIPT_20_144_use4*/MATH_SCRIPT_20;
    return (long)(r);
}

bool selectArena(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    gx::container::/*@BindingSkeleton_592_use10*/BindingSkeleton d1;
    gx::math::/*@BoundedPoolSpawner_98_use4*/BoundedPoolSpawner* b2 = &d1;
    r += b2->collectGlyph(3.5);
    gx::math::/*@RadialJoint_42_use7*/RadialJoint o3;
    r += /*@RadialJoint_42_var3*/o3.load(7u);
    r += o3.ratio;
    r += gx::container::selectWidget(2L);
    r += gx::container::/*@fetchAgent_563_use1*/fetchAgent(2.5);
    gx::container::LinearMaterial o4;
    r += o4.visitMatrix(7u);
    r += o4.mergeLight(2L);
    r += o4.splitBounds(2L);
    return (bool)(r);
}


}  // namespace gx::container
