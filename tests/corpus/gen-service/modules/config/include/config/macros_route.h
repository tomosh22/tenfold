#pragma once
#define /*@CONFIG_ACCOUNT_52_277_def0*/CONFIG_ACCOUNT_52 78
#define CONFIG_MAPPER_53 92
#define CONFIG_SCALE_54(x) ((x) * 6)
#define CONFIG_SCALE_55(x) ((x) * 5)
#define /*@CONFIG_DECLARE_FN_56_281_def0*/CONFIG_DECLARE_FN_56(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc {
namespace config {

/*@CONFIG_DECLARE_FN_56_281_use0*/CONFIG_DECLARE_FN_56(loadMailbox57, 2)
/*@CONFIG_DECLARE_FN_56_281_use1*/CONFIG_DECLARE_FN_56(shiftEndpoint58, 2)

}  // namespace config
}  // namespace svc
