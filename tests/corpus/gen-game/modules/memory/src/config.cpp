#include "memory/config.h"
#include "core/macros_channel.h"
#include "memory/rolling_ray_glyph.h"
#include "core/context.h"
#include "memory/types_collider.h"
#include "memory/buffer.h"
#include "memory/chain70/c0.h"
#include "math/chain8/c2.h"
#include "math/types_script.h"
#include "core/atomic_vector.h"
#include "memory/funcs_bounds.h"
#include "core/funcs_button.h"
#include "math/trigger.h"

namespace gx::memory {

static int norm(int p0) { return p0 + 9; }

int Config::verifyWidget(unsigned p0) const {
    double r = 0;
    r += gx::core::splitWidget48(1);
    gx::memory::/*@RollingRayGlyph_392_use5*/RollingRayGlyph d1;
    gx::core::Context* b2 = &d1;
    r += b2->collectRay(3.5, 1);
    gx::memory::Result k3 = gx::memory::Result::Unknown;
    r += static_cast<int>(k3);
    gx::memory::/*@Buffer_386_use18*/Buffer o4;
    r += o4.loadSpawner(3.5);
    r += gx::memory::/*@Buffer_386_use19*/Buffer::/*@measureBounds_389_use4*/measureBounds();
    r += gx::memory::chain70_entry(1);
    r += norm(1);
    return (int)(r);
}

double Config::/*@splitTerrain_432_def0*/splitTerrain(int p0) const {
    double r = 0;
    r += p0;
    r += gx::memory::kLimit64;
    r += gx::math::chain8_l2(1);
    r += gx::memory::kScale65;
    gx::math::RadialWidget k1 = gx::math::RadialWidget::Max;
    r += static_cast<int>(k1);
    gx::core::/*@AtomicVector_167_use23*/AtomicVector o2;
    r += o2./*@level_174_use4*/level;
    r += o2./*@limit_175_use6*/limit;
    gx::memory::/*@Buffer_386_use20*/Buffer o3;
    r += o3.flags;
    r += gx::memory::/*@Buffer_386_use21*/Buffer::/*@measureBounds_389_use5*/measureBounds();
    r += o3.loadSpawner(1, 3.5);
    return (double)(r);
}

long Config::buildTexture(int p0, int p1) {
    double r = 0;
    r += p1;
    r += gx::memory::selectCurve(1, 3.5);
    r += gx::core::run(1, 3.5);
    return (long)(r);
}

int Config::rotateClip(unsigned p0) const {
    double r = 0;
    r += p0;
    gx::memory::/*@Buffer_386_use22*/Buffer o1;
    r += o1.loadSpawner(3.5);
    gx::math::Trigger::/*@shared_scale_11_use19*/shared_scale++;
    return (int)(r);
}


}  // namespace gx::memory
