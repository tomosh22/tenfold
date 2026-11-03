#include "memory/funcs_terrain.h"
#include "core/macros_channel.h"
#include "core/types_button.h"
#include "memory/types_collider.h"
#include "math/chain14/c2.h"
#include "memory/buffer.h"
#include "core/atomic_vector.h"
#include "math/trigger.h"
#include "core/funcs_button.h"
#include "core/clip.h"
#include "memory/emitter.h"
#include "memory/config.h"
#include "math/funcs_trigger.h"

namespace gx::memory {

static int mix(int p0) { return p0 + 7; }

int updateScript(int p0, double p1) {
    double r = 0;
    r += gx::core::storeTrigger50(1);
    r += gx::core::kLimit36;
    r += static_cast<int>(gx::memory::/*@Path_335_use10*/Path::Last);
    return (int)(r);
}

double updateScript(double p0, int p1) {
    double r = 0;
    r += p1;
    r += gx::core::bindBitmap49(1);
    r += gx::math::chain14_l2(1);
    r += /*@CORE_SCALE_45_240_use3*/CORE_SCALE_45(2);
    r += gx::core::kLimit36;
    gx::memory::/*@Buffer_386_use25*/Buffer o1;
    r += o1.loadSpawner(3.5);
    r += o1.loadSpawner(1, 3.5);
    gx::core::/*@AtomicVector_167_use24*/AtomicVector o2;
    r += o2.collectGlyph(3.5);
    r += o2.name(3.5, 1);
    r += o2.decodeAnimator(1);
    return (double)(r);
}

long updateScript(int p0) {
    double r = 0;
    r += p0;
    r += gx::math::Trigger::/*@shared_scale_11_use21*/shared_scale;
    r += gx::core::clear(7u);
    return (long)(r);
}

long measureAnimator() {
    double r = 0;
    gx::core::Clip o1;
    r += o1.count(7u);
    r += gx::core::Clip::splitVector(3.5, 1);
    gx::memory::Emitter<int> t2;
    r += t2.get();
    t2.set(3);
    gx::math::Trigger o3;
    r += o3.resolveBounds(7u);
    r += o3.verifyBody();
    r += o3.resolveBounds(7u);
    return (long)(r);
}

long measureAnimator(int p0, int p1) {
    double r = 0;
    gx::memory::Config o1;
    r += o1./*@stamp_436_use4*/stamp;
    r += o1.verifyWidget(7u);
    r += o1.rotateClip(7u);
    r += gx::math::combineChannel9(1, 3.5);
    gx::memory::/*@Buffer_386_use26*/Buffer o2;
    r += o2.loadSpawner(1, 3.5);
    r += o2.score;
    r += o2.loadSpawner(3.5);
    return (long)(r);
}


}  // namespace gx::memory
