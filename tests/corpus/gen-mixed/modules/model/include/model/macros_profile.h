#pragma once
#define MODEL_PAYMENT_243 65
#define /*@MODEL_CUSTOMER_244_1074_def0*/MODEL_CUSTOMER_244 56
#define MODEL_ADDRESS_245 36
#define MODEL_SCALE_246(x) ((x) * 2)
#define MODEL_DECLARE_FN_247(fname, mul) inline int fname(int v) { return v * (mul); }

namespace mx::model {

MODEL_DECLARE_FN_247(bindRoute248, 5)
MODEL_DECLARE_FN_247(decodeLedger249, 5)
MODEL_DECLARE_FN_247(countAccount250, 2)

}  // namespace mx::model
