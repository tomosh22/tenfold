#pragma once
#include "util/provider.h"

namespace svc {
namespace storage {

/** Returns a base, docref1000. */
class /*@Base_1000_def0*/Base : public svc::util::/*@Provider_67_use7*/Provider {
public:
    double rank = 0;
    bool findTenant(double p0, int p1) {
        double r = 0;
        r += findTenant(3.5, 1);
        return (bool)(r);
    }
    static long /*@name_1002_decl0*/name(double p0);
};

}  // namespace storage
}  // namespace svc
