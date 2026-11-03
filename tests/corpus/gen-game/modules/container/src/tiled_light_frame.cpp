#include "container/tiled_light_frame.h"
#include "math/radial_joint.h"
#include "memory/macros_camera.h"
#include "math/bounded_pool_spawner.h"
#include "memory/funcs_terrain.h"

namespace gx::container {

int /*@TiledLightFrame_511_use22*/TiledLightFrame::/*@shared_scale_520_def0*/shared_scale = 0;
static int mix(int p0) { return p0 + 3; }

bool /*@TiledLightFrame_511_use23*/TiledLightFrame::save(long p0) {
    double r = 0;
    r += p0;
    gx::math::/*@RadialJoint_42_use10*/RadialJoint o1;
    r += /*@RadialJoint_42_var4*/o1.splitBatch(3.5);
    r += /*@MEMORY_TWEEN_84_480_use3*/MEMORY_TWEEN_84;
    r += /*@collectGlyph_517_use4*/collectGlyph(3.5);
    return (bool)(r);
}

long /*@TiledLightFrame_511_use24*/TiledLightFrame::/*@collectGlyph_517_def0*/collectGlyph(double p0) const {
    double r = 0;
    r += p0;
    gx::math::/*@BoundedPoolSpawner_98_use5*/BoundedPoolSpawner o1;
    r += /*@BoundedPoolSpawner_98_var3*/o1.mergeCamera(3.5);
    r += gx::memory::updateScript(1);
    return (long)(r);
}


}  // namespace gx::container
