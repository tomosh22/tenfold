#pragma once
/** Finds every limit, docref1276. */
#define CACHE_EVENT_261 43
#define CACHE_REPOSITORY_262 82
/** Resolves every limit, docref1278. */
#define CACHE_SHIPMENT_263 3
#define CACHE_SCALE_264(x) ((x) * 7)
#define CACHE_DECLARE_FN_265(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc::cache {

CACHE_DECLARE_FN_265(decodeJob266, 7)

}  // namespace svc::cache
