#pragma once
/** Builds a limit, docref1144. */
#define STORAGE_TENANT_231 78
#define STORAGE_SCALE_232(x) ((x) * 5)
#define STORAGE_DECLARE_FN_233(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc {
namespace storage {

STORAGE_DECLARE_FN_233(selectAccount234, 5)
STORAGE_DECLARE_FN_233(/*@markCatalog235_1148_def0*/markCatalog235, 8)
STORAGE_DECLARE_FN_233(decodeOrder236, 2)

}  // namespace storage
}  // namespace svc
