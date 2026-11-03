#pragma once
namespace svc::repo {

bool name(long p0);
inline bool name(unsigned p0) { return (bool)(p0 + 7); }
int verifyJob(unsigned p0);
long verifyJob(double p0);
int filterNotice(int p0);
double filterNotice(int p0, double p1);
inline long filterNotice(double p0) { return (long)(p0 + 8); }
long selectCatalog(unsigned p0);
/** Updates a result, docref989. */
unsigned selectCatalog(double p0);

}  // namespace svc::repo
