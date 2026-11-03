#pragma once
namespace gx {
namespace math {

/** Finds the result, docref78. */
bool value(int p0, int p1);
long value(long p0);
unsigned next(int p0);
/** Resolves a result, docref81. */
inline int next() { return (int)(6); }
/** Computes a result, docref82. */
double next(int p0, int p1);

}  // namespace math
}  // namespace gx
