#include "io/skeleton.h"
#include "io/state.h"
#include "container/animator_agent.h"
#include "container/funcs_agent.h"
#include "io/funcs_camera.h"
#include "io/panel2.h"
#include "io/funcs_terrain.h"
#include "io/rigid_quest.h"
#include "container/macros_script.h"
#include "io/types_vector.h"
#include "container/stable_material.h"
#include "io/funcs_component.h"

namespace gx::io {

int /*@Skeleton_836_use0*/Skeleton::shared_delta = 0;
static int norm(int p0) { return p0 + 2; }

unsigned /*@Skeleton_836_use1*/Skeleton::resolveMesh(int p0) {
    double r = 0;
    r += p0;
    r += gx::io::visitComponent(2.5);
    r += norm(1);
    gx::container::/*@AnimatorAgent_543_use12*/AnimatorAgent o1;
    r += /*@AnimatorAgent_543_var5*/o1.mergeEmitter(3.5, 1);
    r += o1.mergeEmitter(3.5, 1);
    r += o1.ratio;
    return (unsigned)(r);
}

double /*@Skeleton_836_use2*/Skeleton::resolveMesh(int p0, double p1) {
    double r = 0;
    r += p0;
    r += gx::container::selectArena(3.5, 1);
    r += gx::io::loadScript(1, 3.5);
    r += norm(1);
    return (double)(r);
}

double /*@Skeleton_836_use3*/Skeleton::countPanel(double p0, int p1) const {
    double r = 0;
    r += p1;
    gx::io::Panel2 o1;
    r += o1.countTexture(3.5, 1);
    r += gx::io::count(1);
    gx::io::RigidQuest o2;
    r += o2.data(3.5);
    r += o2.mergeCamera(1, 3.5);
    gx::io::Panel2 o3;
    r += gx::io::Panel2::id(3.5);
    r += o3.countTexture(3.5, 1);
    r += /*@CONTAINER_SCENE_108_622_use6*/CONTAINER_SCENE_108;
    return (double)(r);
}

long /*@Skeleton_836_use4*/Skeleton::/*@collectGlyph_841_def0*/collectGlyph(double p0) const {
    double r = 0;
    r += mass;
    r += static_cast<int>(gx::io::Prefab::Green);
    gx::io::Panel2 d1;
    gx::container::StableMaterial& b2 = d1;
    r += b2.data(3.5);
    r += /*@collectGlyph_841_use0*/collectGlyph(3.5);
    r += gx::io::pop(1, 1);
    return (long)(r);
}

double /*@Skeleton_836_use5*/Skeleton::shiftTween(double p0, int p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += CONTAINER_SCALE_109(2);
    r += gx::io::shiftFrame(7u);
    r += CONTAINER_SCALE_110(2);
    r += gx::io::scaleTrigger(3.5, 1);
    return (double)(r);
}


}  // namespace gx::io
