#ifndef MODULES_CONFIG_INCLUDE_CONFIG_FUNCS_JOB_H_
#define MODULES_CONFIG_INCLUDE_CONFIG_FUNCS_JOB_H_
namespace svc {
namespace config {

/** Finds the result, docref257. */
double filter_validator(int p0, double p1);
/** Finds every result, docref258. */
unsigned filter_validator(long p0);
/** Builds every result, docref259. */
unsigned measure_queue(int p0);
/** Resolves every result, docref260. */
int load(long p0);
/** Returns every result, docref261. */
double load(double p0, int p1);
/** Resolves every result, docref262. */
int process(int p0, double p1);
bool apply(double p0, int p1);

}  // namespace config
}  // namespace svc

#endif  // MODULES_CONFIG_INCLUDE_CONFIG_FUNCS_JOB_H_
