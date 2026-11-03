#include "render/funcs_batch.h"
#include "render/basic_widget.h"
#include "render/funcs_packet.h"
#include "container/table113.h"
#include "container/binding_skeleton.h"
#include "container/animator_agent.h"
#include "math/trigger.h"
#include "math/chain8/c0.h"
#include "container/macros_script.h"

namespace gx {
namespace render {

int scaleScript(long p0) {
    double r = 0;
    r += p0;
    r += gx::render::resetSkeleton(2.5);
    r += gx::render::buildChannel(1);
    return (int)(r);
}

unsigned sweepFrame(int p0, double p1) {
    double r = 0;
    r += gx::container::table113_fn_6(1);
    gx::container::/*@BindingSkeleton_592_use13*/BindingSkeleton d1;
    gx::container::/*@AnimatorAgent_543_use13*/AnimatorAgent& b2 = d1;
    r += b2.count(3.5, 1);
    gx::math::Trigger::/*@shared_scale_11_use23*/shared_scale++;
    r += gx::math::chain8_entry(1);
    r += buildChannel(1);
    r += /*@CONTAINER_SCENE_108_622_use9*/CONTAINER_SCENE_108;
    return (unsigned)(r);
}


}  // namespace render
}  // namespace gx
