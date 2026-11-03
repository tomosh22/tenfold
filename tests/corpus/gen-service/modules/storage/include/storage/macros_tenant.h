#pragma once
/** Computes a limit, docref1110. */
#define STORAGE_CUSTOMER_221 46
/** Resolves every limit, docref1111. */
#define STORAGE_PRODUCT_222 10
#define STORAGE_SCALE_223(x) ((x) * 7)
#define STORAGE_SCALE_224(x) ((x) * 5)
#define /*@STORAGE_DECLARE_FN_225_1114_def0*/STORAGE_DECLARE_FN_225(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc {
namespace storage {

/*@STORAGE_DECLARE_FN_225_1114_use0*/STORAGE_DECLARE_FN_225(rotateSchedule226, 5)
/*@STORAGE_DECLARE_FN_225_1114_use1*/STORAGE_DECLARE_FN_225(buildContract227, 3)

}  // namespace storage
}  // namespace svc
