#include "memory/rolling_ray_glyph.h"
#include "rolling_ray_glyph_priv.h"
#include "memory/funcs_bounds.h"
#include "core/provider.h"
#include "core/clip.h"
#include "core/atomic_vector.h"
#include "memory/buffer.h"
#include "memory/emitter.h"
#include "math/cyclic_clip_prefab.h"
#include "math/macros_skeleton.h"

namespace gx::memory {


bool /*@RollingRayGlyph_392_use10*/RollingRayGlyph::rotateAnimator(long p0) {
    double r = 0;
    r += p0;
    r += rotateAnimator(2L);
    r += gx::memory::rotateBounds(7u);
    r += rotateAnimator(2L);
    gx::core::Provider o1;
    r += o1.name(2L);
    r += o1.bindAgent(1, 3.5);
    r += o1.name(2L);
    r += rotateAnimator(2L);
    r += rotateAnimator(2L);
    return (bool)(r);
}

double /*@RollingRayGlyph_392_use11*/RollingRayGlyph::fetchScript(int p0, int p1) const {
    double r = 0;
    gx::core::Clip::/*@shared_total_322_use2*/shared_total++;
    r += gx::memory::selectCurve(1, 1);
    gx::core::/*@AtomicVector_167_use27*/AtomicVector o1;
    r += o1.name(3.5, 1);
    gx::memory::/*@Buffer_386_use27*/Buffer o2;
    r += o2.loadSpawner(1, 3.5);
    r += gx::memory::/*@Buffer_386_use28*/Buffer::/*@measureBounds_389_use6*/measureBounds();
    r += o2.flags;
    r += gx::memory::resetBatch<int>(2);
    r += visitBuffer(1);
    return (double)(r);
}

unsigned /*@RollingRayGlyph_392_use12*/RollingRayGlyph::rotateAnimator(int p0, double p1) {
    double r = 0;
    r += visitBuffer(1);
    gx::math::CyclicClipPrefab d1;
    gx::math::CyclicClipPrefab* b2 = &d1;
    r += b2->collectGlyph(3.5);
    items76.push_back(1);
    r += items76.size();
    return (unsigned)(r);
}

double /*@RollingRayGlyph_392_use13*/RollingRayGlyph::markTrigger() const {
    double r = 0;
    r += /*@MATH_JOINT_19_143_use6*/MATH_JOINT_19;
    gx::memory::/*@Buffer_386_use29*/Buffer d1;
    gx::memory::/*@Buffer_386_use30*/Buffer& b2 = d1;
    r += b2.decodeAnimator(1);
    return (double)(r);
}


}  // namespace gx::memory
