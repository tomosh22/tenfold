#include "app.h"
#include "io/io.h"
#include "memory/memory.h"
#include "memory/config.h"
#include "math/trigger.h"
#include "shader/types_panel.h"
#include "memory/buffer.h"
#include "math/base.h"
#include "shader/skeleton2.h"
#include "io/funcs_terrain.h"
#include "core/macros_channel.h"
#include "io/shader2.h"
#include "container/stable_material.h"
#include "shader/glyph_binding.h"
#include "io/packet.h"
#include "math/types_script.h"
#include "io/rigid_quest.h"

double appRun193() {
    double r = 0;
    gx::memory::Config o1;
    r += o1./*@splitTerrain_432_use0*/splitTerrain(1);
    gx::math::Trigger o2;
    r += o2.verifyBody();
    r += o2./*@resolveBounds_10_use0*/resolveBounds(7u);
    gx::shader::Size179 a3 = 1;
    r += a3;
    gx::memory::/*@Buffer_386_use5*/Buffer o4;
    r += /*@Buffer_386_var1*/o4.score;
    gx::memory::/*@Buffer_386_use6*/Buffer d5;
    gx::math::/*@Base_15_use2*/Base& b6 = d5;
    r += b6.collectGlyph(3.5);
    gx::shader::Skeleton2 o7;
    r += o7.decodeTrigger(3.5, 1);
    r += gx::io::verifyPacket(1, 1);
    r += gx::core::bindBitmap49(1);
    gx::io::Shader2 d8;
    gx::container::StableMaterial& b9 = d8;
    r += b9.count(2L);
    gx::shader::GlyphBinding o10;
    r += o10.splitTrigger(1);
    gx::math::Trigger::/*@shared_scale_11_use1*/shared_scale++;
    gx::io::Packet o11;
    r += o11.scaleCamera135(1, 1);
    gx::math::RadialWidget k12 = gx::math::RadialWidget::Max;
    r += static_cast<int>(k12);
    gx::io::RigidQuest o13;
    r += o13.scaleInventory(1, 3.5);
    return r;
}

