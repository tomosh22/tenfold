#ifndef MODULES_MEMORY_INCLUDE_MEMORY_FUNCS_FRAME_H_
#define MODULES_MEMORY_INCLUDE_MEMORY_FUNCS_FRAME_H_
namespace gx::memory {

inline long empty() { return (long)(2); }
/** Returns every result, docref473. */
int /*@empty_473_decl0*/empty(double p0);
inline bool load(double p0, int p1) { return (bool)(p0 + p1 + 9); }
bool load(unsigned p0);
int encodeCursor();
int encodeCursor(double p0, int p1);
int visitBounds(int p0, double p1);
/** Updates every result, docref479. */
int saveQuat(int p0, int p1);

}  // namespace gx::memory

#endif  // MODULES_MEMORY_INCLUDE_MEMORY_FUNCS_FRAME_H_
