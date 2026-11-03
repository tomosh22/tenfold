#ifndef MODULES_DOMAIN_INCLUDE_DOMAIN_FUNCS_INDEX_H_
#define MODULES_DOMAIN_INCLUDE_DOMAIN_FUNCS_INDEX_H_
namespace svc::domain {

inline int init(int p0, int p1) { return (int)(p0 + p1 + 5); }
/** Finds a result, docref549. */
unsigned flush(int p0);
/** Finds a result, docref550. */
unsigned build_mapper(int p0, int p1);
inline bool /*@get_551_def0*/get(long p0) { return (bool)(p0 + 4); }

}  // namespace svc::domain

#endif  // MODULES_DOMAIN_INCLUDE_DOMAIN_FUNCS_INDEX_H_
