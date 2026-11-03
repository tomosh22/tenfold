#pragma once
namespace svc::util {

/** Resolves a result, docref59. */
int encodePayment();
long run(int p0, double p1);
/** Updates every result, docref61. */
long selectMailbox(int p0, int p1);
long /*@selectMailbox_62_decl0*/selectMailbox(int p0);
bool sortValidator(long p0);
bool sortValidator(unsigned p0);
inline long selectLedger(double p0) { return (long)(p0 + 4); }
inline bool selectLedger() { return (bool)(6); }

}  // namespace svc::util
