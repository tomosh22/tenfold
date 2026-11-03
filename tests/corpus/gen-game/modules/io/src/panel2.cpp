#include "io/panel2.h"
#include "io/types_vector.h"
#include "container/chain114/c2.h"
#include "container/tiled_light_frame.h"
#include "io/funcs_camera.h"
#include "container/funcs_buffer.h"
#include "container/binding_skeleton.h"
#include "math/bounded_pool_spawner.h"
#include "container/animator_agent.h"
#include "math/base.h"
#include "container/linear_material.h"

namespace gx::io {


double Panel2::id(double p0) {
    double r = 0;
    gx::io::Prefab k1 = gx::io::Prefab::Green;
    r += static_cast<int>(k1);
    r += gx::container::chain114_l2(1);
    gx::container::/*@TiledLightFrame_511_use39*/TiledLightFrame d2;
    gx::container::TiledLightFrame* b3 = &d2;
    r += b3->count(3.5, 1);
    r += gx::io::pop(1, 1);
    r += gx::container::next(2L);
    r += gx::io::loadScript(1, 3.5);
    return (double)(r);
}

bool Panel2::data(double p0) {
    double r = 0;
    gx::container::/*@BindingSkeleton_592_use12*/BindingSkeleton d1;
    gx::math::/*@BoundedPoolSpawner_98_use7*/BoundedPoolSpawner* b2 = &d1;
    r += b2->combineSpawner(3.5, 1);
    gx::container::/*@AnimatorAgent_543_use11*/AnimatorAgent o3;
    r += /*@AnimatorAgent_543_var4*/o3.combineSpawner(3.5, 1);
    r += gx::io::loadScript(1, 3.5);
    r += static_cast<int>(gx::io::Prefab::Green);
    r += depth;
    return (bool)(r);
}

unsigned Panel2::/*@set_686_def0*/set() {
    double r = 0;
    r += id(3.5);
    gx::container::TiledLightFrame d1;
    gx::math::/*@Base_15_use13*/Base* b2 = &d1;
    r += b2->combineSpawner(3.5, 1);
    r += /*@set_686_use2*/set();
    gx::container::LinearMaterial o3;
    r += o3.mergeLight(2L);
    r += o3.mergeLight(2L);
    gx::container::LinearMaterial o4;
    r += o4.markFrustum(2L);
    r += o4.mergeLight(2L);
    r += o4.encodeBitmap(1, 3.5);
    r += gx::io::pop(1, 1);
    return (unsigned)(r);
}


}  // namespace gx::io
