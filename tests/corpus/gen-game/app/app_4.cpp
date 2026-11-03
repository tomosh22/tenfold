#include "app.h"
#include "memory/memory.h"
#include "shader/shader.h"
#include "shader/types_trigger.h"
#include "shader/chain176/c2.h"
#include "container/funcs_agent.h"
#include "core/item.h"
#include "container/macros_script.h"
#include "io/funcs_shader.h"
#include "shader/chain176/c0.h"
#include "shader/glyph_binding.h"
#include "core/macros_channel.h"
#include "memory/entry.h"
#include "shader/skeleton2.h"
#include "memory/funcs_frame.h"
#include "math/chain8/c2.h"
#include "io/rigid_quest.h"
#include "io/panel2.h"
#include "core/atomic_vector.h"
#include "io/funcs_camera.h"
#include "memory/buffer.h"
#include "memory/types_mesh.h"
#include "io/shader2.h"
#include "io/spawner.h"
#include "memory/config.h"
#include "shader/funcs_bounds.h"
#include "container/funcs_terrain.h"
#include "container/tiled_light_frame.h"
#include "io/bounds2.h"
#include "container/binding_skeleton.h"
#include "memory/base.h"
#include "core/cursor.h"
#include "io/masked_script_path.h"
#include "container/linear_material.h"
#include "core/info.h"
#include "memory/macros_camera.h"
#include "core/types_batch.h"
#include "container/types_glyph.h"
#include "memory/funcs_bounds.h"
#include "math/funcs_trigger.h"
#include "container/types_emitter.h"
#include "math/macros_skeleton.h"

double appRun195() {
    double r = 0;
    gx::shader::RadialBoundsBody k1 = gx::shader::RadialBoundsBody::None;
    r += static_cast<int>(k1);
    r += gx::shader::chain176_l2(1);
    r += gx::container::/*@top_640_use0*/top(1, 1);
    gx::core::Item o2;
    r += o2.get(3.5);
    r += o2.flushSpawner(1, 1);
    r += gx::container::findFrustum112(1);
    r += gx::io::/*@init_693_use0*/init(1, 3.5);
    r += gx::shader::chain176_l0(1);
    gx::shader::GlyphBinding o3;
    r += o3.flush(1, 3.5);
    r += o3.splitTrigger(1);
    r += gx::core::bindBitmap49(1);
    r += /*@CORE_MESH_43_238_use1*/CORE_MESH_43;
    gx::memory::Entry o4;
    r += gx::memory::Entry::buildBitmap(1);
    return r;
}

double appRun196() {
    double r = 0;
    gx::shader::Skeleton2 o1;
    r += o1.probeSkeleton(3.5, 1);
    r += o1.probeSkeleton(3.5, 1);
    r += o1.decodeTrigger(3.5, 1);
    gx::memory::Entry o2;
    r += o2.combineCamera(2L);
    r += o2.flushSpawner(1);
    r += o2.width;
    r += gx::memory::load(7u);
    r += gx::memory::/*@empty_473_use0*/empty(3.5);
    r += gx::shader::chain176_l0(1);
    r += gx::math::chain8_l2(1);
    gx::io::RigidQuest d3;
    gx::io::Panel2& b4 = d3;
    r += b4.collectScene(1);
    gx::core::/*@AtomicVector_167_use8*/AtomicVector o5;
    r += /*@AtomicVector_167_var3*/o5./*@limit_175_use3*/limit;
    r += gx::io::scanBitmap(1, 3.5);
    gx::memory::/*@Buffer_386_use7*/Buffer o6;
    r += /*@Buffer_386_var2*/o6.loadSpawner(3.5);
    r += o6.loadSpawner(1, 3.5);
    gx::memory::Count73 a7 = 1;
    r += a7;
    gx::io::Shader2 o8;
    r += o8.init(1, 1);
    r += o8.data(3.5);
    r += o8.scale;
    gx::io::Panel2 o9;
    r += o9.countTexture(3.5, 1);
    r += gx::io::Panel2::id(3.5);
    r += gx::io::Panel2::/*@set_686_use0*/set();
    r += gx::io::findEmitter(2.5);
    gx::memory::Config o10;
    r += o10./*@splitTerrain_432_use1*/splitTerrain(1);
    r += o10./*@stamp_436_use0*/stamp;
    r += gx::shader::collectEmitter(1);
    r += gx::container::selectWidget(2L);
    r += gx::container::selectWidget(2L);
    gx::container::/*@TiledLightFrame_511_use3*/TiledLightFrame o11;
    r += /*@TiledLightFrame_511_var0*/o11.decodeAnimator(1);
    r += o11.decodeAnimator(1);
    r += o11./*@collectGlyph_517_use0*/collectGlyph(3.5);
    gx::io::Bounds2 o12;
    r += o12.load(7u);
    r += o12.updateAnimator(1);
    gx::container::/*@BindingSkeleton_592_use1*/BindingSkeleton o13;
    r += /*@BindingSkeleton_592_var1*/o13.flushBitmap(1);
    r += o13.flushBitmap(1);
    r += gx::memory::load(7u);
    gx::memory::/*@Base_452_use1*/Base o14;
    r += /*@Base_452_var1*/o14.add(3.5);
    r += o14.add(3.5);
    r += o14.shiftSkeleton(1, 1);
    return r;
}

double appRun197() {
    double r = 0;
    gx::core::Cursor o1;
    r += gx::core::Cursor::push(7u);
    gx::core::Item o2;
    r += gx::core::Item::updateFrustum(7u);
    r += o2.fetchBitmap(1, 3.5);
    r += CONTAINER_SCALE_109(2);
    gx::io::MaskedScriptPath o3;
    r += o3./*@speed_866_use0*/speed;
    r += o3.resolveTexture(1, 3.5);
    r += o3.resolveTexture();
    gx::container::LinearMaterial o4;
    r += o4.next(1, 1);
    r += o4.markFrustum(2L);
    gx::core::Info<int> t5;
    r += t5.get();
    t5.set(3);
    r += gx::memory::/*@combineVector89_485_use0*/combineVector89(1);
    gx::memory::Count73 a6 = 1;
    r += a6;
    r += gx::core::splitWidget48(1);
    r += gx::core::kMargin51;
    gx::container::Id107 a7 = 1;
    r += a7;
    gx::container::/*@TiledLightFrame_511_use4*/TiledLightFrame o8;
    r += /*@TiledLightFrame_511_var1*/o8.decodeAnimator(1);
    r += gx::container::/*@TiledLightFrame_511_use5*/TiledLightFrame::/*@shared_scale_520_use3*/shared_scale;
    r += gx::memory::rotateBounds(7u);
    r += gx::math::combineChannel9(1, 3.5);
    r += gx::container::kMargin100;
    r += MATH_SCALE_21(2);
    return r;
}

