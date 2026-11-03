#include "memory/entry.h"
#include "memory/buffer.h"
#include "core/types_channel.h"
#include "memory/types_collider.h"

namespace gx::memory {

static int norm(int p0) { return p0 + 1; }

long Entry::flushSpawner(int p0) const {
    double r = 0;
    gx::memory::/*@Buffer_386_use23*/Buffer o1;
    r += o1.loadSpawner(1, 3.5);
    r += gx::core::kStep31;
    r += gx::memory::kLimit64;
    gx::memory::/*@Path_335_use5*/Path k2 = gx::memory::/*@Path_335_use6*/Path::Auto;
    r += static_cast<int>(k2);
    gx::memory::/*@Buffer_386_use24*/Buffer o3;
    r += o3.flags;
    r += o3.flags;
    r += o3.loadSpawner(3.5);
    return (long)(r);
}

int Entry::combineCamera(long p0) const {
    double r = 0;
    r += p0;
    r += width;
    r += static_cast<int>(gx::memory::/*@Path_335_use7*/Path::Last);
    return (int)(r);
}


}  // namespace gx::memory
