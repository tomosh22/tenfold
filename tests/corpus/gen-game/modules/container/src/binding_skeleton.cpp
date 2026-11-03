#include "container/binding_skeleton.h"
#include "container/animator_agent.h"
#include "memory/types_collider.h"
#include "memory/chain70/c2.h"
#include "math/chain8/c0.h"
#include "container/types_emitter.h"
#include "container/shader.h"
#include "memory/macros_camera.h"
#include "memory/emitter.h"
#include "memory/types_quat.h"
#include "container/funcs_terrain.h"
#include "math/macros_skeleton.h"
#include "container/types_channel.h"
#include "container/camera.h"
#include "container/tiled_light_frame.h"
#include "container/types_vector.h"

namespace gx::container {

static int clamp(int p0) { return p0 + 1; }

long /*@BindingSkeleton_592_use4*/BindingSkeleton::decodeAnimator(int p0) {
    double r = 0;
    r += p0;
    gx::container::/*@AnimatorAgent_543_use5*/AnimatorAgent o1;
    r += /*@AnimatorAgent_543_var1*/o1.weight;
    r += o1.ratio;
    r += o1.weight;
    r += gx::memory::kStep63;
    return (long)(r);
}

long /*@BindingSkeleton_592_use5*/BindingSkeleton::count(double p0, int p1) {
    double r = 0;
    r += p1;
    r += decodeAnimator(1);
    r += gx::memory::chain70_l2(1);
    r += gx::math::chain8_entry(1);
    gx::container::Buffer k1 = gx::container::Buffer::Auto;
    r += static_cast<int>(k1);
    return (long)(r);
}

bool /*@BindingSkeleton_592_use6*/BindingSkeleton::combineSpawner(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::container::flushBinding<int>(2);
    gx::container::/*@AnimatorAgent_543_use6*/AnimatorAgent o1;
    r += /*@AnimatorAgent_543_var2*/o1.combineSpawner(3.5, 1);
    r += /*@MEMORY_TWEEN_84_480_use2*/MEMORY_TWEEN_84;
    gx::memory::Emitter<int> t2;
    r += t2.get();
    t2.set(3);
    r += gx::container::kStep101;
    return (bool)(r);
}

unsigned /*@BindingSkeleton_592_use7*/BindingSkeleton::value() {
    double r = 0;
    r += gx::memory::kStep77;
    r += gx::container::init(3.5, 1);
    r += MATH_SCALE_21(2);
    r += gx::container::kStep92;
    r += gx::container::/*@fetchAgent_563_use0*/fetchAgent<int>(2);
    return (unsigned)(r);
}

long /*@BindingSkeleton_592_use8*/BindingSkeleton::collectGlyph(double p0) const {
    double r = 0;
    r += gx::container::selectWidget();
    r += gx::container::markCamera(1, 3.5);
    r += gx::container::/*@kStep91_500_use2*/kStep91;
    return (long)(r);
}

unsigned /*@BindingSkeleton_592_use9*/BindingSkeleton::flushBitmap(int p0) const {
    double r = 0;
    gx::container::/*@TiledLightFrame_511_use10*/TiledLightFrame o1;
    r += /*@TiledLightFrame_511_var4*/o1./*@collectGlyph_517_use3*/collectGlyph(3.5);
    r += o1.applyShader(3.5);
    r += gx::container::markCamera(1, 3.5);
    gx::container::/*@TiledLightFrame_511_use11*/TiledLightFrame o2;
    r += /*@TiledLightFrame_511_var5*/o2.rank;
    r += o2.applyShader(3.5);
    r += gx::container::/*@selectButton_542_use0*/selectButton(1, 3.5);
    gx::container::/*@Light_528_use0*/Light k3 = gx::container::/*@Light_528_use1*/Light::Last;
    r += static_cast<int>(k3);
    return (unsigned)(r);
}


}  // namespace gx::container
