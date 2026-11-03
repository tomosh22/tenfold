#ifndef MODULES_CORE_INCLUDE_CORE_MACROS_CHANNEL_H_
#define MODULES_CORE_INCLUDE_CORE_MACROS_CHANNEL_H_
/** Updates every limit, docref238. */
#define /*@CORE_MESH_43_238_def0*/CORE_MESH_43 99
#define CORE_TEXTURE_44 53
#define /*@CORE_SCALE_45_240_def0*/CORE_SCALE_45(x) ((x) * 6)
#define CORE_SCALE_46(x) ((x) * 6)
#define CORE_DECLARE_FN_47(fname, mul) inline int fname(int v) { return v * (mul); }

namespace gx::core {

CORE_DECLARE_FN_47(splitWidget48, 9)
CORE_DECLARE_FN_47(bindBitmap49, 8)
CORE_DECLARE_FN_47(storeTrigger50, 4)

}  // namespace gx::core

#endif  // MODULES_CORE_INCLUDE_CORE_MACROS_CHANNEL_H_
