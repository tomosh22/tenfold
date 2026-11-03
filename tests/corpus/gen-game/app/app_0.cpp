#include "app.h"
#include "math/math.h"
#include "container/container.h"
#include "memory/chain70/c0.h"
#include "io/bounds2.h"
#include "math/cyclic_clip_prefab.h"
#include "math/base.h"
#include "memory/rolling_ray_glyph.h"
#include "io/funcs_shader.h"
#include "math/panel.h"
#include "math/types_prefab.h"
#include "io/spawner.h"
#include "memory/base.h"
#include "math/funcs_light.h"
#include "math/trigger.h"
#include "shader/skeleton2.h"
#include "container/chain114/c0.h"
#include "math/chain14/c0.h"
#include "core/atomic_vector.h"
#include "container/types_glyph.h"
#include "io/funcs_component.h"
#include "io/types_prefab.h"
#include "core/types_channel.h"
#include "math/macros_skeleton.h"
#include "container/shader.h"
#include "container/animator_agent.h"
#include "shader/glyph_binding.h"
#include "shader/queued_cursor.h"
#include "memory/macros_camera.h"
#include "container/tiled_light_frame.h"
#include "memory/entry.h"
#include "core/macros_channel.h"
#include "memory/emitter.h"
#include "io/shader2.h"
#include "memory/funcs_bounds.h"
#include "shader/funcs_bounds.h"
#include "core/clip.h"
#include "io/packed_matrix.h"
#include "container/remote_collider.h"
#include "math/types_emitter.h"
#include "shader/chain181/c2.h"
#include "core/cursor.h"
#include "memory/types_collider.h"
#include "shader/chain176/c0.h"
#include "container/binding_skeleton.h"
#include "container/funcs_buffer.h"
#include "container/funcs_terrain.h"

double appRun188() {
    double r = 0;
    r += gx::memory::chain70_l0(1);
    r += gx::io::Bounds2::shared_delta;
    gx::math::/*@CyclicClipPrefab_33_use0*/CyclicClipPrefab d1;
    gx::math::/*@Base_15_use0*/Base& b2 = d1;
    r += b2.count(3.5, 1);
    gx::memory::/*@RollingRayGlyph_392_use0*/RollingRayGlyph o3;
    r += gx::memory::/*@RollingRayGlyph_392_use1*/RollingRayGlyph::rotateAnimator(2L);
    r += gx::io::clear(1, 3.5);
    gx::math::/*@Panel_65_use0*/Panel o4;
    r += /*@Panel_65_var0*/o4.bindMesh(3.5, 1);
    r += gx::math::/*@Panel_65_use1*/Panel::sortChannel(1, 1);
    r += gx::math::QueuedPool_Unknown;
    r += gx::io::findEmitter(2.5);
    gx::memory::/*@Base_452_use0*/Base o5;
    r += /*@Base_452_var0*/o5.add(3.5, 1);
    r += o5.rotateArena(1);
    r += gx::math::value(2L);
    r += gx::io::Bounds2::shared_delta;
    r += gx::math::Trigger::/*@shared_scale_11_use0*/shared_scale;
    gx::io::Bounds2 o6;
    r += o6.measureLight(1);
    r += o6.load(7u);
    gx::shader::Skeleton2 o7;
    r += o7.run(1, 1);
    r += gx::container::chain114_entry(1);
    r += gx::math::chain14_l0(1);
    r += gx::io::Bounds2::shared_delta;
    r += gx::io::clear(1, 3.5);
    gx::core::/*@AtomicVector_167_use0*/AtomicVector d8;
    gx::core::/*@AtomicVector_167_use1*/AtomicVector& b9 = d8;
    r += b9.collectGlyph(3.5);
    r += gx::container::kStep106;
    r += gx::io::make(3.5);
    return r;
}

double appRun189() {
    double r = 0;
    r += static_cast<int>(gx::io::StableTrigger::Count);
    gx::core::/*@AtomicVector_167_use2*/AtomicVector o1;
    r += /*@AtomicVector_167_var0*/o1./*@limit_175_use0*/limit;
    r += gx::core::kStep31;
    gx::io::Bounds2 o2;
    r += o2.reset(1);
    r += gx::io::process();
    r += MATH_SCALE_21(2);
    r += gx::container::findScript<int>(2);
    gx::container::/*@AnimatorAgent_543_use0*/AnimatorAgent o3;
    r += /*@AnimatorAgent_543_var0*/o3.data(2L);
    r += o3.weight;
    gx::shader::GlyphBinding o4;
    r += o4.flush(1, 3.5);
    r += o4.size(1);
    r += o4.flush(1, 3.5);
    gx::shader::/*@QueuedCursor_1265_use0*/QueuedCursor<int> t5;
    r += t5.get();
    t5.set(3);
    r += /*@MEMORY_TWEEN_84_480_use0*/MEMORY_TWEEN_84;
    gx::core::/*@AtomicVector_167_use3*/AtomicVector o6;
    r += /*@AtomicVector_167_var1*/o6./*@limit_175_use1*/limit;
    r += o6.count(3.5, 1);
    gx::container::/*@TiledLightFrame_511_use0*/TiledLightFrame::/*@shared_scale_520_use0*/shared_scale++;
    gx::memory::Entry o7;
    r += o7.init(1);
    r += gx::core::bindBitmap49(1);
    r += gx::memory::resetBatch<int>(2);
    gx::io::Shader2 o8;
    r += o8.count(2L);
    r += o8.collectScene(1);
    r += gx::memory::fetchMesh(1, 1);
    return r;
}

double appRun190() {
    double r = 0;
    r += gx::shader::size(1, 1);
    r += gx::core::Clip::/*@shared_total_322_use0*/shared_total;
    gx::io::PackedMatrix d1;
    gx::container::RemoteCollider* b2 = &d1;
    r += b2->set(1);
    r += static_cast<int>(gx::math::SortedPanel::Green);
    r += gx::shader::chain181_l2(1);
    r += /*@MEMORY_TWEEN_84_480_use1*/MEMORY_TWEEN_84;
    gx::core::Cursor o3;
    r += o3.mergeMesh(3.5);
    r += static_cast<int>(gx::memory::/*@Path_335_use0*/Path::Auto);
    r += gx::shader::chain176_entry(1);
    gx::container::/*@BindingSkeleton_592_use0*/BindingSkeleton o4;
    r += /*@BindingSkeleton_592_var0*/o4.collectGlyph(3.5);
    r += gx::container::/*@TiledLightFrame_511_use1*/TiledLightFrame::/*@shared_scale_520_use1*/shared_scale;
    gx::memory::Count66 a5 = 1;
    r += a5;
    r += gx::container::selectTween(3.5);
    gx::io::Bounds2 o6;
    r += o6.score;
    r += o6.count(1, 1);
    r += o6.score;
    r += gx::container::init(3.5, 1);
    return r;
}

