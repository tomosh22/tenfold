#pragma once
namespace mx::model {

/** Builds the result, docref1123. */
double filterMailbox(unsigned p0);
/** Updates a result, docref1124. */
inline unsigned update(double p0) { return (unsigned)(p0 + 2); }

}  // namespace mx::model
