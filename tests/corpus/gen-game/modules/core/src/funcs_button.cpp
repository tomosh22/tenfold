#include "core/funcs_button.h"
#include "math/funcs_light.h"
#include "core/buffer.h"
#include "core/cursor.h"
#include "core/macros_channel.h"
#include "math/types_font.h"
#include "math/bounded_pool_spawner.h"
#include "core/info.h"
#include "core/atomic_vector.h"
#include "core/bounds.h"
#include "core/cyclic_arena.h"

namespace gx::core {

unsigned run(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::math::next();
    gx::core::/*@Buffer_208_use10*/Buffer o1;
    r += /*@Buffer_208_var0*/o1.rotateQuest(1, 3.5);
    r += o1.collectMatrix(3.5);
    gx::core::Cursor o2;
    r += gx::core::Cursor::push(7u);
    r += o2.mergeMesh(3.5);
    r += gx::core::splitWidget48(1);
    gx::math::Frustum k3 = gx::math::Frustum::Last;
    r += static_cast<int>(k3);
    return (unsigned)(r);
}

long clear(unsigned p0) {
    double r = 0;
    r += CORE_SCALE_46(2);
    gx::math::/*@BoundedPoolSpawner_98_use6*/BoundedPoolSpawner o1;
    r += /*@BoundedPoolSpawner_98_var4*/o1.total;
    r += o1.decodeAnimator(1);
    gx::core::/*@Info_221_use1*/Info<bool> t2;
    r += t2.get();
    t2.set(true);
    r += bindBitmap49(1);
    gx::core::/*@AtomicVector_167_use18*/AtomicVector o3;
    r += /*@AtomicVector_167_var5*/o3.count(3.5, 1);
    r += o3.decodeAnimator(1);
    return (long)(r);
}

int sweepTrigger(int p0, double p1) {
    double r = 0;
    r += p1;
    gx::core::/*@Bounds_286_use4*/Bounds d1;
    gx::core::/*@Buffer_208_use11*/Buffer* b2 = &d1;
    r += b2->run(3.5, 1);
    gx::core::Cursor d3;
    gx::core::CyclicArena* b4 = &d3;
    r += b4->resolveShader();
    r += gx::math::next(1);
    return (int)(r);
}


}  // namespace gx::core
