#include "app.h"
#include "math/math.h"
#include "shader/shader.h"
#include "core/macros_channel.h"
#include "memory/funcs_frame.h"
#include "container/funcs_terrain.h"
#include "core/clip.h"
#include "io/spawner.h"
#include "container/binding_skeleton.h"
#include "memory/types_collider.h"
#include "core/types_channel.h"
#include "math/radial_joint.h"
#include "memory/emitter.h"
#include "io/bounds2.h"
#include "container/types_channel.h"
#include "math/chain8/c2.h"
#include "io/panel2.h"
#include "io/funcs_camera.h"
#include "core/cursor.h"
#include "core/cyclic_arena.h"
#include "container/stable_material.h"

double appRun198() {
    double r = 0;
    r += gx::core::storeTrigger50(1);
    r += gx::memory::encodeCursor();
    r += gx::container::get(1, 3.5);
    r += gx::core::Clip::/*@shared_total_322_use1*/shared_total;
    r += gx::io::findEmitter<int>(2);
    gx::container::/*@BindingSkeleton_592_use2*/BindingSkeleton o1;
    r += /*@BindingSkeleton_592_var2*/o1.value();
    r += o1.flushBitmap(1);
    gx::container::/*@BindingSkeleton_592_use3*/BindingSkeleton o2;
    r += /*@BindingSkeleton_592_var3*/o2.value();
    gx::memory::/*@Path_335_use3*/Path k3 = gx::memory::/*@Path_335_use4*/Path::Last;
    r += static_cast<int>(k3);
    r += gx::core::kStep31;
    gx::math::/*@RadialJoint_42_use0*/RadialJoint o4;
    r += /*@RadialJoint_42_var0*/o4.ratio7;
    r += o4.ratio;
    r += o4.collectGlyph(3.5);
    r += gx::memory::resetBatch<int>(2);
    gx::io::Bounds2::shared_delta++;
    r += static_cast<int>(gx::memory::Result::Count);
    r += gx::core::bindBitmap49(1);
    r += gx::container::/*@kStep91_500_use0*/kStep91;
    r += gx::math::chain8_l2(1);
    gx::io::Panel2 o5;
    r += o5.depth;
    r += gx::io::scanBitmap(1, 3.5);
    r += gx::memory::kLimit64;
    gx::core::Cursor d6;
    gx::core::CyclicArena& b7 = d6;
    r += b7.get(7u);
    gx::io::Panel2 d8;
    gx::container::StableMaterial& b9 = d8;
    r += b9.count(2L);
    return r;
}

