#include "app.h"
#include "container/container.h"
#include "memory/memory.h"
#include "shader/options.h"
#include "math/bounded_pool_spawner.h"
#include "io/types_vector.h"
#include "container/linear_material.h"
#include "math/funcs_trigger.h"
#include "container/tiled_light_frame.h"
#include "memory/types_collider.h"
#include "math/primary_panel.h"
#include "core/macros_channel.h"
#include "io/bounds2.h"

double appRun194() {
    double r = 0;
    gx::shader::Options<int> t1;
    r += t1.get();
    t1.set(3);
    gx::math::/*@BoundedPoolSpawner_98_use1*/BoundedPoolSpawner o2;
    r += /*@BoundedPoolSpawner_98_var1*/o2.shiftFont(7u);
    r += o2.collectGlyph(3.5);
    r += o2.count(3.5, 1);
    gx::io::Count118 a3 = 1;
    r += a3;
    gx::container::LinearMaterial o4;
    r += o4.encodeBitmap(1, 3.5);
    r += gx::math::combineChannel9(1);
    r += gx::container::/*@TiledLightFrame_511_use2*/TiledLightFrame::/*@shared_scale_520_use2*/shared_scale;
    gx::memory::/*@Path_335_use1*/Path k5 = gx::memory::/*@Path_335_use2*/Path::Auto;
    r += static_cast<int>(k5);
    gx::math::/*@PrimaryPanel_111_use0*/PrimaryPanel d6;
    gx::math::/*@PrimaryPanel_111_use1*/PrimaryPanel* b7 = &d6;
    r += b7->decodeAnimator(1);
    r += gx::core::splitWidget48(1);
    r += /*@CORE_MESH_43_238_use0*/CORE_MESH_43;
    gx::io::Bounds2::shared_delta++;
    return r;
}

