#include "core/cursor.h"
#include "core/types_channel.h"
#include "core/cyclic_arena.h"
#include "math/trigger.h"
#include "core/macros_channel.h"

namespace gx::core {

static int step(int p0) { return p0 + 2; }

double Cursor::mergeMesh(double p0) {
    double r = 0;
    r += gx::core::kStep31;
    if (link55) r += link55->saveButton(1, 3.5);
    r += gx::math::Trigger::/*@shared_scale_11_use9*/shared_scale;
    r += gx::core::splitWidget48(1);
    return (double)(r);
}


}  // namespace gx::core
