#include "app.h"
#include "io/io.h"
#include "math/math.h"
#include "core/cursor.h"
#include "io/packed_matrix.h"
#include "container/remote_collider.h"
#include "math/types_font.h"
#include "memory/buffer.h"
#include "io/types_path.h"
#include "io/trigger2.h"
#include "io/bounds2.h"
#include "io/panel2.h"
#include "container/types_glyph.h"
#include "shader/funcs_bounds.h"
#include "io/spawner.h"
#include "container/types_channel.h"
#include "math/macros_skeleton.h"
#include "core/macros_channel.h"
#include "core/item.h"
#include "core/atomic_vector.h"
#include "math/base.h"
#include "shader/glyph_binding.h"
#include "shader/types_trigger.h"
#include "memory/funcs_frame.h"
#include "io/types_prefab.h"
#include "io/rigid_quest.h"
#include "container/shader.h"
#include "shader/skeleton2.h"
#include "math/bounded_pool_spawner.h"

double appRun191() {
    double r = 0;
    gx::core::Cursor d1;
    gx::core::Cursor* b2 = &d1;
    r += b2->resolveShader();
    gx::io::PackedMatrix d3;
    gx::container::RemoteCollider& b4 = d3;
    r += b4.updatePath(1);
    r += gx::math::kBase4;
    gx::memory::/*@Buffer_386_use0*/Buffer d5;
    gx::memory::/*@Buffer_386_use1*/Buffer* b6 = &d5;
    r += b6->combineSpawner(3.5, 1);
    gx::io::Handle123 a7 = 1;
    r += a7;
    r += gx::io::/*@resolveSpawner_778_use0*/resolveSpawner(2.5);
    r += gx::io::Bounds2::shared_delta;
    gx::io::Panel2 d8;
    gx::io::Panel2& b9 = d8;
    r += b9.collectScene(1);
    r += gx::container::Entity2_First;
    gx::memory::/*@Buffer_386_use2*/Buffer o10;
    r += gx::memory::/*@Buffer_386_use3*/Buffer::/*@measureBounds_389_use0*/measureBounds();
    r += gx::shader::size(1, 1);
    gx::io::Spawner<bool> t11;
    r += t11.get();
    t11.set(true);
    gx::container::Handle94 a12 = 1;
    r += a12;
    gx::memory::/*@Buffer_386_use4*/Buffer o13;
    r += /*@Buffer_386_var0*/o13.loadSpawner(1, 3.5);
    r += o13.loadSpawner(1, 3.5);
    r += /*@MATH_JOINT_19_143_use0*/MATH_JOINT_19;
    r += gx::core::splitWidget48(1);
    gx::core::Item o14;
    r += o14.id(1, 3.5);
    gx::core::/*@AtomicVector_167_use4*/AtomicVector d15;
    gx::math::/*@Base_15_use1*/Base& b16 = d15;
    r += b16.decodeAnimator(1);
    gx::shader::GlyphBinding o17;
    r += o17.size(1);
    r += o17.size(1);
    return r;
}

double /*@appRun192_1288_def0*/appRun192() {
    double r = 0;
    r += static_cast<int>(gx::container::Matrix3::Red);
    r += gx::math::fetchMaterial23(1);
    r += static_cast<int>(gx::shader::/*@Policy_1184_use0*/Policy::Auto);
    gx::io::Bounds2 o1;
    r += o1.encodeScene();
    r += o1.load(7u);
    r += gx::memory::visitBounds(1, 3.5);
    gx::io::Count127 a2 = 1;
    r += a2;
    gx::io::RigidQuest d3;
    gx::io::RigidQuest& b4 = d3;
    r += b4.count(2L);
    r += gx::container::flushBinding<int>(2);
    r += gx::container::findScript<int>(2);
    gx::core::/*@AtomicVector_167_use5*/AtomicVector o5;
    r += /*@AtomicVector_167_var2*/o5./*@limit_175_use2*/limit;
    r += o5.name(3.5, 1);
    gx::shader::Skeleton2 o6;
    r += o6.probeSkeleton(3.5, 1);
    r += o6.run(1, 1);
    r += o6.decodeTrigger(3.5, 1);
    gx::core::/*@AtomicVector_167_use6*/AtomicVector d7;
    gx::core::/*@AtomicVector_167_use7*/AtomicVector* b8 = &d7;
    r += b8->combineSpawner(3.5, 1);
    gx::math::/*@BoundedPoolSpawner_98_use0*/BoundedPoolSpawner o9;
    r += /*@BoundedPoolSpawner_98_var0*/o9./*@save_101_use0*/save(2L);
    r += o9./*@markTerrain_107_use0*/markTerrain();
    r += o9.decodeAnimator(1);
    gx::io::Panel2 o10;
    r += o10.collectScene(1);
    r += gx::io::Panel2::id(3.5);
    return r;
}

