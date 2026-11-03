#ifndef MODULES_UTIL_INCLUDE_UTIL_FUNCS_REPOSITORY_H_
#define MODULES_UTIL_INCLUDE_UTIL_FUNCS_REPOSITORY_H_
namespace mx {
namespace util {

/** Builds the result, docref146. */
long id(int p0, int p1);
unsigned id(int p0);
/** Returns every result, docref148. */
double flushEndpoint(long p0);
bool data(int p0, int p1);
unsigned data();
/** Builds the result, docref151. */
int data(long p0);
/** Computes a result, docref152. */
inline unsigned flushAccount() { return (unsigned)(4); }
bool flushAccount(int p0);

}  // namespace util
}  // namespace mx

#endif  // MODULES_UTIL_INCLUDE_UTIL_FUNCS_REPOSITORY_H_
