#include "render/basic_body.h"
#include "basic_body_priv.h"
#include "render/types_channel.h"
#include "render/options.h"
#include "render/dirty_terrain.h"
#include "container/funcs_terrain.h"
#include "render/funcs_mesh.h"
#include "container/macros_script.h"
#include "render/sorted_spawner.h"
#include "asset/cached_vector.h"
#include "asset/helper.h"

namespace gx {
namespace render {

static int /*@mix_1147_def0*/mix(int p0) { return p0 + 4; }

unsigned BasicBody::visitChannel(long p0) {
    double r = 0;
    r += /*@mix_1147_use0*/mix(1);
    r += gx::render::/*@RollingChannel_Auto_1067_use0*/RollingChannel_Auto;
    gx::render::Options d1;
    gx::render::DirtyTerrain& b2 = d1;
    r += b2.saveMixer(2L);
    r += gx::container::markCamera(1, 3.5);
    r += gx::render::bindQuest();
    r += CONTAINER_SCALE_110(2);
    return (unsigned)(r);
}

bool BasicBody::resetComponent(int p0) const {
    double r = 0;
    gx::render::SortedSpawner o1;
    r += gx::render::SortedSpawner::rotateTerrain(1);
    r += gx::render::SortedSpawner::rotateTerrain(1);
    r += gx::render::SortedSpawner::rotateTerrain(1);
    r += offset;
    gx::asset::CachedVector d2;
    gx::asset::Helper& b3 = d2;
    r += b3.visitWidget();
    return (bool)(r);
}


}  // namespace render
}  // namespace gx
