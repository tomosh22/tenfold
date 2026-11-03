#include "io/funcs_shader.h"
#include "io/funcs_camera.h"
#include "io/types_vector.h"
#include "io/panel2.h"
#include "container/tiled_light_frame.h"
#include "container/macros_script.h"
#include "container/camera.h"
#include "container/shader.h"
#include "container/chain114/c0.h"

namespace gx::io {

bool clear(int p0, double p1) {
    double r = 0;
    r += p1;
    r += gx::io::pop(1, 1);
    r += static_cast<int>(gx::io::Prefab::/*@Max_654_use1*/Max);
    return (bool)(r);
}

bool process() {
    double r = 0;
    gx::io::Prefab k1 = gx::io::Prefab::Green;
    r += static_cast<int>(k1);
    gx::io::Panel2 o2;
    r += o2.data(3.5);
    gx::io::Panel2 o3;
    r += o3.data(3.5);
    r += o3.data(3.5);
    r += o3.data(3.5);
    r += gx::io::pop(1, 1);
    gx::io::Panel2 o4;
    r += o4.count(2L);
    r += gx::io::loadScript(1, 3.5);
    return (bool)(r);
}

bool resetBatch(double p0) {
    double r = 0;
    r += p0;
    r += gx::container::/*@TiledLightFrame_511_use35*/TiledLightFrame::/*@shared_scale_520_use5*/shared_scale;
    gx::io::Panel2 o1;
    r += o1.depth;
    r += o1.depth;
    r += o1.count(2L);
    r += static_cast<int>(gx::io::Prefab::Green);
    r += gx::io::scanBitmap(1, 3.5);
    gx::container::/*@TiledLightFrame_511_use36*/TiledLightFrame o2;
    r += o2.applyShader(3.5);
    r += o2.combineSpawner(3.5, 1);
    r += o2.resetTrigger(1, 3.5);
    r += gx::io::scanBitmap(1, 3.5);
    return (bool)(r);
}

double resetBatch() {
    double r = 0;
    gx::io::Prefab k1 = gx::io::Prefab::Count;
    r += static_cast<int>(k1);
    r += gx::container::findFrustum112(1);
    gx::io::Panel2 o2;
    r += o2.count(2L);
    r += o2.data(3.5);
    r += gx::io::Panel2::id(3.5);
    gx::io::Panel2 o3;
    r += o3.depth;
    r += o3.countTexture(3.5, 1);
    r += static_cast<int>(gx::io::Prefab::Count);
    return (double)(r);
}

double /*@init_693_def0*/init(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    gx::container::Camera<int> t1;
    r += t1.get();
    t1.set(3);
    r += gx::container::/*@TiledLightFrame_511_use37*/TiledLightFrame::/*@shared_scale_520_use6*/shared_scale;
    r += CONTAINER_SCALE_109(2);
    gx::container::Camera<int> t2;
    r += t2.get();
    t2.set(3);
    return (double)(r);
}

int init(double p0) {
    double r = 0;
    gx::container::/*@Shader_586_use0*/Shader<int> t1;
    r += t1.get();
    t1.set(3);
    r += resetAnimator(1, 3.5);
    gx::container::/*@TiledLightFrame_511_use38*/TiledLightFrame o2;
    r += o2.combineSpawner(3.5, 1);
    r += o2.save(2L);
    r += gx::container::chain114_entry(1);
    gx::io::Panel2 o3;
    r += o3.depth;
    return (int)(r);
}

bool mergeQuest(unsigned p0) {
    double r = 0;
    r += p0;
    r += pop(1, 1);
    gx::io::Panel2 o1;
    r += o1.count(2L);
    gx::container::Camera<int> t2;
    r += t2.get();
    t2.set(3);
    return (bool)(r);
}


}  // namespace gx::io
