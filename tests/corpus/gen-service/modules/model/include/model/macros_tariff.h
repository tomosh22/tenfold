#pragma once
#define MODEL_ADDRESS_164 89
#define MODEL_SCALE_165(x) ((x) * 7)
#define MODEL_DECLARE_FN_166(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc::model {

MODEL_DECLARE_FN_166(measureRepository167, 6)
MODEL_DECLARE_FN_166(saveLedger168, 7)
MODEL_DECLARE_FN_166(verifyMapper169, 7)

}  // namespace svc::model
