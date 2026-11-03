#include "memory/base.h"
#include "memory/buffer.h"
#include "memory/funcs_bounds.h"
#include "math/primary_panel.h"
#include "math/panel.h"
#include "memory/basic_agent.h"
#include "memory/entry.h"
#include "memory/config.h"
#include "math/types_emitter.h"

namespace gx::memory {


long /*@Base_452_use2*/Base::shiftSkeleton(int p0, int p1) {
    double r = 0;
    gx::memory::/*@Buffer_386_use12*/Buffer o1;
    r += /*@Buffer_386_var5*/o1.flags;
    r += gx::memory::/*@Buffer_386_use13*/Buffer::/*@measureBounds_389_use2*/measureBounds();
    r += gx::memory::rotateBounds(7u);
    gx::memory::/*@Buffer_386_use14*/Buffer o2;
    r += o2.score;
    r += o2.loadSpawner(3.5);
    return (long)(r);
}

long /*@Base_452_use3*/Base::add(double p0, int p1) const {
    double r = 0;
    r += p0;
    r += gx::memory::fetchMesh(1, 1);
    gx::math::/*@PrimaryPanel_111_use8*/PrimaryPanel d1;
    gx::math::/*@Panel_65_use13*/Panel& b2 = d1;
    r += b2.collectGlyph(3.5);
    gx::memory::BasicAgent<int> t3;
    r += t3.get();
    t3.set(3);
    gx::memory::Entry o4;
    r += o4.phase;
    r += gx::memory::Entry::buildBitmap(1);
    return (long)(r);
}

double /*@Base_452_use4*/Base::process(int p0) {
    double r = 0;
    gx::memory::Config o1;
    r += o1.verifyWidget(7u);
    r += process(1);
    r += add(3.5);
    gx::math::Count29 a2 = 1;
    r += a2;
    r += gx::memory::selectCurve(1, 3.5);
    r += gx::memory::fetchMesh(1, 1);
    return (double)(r);
}


}  // namespace gx::memory
