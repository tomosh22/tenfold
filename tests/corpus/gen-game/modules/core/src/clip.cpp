#include "core/clip.h"
#include "core/macros_channel.h"
#include "core/atomic_vector.h"
#include "math/types_prefab.h"
#include "core/remote_material.h"
#include "core/buffer.h"
#include "core/weighted_mesh_collider.h"
#include "core/info.h"
#include "core/bounds.h"
#include "math/macros_skeleton.h"
#include "core/types_button.h"

namespace gx::core {

int Clip::/*@shared_total_322_def0*/shared_total = 0;
static int norm(int p0) { return p0 + 3; }

unsigned Clip::/*@save_317_def0*/save(double p0) const {
    double r = 0;
    r += gx::core::storeTrigger50(1);
    gx::core::/*@AtomicVector_167_use17*/AtomicVector o1;
    r += /*@AtomicVector_167_var4*/o1.count(3.5, 1);
    r += o1.collectGlyph(3.5);
    r += o1.collectGlyph(3.5);
    r += /*@save_317_use0*/save(3.5);
    return (unsigned)(r);
}

bool Clip::collectEntity(long p0) const {
    double r = 0;
    r += p0;
    r += norm(1);
    r += gx::core::splitWidget48(1);
    r += gx::math::kScale1;
    gx::core::/*@RemoteMaterial_307_use0*/RemoteMaterial d1;
    gx::core::/*@Buffer_208_use7*/Buffer& b2 = d1;
    r += b2.run(3.5, 1);
    r += gx::core::fetchScript(2.5);
    return (bool)(r);
}

int Clip::flushAnimator(double p0) {
    double r = 0;
    gx::core::Info<int> t1;
    r += t1.get();
    t1.set(3);
    r += gx::core::storeTrigger50(1);
    gx::core::/*@Bounds_286_use3*/Bounds o2;
    r += /*@Bounds_286_var0*/o2.bindScene(1, 1);
    r += o2.bindScene(1, 1);
    r += MATH_SCALE_21(2);
    r += norm(1);
    gx::core::Handle38 a3 = 1;
    r += a3;
    return (int)(r);
}

int Clip::splitVector(double p0, int p1) {
    double r = 0;
    r += p1;
    r += /*@CORE_SCALE_45_240_use0*/CORE_SCALE_45(2);
    r += gx::core::splitWidget48(1);
    r += gx::core::splitWidget48(1);
    r += gx::core::/*@ActiveBuffer_Default_197_use1*/ActiveBuffer_Default;
    gx::math::/*@Id2_6_use0*/Id2 a1 = 1;
    r += a1;
    return (int)(r);
}


}  // namespace gx::core
