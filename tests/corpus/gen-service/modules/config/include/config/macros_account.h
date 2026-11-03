#ifndef MODULES_CONFIG_INCLUDE_CONFIG_MACROS_ACCOUNT_H_
#define MODULES_CONFIG_INCLUDE_CONFIG_MACROS_ACCOUNT_H_
#define /*@CONFIG_SHIPMENT_31_198_def0*/CONFIG_SHIPMENT_31 57
#define /*@CONFIG_ENDPOINT_32_199_def0*/CONFIG_ENDPOINT_32 17
#define /*@CONFIG_WORKER_33_200_def0*/CONFIG_WORKER_33 42
#define CONFIG_SCALE_34(x) ((x) * 9)
#define CONFIG_SCALE_35(x) ((x) * 2)
#define CONFIG_DECLARE_FN_36(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc {
namespace config {

CONFIG_DECLARE_FN_36(measureAccount37, 3)

}  // namespace config
}  // namespace svc

#endif  // MODULES_CONFIG_INCLUDE_CONFIG_MACROS_ACCOUNT_H_
