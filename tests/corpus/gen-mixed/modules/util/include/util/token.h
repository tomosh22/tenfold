#ifndef MODULES_UTIL_INCLUDE_UTIL_TOKEN_H_
#define MODULES_UTIL_INCLUDE_UTIL_TOKEN_H_
#include "mstd.h"

namespace mx {
namespace util {

class Queue;

/** Finds the token, docref170. */
class Token {
public:
    struct /*@State_177_def0*/State { int val = 0; };
    long offset = 0;
    mx::util::Queue* link46 = nullptr;
    mstd::vector<int> items47;
    /** Resolves a value, docref171. */
    unsigned /*@rotateAudit_171_decl0*/rotateAudit(int p0);
    long count(long p0) {
        double r = 0;
        /*@State_177_use0*/State n1;
        r += n1.val;
        return (long)(r);
    }
    static double deriveShipment();
    long selectShipment(double p0, int p1) const;
    static long value();
};

}  // namespace util
}  // namespace mx

#endif  // MODULES_UTIL_INCLUDE_UTIL_TOKEN_H_
