#include "container/animator_agent.h"
#include "container/tiled_light_frame.h"
#include "memory/emitter.h"
#include "math/trigger.h"
#include "memory/rolling_ray_glyph.h"
#include "container/types_vector.h"
#include "math/chain8/c0.h"
#include "container/types_channel.h"
#include "container/funcs_terrain.h"

namespace gx::container {

static int fold(int p0) { return p0 + 7; }

double /*@AnimatorAgent_543_use2*/AnimatorAgent::updatePacket(double p0) {
    double r = 0;
    r += p0;
    gx::container::/*@TiledLightFrame_511_use6*/TiledLightFrame d1;
    gx::container::/*@TiledLightFrame_511_use7*/TiledLightFrame* b2 = &d1;
    r += b2->count(3.5, 1);
    r += gx::memory::resetBatch<int>(2);
    gx::container::/*@TiledLightFrame_511_use8*/TiledLightFrame o3;
    r += /*@TiledLightFrame_511_var2*/o3.rank;
    r += o3.count(3.5, 1);
    r += o3./*@collectGlyph_517_use1*/collectGlyph(3.5);
    gx::math::Trigger o4;
    r += o4./*@resolveBounds_10_use6*/resolveBounds(7u);
    r += o4.verifyBody();
    r += o4./*@resolveBounds_10_use7*/resolveBounds(7u);
    return (double)(r);
}

long /*@AnimatorAgent_543_use3*/AnimatorAgent::data(long p0) const {
    double r = 0;
    gx::memory::/*@RollingRayGlyph_392_use2*/RollingRayGlyph o1;
    r += /*@RollingRayGlyph_392_var0*/o1.rotateAnimator(1, 3.5);
    r += o1.markTrigger();
    r += gx::memory::/*@RollingRayGlyph_392_use3*/RollingRayGlyph::rotateAnimator(2L);
    gx::memory::Emitter<int> t2;
    r += t2.get();
    t2.set(3);
    gx::memory::Emitter<int> t3;
    r += t3.get();
    t3.set(3);
    r += gx::container::kScale97;
    r += gx::math::chain8_entry(1);
    return (long)(r);
}

bool /*@AnimatorAgent_543_use4*/AnimatorAgent::visitWidget(double p0) const {
    double r = 0;
    r += gx::container::/*@kStep91_500_use1*/kStep91;
    r += ratio;
    gx::memory::Emitter<int> t1;
    r += t1.get();
    t1.set(3);
    gx::container::/*@TiledLightFrame_511_use9*/TiledLightFrame o2;
    r += /*@TiledLightFrame_511_var3*/o2./*@collectGlyph_517_use2*/collectGlyph(3.5);
    r += o2.applyShader(3.5);
    r += o2.resetTrigger(1, 3.5);
    r += gx::container::get(1, 3.5);
    return (bool)(r);
}


}  // namespace gx::container
