#include "core/bounds.h"
#include "core/types_batch.h"
#include "core/atomic_vector.h"
#include "core/types_channel.h"
#include "core/cyclic_arena.h"
#include "core/macros_channel.h"
#include "math/trigger.h"
#include "core/context.h"
#include "core/types_button.h"

namespace gx::core {

static int mix(int p0) { return p0 + 2; }

bool /*@Bounds_286_use0*/Bounds::bindScene() {
    double r = 0;
    r += gx::core::kMargin51;
    r += mix(1);
    gx::core::/*@AtomicVector_167_use15*/AtomicVector d1;
    gx::core::/*@AtomicVector_167_use16*/AtomicVector& b2 = d1;
    r += b2.count(3.5, 1);
    r += bindScene();
    gx::core::PooledPacketTerrain k3 = gx::core::PooledPacketTerrain::Red;
    r += static_cast<int>(k3);
    gx::core::CyclicArena o4;
    r += o4.mergeArena(1, 3.5);
    r += gx::core::CyclicArena::process();
    return (bool)(r);
}

unsigned /*@Bounds_286_use1*/Bounds::empty() {
    double r = 0;
    r += /*@CORE_MESH_43_238_use2*/CORE_MESH_43;
    gx::math::Trigger::/*@shared_scale_11_use7*/shared_scale++;
    r += gx::math::Trigger::/*@shared_scale_11_use8*/shared_scale;
    r += empty();
    r += gx::core::splitWidget48(1);
    return (unsigned)(r);
}

long /*@Bounds_286_use2*/Bounds::sweepAnimator(unsigned p0) {
    double r = 0;
    r += p0;
    r += this->offset;
    gx::core::Real32 a1 = 1;
    r += a1;
    gx::core::Context o2;
    r += o2.collectRay(3.5, 1);
    r += o2.collectRay(3.5, 1);
    r += gx::core::/*@ActiveBuffer_Default_197_use0*/ActiveBuffer_Default;
    return (long)(r);
}


}  // namespace gx::core
