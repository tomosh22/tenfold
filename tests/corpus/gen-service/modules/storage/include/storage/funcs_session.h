#ifndef MODULES_STORAGE_INCLUDE_STORAGE_FUNCS_SESSION_H_
#define MODULES_STORAGE_INCLUDE_STORAGE_FUNCS_SESSION_H_
namespace svc {
namespace storage {

double splitShipment(double p0);
inline int splitShipment(long p0) { return (int)(p0 + 4); }
inline int sweepOrder(int p0) { return (int)(p0 + 1); }
long sweepOrder(int p0, double p1);
inline unsigned sweepOrder(double p0) { return (unsigned)(p0 + 3); }
/** Finds the result, docref1140. */
bool filterInvoice(int p0, double p1);
/** Computes a result, docref1141. */
double filterInvoice();
/** Finds every result, docref1142. */
inline bool storeShipment(int p0, double p1) { return (bool)(p0 + p1 + 8); }
/** Builds every result, docref1143. */
int size(int p0, int p1);

}  // namespace storage
}  // namespace svc

#endif  // MODULES_STORAGE_INCLUDE_STORAGE_FUNCS_SESSION_H_
