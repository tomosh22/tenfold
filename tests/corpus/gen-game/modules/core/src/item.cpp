#include "core/item.h"
#include "math/cyclic_clip_prefab.h"
#include "core/macros_channel.h"
#include "math/chain8/c0.h"
#include "math/trigger.h"
#include "core/types_ray.h"
#include "math/funcs_light.h"
#include "core/buffer.h"
#include "core/atomic_vector.h"
#include "core/types_channel.h"

namespace gx::core {

static int sum(int p0) { return p0 + 9; }

unsigned Item::id(int p0, double p1) {
    double r = 0;
    gx::math::/*@CyclicClipPrefab_33_use11*/CyclicClipPrefab o1;
    r += /*@CyclicClipPrefab_33_var3*/o1.decodeAnimator(1);
    r += gx::core::bindBitmap49(1);
    r += gx::math::chain8_l0(1);
    return (unsigned)(r);
}

double Item::get(double p0) const {
    double r = 0;
    r += p0;
    gx::math::Trigger::/*@shared_scale_11_use10*/shared_scale++;
    r += gx::core::splitWidget48(1);
    gx::core::/*@Material_277_use0*/Material k1 = gx::core::/*@Material_277_use1*/Material::Green;
    r += static_cast<int>(k1);
    r += /*@CORE_SCALE_45_240_use1*/CORE_SCALE_45(2);
    r += gx::math::value(2L);
    return (double)(r);
}

bool Item::updateFrustum(unsigned p0) {
    double r = 0;
    r += p0;
    r += gx::core::splitWidget48(1);
    r += sum(1);
    r += gx::core::splitWidget48(1);
    r += sum(1);
    r += updateFrustum(7u);
    gx::math::/*@CyclicClipPrefab_33_use12*/CyclicClipPrefab o1;
    r += /*@CyclicClipPrefab_33_var4*/o1.encodeVector(3.5);
    r += gx::math::/*@CyclicClipPrefab_33_use13*/CyclicClipPrefab::name(1, 1);
    r += o1.selectLight();
    return (bool)(r);
}

long Item::fetchBitmap(int p0, double p1) {
    double r = 0;
    r += p1;
    r += gx::core::splitWidget48(1);
    gx::core::/*@Buffer_208_use12*/Buffer o1;
    r += /*@Buffer_208_var1*/o1.collectMatrix(3.5);
    r += sum(1);
    gx::core::/*@AtomicVector_167_use19*/AtomicVector o2;
    r += o2.applyArena(1, 1);
    r += gx::core::bindBitmap49(1);
    r += gx::core::kStep31;
    return (long)(r);
}


}  // namespace gx::core
