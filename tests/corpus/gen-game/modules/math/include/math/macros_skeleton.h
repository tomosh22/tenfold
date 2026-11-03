#ifndef MODULES_MATH_INCLUDE_MATH_MACROS_SKELETON_H_
#define MODULES_MATH_INCLUDE_MATH_MACROS_SKELETON_H_
#define /*@MATH_JOINT_19_143_def0*/MATH_JOINT_19 46
#define /*@MATH_SCRIPT_20_144_def0*/MATH_SCRIPT_20 29
#define MATH_SCALE_21(x) ((x) * 8)
#define MATH_DECLARE_FN_22(fname, mul) inline int fname(int v) { return v * (mul); }

namespace gx {
namespace math {

MATH_DECLARE_FN_22(fetchMaterial23, 9)
MATH_DECLARE_FN_22(storeMesh24, 9)
MATH_DECLARE_FN_22(/*@bindLight25_149_def0*/bindLight25, 4)

}  // namespace math
}  // namespace gx

#endif  // MODULES_MATH_INCLUDE_MATH_MACROS_SKELETON_H_
