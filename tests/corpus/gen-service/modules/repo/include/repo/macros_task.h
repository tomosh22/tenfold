#ifndef MODULES_REPO_INCLUDE_REPO_MACROS_TASK_H_
#define MODULES_REPO_INCLUDE_REPO_MACROS_TASK_H_
/** Returns a limit, docref970. */
#define REPO_CART_197 53
#define REPO_ROUTE_198 61
#define REPO_SCALE_199(x) ((x) * 5)
#define REPO_DECLARE_FN_200(fname, mul) inline int fname(int v) { return v * (mul); }

namespace svc::repo {

REPO_DECLARE_FN_200(sweepQuota201, 8)
REPO_DECLARE_FN_200(computeContract202, 8)
REPO_DECLARE_FN_200(sweepBalance203, 7)

}  // namespace svc::repo

#endif  // MODULES_REPO_INCLUDE_REPO_MACROS_TASK_H_
