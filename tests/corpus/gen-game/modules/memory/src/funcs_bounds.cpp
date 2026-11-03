#include "memory/funcs_bounds.h"
#include "core/buffer.h"
#include "math/cyclic_clip_prefab.h"
#include "math/funcs_light.h"
#include "math/primary_panel.h"
#include "math/panel.h"
#include "core/macros_channel.h"
#include "memory/emitter.h"
#include "memory/types_collider.h"
#include "math/radial_joint.h"
#include "math/base.h"

namespace gx::memory {

static int helper(int p0) { return p0 + 8; }

double rotateBounds(unsigned p0) {
    double r = 0;
    r += p0;
    gx::core::/*@Buffer_208_use14*/Buffer o1;
    r += /*@Buffer_208_var3*/o1.applyPrefab(2L);
    r += o1./*@phase_216_use2*/phase;
    gx::math::/*@CyclicClipPrefab_33_use38*/CyclicClipPrefab o2;
    r += o2.mergeBody(1, 1);
    r += gx::math::value(1, 1);
    gx::math::/*@PrimaryPanel_111_use10*/PrimaryPanel d3;
    gx::math::/*@Panel_65_use14*/Panel* b4 = &d3;
    r += b4->decodeAnimator(1);
    r += /*@CORE_MESH_43_238_use4*/CORE_MESH_43;
    return (double)(r);
}

unsigned selectCurve(int p0, int p1) {
    double r = 0;
    r += p0;
    r += helper(1);
    gx::memory::Emitter<int> t1;
    r += t1.get();
    t1.set(3);
    r += static_cast<int>(gx::memory::/*@Path_335_use8*/Path::Auto);
    return (unsigned)(r);
}

unsigned selectCurve(int p0, double p1) {
    double r = 0;
    gx::core::/*@Buffer_208_use15*/Buffer o1;
    r += /*@Buffer_208_var4*/o1./*@phase_216_use3*/phase;
    r += gx::core::/*@Buffer_208_use16*/Buffer::flush();
    r += gx::memory::kScale65;
    gx::math::/*@RadialJoint_42_use30*/RadialJoint d2;
    gx::math::/*@Base_15_use22*/Base* b3 = &d2;
    r += b3->decodeAnimator(1);
    gx::memory::Emitter<int> t4;
    r += t4.get();
    t4.set(3);
    gx::math::/*@CyclicClipPrefab_33_use39*/CyclicClipPrefab o5;
    r += o5.decodeAnimator(1);
    return (unsigned)(r);
}


}  // namespace gx::memory
