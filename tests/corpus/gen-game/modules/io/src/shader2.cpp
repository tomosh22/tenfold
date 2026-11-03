#include "io/shader2.h"
#include "container/funcs_terrain.h"
#include "io/panel2.h"
#include "container/tiled_light_frame.h"
#include "io/types_prefab.h"
#include "io/funcs_camera.h"
#include "io/types_path.h"
#include "container/stable_material.h"
#include "io/spawner.h"
#include "io/funcs_terrain.h"

namespace gx::io {


bool Shader2::init(int p0, int p1) {
    double r = 0;
    r += gx::container::/*@selectButton_542_use1*/selectButton(1, 3.5);
    gx::io::Panel2 o1;
    r += o1.countTexture(3.5, 1);
    r += gx::io::Panel2::/*@set_686_use3*/set();
    gx::container::TiledLightFrame::/*@shared_scale_520_use7*/shared_scale++;
    gx::io::Count127 a2 = 1;
    r += a2;
    gx::io::Panel2 o3;
    r += o3.count(2L);
    r += o3.data(3.5);
    r += gx::io::Panel2::/*@set_686_use4*/set();
    return (bool)(r);
}

int Shader2::next(int p0, double p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::io::pop(1, 1);
    gx::io::Panel2 o1;
    r += o1.collectScene(1);
    r += gx::io::Panel2::id(3.5);
    r += o1.count(2L);
    gx::io::Handle123 a2 = 1;
    r += a2;
    r += gx::io::pop(1, 1);
    gx::io::Panel2 d3;
    gx::container::StableMaterial& b4 = d3;
    r += b4.data(3.5);
    return (int)(r);
}

bool Shader2::data(double p0) {
    double r = 0;
    r += gx::io::Base_Red;
    r += gx::io::shiftFrame(7u);
    r += gx::io::pop(1, 1);
    r += gx::io::findEmitter<int>(2);
    r += gx::io::sweepQuat(7u);
    return (bool)(r);
}


}  // namespace gx::io
