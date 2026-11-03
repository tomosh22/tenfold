#include "container/funcs_terrain.h"
#include "memory/funcs_bounds.h"
#include "container/tiled_light_frame.h"
#include "memory/buffer.h"
#include "memory/emitter.h"
#include "memory/basic_agent.h"
#include "math/funcs_light.h"
#include "memory/funcs_frame.h"
#include "memory/macros_camera.h"
#include "math/funcs_trigger.h"
#include "memory/funcs_terrain.h"
#include "container/types_channel.h"
#include "math/chain8/c0.h"

namespace gx::container {

bool markCamera(int p0, double p1) {
    double r = 0;
    r += p1;
    r += gx::memory::rotateBounds(7u);
    gx::container::/*@TiledLightFrame_511_use13*/TiledLightFrame o1;
    r += o1.count(3.5, 1);
    r += o1.decodeAnimator(1);
    r += o1.resetTrigger(1, 3.5);
    gx::memory::/*@Buffer_386_use8*/Buffer o2;
    r += /*@Buffer_386_var3*/o2.flags;
    r += o2.loadSpawner(3.5);
    r += o2.loadSpawner(1, 3.5);
    r += gx::memory::rotateBounds(7u);
    r += gx::memory::resetBatch<int>(2);
    return (bool)(r);
}

int get(int p0, double p1) {
    double r = 0;
    r += p0;
    r += gx::memory::/*@measureBody_363_use0*/measureBody(2.5);
    r += gx::math::next(1, 1);
    r += gx::memory::saveQuat(1, 1);
    return (int)(r);
}

bool init(double p0, int p1) {
    double r = 0;
    r += p1;
    r += gx::memory::/*@flushBody90_486_use0*/flushBody90(1);
    r += gx::math::combineChannel9(1);
    gx::memory::Emitter<int> t1;
    r += t1.get();
    t1.set(3);
    gx::container::/*@TiledLightFrame_511_use14*/TiledLightFrame o2;
    r += o2.save(2L);
    r += gx::memory::resetBatch(2.5);
    gx::memory::/*@Buffer_386_use9*/Buffer o3;
    r += /*@Buffer_386_var4*/o3.loadSpawner(1, 3.5);
    r += gx::memory::/*@Buffer_386_use10*/Buffer::/*@measureBounds_389_use1*/measureBounds();
    return (bool)(r);
}

double selectWidget(long p0) {
    double r = 0;
    r += gx::container::/*@TiledLightFrame_511_use15*/TiledLightFrame::/*@shared_scale_520_use4*/shared_scale;
    r += gx::memory::measureAnimator();
    return (double)(r);
}

unsigned selectWidget() {
    double r = 0;
    r += gx::container::kBase93;
    gx::container::/*@TiledLightFrame_511_use16*/TiledLightFrame o1;
    r += o1.save(2L);
    r += o1.combineSpawner(3.5, 1);
    gx::container::/*@TiledLightFrame_511_use17*/TiledLightFrame o2;
    r += o2.count(3.5, 1);
    r += o2.combineSpawner(3.5, 1);
    r += o2.applyShader(3.5);
    return (unsigned)(r);
}

bool /*@selectButton_542_def0*/selectButton(int p0, double p1) {
    double r = 0;
    gx::container::Handle94 a1 = 1;
    r += a1;
    r += gx::math::chain8_l0(1);
    gx::container::/*@TiledLightFrame_511_use18*/TiledLightFrame d2;
    gx::container::/*@TiledLightFrame_511_use19*/TiledLightFrame& b3 = d2;
    r += b3.count(3.5, 1);
    return (bool)(r);
}


}  // namespace gx::container
