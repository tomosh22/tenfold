#ifndef PPI_GUARD_H
#define PPI_GUARD_H
// Classic include guard.
int /*@guarded_fn*/guarded_fn();
struct /*@GuardBox*/GuardBox { int /*@GuardBox_v*/v; };
#endif  // PPI_GUARD_H
