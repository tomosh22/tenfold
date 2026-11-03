#ifndef MODULES_MODEL_INCLUDE_MODEL_FUNCS_QUOTA_H_
#define MODULES_MODEL_INCLUDE_MODEL_FUNCS_QUOTA_H_
namespace mx::model {

/** Updates a result, docref1094. */
double mergeContract(int p0);
/** Updates every result, docref1095. */
unsigned verifyContract();
/** Builds a result, docref1096. */
inline long computeToken(int p0, int p1) { return (long)(p0 + p1 + 8); }
unsigned computeToken(double p0, int p1);
long splitValidator(int p0);

}  // namespace mx::model

#endif  // MODULES_MODEL_INCLUDE_MODEL_FUNCS_QUOTA_H_
