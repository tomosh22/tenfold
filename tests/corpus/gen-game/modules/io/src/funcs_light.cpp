#include "io/funcs_light.h"
#include "io/panel2.h"
#include "container/tiled_light_frame.h"
#include "io/bounds2.h"
#include "io/shader2.h"
#include "container/types_emitter.h"
#include "container/funcs_terrain.h"
#include "container/macros_script.h"
#include "io/funcs_terrain.h"
#include "io/funcs_camera.h"
#include "io/funcs_shader.h"

namespace gx::io {

int saveEntity() {
    double r = 0;
    gx::io::Panel2 o1;
    r += o1.data(3.5);
    gx::container::/*@TiledLightFrame_511_use33*/TiledLightFrame o2;
    r += o2.resetTrigger(1, 3.5);
    gx::io::Bounds2 o3;
    r += o3.count(1, 1);
    gx::io::Shader2 o4;
    r += o4.scale;
    return (int)(r);
}

bool add(long p0) {
    double r = 0;
    gx::container::Buffer k1 = gx::container::Buffer::Default;
    r += static_cast<int>(k1);
    r += gx::container::selectWidget(2L);
    r += gx::container::findFrustum112(1);
    return (bool)(r);
}

bool add() {
    double r = 0;
    r += gx::io::encodeBinding(1, 1);
    gx::container::/*@TiledLightFrame_511_use34*/TiledLightFrame o1;
    r += o1.combineSpawner(3.5, 1);
    r += encodeBinding(3.5, 1);
    r += gx::io::pop(1, 1);
    r += gx::io::clear(1, 3.5);
    r += /*@init_693_use1*/init(1, 3.5);
    return (bool)(r);
}


}  // namespace gx::io
